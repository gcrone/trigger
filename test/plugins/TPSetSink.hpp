/**
 * @file TPSetSink.hpp
 *
 * TPSetSink is a DAQModule that emulates the data flow application for the
 * sole purpose of testing trigger modules standalone.
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_TEST_PLUGINS_TPSETSINK_HPP_
#define TRIGGER_TEST_PLUGINS_TPSETSINK_HPP_

#include "trigger/TPSet.hpp"

#include "appfwk/DAQModule.hpp"
#include "iomanager/Receiver.hpp"

#include <memory>
#include <string>

namespace dunedaq {

namespace trigger {

/**
 * @brief TPSetSink receives TPSets from a queue and prints them out
 */
class TPSetSink : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief TPSetSink Constructor
   * @param name Instance name for this TPSetSink instance
   */
  explicit TPSetSink(const std::string& name);

  TPSetSink(const TPSetSink&) = delete;            ///< TPSetSink is not copy-constructible
  TPSetSink& operator=(const TPSetSink&) = delete; ///< TPSetSink is not copy-assignable
  TPSetSink(TPSetSink&&) = delete;                 ///< TPSetSink is not move-constructible
  TPSetSink& operator=(TPSetSink&&) = delete;      ///< TPSetSink is not move-assignable

  void init(const nlohmann::json& obj) override;

private:
  // Commands
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);

  void do_work();

  std::atomic<bool> m_running_flag{ false };
  std::thread m_thread;

  // Queue sources and sinks
  using source_t = iomanager::ReceiverConcept<TPSet>;
  std::shared_ptr<source_t> m_tpset_source;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_TEST_PLUGINS_TPSETSINK_HPP_
