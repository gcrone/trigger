/**
 * @file TriggerPrimitiveMaker.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TriggerPrimitiveMaker.hpp"

#include "trigger/Issues.hpp" // For TLVL_*

#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/cmd/Nljs.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"
#include "rcif/cmd/Nljs.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace triggeralgs;

namespace dunedaq::trigger {

TriggerPrimitiveMaker::TriggerPrimitiveMaker(const std::string& name)
  : dunedaq::appfwk::DAQModule(name)
  , m_queue_timeout(100)
{
  // clang-format off
  register_command("conf",  &TriggerPrimitiveMaker::do_configure);
  register_command("start", &TriggerPrimitiveMaker::do_start);
  register_command("stop",  &TriggerPrimitiveMaker::do_stop);
  register_command("scrap", &TriggerPrimitiveMaker::do_scrap);
  // clang-format on
}

void
TriggerPrimitiveMaker::init(const nlohmann::json& obj)
{
  m_init_obj = obj;
}

void
TriggerPrimitiveMaker::do_configure(const nlohmann::json& obj)
{
  m_conf = obj.get<triggerprimitivemaker::ConfParams>();

  // For each of the streams that are specified in the config, we read
  // the input file, and create an outgoing sink. We also keep track
  // of the total timestamp range of all the streams, so we can keep
  // the timestamps of the multiple streams in sync when replaying,
  // even when they don't all start or end at the same time

  m_earliest_first_tpset_timestamp = std::numeric_limits<triggeralgs::timestamp_t>::max();
  m_latest_last_tpset_timestamp = 0;

  for (auto& stream : m_conf.tp_streams) {
    TPStream this_stream;
    this_stream.tpset_sink = get_iom_sender<TPSet>(appfwk::connection_inst(m_init_obj, stream.output_sink_name));

    this_stream.tpsets = read_tpsets(stream.filename, stream.region_id, stream.element_id);

    m_earliest_first_tpset_timestamp =
      std::min(m_earliest_first_tpset_timestamp, this_stream.tpsets.front().start_time);

    m_latest_last_tpset_timestamp = std::max(m_latest_last_tpset_timestamp, this_stream.tpsets.back().start_time);

    m_tp_streams.push_back(std::move(this_stream));
  }
}

void
TriggerPrimitiveMaker::do_start(const nlohmann::json& args)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_start() method";

  rcif::cmd::StartParams start_params = args.get<rcif::cmd::StartParams>();
  m_run_number = start_params.run;

  m_running_flag.store(true);

  // We need the wall-clock time at which we'll send out the TPSet
  // with the earliest timestamp, so we can keep all of the output
  // streams in sync. We pick "now" plus a bit, to allow time for all
  // of the threads to start up
  auto earliest_timestamp_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);

  for (auto& stream : m_tp_streams) {
    m_threads.push_back(std::make_unique<std::thread>(&TriggerPrimitiveMaker::do_work,
                                                      this,
                                                      std::ref(m_running_flag),
                                                      std::ref(stream.tpsets),
                                                      std::ref(stream.tpset_sink),
                                                      earliest_timestamp_time));
  }
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_start() method";
}

void
TriggerPrimitiveMaker::do_stop(const nlohmann::json& /*args*/)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_stop() method";
  m_running_flag.store(false);
  for (auto& thr : m_threads) {
    if (thr != nullptr && thr->joinable()) {
      thr->join();
    }
  }
  m_threads.clear();
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_stop() method";
}

void
TriggerPrimitiveMaker::do_scrap(const nlohmann::json& /*args*/)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_scrap() method";
  m_tp_streams.clear();
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_scrap() method";
}

std::vector<TPSet>
TriggerPrimitiveMaker::read_tpsets(std::string filename, int region, int element)
{
  std::ifstream file(filename);
  if (!file || file.bad()) {
    throw BadTPInputFile(ERS_HERE, get_name(), filename);
  }

  TriggerPrimitive tp;
  TPSet tpset;
  std::vector<TPSet> tpsets;

  uint64_t prev_tpset_number = 0; // NOLINT(build/unsigned)
  uint32_t seqno = 0;             // NOLINT(build/unsigned)
  uint64_t old_time_start = 0;    // NOLINT(build/unsigned)

  // Read in the file and place the TPs in TPSets. TPSets have time
  // boundaries ( n*tpset_time_width + tpset_time_offset ), and TPs are placed
  // in TPSets based on the TP start time
  //
  // This loop assumes the input file is sorted by TP start time
  while (file >> tp.time_start >> tp.time_over_threshold >> tp.time_peak >> tp.channel >> tp.adc_integral >>
         tp.adc_peak >> tp.detid >> tp.type) {
    if (tp.time_start >= old_time_start) {
      // NOLINTNEXTLINE(build/unsigned)
      uint64_t current_tpset_number = (tp.time_start + m_conf.tpset_time_offset) / m_conf.tpset_time_width;
      old_time_start = tp.time_start;

      // If we crossed a time boundary, push the current TPSet and reset it
      if (current_tpset_number > prev_tpset_number) {
        tpset.start_time = prev_tpset_number * m_conf.tpset_time_width + m_conf.tpset_time_offset;
        tpset.end_time = tpset.start_time + m_conf.tpset_time_width;
        tpset.seqno = seqno;
        ++seqno;

        // 12-Jul-2021, KAB: setting origin fields from configuration
        tpset.origin.region_id = region;
        tpset.origin.element_id = element;

        tpset.type = TPSet::Type::kPayload;

        if (!tpset.objects.empty()) {
          // We don't send empty TPSets, so there's no point creating them
          tpsets.push_back(tpset);
        }
        prev_tpset_number = current_tpset_number;

        tpset.objects.clear();
      }
      tpset.objects.push_back(tp);
    } else {
      ers::warning(UnsortedTP(ERS_HERE, get_name(), tp.time_start));
    }
  }
  if (!tpset.objects.empty()) {
    // We don't send empty TPSets, so there's no point creating them
    tpsets.push_back(tpset);
  }
  TLOG_DEBUG(0) << "Read " << seqno << " TPs into " << tpsets.size() << " TPSets, from file " << filename;
  return tpsets;
}

