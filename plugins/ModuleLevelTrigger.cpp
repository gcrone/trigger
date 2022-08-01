/**
 * @file ModuleLevelTrigger.cpp ModuleLevelTrigger class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleLevelTrigger.hpp"

#include "trigger/Issues.hpp"
#include "trigger/LivetimeCounter.hpp"
#include "trigger/moduleleveltrigger/Nljs.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/app/Nljs.hpp"
#include "daqdataformats/ComponentRequest.hpp"
#include "detdataformats/trigger/Types.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"
#include "timinglibs/TimestampEstimator.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace dunedaq {
namespace trigger {

ModuleLevelTrigger::ModuleLevelTrigger(const std::string& name)
  : DAQModule(name)
  , m_last_trigger_number(0)
  , m_run_number(0)
{
  // clang-format off
  register_command("conf",   &ModuleLevelTrigger::do_configure);
  register_command("start",  &ModuleLevelTrigger::do_start);
  register_command("stop",   &ModuleLevelTrigger::do_stop);
  register_command("disable_triggers",  &ModuleLevelTrigger::do_pause);
  register_command("enable_triggers", &ModuleLevelTrigger::do_resume);
  register_command("scrap",  &ModuleLevelTrigger::do_scrap);
  // clang-format on
}

void
ModuleLevelTrigger::init(const nlohmann::json& iniobj)
{
  m_candidate_source =
    get_iom_receiver<triggeralgs::TriggerCandidate>(appfwk::connection_inst(iniobj, "trigger_candidate_source"));
}

void
ModuleLevelTrigger::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  moduleleveltriggerinfo::Info i;

  i.tc_received_count = m_tc_received_count.load();

  i.td_sent_count = m_td_sent_count.load();
  i.td_sent_tc_count = m_td_sent_tc_count.load();
  i.td_inhibited_count = m_td_inhibited_count.load();
  i.td_inhibited_tc_count = m_td_inhibited_tc_count.load();
  i.td_paused_count = m_td_paused_count.load();
  i.td_paused_tc_count = m_td_paused_tc_count.load();
  i.td_dropped_count = m_td_dropped_count.load();
  i.td_dropped_tc_count = m_td_dropped_tc_count.load();
  i.td_cleared_count = m_td_cleared_count.load();
  i.td_cleared_tc_count = m_td_cleared_tc_count.load();
  i.td_total_count = m_td_total_count.load();

  if (m_livetime_counter.get() != nullptr) {
    i.lc_kLive = m_livetime_counter->get_time(LivetimeCounter::State::kLive);
    i.lc_kPaused = m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
    i.lc_kDead = m_livetime_counter->get_time(LivetimeCounter::State::kDead);
  }

  ci.add(i);
}

void
ModuleLevelTrigger::do_configure(const nlohmann::json& confobj)
{
  auto params = confobj.get<moduleleveltrigger::ConfParams>();

  m_links.clear();
  for (auto const& link : params.links) {
    m_links.push_back(
      dfmessages::GeoID{ daqdataformats::GeoID::string_to_system_type(link.system), link.region, link.element });
  }
  m_trigger_decision_connection = params.dfo_connection;
  m_inhibit_connection = params.dfo_busy_connection;
  m_hsi_passthrough = params.hsi_trigger_type_passthrough;

  m_configured_flag.store(true);

  m_buffer_timeout = params.buffer_timeout;
  m_send_timed_out_tds = params.td_out_of_timeout;
  m_td_readout_limit = params.td_readout_limit;
  TLOG_DEBUG(3) << "Buffer timeout: " << m_buffer_timeout;
  TLOG_DEBUG(3) << "Should send timed out TDs: " << m_send_timed_out_tds;
  TLOG_DEBUG(3) << "TD readout limit: " << m_td_readout_limit;
}

void
ModuleLevelTrigger::do_start(const nlohmann::json& startobj)
{
  m_run_number = startobj.value<dunedaq::daqdataformats::run_number_t>("run", 0);

  m_paused.store(true);
  m_running_flag.store(true);
  m_dfo_is_busy.store(false);

  m_livetime_counter.reset(new LivetimeCounter(LivetimeCounter::State::kPaused));

  m_inhibit_receiver = get_iom_receiver<dfmessages::TriggerInhibit>(m_inhibit_connection);
  m_inhibit_receiver->add_callback(std::bind(&ModuleLevelTrigger::dfo_busy_callback, this, std::placeholders::_1));

  m_send_trigger_decisions_thread = std::thread(&ModuleLevelTrigger::send_trigger_decisions, this);
  pthread_setname_np(m_send_trigger_decisions_thread.native_handle(), "mlt-trig-dec");

  ers::info(TriggerStartOfRun(ERS_HERE, m_run_number));
}

void
ModuleLevelTrigger::do_stop(const nlohmann::json& /*stopobj*/)
{
  // flush all pending TDs at run stop
  flush_td_vectors();

  m_running_flag.store(false);
  m_send_trigger_decisions_thread.join();

  // Drop all TDs in vectors at run stage change. Have to do this
  // after joining m_send_trigger_decisions_thread so we don't
  // concurrently access the vectors
  clear_td_vectors();

  m_lc_deadtime = m_livetime_counter->get_time(LivetimeCounter::State::kDead) +
                  m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
  TLOG(3) << "LivetimeCounter - total deadtime+paused: " << m_lc_deadtime << std::endl;
  m_livetime_counter.reset(); // Calls LivetimeCounter dtor?

  m_inhibit_receiver->remove_callback();
  ers::info(TriggerEndOfRun(ERS_HERE, m_run_number));
}

void
ModuleLevelTrigger::do_pause(const nlohmann::json& /*pauseobj*/)
{
  // Drop all TDs in vetors at run stage change
  clear_td_vectors();

  m_paused.store(true);
  m_livetime_counter->set_state(LivetimeCounter::State::kPaused);
  TLOG() << "******* Triggers PAUSED! in run " << m_run_number << " *********";
  ers::info(TriggerPaused(ERS_HERE));
}

void
ModuleLevelTrigger::do_resume(const nlohmann::json& /*resumeobj*/)
{
  ers::info(TriggerActive(ERS_HERE));
  TLOG() << "******* Triggers RESUMED! in run " << m_run_number << " *********";
  m_livetime_counter->set_state(LivetimeCounter::State::kLive);
  m_paused.store(false);
}

void
ModuleLevelTrigger::do_scrap(const nlohmann::json& /*scrapobj*/)
{
  m_links.clear();
  m_configured_flag.store(false);
}

dfmessages::TriggerDecision
ModuleLevelTrigger::create_decision(const ModuleLevelTrigger::PendingTD& pending_td)
{
  m_earliest_tc_index = get_earliest_tc_index(pending_td);
  TLOG_DEBUG(3) << "earliest TC index: " << m_earliest_tc_index;

  if (pending_td.contributing_tcs.size() > 1) {
    TLOG_DEBUG(3) << "!!! TD created from " << pending_td.contributing_tcs.size() << " TCs !!!";
  }

  dfmessages::TriggerDecision decision;
  decision.trigger_number = m_last_trigger_number + 1;
  decision.run_number = m_run_number;
  decision.trigger_timestamp = pending_td.contributing_tcs[m_earliest_tc_index].time_candidate;
  decision.readout_type = dfmessages::ReadoutType::kLocalized;

  if (m_hsi_passthrough == true) {
    if (pending_td.contributing_tcs[m_earliest_tc_index].type == triggeralgs::TriggerCandidate::Type::kTiming) {
      decision.trigger_type = pending_td.contributing_tcs[m_earliest_tc_index].detid & 0xff;
    } else {
      m_trigger_type_shifted = (static_cast<int>(pending_td.contributing_tcs[m_earliest_tc_index].type) << 8);
      decision.trigger_type = m_trigger_type_shifted;
    }
  } else {
    decision.trigger_type = 1; // m_trigger_type;
  }

  TLOG_DEBUG(3) << "HSI passthrough: " << m_hsi_passthrough
                << ", TC detid: " << pending_td.contributing_tcs[m_earliest_tc_index].detid
                << ", TC type: " << static_cast<int>(pending_td.contributing_tcs[m_earliest_tc_index].type)
                << ", DECISION trigger type: " << decision.trigger_type
                << " request window begin: " << pending_td.readout_start
                << ", request window end: " << pending_td.readout_end;

  for (auto link : m_links) {
    dfmessages::ComponentRequest request;
    request.component = link;
    request.window_begin = pending_td.readout_start;
    request.window_end = pending_td.readout_end;

    decision.components.push_back(request);
  }

  return decision;
}

