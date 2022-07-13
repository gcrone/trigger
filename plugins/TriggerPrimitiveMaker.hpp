/**
 * @file TriggerPrimitiveMaker.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TRIGGERPRIMITIVEMAKER_HPP_
#define TRIGGER_PLUGINS_TRIGGERPRIMITIVEMAKER_HPP_

#include "trigger/TPSet.hpp"
#include "trigger/triggerprimitivemaker/Nljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "iomanager/Sender.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"
#include "triggeralgs/Types.hpp"
#include "utilities/WorkerThread.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dunedaq {
namespace trigger {
class TriggerPrimitiveMaker : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief RandomDataListGenerator Constructor
   * @param name Instance name for this RandomDataListGenerator instance
   */
  explicit TriggerPrimitiveMaker(const std::string& name);

  TriggerPrimitiveMaker(const TriggerPrimitiveMaker&) = delete; ///< TriggerPrimitiveMaker is not copy-constructible
  TriggerPrimitiveMaker& operator=(const TriggerPrimitiveMaker&) =
    delete;                                                ///< TriggerPrimitiveMaker is not copy-assignable
  TriggerPrimitiveMaker(TriggerPrimitiveMaker&&) = delete; ///< TriggerPrimitiveMaker is not move-constructible
  TriggerPrimitiveMaker& operator=(TriggerPrimitiveMaker&&) = delete; ///< TriggerPrimitiveMaker is not move-assignable

  void init(const nlohmann::json& obj) override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  // Threading
  void do_work(std::atomic<bool>&,
               std::vector<TPSet>& tpsets,
               std::shared_ptr<iomanager::SenderConcept<TPSet>>& tpset_sink,
               std::chrono::steady_clock::time_point earliest_timestamp_time);
  std::vector<std::unique_ptr<std::thread>> m_threads;
  std::atomic<bool> m_running_flag;

  std::vector<TPSet> read_tpsets(std::string filename, int region, int element);

  // Configuration
  triggerprimitivemaker::ConfParams m_conf;

  daqdataformats::run_number_t m_run_number{ daqdataformats::TypeDefaults::s_invalid_run_number };

  nlohmann::json m_init_obj; // Stash this so we know name -> instance mappings

  struct TPStream
  {
    std::shared_ptr<iomanager::SenderConcept<TPSet>> tpset_sink;
    std::vector<TPSet> tpsets;
  };

  std::vector<TPStream> m_tp_streams;

  std::chrono::milliseconds m_queue_timeout;

  // Variables to keep track of the total time span of multiple TP streams
  triggeralgs::timestamp_t m_earliest_first_tpset_timestamp;
  triggeralgs::timestamp_t m_latest_last_tpset_timestamp;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_TRIGGERPRIMITIVEMAKER_HPP_