void
TriggerPrimitiveMaker::do_work(std::atomic<bool>& running_flag,
                               std::vector<TPSet>& tpsets,
                               std::shared_ptr<iomanager::SenderConcept<TPSet>>& tpset_sink,
                               std::chrono::steady_clock::time_point earliest_timestamp_time)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_work() method";
  uint64_t current_iteration = 0; // NOLINT(build/unsigned)
  size_t generated_count = 0;
  size_t push_failed_count = 0;
  size_t generated_tp_count = 0;

  uint64_t prev_tpset_start_time = 0; // NOLINT(build/unsigned)
  auto prev_tpset_send_time = std::chrono::steady_clock::now();

  auto const total_stream_duration = m_latest_last_tpset_timestamp - m_earliest_first_tpset_timestamp;

  auto run_start_time = std::chrono::steady_clock::now();

  uint32_t seqno = 0; // NOLINT(build/unsigned)

  auto const clocks_per_us = m_conf.clock_frequency_hz / 1'000'000;

  while (running_flag.load()) {
    if (m_conf.number_of_loops > 0 && current_iteration >= m_conf.number_of_loops) {
      break;
    }

    for (auto& tpset : tpsets) {

      if (!running_flag.load()) {
        break;
      }

      // The argument `earliest_timestamp_time` is the wall-clock time
      // of the earliest first tpset timestamp in _any_ of the input
      // streams. So for the first TPSet we send out, we wait until
      // _this_ stream's first timestamp comes up
      auto wait_time_us = 0;
      std::chrono::steady_clock::time_point next_tpset_send_time;
      if (prev_tpset_start_time == 0) {
        wait_time_us = (tpset.start_time - m_earliest_first_tpset_timestamp) / clocks_per_us;
        next_tpset_send_time = earliest_timestamp_time + std::chrono::microseconds(wait_time_us);
      } else {
        wait_time_us = (tpset.start_time - prev_tpset_start_time) / clocks_per_us;
        next_tpset_send_time = prev_tpset_send_time + std::chrono::microseconds(wait_time_us);
      }

      // check running_flag periodically so we can stop punctually
      auto slice_period = std::chrono::microseconds(m_conf.maximum_wait_time_us);
      auto next_slice_send_time = prev_tpset_send_time + slice_period;
      bool break_flag = false;
      while (next_tpset_send_time > next_slice_send_time + slice_period) {
        if (!running_flag.load()) {
          TLOG() << "while waiting to send next TP, negative running flag detected.";
          break_flag = true;
          break;
        }
        std::this_thread::sleep_until(next_slice_send_time);
        next_slice_send_time = next_slice_send_time + slice_period;
      }
      if (!break_flag) {
        std::this_thread::sleep_until(next_tpset_send_time);
      }
      prev_tpset_send_time = next_tpset_send_time;
      prev_tpset_start_time = tpset.start_time;

      ++generated_count;
      generated_tp_count += tpset.objects.size();
      try {
          TPSet tpset_copy(tpset);
        tpset_sink->send(std::move(tpset_copy), m_queue_timeout);
      } catch (const dunedaq::iomanager::TimeoutExpired& e) {
        ers::warning(e);
        ++push_failed_count;
      }

      tpset.run_number = m_run_number;
      // Increase seqno and the timestamps in the TPSet and TPs so they don't
      // repeat when we do multiple loops over the file
      tpset.start_time += total_stream_duration;
      tpset.end_time += total_stream_duration;
      for (auto& tp : tpset.objects) {
        tp.time_start += total_stream_duration;
        tp.time_peak += total_stream_duration;
      }
      tpset.seqno = seqno;
      ++seqno;

    } // end loop over tpsets
    ++current_iteration;

  } // end while(running_flag.load())

  auto run_end_time = std::chrono::steady_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(run_end_time - run_start_time).count();
  float rate_hz = 1e3 * static_cast<float>(generated_count) / time_ms;

  TLOG() << "Generated " << generated_count << " TP sets (" << generated_tp_count << " TPs) in " << time_ms << " ms. ("
         << rate_hz << " TPSets/s). " << push_failed_count << " failed to push";

  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_work() method";
}

} // namespace dunedaq::trigger

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerPrimitiveMaker)
