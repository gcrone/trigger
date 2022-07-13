/**
 * @file TPSetBufferCreator.cpp TPSetBufferCreator class
 * implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

//#include "CommonIssues.hpp"
#include "TPSetBufferCreator.hpp"
#include "trigger/Issues.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "appfwk/app/Nljs.hpp"
#include "daqdataformats/FragmentHeader.hpp"
#include "daqdataformats/GeoID.hpp"
#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"

#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dunedaq {
namespace trigger {

TPSetBufferCreator::TPSetBufferCreator(const std::string& name)
  : dunedaq::appfwk::DAQModule(name)
  , m_thread(std::bind(&TPSetBufferCreator::do_work, this, std::placeholders::_1))
  , m_queueTimeout(100)
  , m_input_queue_tps()
  , m_input_queue_dr()
  , m_output_queue_frag()
  , m_tps_buffer()
{
  register_command("conf", &TPSetBufferCreator::do_configure);
  register_command("start", &TPSetBufferCreator::do_start);
  register_command("stop", &TPSetBufferCreator::do_stop);
  register_command("scrap", &TPSetBufferCreator::do_scrap);
}

void
TPSetBufferCreator::init(const nlohmann::json& init_data)
{
  auto qi = appfwk::connection_index(init_data, { "tpset_source", "data_request_source", "fragment_sink" });
  try {
    m_input_queue_tps = get_iom_receiver<trigger::TPSet>(qi["tpset_source"]);
  } catch (const ers::Issue& excpt) {
    throw InvalidQueueFatalError(ERS_HERE, get_name(), "tpset_source", excpt);
  }
  try {
    m_input_queue_dr = get_iom_receiver<dfmessages::DataRequest>(qi["data_request_source"]);
  } catch (const ers::Issue& excpt) {
    throw InvalidQueueFatalError(ERS_HERE, get_name(), "data_request_source", excpt);
  }
  try {
    m_output_queue_frag = get_iom_sender<std::pair<std::unique_ptr<daqdataformats::Fragment>, std::string>>(qi["fragment_sink"]);
  } catch (const ers::Issue& excpt) {
    throw InvalidQueueFatalError(ERS_HERE, get_name(), "fragment_sink", excpt);
  }
}

void
TPSetBufferCreator::get_info(opmonlib::InfoCollector& /*ci*/, int /*level*/)
{}

void
TPSetBufferCreator::do_configure(const nlohmann::json& obj)
{
  m_conf = obj.get<tpsetbuffercreator::Conf>();

  m_tps_buffer_size = m_conf.tpset_buffer_size;

  m_tps_buffer.reset(new TPSetBuffer(m_tps_buffer_size));

  m_tps_buffer->set_buffer_size(m_tps_buffer_size);
}

void
TPSetBufferCreator::do_start(const nlohmann::json& /*args*/)
{
  m_thread.start_working_thread("buffer-man");
  TLOG() << get_name() << " successfully started";
}

void
TPSetBufferCreator::do_stop(const nlohmann::json& /*args*/)
{
  m_thread.stop_working_thread();


  size_t sentCount = 0;
  TPSetBuffer::DataRequestOutput requested_tpset;
  if (m_dr_on_hold.size()) { // check if there are still data request on hold
    TLOG() << get_name() << ": On hold DRs: " << m_dr_on_hold.size();
    std::map<dfmessages::DataRequest, std::vector<trigger::TPSet>>::iterator it = m_dr_on_hold.begin();
    while (it != m_dr_on_hold.end()) {

      requested_tpset.txsets_in_window = it->second;
      std::unique_ptr<daqdataformats::Fragment> frag_out = convert_to_fragment(requested_tpset.txsets_in_window, it->first);
      TLOG() << get_name() << ": Sending late requested data (" << (it->first).request_information.window_begin << ", "
             << (it->first).request_information.window_end << "), containing "
             << requested_tpset.txsets_in_window.size() << " TPSets.";

      if (requested_tpset.txsets_in_window.size()) {
        frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kIncomplete, true);
      } else {
        frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kDataNotFound, true);
      }

      send_out_fragment(std::move(frag_out), it->first.data_destination);
      sentCount++;
      it++;
    }
  }

  m_tps_buffer->clear_buffer(); // emptying buffer

  TLOG() << get_name() << ": Exiting do_stop() method : sent " << sentCount << " incomplete fragments";
}

void
TPSetBufferCreator::do_scrap(const nlohmann::json& /*args*/)
{
  m_tps_buffer.reset(nullptr); // calls dtor
}

std::unique_ptr<daqdataformats::Fragment>
TPSetBufferCreator::convert_to_fragment(std::vector<TPSet>& tpsets, dfmessages::DataRequest input_data_request)
{

  using detdataformats::trigger::TriggerPrimitive;

  std::vector<TriggerPrimitive> tps;
  
  for (auto const& tpset : tpsets) {
    for (auto const& tp : tpset.objects) {
      if (tp.time_start >= input_data_request.request_information.window_begin &&
          tp.time_start <= input_data_request.request_information.window_end) {
        tps.push_back(tp);
      }
    }
  }

  std::unique_ptr<daqdataformats::Fragment> ret;

  // If tps is empty, tps.data() will be nullptr, so we need this `if`
  if(tps.empty()){
    ret = std::make_unique<daqdataformats::Fragment>(std::vector<std::pair<void*, size_t>>());
  }
  else{
    ret = std::make_unique<daqdataformats::Fragment>(tps.data(), sizeof(TriggerPrimitive)*tps.size());
  }
  auto& frag = *ret.get();

  daqdataformats::GeoID geoid(daqdataformats::GeoID::SystemType::kDataSelection, m_conf.region, m_conf.element);
  daqdataformats::FragmentHeader frag_h;
  frag_h.trigger_number = input_data_request.trigger_number;
  frag_h.trigger_timestamp = input_data_request.trigger_timestamp;
  frag_h.window_begin = input_data_request.request_information.window_begin;
  frag_h.window_end = input_data_request.request_information.window_end;
  frag_h.run_number = input_data_request.run_number;
  frag_h.element_id = geoid;
  frag_h.fragment_type = (daqdataformats::fragment_type_t)daqdataformats::FragmentType::kTriggerPrimitives;
  frag_h.sequence_number = input_data_request.sequence_number;

  frag.set_header_fields(frag_h);

  return ret;
}

void
TPSetBufferCreator::send_out_fragment(std::unique_ptr<daqdataformats::Fragment> frag_out,
                                      std::string data_destination,
                                      size_t& sentCount,
                                      std::atomic<bool>& running_flag)
{
  std::string thisQueueName = m_output_queue_frag->get_name();
  bool successfullyWasSent = false;
  // do...while so that we always try at least once to send
  // everything we generate, even if running_flag is changed
  // to false between the top of the main loop and here
  do {
    TLOG_DEBUG(2) << get_name() << ": Pushing the requested TPSet onto queue " << thisQueueName;
    try {
      auto the_pair = std::make_pair(std::move(frag_out), data_destination);
      m_output_queue_frag->send(std::move(the_pair), m_queueTimeout);
      successfullyWasSent = true;
      ++sentCount;
    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
      std::ostringstream oss_warn;
      oss_warn << "push to output queue \"" << thisQueueName << "\"";
      ers::warning(dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queueTimeout.count()));
    }
  } while (!successfullyWasSent && running_flag.load());
}

void
TPSetBufferCreator::send_out_fragment(std::unique_ptr<daqdataformats::Fragment> frag_out, std::string data_destination)
{
  std::string thisQueueName = m_output_queue_frag->get_name();
  bool successfullyWasSent = false;
  do {
    TLOG_DEBUG(2) << get_name() << ": Pushing the requested TPSet onto queue " << thisQueueName;
    try {
      auto the_pair = std::make_pair(std::move(frag_out), data_destination);
      m_output_queue_frag->send(std::move(the_pair), m_queueTimeout);
      successfullyWasSent = true;
    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
      std::ostringstream oss_warn;
      oss_warn << "push to output queue \"" << thisQueueName << "\"";
      ers::warning(dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queueTimeout.count()));
    }
  } while (!successfullyWasSent);
}

