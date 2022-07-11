/**
 * @file TABuffer.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TABuffer.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "daqdataformats/SourceID.hpp"

#include <string>

namespace dunedaq {
namespace trigger {

TABuffer::TABuffer(const std::string& name)
  : DAQModule(name)
  , m_thread(std::bind(&TABuffer::do_work, this, std::placeholders::_1))
  , m_queue_timeout(100)
{

  register_command("conf", &TABuffer::do_conf);
  register_command("start", &TABuffer::do_start);
  register_command("stop", &TABuffer::do_stop);
  register_command("scrap", &TABuffer::do_scrap);
}

void
TABuffer::init(const nlohmann::json& init_data)
{
  try {
    m_input_queue_tas = get_iom_receiver<TASet>(appfwk::connection_inst(init_data, "taset_source"));
    m_input_queue_dr = get_iom_receiver<dfmessages::DataRequest>(appfwk::connection_inst(init_data, "data_request_source"));
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }
  m_error_registry = std::make_unique<readoutlibs::FrameErrorRegistry>();
  m_latency_buffer_impl = std::make_unique<latency_buffer_t>();
  m_request_handler_impl = std::make_unique<request_handler_t>(m_latency_buffer_impl, m_error_registry);
  m_request_handler_impl->init(init_data);
}

void
TABuffer::get_info(opmonlib::InfoCollector& /* ci */, int /*level*/)
{
}

void
TABuffer::do_conf(const nlohmann::json& args)
{
  // Configure the latency buffer before the request handler so the request handler can check for alignment
  // restrictions

  m_latency_buffer_impl->conf(args);
  m_request_handler_impl->conf(args);

  TLOG_DEBUG(2) << get_name() + " configured.";
}

void
TABuffer::do_start(const nlohmann::json& args)
{
  m_request_handler_impl->start(args);
  m_thread.start_working_thread("tabuffer");
  TLOG_DEBUG(2) << get_name() + " successfully started.";
}

void
TABuffer::do_stop(const nlohmann::json& args)
{
  m_thread.stop_working_thread();
  m_request_handler_impl->stop(args);
  m_latency_buffer_impl->flush();
  TLOG_DEBUG(2) << get_name() + " successfully stopped.";
}

void
TABuffer::do_scrap(const nlohmann::json& args)
{
    m_request_handler_impl->scrap(args);
    m_latency_buffer_impl->scrap(args);
}

void
TABuffer::do_work(std::atomic<bool>& running_flag)
{
  size_t n_tas_received = 0;
  size_t n_requests_received = 0;

  while (running_flag.load()) {

    bool popped_anything=false;
    std::optional<TASet> taset = m_input_queue_tas->try_receive(std::chrono::milliseconds(0));
    if (taset.has_value()) {
      popped_anything = true;
      for (auto const& ta: taset->objects) {
        m_latency_buffer_impl->write(TAWrapper(ta));
        ++n_tas_received;
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

  TLOG() << get_name() << " exiting do_work() method. Received " << n_tas_received << " TAs " << " and " << n_requests_received << " data requests";
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TABuffer)