void
ModuleLevelTrigger::send_trigger_decisions()
{

  // We get here at start of run, so reset the trigger number
  m_last_trigger_number = 0;

  // OpMon.
  m_tc_received_count.store(0);
  m_td_sent_count.store(0);
  m_td_sent_tc_count.store(0);
  m_td_inhibited_count.store(0);
  m_td_inhibited_tc_count.store(0);
  m_td_paused_count.store(0);
  m_td_paused_tc_count.store(0);
  m_td_dropped_count.store(0);
  m_td_dropped_tc_count.store(0);
  m_td_cleared_count.store(0);
  m_td_cleared_tc_count.store(0);
  m_td_total_count.store(0);
  m_lc_kLive.store(0);
  m_lc_kPaused.store(0);
  m_lc_kDead.store(0);

  // New buffering logic here
  while (m_running_flag) {
    std::optional<triggeralgs::TriggerCandidate> tc = m_candidate_source->try_receive(std::chrono::milliseconds(10));
    if (tc.has_value()) {
      TLOG_DEBUG(1) << "Got TC of type " << static_cast<int>(tc->type) << ", timestamp " << tc->time_candidate
                    << ", start/end " << tc->time_start << "/" << tc->time_end;
      ++m_tc_received_count;
      std::lock_guard<std::mutex> lock(m_td_vector_mutex);
      add_tc(*tc);
      TLOG_DEBUG(3) << "pending tds size: " << m_pending_tds.size();
    } else {
      // The condition to exit the loop is that we've been stopped and
      // there's nothing left on the input queue
      if (!m_running_flag.load()) {
        break;
      }
    }

    std::lock_guard<std::mutex> lock(m_td_vector_mutex);
    auto ready_tds = get_ready_tds(m_pending_tds);
    TLOG_DEBUG(3) << "ready tds: " << ready_tds.size() << ", updated pending tds: " << m_pending_tds.size()
                  << ", sent tds: " << m_sent_tds.size();

    for (std::vector<PendingTD>::iterator it = ready_tds.begin(); it != ready_tds.end();) {
      if (check_overlap_td(*it)) {
        m_earliest_tc_index = get_earliest_tc_index(*it);
        auto const& earliest_tc = it->contributing_tcs[m_earliest_tc_index];
        ers::error(TCOutOfTimeout(ERS_HERE,
                                  get_name(),
                                  static_cast<int>(earliest_tc.type),
                                  earliest_tc.time_candidate,
                                  it->readout_start,
                                  it->readout_end));
        if (!m_send_timed_out_tds) { // if this is not set, drop the td
          ++m_td_dropped_count;
          m_td_dropped_tc_count += it->contributing_tcs.size();
          it = ready_tds.erase(it);
          TLOG_DEBUG(3) << "overlapping previous TD, dropping!";
        } else {
          call_tc_decision(*it);
          ++it;
        }
      } else {
        call_tc_decision(*it);
        ++it;
      }
    }

    TLOG_DEBUG(3) << "updated sent tds: " << m_sent_tds.size();
  }

  TLOG() << "Run " << m_run_number << ": "
         << "Received " << m_tc_received_count << " TCs. Sent " << m_td_sent_count.load() << " TDs consisting of "
         << m_td_sent_tc_count.load() << " TCs. " << m_td_paused_count.load() << " TDs (" << m_td_paused_tc_count.load()
         << " TCs) were created during pause, and " << m_td_inhibited_count.load() << " TDs ("
         << m_td_inhibited_tc_count.load() << " TCs) were inhibited. " << m_td_dropped_count.load() << " TDs ("
         << m_td_dropped_tc_count.load() << " TCs) were dropped. " << m_td_cleared_count.load() << " TDs ("
         << m_td_cleared_tc_count.load() << " TCs) were cleared.";

  m_lc_kLive_count = m_livetime_counter->get_time(LivetimeCounter::State::kLive);
  m_lc_kPaused_count = m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
  m_lc_kDead_count = m_livetime_counter->get_time(LivetimeCounter::State::kDead);
  m_lc_kLive = m_lc_kLive_count;
  m_lc_kPaused = m_lc_kPaused_count;
  m_lc_kDead = m_lc_kDead_count;

  m_lc_deadtime = m_livetime_counter->get_time(LivetimeCounter::State::kDead) +
                  m_livetime_counter->get_time(LivetimeCounter::State::kPaused);
}