void
TPSetBufferCreator::do_work(std::atomic<bool>& running_flag)
{
  size_t addedCount = 0;
  size_t addFailedCount = 0;
  size_t requestedCount = 0;
  size_t sentCount = 0;

  bool first = true;

  while (running_flag.load()) {

    trigger::TPSet input_tpset;
    dfmessages::DataRequest input_data_request;
    TPSetBuffer::DataRequestOutput requested_tpset;

    // Block that receives TPSets and add them in buffer and check for pending data requests
    try {
      input_tpset = m_input_queue_tps->receive(m_queueTimeout);
      if (first) {
        TLOG() << get_name() << ": Got first TPSet, with start_time=" << input_tpset.start_time
               << " and end_time=" << input_tpset.end_time;
        first = false;
      }

      if (m_tps_buffer->add(input_tpset)) {
        ++addedCount;
        // TLOG() << "TPSet start_time=" << input_tpset.start_time << " and end_time=" << input_tpset.end_time<< " added
        // ("<< addedCount << ")! And buffer size is: " << m_tps_buffer->get_stored_size() << " /
        // "<<m_tps_buffer->get_buffer_size();
      } else {
        ++addFailedCount;
      }

      if (!m_dr_on_hold.empty()) { // check if new data is part of data request on hold

        // TLOG() << "On hold DRs: "<<m_dr_on_hold.size();

        std::map<dfmessages::DataRequest, std::vector<trigger::TPSet>>::iterator it = m_dr_on_hold.begin();

        while (it != m_dr_on_hold.end()) {
          // TLOG() << "Checking TPSet (sart_time= "<<input_tpset.start_time <<", end_time= "<< input_tpset.end_time <<"
          // w.r.t. DR on hold ("<< it->first.window_begin  <<", "<< it->first.window_end  <<"). TPSet count:
          // "<<it->second.size();
          if ((it->first.request_information.window_begin < input_tpset.end_time &&
               it->first.request_information.window_begin > input_tpset.start_time) ||
              (it->first.request_information.window_end > input_tpset.start_time &&
               it->first.request_information.window_end < input_tpset.end_time) ||
              (it->first.request_information.window_end > input_tpset.end_time &&
               it->first.request_information.window_begin <
                 input_tpset.start_time)) { // new tpset is whithin data request windown?
            it->second.push_back(input_tpset);
            // TLOG() << "Adding TPSet (sart_time="<<input_tpset.start_time <<" on DR on hold ("<<
            // it->first.window_begin  <<", "<< it->first.window_end  <<"). TPSet count: "<<it->second.size();
          }
          if (it->first.request_information.window_end <
              input_tpset
                .start_time) { // If more TPSet aren't expected to arrive then push and remove pending data request
            requested_tpset.txsets_in_window = std::move(it->second);
            std::unique_ptr<daqdataformats::Fragment> frag_out =
              convert_to_fragment(requested_tpset.txsets_in_window, it->first);
            TLOG_DEBUG(1) << get_name() << ": Sending late requested data (" << (it->first).request_information.window_begin
                   << ", " << (it->first).request_information.window_end << "), containing "
                   << requested_tpset.txsets_in_window.size() << " TPSets.";
            if (requested_tpset.txsets_in_window.empty()) {
              frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kDataNotFound, true);
            }

            send_out_fragment(std::move(frag_out), it->first.data_destination, sentCount, running_flag);
            it = m_dr_on_hold.erase(it);
            continue;
          }
          it++;
        }
      } // end if(!m_dr_on_hold.empty())

    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
    }

    // Block that receives data requests and return fragments from buffer
    try {
      input_data_request = m_input_queue_dr->receive(std::chrono::milliseconds(0));
      requested_tpset = m_tps_buffer->get_txsets_in_window(input_data_request.request_information.window_begin,
                                                           input_data_request.request_information.window_end);
      ++requestedCount;

      TLOG_DEBUG(1) << get_name() << ": Got request number " << input_data_request.request_number << ", trigger number "
                    << input_data_request.trigger_number << " begin/end ("
                    << input_data_request.request_information.window_begin << ", "
                    << input_data_request.request_information.window_end << ")";

      auto frag_out = convert_to_fragment(requested_tpset.txsets_in_window, input_data_request);

      switch (requested_tpset.ds_outcome) {
        case TPSetBuffer::kEmpty:
          TLOG_DEBUG(1) << get_name() << ": Requested data (" << input_data_request.request_information.window_begin << ", "
                 << input_data_request.request_information.window_end << ") not in buffer, which contains "
                 << m_tps_buffer->get_stored_size() << " TPSets between (" << m_tps_buffer->get_earliest_start_time()
                 << ", " << m_tps_buffer->get_latest_end_time() << "). Returning empty fragment.";
          frag_out->set_error_bit(daqdataformats::FragmentErrorBits::kDataNotFound, true);
          send_out_fragment(std::move(frag_out), input_data_request.data_destination, sentCount, running_flag);
          break;
        case TPSetBuffer::kLate:
          TLOG_DEBUG(1) << get_name() << ": Requested data (" << input_data_request.request_information.window_begin << ", "
                 << input_data_request.request_information.window_end << ") has not arrived in buffer, which contains "
                 << m_tps_buffer->get_stored_size() << " TPSets between (" << m_tps_buffer->get_earliest_start_time()
                 << ", " << m_tps_buffer->get_latest_end_time() << "). Holding request until more data arrives.";
          m_dr_on_hold.insert(std::make_pair(input_data_request, requested_tpset.txsets_in_window));
          break; // don't send anything yet. Wait for more data to arrived.
        case TPSetBuffer::kSuccess:
          TLOG_DEBUG(1) << get_name() << ": Sending requested data (" << input_data_request.request_information.window_begin
                 << ", " << input_data_request.request_information.window_end << "), containing "
                 << requested_tpset.txsets_in_window.size() << " TPSets.";

          send_out_fragment(std::move(frag_out), input_data_request.data_destination, sentCount, running_flag);
          break;
        default:
          TLOG() << get_name() << ": Data request failed!";
      }

    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
      // skip if no data request in the queue
      continue;
    }
  } // end while(running_flag.load())

  TLOG() << get_name() << ": Exiting the do_work() method: received " << addedCount << " Sets and " << requestedCount
         << " data requests. " << addFailedCount << " Sets failed to add. Sent " << sentCount << " fragments";

} // NOLINT Function length

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPSetBufferCreator)

// Local Variables:
// c-basic-offset: 2
// End:
