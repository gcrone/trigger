/**
 * @file TPSetSink.cpp TPSetSink class implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TPSetSink.hpp"

#include "logging/Logging.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/app/Nljs.hpp"
#include <chrono>
#include <sstream>

namespace dunedaq {
namespace trigger {

TPSetSink::TPSetSink(const std::string& name)
  : DAQModule(name)
{
  register_command("start", &TPSetSink::do_start);
  register_command("stop", &TPSetSink::do_stop);
}

void
TPSetSink::init(const nlohmann::json& obj)
{
  m_tpset_source.reset(new source_t(appfwk::queue_inst(obj, "tpset_source")));
}

void
TPSetSink::do_start(const nlohmann::json& /*obj*/)
{
  m_running_flag.store(true);
  m_thread = std::thread(&TPSetSink::do_work, this);
  pthread_setname_np(m_thread.native_handle(), "tpsink");
}

void
TPSetSink::do_stop(const nlohmann::json& /*obj*/)
{
  m_running_flag.store(false);
  m_thread.join();
}

void
TPSetSink::do_work()
{
  using namespace std::chrono;

  size_t n_tpset_received = 0;

  auto start_time = steady_clock::now();

  uint64_t first_timestamp = 0;
  uint64_t last_timestamp = 0;

  while (true) {
    TPSet tpset;
    try {
      m_tpset_source->pop(tpset, std::chrono::milliseconds(100));
      ++n_tpset_received;
    } catch (appfwk::QueueTimeoutExpired&) {
      // The condition to exit the loop is that we've been stopped and
      // there's nothing left on the input queue
      if (!m_running_flag.load()) {
        break;
      } else {
        continue;
      }
    }

    // Do some checks on the received TPSet
    if (tpset.start_time <= last_timestamp) {
      TLOG() << "TPSets out of order: last start time " << last_timestamp << ", current start time "
             << tpset.start_time;
    }
    if (tpset.objects.empty()) {
      TLOG() << "Empty TPSet with start time " << tpset.start_time;
    }
    for (auto const& tp : tpset.objects) {
      // TODO P. Rodrigues 2021-06-15 TriggerPrimitive timestamps are
      // int64_t. When that is fixed, these horrible casts can go away
      if (static_cast<uint64_t>(tp.time_start) < tpset.start_time ||
          static_cast<uint64_t>(tp.time_start) > tpset.end_time) {
        TLOG() << "TPSet with start time " << tpset.start_time << ", end time " << tpset.end_time
               << " contains out-of-bounds TP with start time " << tp.time_start;
      }
    }

    if (first_timestamp == 0) {
      first_timestamp = tpset.start_time;
    }
    last_timestamp = tpset.start_time;
  } // while(true)

  auto end_time = steady_clock::now();
  auto time_ms = duration_cast<milliseconds>(end_time - start_time).count();
  float rate_hz = 1e3 * static_cast<float>(n_tpset_received) / time_ms;
  float inferred_clock_frequency = 1e3 * (last_timestamp - first_timestamp) / time_ms;

  TLOG() << "Received " << n_tpset_received << " TPSets in " << time_ms << "ms. " << rate_hz
         << " TPSet/s. Inferred clock frequency " << inferred_clock_frequency << "Hz";
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPSetSink)