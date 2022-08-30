/**
 * @file FakeTPCreatorHeartbeatMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "FakeTPCreatorHeartbeatMaker.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "iomanager/IOManager.hpp"
#include "rcif/cmd/Nljs.hpp"

#include <string>

namespace dunedaq {
namespace trigger {
FakeTPCreatorHeartbeatMaker::FakeTPCreatorHeartbeatMaker(const std::string& name)
  : DAQModule(name)
  , m_thread(std::bind(&FakeTPCreatorHeartbeatMaker::do_work, this, std::placeholders::_1))
  , m_input_queue(nullptr)
  , m_output_queue(nullptr)
  , m_queue_timeout(100)
{

  register_command("conf", &FakeTPCreatorHeartbeatMaker::do_conf);
  register_command("start", &FakeTPCreatorHeartbeatMaker::do_start);
  register_command("stop", &FakeTPCreatorHeartbeatMaker::do_stop);
  register_command("scrap", &FakeTPCreatorHeartbeatMaker::do_scrap);
}

void
FakeTPCreatorHeartbeatMaker::init(const nlohmann::json& iniobj)
{
  try {
    m_input_queue = get_iom_receiver<trigger::TPSet>(appfwk::connection_inst(iniobj, "tpset_source"));
    m_output_queue = get_iom_sender<trigger::TPSet>(appfwk::connection_inst(iniobj, "tpset_sink"));
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }
}

void
FakeTPCreatorHeartbeatMaker::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  faketpcreatorheartbeatmakerinfo::Info i;

  i.tpset_received_count = m_tpset_received_count.load();
  i.tpset_sent_count = m_tpset_sent_count.load();
  i.heartbeats_sent = m_heartbeats_sent.load();

  ci.add(i);
}

void
FakeTPCreatorHeartbeatMaker::do_conf(const nlohmann::json& conf)
{
  m_heartbeat_interval = conf.get<dunedaq::trigger::faketpcreatorheartbeatmaker::Conf>().heartbeat_interval;
  TLOG_DEBUG(2) << get_name() + " configured.";
}

void
FakeTPCreatorHeartbeatMaker::do_start(const nlohmann::json& args)
{
  rcif::cmd::StartParams start_params = args.get<rcif::cmd::StartParams>();
  m_run_number = start_params.run;

  m_thread.start_working_thread("heartbeater");
  TLOG_DEBUG(2) << get_name() + " successfully started.";
}

void
FakeTPCreatorHeartbeatMaker::do_stop(const nlohmann::json&)
{
  m_thread.stop_working_thread();
  TLOG_DEBUG(2) << get_name() + " successfully stopped.";
}

void
FakeTPCreatorHeartbeatMaker::do_scrap(const nlohmann::json&)
{}

void
FakeTPCreatorHeartbeatMaker::do_work(std::atomic<bool>& running_flag)
{
  // OpMon.
  m_tpset_received_count.store(0);
  m_tpset_sent_count.store(0);
  m_heartbeats_sent.store(0);

  bool is_first_tpset_received = true;

  daqdataformats::timestamp_t last_sent_heartbeat_time;

  TPSet::seqno_t sequence_number = 0;
  
  while (true) {
    std::optional<TPSet> tpset = m_input_queue->try_receive(m_queue_timeout);
    if(!tpset.has_value()){
      // The condition to exit the loop is that we've been stopped and
      // there's nothing left on the input queue
      if (!running_flag.load()) {
        break;
      } else {
        continue;
      }
    }

    // We got a TPSet
    m_tpset_received_count++;
    if (m_sourceid.id == daqdataformats::SourceID::s_invalid_id) {
      m_sourceid = tpset->origin;
    }
    TLOG_DEBUG(3) << "Activity received.";

    daqdataformats::timestamp_t current_tpset_start_time = tpset->start_time;

    bool send_heartbeat =
      should_send_heartbeat(last_sent_heartbeat_time, current_tpset_start_time, is_first_tpset_received);

    bool successfully_sent_real_tpset = false;
    bool successfully_sent_heartbeat = false;

    if (send_heartbeat) {
      TPSet tpset_heartbeat;
      get_heartbeat(tpset_heartbeat, current_tpset_start_time);
      tpset_heartbeat.seqno = sequence_number;
      ++sequence_number;
      while (!successfully_sent_heartbeat) {
        try {
          m_output_queue->send(std::move(tpset_heartbeat), m_queue_timeout);
          successfully_sent_heartbeat = true;
          m_heartbeats_sent++;
          last_sent_heartbeat_time = current_tpset_start_time;
          is_first_tpset_received = false;
        } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
          std::ostringstream oss_warn;
          oss_warn << "push to output queue \"" << m_output_queue->get_name() << "\"";
          ers::warning(
            dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queue_timeout.count()));
        }
      }
    }
    
    tpset->seqno = sequence_number;
    ++sequence_number;

    while (!successfully_sent_real_tpset) {
      try {
        m_output_queue->send(std::move(*tpset), m_queue_timeout);
        successfully_sent_real_tpset = true;
        m_tpset_sent_count++;
      } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
        std::ostringstream oss_warn;
        oss_warn << "push to output queue \"" << m_output_queue->get_name() << "\"";
        ers::warning(dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queue_timeout.count()));
      }
    }
  }

  TLOG() << "Received " << m_tpset_received_count << " and sent " << m_tpset_sent_count << " real TPSets. Sent "
         << m_heartbeats_sent << " fake heartbeats." << std::endl;
  TLOG_DEBUG(2) << "Exiting do_work() method";
}

bool
FakeTPCreatorHeartbeatMaker::should_send_heartbeat(daqdataformats::timestamp_t const& last_sent_heartbeat_time,
                                                   daqdataformats::timestamp_t const& current_tpset_start_time,
                                                   bool const& is_first_tpset_received)
{
  // If it is the first TPSet received, send out a heartbeat.
  // Else, can assume that the TPSets are already ordered by start_time. Therefore, check only
  // that the difference between the start_time of the current TPSet and the time that the
  // last heartbeat was sent out is greater than the specified heartbeat interval.
  if (is_first_tpset_received)
    return true;
  else
    return last_sent_heartbeat_time + m_heartbeat_interval < current_tpset_start_time;
}

void
FakeTPCreatorHeartbeatMaker::get_heartbeat(TPSet& tpset_heartbeat,
                                           daqdataformats::timestamp_t const& current_tpset_start_time)
{
  tpset_heartbeat.type = TPSet::Type::kHeartbeat;
  tpset_heartbeat.start_time = current_tpset_start_time;
  tpset_heartbeat.end_time = current_tpset_start_time;
  tpset_heartbeat.run_number = m_run_number;
  tpset_heartbeat.origin = m_sourceid;
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::FakeTPCreatorHeartbeatMaker)
