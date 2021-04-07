/**
 * @file ModuleLevelTrigger.hpp
 *
 * ModuleLevelTrigger is a DAQModule that generates trigger decisions
 * for standalone tests. It receives information on the current time and the
 * availability of the DF to absorb data and forms decisions at a configurable
 * rate and with configurable size.
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_
#define TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_

#include "trigger/TimestampEstimator.hpp"
#include "trigger/TokenManager.hpp"

#include "dune-trigger-algs/TriggerCandidate.hh"

#include "dataformats/GeoID.hpp"
#include "dfmessages/TimeSync.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerDecisionToken.hpp"
#include "dfmessages/TriggerInhibit.hpp"
#include "dfmessages/Types.hpp"

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQSink.hpp"
#include "appfwk/DAQSource.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dunedaq {

namespace trigger {

/**
 * @brief ModuleLevelTrigger reads lists of integers from one queue,
 * reverses the order of the list, and writes out the reversed list.
 */
class ModuleLevelTrigger : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief ModuleLevelTrigger Constructor
   * @param name Instance name for this ModuleLevelTrigger instance
   */
  explicit ModuleLevelTrigger(const std::string& name);

  ModuleLevelTrigger(const ModuleLevelTrigger&) = delete;            ///< ModuleLevelTrigger is not copy-constructible
  ModuleLevelTrigger& operator=(const ModuleLevelTrigger&) = delete; ///< ModuleLevelTrigger is not copy-assignable
  ModuleLevelTrigger(ModuleLevelTrigger&&) = delete;                 ///< ModuleLevelTrigger is not move-constructible
  ModuleLevelTrigger& operator=(ModuleLevelTrigger&&) = delete;      ///< ModuleLevelTrigger is not move-assignable

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_pause(const nlohmann::json& obj);
  void do_resume(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  void send_trigger_decisions();
  std::thread m_send_trigger_decisions_thread;

  std::unique_ptr<TokenManager> m_token_manager;

  // Create the next trigger decision
  dfmessages::TriggerDecision create_decision(const triggeralgs::TriggerCandidate& tc);

  // Queue sources and sinks
  std::unique_ptr<appfwk::DAQSource<dfmessages::TriggerDecisionToken>> m_token_source;
  std::unique_ptr<appfwk::DAQSink<dfmessages::TriggerDecision>> m_trigger_decision_sink;
  std::unique_ptr<appfwk::DAQSource<triggeralgs::TriggerCandidate>> m_candidate_source;
  
  std::vector<dfmessages::GeoID> m_links;

  int m_repeat_trigger_count{ 1 };

  // paused state, in which we don't send triggers
  std::atomic<bool> m_paused;

  int m_initial_tokens;

  dfmessages::trigger_number_t m_last_trigger_number;

  dfmessages::run_number_t m_run_number;

  // Are we in the RUNNING state?
  std::atomic<bool> m_running_flag{ false };
  // Are we in a configured state, ie after conf and before scrap?
  std::atomic<bool> m_configured_flag{ false };

  // Opmon variables
  std::atomic<uint64_t> m_trigger_count{ 0 };
  std::atomic<uint64_t> m_trigger_count_tot{ 0 };
  std::atomic<uint64_t> m_inhibited_trigger_count{ 0 };
  std::atomic<uint64_t> m_inhibited_trigger_count_tot{ 0 };
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_MODULELEVELTRIGGER_HPP_

// Local Variables:
// c-basic-offset: 2
// End:
