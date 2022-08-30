/**
 * @file TPBuffer.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TPBuffer.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "daqdataformats/SourceID.hpp"

#include <string>

namespace dunedaq {
namespace trigger {

TPBuffer::TPBuffer(const std::string& name)
  : DAQModule(name)
  , m_thread(std::bind(&TPBuffer::do_work, this, std::placeholders::_1))
  , m_queue_timeout(100)
{

  register_command("conf", &TPBuffer::do_conf);
  register_command("start", &TPBuffer::do_start);
  register_command("stop", &TPBuffer::do_stop);
  register_command("scrap", &TPBuffer::do_scrap);
}

void
TPBuffer::init(const nlohmann::json& init_data)
{
  try {
    m_input_queue_tps = get_iom_receiver<TPSet>(appfwk::connection_inst(init_data, "tpset_source").uid);
    m_input_queue_dr = get_iom_receiver<dfmessages::DataRequest>(appfwk::connection_inst(init_data, "data_request_source").uid);
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }
  m_error_registry = std::make_unique<readoutlibs::FrameErrorRegistry>();
  m_latency_buffer_impl = std::make_unique<latency_buffer_t>();
  m_request_handler_impl = std::make_unique<request_handler_t>(m_latency_buffer_impl, m_error_registry);
  m_request_handler_impl->init(init_data);
}

void
TPBuffer::get_info(opmonlib::InfoCollector& /* ci */, int /*level*/)
{
}

void
TPBuffer::do_conf(const nlohmann::json& args)
{
  // Configure the latency buffer before the request handler so the request handler can check for alignment
  // restrictions

  m_latency_buffer_impl->conf(args);
  m_request_handler_impl->conf(args);

  TLOG_DEBUG(2) << get_name() + " configured.";
}

void
TPBuffer::do_start(const nlohmann::json& args)
{
  m_request_handler_impl->start(args);
  m_thread.start_working_thread("tpbuffer");
  TLOG_DEBUG(2) << get_name() + " successfully started.";
}

void
TPBuffer::do_stop(const nlohmann::json& args)
{
  m_thread.stop_working_thread();
  m_request_handler_impl->stop(args);
  m_latency_buffer_impl->flush();
  TLOG_DEBUG(2) << get_name() + " successfully stopped.";
}

void
TPBuffer::do_scrap(const nlohmann::json& args)
{
    m_request_handler_impl->scrap(args);
    m_latency_buffer_impl->scrap(args);
}

void
TPBuffer::do_work(std::atomic<bool>& running_flag)
{
  size_t n_tps_received = 0;
  size_t n_requests_received = 0;
  
  while (running_flag.load()) {
    
    bool popped_anything=false;
    
    std::optional<TPSet> tpset = m_input_queue_tps->try_receive(std::chrono::milliseconds(0));
    if (tpset.has_value()) {
      popped_anything = true;
      for (auto const& tp: tpset->objects) {
        m_latency_buffer_impl->write(TPWrapper(tp));
        ++n_tps_received;
      }
    }

    std::optional<dfmessages::DataRequest> data_request = m_input_queue_dr->try_receive(std::chrono::milliseconds(0));
    if (data_request.has_value()) {
      popped_anything = true;
      ++n_requests_received;
      m_request_handler_impl->issue_request(*data_request, false);
    }

    if (!popped_anything) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  } // while (running_flag.load())

  TLOG() << get_name() << " exiting do_work() method. Received " << n_tps_received << " TPs " << " and " << n_requests_received << " data requests";
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPBuffer)
