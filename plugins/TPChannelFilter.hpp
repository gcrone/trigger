/**
 * @file TPChannelFilter.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TPCHANNELFILTER_HPP_
#define TRIGGER_PLUGINS_TPCHANNELFILTER_HPP_

#include "trigger/Issues.hpp"
#include "trigger/TPSet.hpp"
#include "trigger/tpchannelfilter/Nljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "detchannelmaps/TPCChannelMap.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "utilities/WorkerThread.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace dunedaq {
namespace trigger {
class TPChannelFilter : public dunedaq::appfwk::DAQModule
{
public:
  explicit TPChannelFilter(const std::string& name);

  TPChannelFilter(const TPChannelFilter&) = delete;
  TPChannelFilter& operator=(const TPChannelFilter&) = delete;
  TPChannelFilter(TPChannelFilter&&) = delete;
  TPChannelFilter& operator=(TPChannelFilter&&) = delete;

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  void do_conf(const nlohmann::json& config);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);
  void do_work(std::atomic<bool>&);

  bool channel_should_be_removed(int channel) const;
  dunedaq::utilities::WorkerThread m_thread;

  using source_t = dunedaq::iomanager::ReceiverConcept<TPSet>;
  std::shared_ptr<source_t> m_input_queue;
  using sink_t = dunedaq::iomanager::SenderConcept<TPSet>;
  std::shared_ptr<sink_t> m_output_queue;
  std::chrono::milliseconds m_queue_timeout;

  std::shared_ptr<detchannelmaps::TPCChannelMap> m_channel_map;

  dunedaq::trigger::tpchannelfilter::Conf m_conf;
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_TPCHANNELFILTER_HPP_
