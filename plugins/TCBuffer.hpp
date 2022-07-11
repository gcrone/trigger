/**
 * @file TCBuffer.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TCBUFFER_HPP_
#define TRIGGER_PLUGINS_TCBUFFER_HPP_

#include "appfwk/DAQModule.hpp"
#include "daqdataformats/Fragment.hpp"
#include "iomanager/Receiver.hpp"

#include "readoutlibs/FrameErrorRegistry.hpp"
#include "readoutlibs/models/DefaultRequestHandlerModel.hpp"
#include "readoutlibs/models/SkipListLatencyBufferModel.hpp"
#include "readoutlibs/models/DefaultSkipListRequestHandler.hpp"
#include "triggeralgs/TriggerActivity.hpp"
#include "triggeralgs/TriggerObjectOverlay.hpp"
#include "utilities/WorkerThread.hpp"

#include "trigger/Issues.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace dunedaq {
namespace trigger {
class TCBuffer : public dunedaq::appfwk::DAQModule
{
public:
  explicit TCBuffer(const std::string& name);

  TCBuffer(const TCBuffer&) = delete;
  TCBuffer& operator=(const TCBuffer&) = delete;
  TCBuffer(TCBuffer&&) = delete;
  TCBuffer& operator=(TCBuffer&&) = delete;

  void init(const nlohmann::json& iniobj) override;
  void get_info(opmonlib::InfoCollector& ci, int level) override;

private:

  struct TCWrapper
  {
    triggeralgs::TriggerCandidate candidate;
    std::vector<uint8_t> candidate_overlay_buffer;
    // Don't really want this default ctor, but IterableQueueModel requires it
    TCWrapper() {}
    
    TCWrapper(triggeralgs::TriggerCandidate c)
      : candidate(c)
    {
      populate_buffer();
    }

    void populate_buffer()
    {
      candidate_overlay_buffer.resize(triggeralgs::get_overlay_nbytes(candidate));
      triggeralgs::write_overlay(candidate, candidate_overlay_buffer.data());
    }
    
    // comparable based on first timestamp
    bool operator<(const TCWrapper& other) const
    {
      return this->candidate.time_start < other.candidate.time_start;
    }

    uint64_t get_first_timestamp() const // NOLINT(build/unsigned)
    {
      return candidate.time_start;
    }

    void set_first_timestamp(uint64_t ts) // NOLINT(build/unsigned)
    {
      candidate.time_start = ts;
    }

    size_t get_payload_size() { return candidate_overlay_buffer.size(); }

    size_t get_num_frames() { return 1; }

    size_t get_frame_size() { return get_payload_size(); }

    uint8_t* begin()
    {
      return candidate_overlay_buffer.data();
    }
    
    uint8_t* end()
    {
      return candidate_overlay_buffer.data()+candidate_overlay_buffer.size();
    }

    //static const constexpr size_t fixed_payload_size = 5568;
    static const constexpr daqdataformats::SourceID::Subsystem subsystem = daqdataformats::SourceID::Subsystem::kTRG;
    static const constexpr daqdataformats::FragmentType fragment_type = daqdataformats::FragmentType::kTC;
    // No idea what this should really be set to
    static const constexpr uint64_t expected_tick_difference = 16; // NOLINT(build/unsigned)

};

  void do_conf(const nlohmann::json& config);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);
  void do_work(std::atomic<bool>&);

  dunedaq::utilities::WorkerThread m_thread;

  using tcs_source_t = iomanager::ReceiverConcept<triggeralgs::TriggerCandidate>;
  std::shared_ptr<tcs_source_t> m_input_queue_tcs{nullptr};

  using dr_source_t = iomanager::ReceiverConcept<dfmessages::DataRequest>;
  std::shared_ptr<dr_source_t> m_input_queue_dr{nullptr};

  std::chrono::milliseconds m_queue_timeout;

  using buffer_object_t = TCWrapper;
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

#endif // TRIGGER_PLUGINS_TCBUFFER_HPP_