void
ModuleLevelTrigger::call_tc_decision(const ModuleLevelTrigger::PendingTD& pending_td, bool override_flag)
{
  TLOG_DEBUG(3) << "Override?: " << override_flag;
  if ((!m_paused.load() && !m_dfo_is_busy.load()) || override_flag) {

    dfmessages::TriggerDecision decision = create_decision(pending_td);

    TLOG_DEBUG(1)
      << "Sending a decision with triggernumber " << decision.trigger_number << " timestamp "
      << decision.trigger_timestamp << " number of links " << decision.components.size() << " based on TC of type "
      << static_cast<std::underlying_type_t<decltype(pending_td.contributing_tcs[m_earliest_tc_index].type)>>(
           pending_td.contributing_tcs[m_earliest_tc_index].type);

    try {
      auto td_sender = get_iom_sender<dfmessages::TriggerDecision>(m_trigger_decision_connection);
      td_sender->send(std::move(decision), std::chrono::milliseconds(1));
      m_td_sent_count++;
      m_td_sent_tc_count += pending_td.contributing_tcs.size();
      m_last_trigger_number++;
      add_td(pending_td);
    } catch (const ers::Issue& e) {
      ers::error(e);
      TLOG_DEBUG(1) << "The network is misbehaving: it accepted TD but the send failed for "
                    << pending_td.contributing_tcs[m_earliest_tc_index].time_candidate;
      m_td_queue_timeout_expired_err_count++;
      m_td_queue_timeout_expired_err_tc_count += pending_td.contributing_tcs.size();
    }

  } else if (m_paused.load()) {
    ++m_td_paused_count;
    m_td_paused_tc_count += pending_td.contributing_tcs.size();
    TLOG_DEBUG(1) << "Triggers are paused. Not sending a TriggerDecision for pending TD with start/end times "
                  << pending_td.readout_start << "/" << pending_td.readout_end;
  } else {
    ers::warning(TriggerInhibited(ERS_HERE, m_run_number));
    TLOG_DEBUG(1) << "The DFO is busy. Not sending a TriggerDecision for candidate timestamp "
                  << pending_td.contributing_tcs[m_earliest_tc_index].time_candidate;
    m_td_inhibited_count++;
    m_td_inhibited_tc_count += pending_td.contributing_tcs.size();
  }
  m_td_total_count++;
}

void
ModuleLevelTrigger::add_tc(const triggeralgs::TriggerCandidate& tc)
{
  bool added_to_existing = false;
  int64_t tc_wallclock_arrived =
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

  for (std::vector<PendingTD>::iterator it = m_pending_tds.begin(); it != m_pending_tds.end();) {
    if (check_overlap(tc, *it)) {
      TLOG_DEBUG(3) << "TC with start/end times " << tc.time_start << "/" << tc.time_end
                    << " overlaps with pending TD with start/end times " << it->readout_start << "/" << it->readout_end;
      it->contributing_tcs.push_back(tc);
      it->readout_start = (tc.time_start >= it->readout_start) ? it->readout_start : tc.time_start;
      it->readout_end = (tc.time_end >= it->readout_end) ? tc.time_end : it->readout_end;
      it->walltime_expiration = tc_wallclock_arrived + m_buffer_timeout;
      added_to_existing = true;
      break;
    }
    ++it;
  }

  if (!added_to_existing) {
    PendingTD td_candidate;
    td_candidate.contributing_tcs.push_back(tc);
    td_candidate.readout_start = tc.time_start;
    td_candidate.readout_end = tc.time_end;
    td_candidate.walltime_expiration = tc_wallclock_arrived + m_buffer_timeout;
    m_pending_tds.push_back(td_candidate);
  }
  return;
}

bool
ModuleLevelTrigger::check_overlap(const triggeralgs::TriggerCandidate& tc, const PendingTD& pending_td)
{
  return !((tc.time_end < pending_td.readout_start) || (tc.time_start > pending_td.readout_end));
}

bool
ModuleLevelTrigger::check_overlap_td(const PendingTD& pending_td)
{
  bool overlap;

  for (PendingTD sent_td : m_sent_tds) {
    if ((pending_td.readout_end < sent_td.readout_start) || (pending_td.readout_start > sent_td.readout_end)) {
      overlap = false;
    } else {
      overlap = true;
      TLOG_DEBUG(3) << "Pending TD with start/end " << pending_td.readout_start << "/" << pending_td.readout_end
                    << " overlaps with sent TD with start/end " << sent_td.readout_start << "/" << sent_td.readout_end;
      break;
    }
  }
  return overlap;
}

void
ModuleLevelTrigger::add_td(const PendingTD& pending_td)
{
  m_sent_tds.push_back(pending_td);
  while (m_sent_tds.size() > 20) {
    m_sent_tds.erase(m_sent_tds.begin());
  }
}

std::vector<ModuleLevelTrigger::PendingTD>
ModuleLevelTrigger::get_ready_tds(std::vector<PendingTD>& pending_tds)
{
  std::vector<PendingTD> return_tds;
  for (std::vector<PendingTD>::iterator it = pending_tds.begin(); it != pending_tds.end();) {
    auto timestamp_now =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
    if (timestamp_now >= it->walltime_expiration) {
      return_tds.push_back(*it);
      it = pending_tds.erase(it);
    } else if (check_td_readout_length(*it)) { // Also pass on TDs with (too) long readout window
      return_tds.push_back(*it);
      it = pending_tds.erase(it);
    } else {
      ++it;
    }
  }
  return return_tds;
}

int
ModuleLevelTrigger::get_earliest_tc_index(const PendingTD& pending_td)
{
  int earliest_tc_index = -1;
  triggeralgs::timestamp_t earliest_tc_time;
  for (int i = 0; i < static_cast<int>(pending_td.contributing_tcs.size()); i++) {
    if (earliest_tc_index == -1) {
      earliest_tc_time = pending_td.contributing_tcs[i].time_candidate;
      earliest_tc_index = i;
    } else {
      if (pending_td.contributing_tcs[i].time_candidate < earliest_tc_time) {
        earliest_tc_time = pending_td.contributing_tcs[i].time_candidate;
        earliest_tc_index = i;
      }
    }
  }
  return earliest_tc_index;
}

bool
ModuleLevelTrigger::check_td_readout_length(const PendingTD& pending_td)
{
  bool td_too_long = false;
  if (static_cast<int64_t>(pending_td.readout_end - pending_td.readout_start) >= m_td_readout_limit) {
    td_too_long = true;
    TLOG_DEBUG(3) << "Too long readout window: " << (pending_td.readout_end - pending_td.readout_start)
                  << ", sending immediate TD!";
  }
  return td_too_long;
}

void
ModuleLevelTrigger::flush_td_vectors()
{
  TLOG_DEBUG(3) << "Flushing TDs. Size: " << m_pending_tds.size();
  std::lock_guard<std::mutex> lock(m_td_vector_mutex);
  for (PendingTD pending_td : m_pending_tds) {
    call_tc_decision(pending_td, true);
  }
}

void
ModuleLevelTrigger::clear_td_vectors()
{
  std::lock_guard<std::mutex> lock(m_td_vector_mutex);
  TLOG_DEBUG(1) << "clear_td_vectors() clearing " << m_pending_tds.size() << " pending TDs and " << m_sent_tds.size()
                << " sent TDs";
  m_td_cleared_count += m_pending_tds.size();
  for (PendingTD pending_td : m_pending_tds) {
    m_td_cleared_tc_count += pending_td.contributing_tcs.size();
  }
  m_pending_tds.clear();
  m_sent_tds.clear();
}

void
ModuleLevelTrigger::dfo_busy_callback(dfmessages::TriggerInhibit& inhibit)
{
  TLOG_DEBUG(17) << "Received inhibit message with busy status " << inhibit.busy << " and run number "
                 << inhibit.run_number;
  if (inhibit.run_number == m_run_number) {
    TLOG_DEBUG(18) << "Changing our flag for the DFO busy state from " << m_dfo_is_busy.load() << " to "
                   << inhibit.busy;
    m_dfo_is_busy = inhibit.busy;
    m_livetime_counter->set_state(LivetimeCounter::State::kDead);
  }
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::ModuleLevelTrigger)

// Local Variables:
// c-basic-offset: 2
// End:
