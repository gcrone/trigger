/**
 * @file RandomTriggerCandidateMaker.cpp RandomTriggerCandidateMaker class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "RandomTriggerCandidateMaker.hpp"

#include "trigger/Issues.hpp"

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
#include "timinglibs/TimestampEstimatorSystem.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <random>
#include <string>
#include <vector>

namespace dunedaq {
namespace trigger {

RandomTriggerCandidateMaker::RandomTriggerCandidateMaker(const std::string& name)
  : DAQModule(name)
  , m_time_sync_source(nullptr)
  , m_trigger_candidate_sink(nullptr)
  , m_run_number(0)
{
  register_command("conf", &RandomTriggerCandidateMaker::do_configure);
  register_command("start", &RandomTriggerCandidateMaker::do_start);
  register_command("stop", &RandomTriggerCandidateMaker::do_stop);
  register_command("scrap", &RandomTriggerCandidateMaker::do_scrap);
}

void
RandomTriggerCandidateMaker::init(const nlohmann::json& obj)
{
  m_time_sync_source = get_iom_receiver<dfmessages::TimeSync>(appfwk::connection_inst(obj, "time_sync_source"));
  m_trigger_candidate_sink = get_iom_sender<triggeralgs::TriggerCandidate>(appfwk::connection_inst(obj, "trigger_candiate_sink"));
}

void
RandomTriggerCandidateMaker::get_info(opmonlib::InfoCollector& ci, int /*level*/)
{
  randomtriggercandidatemakerinfo::Info i;

  i.tc_sent_count = m_tc_sent_count.load();

  ci.add(i);
}

void
RandomTriggerCandidateMaker::do_configure(const nlohmann::json& obj)
{
  m_conf = obj.get<randomtriggercandidatemaker::ConfParams>();
}

void
RandomTriggerCandidateMaker::do_start(const nlohmann::json& obj)
{
  m_run_number = obj.value<dunedaq::daqdataformats::run_number_t>("run", 0);

  m_running_flag.store(true);

  switch (m_conf.timestamp_method) {
    case randomtriggercandidatemaker::timestamp_estimation::kTimeSync:
      TLOG_DEBUG(0) << "Creating TimestampEstimator";
      m_timestamp_estimator.reset(new timinglibs::TimestampEstimator(m_time_sync_source, m_conf.clock_frequency_hz));
      break;
    case randomtriggercandidatemaker::timestamp_estimation::kSystemClock:
      TLOG_DEBUG(0) << "Creating TimestampEstimatorSystem";
      m_timestamp_estimator.reset(new timinglibs::TimestampEstimatorSystem(m_conf.clock_frequency_hz));
      break;
  }

  m_send_trigger_candidates_thread = std::thread(&RandomTriggerCandidateMaker::send_trigger_candidates, this);
  pthread_setname_np(m_send_trigger_candidates_thread.native_handle(), "random-tc-maker");
}

void
RandomTriggerCandidateMaker::do_stop(const nlohmann::json& /*obj*/)
{
  m_running_flag.store(false);

  m_send_trigger_candidates_thread.join();

  m_timestamp_estimator.reset(nullptr); // Calls TimestampEstimator dtor
}

void
RandomTriggerCandidateMaker::do_scrap(const nlohmann::json& /*obj*/)
{
  m_configured_flag.store(false);
}

triggeralgs::TriggerCandidate
RandomTriggerCandidateMaker::create_candidate(dfmessages::timestamp_t timestamp)
{
  triggeralgs::TriggerCandidate candidate;
  candidate.time_start = timestamp;
  candidate.time_end = timestamp;
  candidate.time_candidate = timestamp;
  candidate.detid = { 0 };
  candidate.type = triggeralgs::TriggerCandidate::Type::kRandom;
  candidate.algorithm = triggeralgs::TriggerCandidate::Algorithm::kHSIEventToTriggerCandidate;

  return candidate;
}

int
RandomTriggerCandidateMaker::get_interval(std::mt19937& gen)
{
  switch (m_conf.time_distribution) {
    default: // Treat an unknown distribution as kUniform, but warn
      TLOG_DEBUG(1) << get_name() << " unknown distribution! Using kUniform.";
      // fall through
    case randomtriggercandidatemaker::distribution_type::kUniform:
      return m_conf.trigger_interval_ticks;
    case randomtriggercandidatemaker::distribution_type::kPoisson:
      std::exponential_distribution<double> d(1.0 / m_conf.trigger_interval_ticks);
      return static_cast<int>(0.5 + d(gen));
  }
}

void
RandomTriggerCandidateMaker::send_trigger_candidates()
{
  // OpMon.
  m_tc_sent_count.store(0);

  std::mt19937 gen(m_run_number);
  // Wait for there to be a valid timestamp estimate before we start
  if (m_timestamp_estimator->wait_for_valid_timestamp(m_running_flag) ==
      timinglibs::TimestampEstimatorBase::kInterrupted) {
    return;
  }

  dfmessages::timestamp_t initial_timestamp = m_timestamp_estimator->get_timestamp_estimate();
  dfmessages::timestamp_t first_interval = get_interval(gen);
  // Round up to the next multiple of trigger_interval_ticks
  dfmessages::timestamp_t next_trigger_timestamp = (initial_timestamp / first_interval + 1) * first_interval;
  TLOG_DEBUG(1) << get_name() << " initial timestamp estimate is " << initial_timestamp
                << ", next_trigger_timestamp is " << next_trigger_timestamp;

  while (m_running_flag.load()) {
    if (m_timestamp_estimator->wait_for_timestamp(next_trigger_timestamp, m_running_flag) ==
        timinglibs::TimestampEstimatorBase::kInterrupted) {
      break;
    }

    triggeralgs::TriggerCandidate candidate = create_candidate(next_trigger_timestamp);

    TLOG_DEBUG(1) << get_name() << " at timestamp " << m_timestamp_estimator->get_timestamp_estimate()
                  << ", pushing a candidate with timestamp " << candidate.time_candidate;
    m_trigger_candidate_sink->send(std::move(candidate), std::chrono::milliseconds(10));
    m_tc_sent_count++;

    next_trigger_timestamp += get_interval(gen);
  }
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::RandomTriggerCandidateMaker)

// Local Variables:
// c-basic-offset: 2
// End:
