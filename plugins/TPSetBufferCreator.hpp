/**
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TPSETBUFFERCREATOR_HPP_
#define TRIGGER_PLUGINS_TPSETBUFFERCREATOR_HPP_

#include "daqdataformats/Fragment.hpp"
#include "daqdataformats/Types.hpp"

#include "dfmessages/DataRequest.hpp"
#include "dfmessages/HSIEvent.hpp"

#include "trigger/TPSet.hpp"
#include "trigger/TPSetBuffer.hpp"
#include "trigger/tpsetbuffercreator/Nljs.hpp"
#include "trigger/tpsetbuffercreator/Structs.hpp"

#include "appfwk/DAQModule.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "utilities/WorkerThread.hpp"

#include <ers/Issue.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dunedaq {
namespace trigger {

/**
 * @brief TPSetBufferCreator creates a buffer that stores TPSets and handles data requests.
 */
class TPSetBufferCreator : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief TPSetBufferCreator Constructor
   * @param name Instance name for this TPSetBufferCreator instance
   */
  explicit TPSetBufferCreator(const std::string& name);

  TPSetBufferCreator(const TPSetBufferCreator&) = delete;            ///< TPSetBufferCreator is not copy-constructible
  TPSetBufferCreator& operator=(const TPSetBufferCreator&) = delete; ///< TPSetBufferCreator is not copy-assignable
  TPSetBufferCreator(TPSetBufferCreator&&) = delete;                 ///< TPSetBufferCreator is not move-constructible
  TPSetBufferCreator& operator=(TPSetBufferCreator&&) = delete;      ///< TPSetBufferCreator is not move-assignable

  void init(const nlohmann::json& obj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:
  // Commands
  void do_configure(const nlohmann::json& obj);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);

  // Threading
  dunedaq::utilities::WorkerThread m_thread;
  void do_work(std::atomic<bool>&);

  // Configuration

  tpsetbuffercreator::Conf m_conf;

  std::chrono::milliseconds m_queueTimeout;

  using tps_source_t = dunedaq::iomanager::ReceiverConcept<trigger::TPSet>;
  std::shared_ptr<tps_source_t> m_input_queue_tps;

  using dr_source_t = dunedaq::iomanager::ReceiverConcept<dfmessages::DataRequest>;
  std::shared_ptr<dr_source_t> m_input_queue_dr;

  using fragment_sink_t = dunedaq::iomanager::SenderConcept<std::pair<std::unique_ptr<daqdataformats::Fragment>, std::string>>;
  std::shared_ptr<fragment_sink_t> m_output_queue_frag;

  std::unique_ptr<trigger::TPSetBuffer> m_tps_buffer;

  uint64_t m_tps_buffer_size; // NOLINT(build/unsigned)

  struct DataRequestComp
  {
    bool operator()(const dfmessages::DataRequest& left, const dfmessages::DataRequest& right) const
    {
      return left.request_information.window_begin < right.request_information.window_end;
    }
  };

  std::map<dfmessages::DataRequest, std::vector<trigger::TPSet>, DataRequestComp>
    m_dr_on_hold; ///< Holds data request when data has not arrived in the buffer yet

  std::unique_ptr<daqdataformats::Fragment> convert_to_fragment(std::vector<TPSet>&,
                                                                dfmessages::DataRequest);

  void send_out_fragment(std::unique_ptr<daqdataformats::Fragment>, std::string, size_t&, std::atomic<bool>&);
  void send_out_fragment(std::unique_ptr<daqdataformats::Fragment>, std::string);
};
} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_PLUGINS_TPSETBUFFERCREATOR_HPP_

// Local Variables:
// c-basic-offset: 2
// End:
