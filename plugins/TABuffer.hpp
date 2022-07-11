/**
 * @file TABuffer.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TABUFFER_HPP_
#define TRIGGER_PLUGINS_TABUFFER_HPP_

#include "daqdataformats/Fragment.hpp"
#include "iomanager/Receiver.hpp"
#include "readoutlibs/FrameErrorRegistry.hpp"
#include "readoutlibs/models/DefaultSkipListRequestHandler.hpp"
#include "readoutlibs/models/SkipListLatencyBufferModel.hpp"
#include "triggeralgs/TriggerObjectOverlay.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"
#include "utilities/WorkerThread.hpp"

#include "trigger/Issues.hpp"
#include "trigger/TASet.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace dunedaq {
namespace trigger {
class TABuffer : public dunedaq::appfwk::DAQModule
{
public:
  explicit TABuffer(const std::string& name);

  TABuffer(const TABuffer&) = delete;
  TABuffer& operator=(const TABuffer&) = delete;
  TABuffer(TABuffer&&) = delete;
  TABuffer& operator=(TABuffer&&) = delete;

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:

  struct TAWrapper
  {
    triggeralgs::TriggerActivity activity;
    std::vector<uint8_t> activity_overlay_buffer;
    
    // Don't really want this default ctor, but IterableQueueModel requires it
    TAWrapper() {}
    
    TAWrapper(triggeralgs::TriggerActivity a)
      : activity(a)
    {
      populate_buffer();
    }

    void populate_buffer()
    {
      activity_overlay_buffer.resize(triggeralgs::get_overlay_nbytes(activity));
      triggeralgs::write_overlay(activity, activity_overlay_buffer.data());
    }
    
    // comparable based on first timestamp
    bool operator<(const TAWrapper& other) const
    {
      return this->activity.time_start < other.activity.time_start;
    }

    uint64_t get_first_timestamp() const // NOLINT(build/unsigned)
    {
      return activity.time_start;
    }

    void set_first_timestamp(uint64_t ts) // NOLINT(build/unsigned)
    {
      activity.time_start = ts;
    }

    uint64_t get_timestamp() const // NOLINT(build/unsigned)
    {
      return activity.time_start;
    }

    size_t get_payload_size() { return activity_overlay_buffer.size(); }

    size_t get_num_frames() { return 1; }

    size_t get_frame_size() { return get_payload_size(); }

    uint8_t* begin()
    {
      return activity_overlay_buffer.data();
    }
    
    uint8_t* end()
    {
      return activity_overlay_buffer.data()+activity_overlay_buffer.size();
    }

    //static const constexpr size_t fixed_payload_size = 5568;
    static const constexpr daqdataformats::SourceID::Subsystem subsystem = daqdataformats::SourceID::Subsystem::kTRG;
    static const constexpr daqdataformats::FragmentType fragment_type = daqdataformats::FragmentType::kTA;
    // No idea what this should really be set to
    static const constexpr uint64_t expected_tick_difference = 16; // NOLINT(build/unsigned)

};

  void do_conf(const nlohmann::json& config);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);
  void do_work(std::atomic<bool>&);

  dunedaq::utilities::WorkerThread m_thread;

  using tas_source_t = iomanager::ReceiverConcept<trigger::TASet>;
  std::shared_ptr<tas_source_t> m_input_queue_tas{nullptr};

  using dr_source_t = iomanager::ReceiverConcept<dfmessages::DataRequest>;
  std::shared_ptr<dr_source_t> m_input_queue_dr{nullptr};

  std::chrono::milliseconds m_queue_timeout;

  using buffer_object_t = TAWrapper;
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
get_frame_iterator_timestamp(uint8_t* it)
{
  detdataformats::trigger::TriggerActivity* activity = reinterpret_cast<detdataformats::trigger::TriggerActivity*>(it);
  return activity->data.time_start;
}

}
}

#endif // TRIGGER_PLUGINS_TABUFFER_HPP_
