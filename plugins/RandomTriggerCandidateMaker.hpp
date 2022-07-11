/**
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_
#define TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_

#include "trigger/TokenManager.hpp"
#include "trigger/randomtriggercandidatemaker/Nljs.hpp"
#include "trigger/randomtriggercandidatemakerinfo/InfoNljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "daqdataformats/SourceID.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "timinglibs/TimestampEstimator.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

/**
 * @brief RandomTriggerCandidateMaker creates TriggerCandidates at regular or
 * Poisson random intervals, based on input from a TimeSync queue or the system
 * clock. The TCs can be fed directly into the MLT.
 */
class RandomTriggerCandidateMaker : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief RandomTriggerCandidateMaker Constructor
   * @param name Instance name for this RandomTriggerCandidateMaker instance
   */
  explicit RandomTriggerCandidateMaker(const std::string& name);

  RandomTriggerCandidateMaker(const RandomTriggerCandidateMaker&) =
    delete; ///< RandomTriggerCandidateMaker is not copy-constructible
  RandomTriggerCandidateMaker& operator=(const RandomTriggerCandidateMaker&) =
    delete; ///< RandomTriggerCandidateMaker is not copy-assignable
  RandomTriggerCandidateMaker(RandomTriggerCandidateMaker&&) =
    delete; ///< RandomTriggerCandidateMaker is not move-constructible
  RandomTriggerCandidateMaker& operator=(RandomTriggerCandidateMaker&&) =
    delete; ///< RandomTriggerCandidateMaker is not move-assignable

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  void send_trigger_candidates();
  std::thread m_send_trigger_candidates_thread;

  std::unique_ptr<timinglibs::TimestampEstimatorBase> m_timestamp_estimator;

  // Create the next trigger decision
  triggeralgs::TriggerCandidate create_candidate(dfmessages::timestamp_t timestamp);

  // Queue sources and sinks
  std::shared_ptr<iomanager::ReceiverConcept<dfmessages::TimeSync>> m_time_sync_source;
  std::shared_ptr<iomanager::SenderConcept<triggeralgs::TriggerCandidate>> m_trigger_candidate_sink;

  randomtriggercandidatemaker::ConfParams m_conf;

  int get_interval(std::mt19937& gen);

  dfmessages::run_number_t m_run_number;

  // Are we in the RUNNING state?
  std::atomic<bool> m_running_flag{ false };
  // Are we in a configured state, ie after conf and before scrap?
  std::atomic<bool> m_configured_flag{ false };

  // OpMon variables
  using metric_counter_type = decltype(randomtriggercandidatemakerinfo::Info::tc_sent_count);
  std::atomic<metric_counter_type> m_tc_sent_count{ 0 };
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_RANDOMTRIGGERCANDIDATEMAKER_HPP_
