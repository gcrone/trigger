/**
 * @file TPBuffer.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TPBUFFER_HPP_
#define TRIGGER_PLUGINS_TPBUFFER_HPP_

#include "iomanager/Receiver.hpp"
#include "readoutlibs/FrameErrorRegistry.hpp"
#include "readoutlibs/models/DefaultSkipListRequestHandler.hpp"
#include "readoutlibs/models/SkipListLatencyBufferModel.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"
#include "utilities/WorkerThread.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TPSet.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace dunedaq {
namespace trigger {
class TPBuffer : public dunedaq::appfwk::DAQModule
{
public:
  explicit TPBuffer(const std::string& name);

  TPBuffer(const TPBuffer&) = delete;
  TPBuffer& operator=(const TPBuffer&) = delete;
  TPBuffer(TPBuffer&&) = delete;
  TPBuffer& operator=(TPBuffer&&) = delete;

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:

  struct TPWrapper
  {
    triggeralgs::TriggerPrimitive primitive;

    // Don't really want this default ctor, but IterableQueueModel requires it
    TPWrapper() {}
    
    TPWrapper(triggeralgs::TriggerPrimitive p)
      : primitive(p)
    {}
    
    // comparable based on first timestamp
    bool operator<(const TPWrapper& other) const
    {
      return this->primitive.time_start < other.primitive.time_start;
    }

    uint64_t get_first_timestamp() const // NOLINT(build/unsigned)
    {
      return primitive.time_start;
    }

    void set_first_timestamp(uint64_t ts) // NOLINT(build/unsigned)
    {
      primitive.time_start = ts;
    }

    uint64_t get_timestamp() const // NOLINT(build/unsigned)
    {
      return primitive.time_start;
    }


    size_t get_payload_size() { return sizeof(triggeralgs::TriggerPrimitive); }

    size_t get_num_frames() { return 1; }

    size_t get_frame_size() { return get_payload_size(); }

    triggeralgs::TriggerPrimitive* begin()
    {
      return &primitive;
    }
    
    triggeralgs::TriggerPrimitive* end()
    {
      return &primitive + 1;
    }

    //static const constexpr size_t fixed_payload_size = 5568;
    static const constexpr daqdataformats::GeoID::SystemType system_type = daqdataformats::GeoID::SystemType::kDataSelection;
    static const constexpr daqdataformats::FragmentType fragment_type = daqdataformats::FragmentType::kTriggerPrimitives;
    // No idea what this should really be set to
    static const constexpr uint64_t expected_tick_difference = 16; // NOLINT(build/unsigned)
};

  void do_conf(const nlohmann::json& config);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);
  void do_work(std::atomic<bool>&);

  dunedaq::utilities::WorkerThread m_thread;

  using tps_source_t = iomanager::ReceiverConcept<trigger::TPSet>;
  std::shared_ptr<tps_source_t> m_input_queue_tps{nullptr};

  using dr_source_t = iomanager::ReceiverConcept<dfmessages::DataRequest>;
  std::shared_ptr<dr_source_t> m_input_queue_dr{nullptr};

  std::chrono::milliseconds m_queue_timeout;

  using buffer_object_t = TPWrapper;
  using latency_buffer_t = readoutlibs::SkipListLatencyBufferModel<buffer_object_t>;
  std::unique_ptr<latency_buffer_t> m_latency_buffer_impl{nullptr};
  using request_handler_t = readoutlibs::DefaultSkipListRequestHandler<buffer_object_t>;
  std::unique_ptr<request_handler_t> m_request_handler_impl{nullptr};

  // Don't actually use this, but it's currently needed as arg to request handler ctor
  std::unique_ptr<readoutlibs::FrameErrorRegistry> m_error_registry;
};
} // namespace trigger
} // namespace dunedaq

namespace dunedaq {
namespace readoutlibs {

template<>
uint64_t
get_frame_iterator_timestamp(triggeralgs::TriggerPrimitive* prim)
{
  return prim->time_start;
}

}
}

#endif // TRIGGER_PLUGINS_TPBUFFER_HPP_
