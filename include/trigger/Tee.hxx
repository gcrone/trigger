/**
 * @file Tee.hxx
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "appfwk/DAQModuleHelper.hpp"
#include "iomanager/IOManager.hpp"
#include "rcif/cmd/Nljs.hpp"
#include "trigger/Issues.hpp"

#include <string>

namespace dunedaq {
namespace trigger {

template<class T>
Tee<T>::Tee(const std::string& name)
  : DAQModule(name)
  , m_thread(std::bind(&Tee<T>::do_work, this, std::placeholders::_1))
  , m_input_queue(nullptr)
  , m_output_queue1(nullptr)
  , m_output_queue2(nullptr)
{

  register_command("conf", &Tee<T>::do_conf);
  register_command("start", &Tee<T>::do_start);
  register_command("stop", &Tee<T>::do_stop);
  register_command("scrap", &Tee<T>::do_scrap);
}

template<class T>
void
Tee<T>::init(const nlohmann::json& iniobj)
{
  try {
    m_input_queue = get_iom_receiver<T>(appfwk::connection_inst(iniobj, "input"));
    m_output_queue1 = get_iom_sender<T>(appfwk::connection_inst(iniobj, "output1"));
    m_output_queue2 = get_iom_sender<T>(appfwk::connection_inst(iniobj, "output2"));
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }
}

template<class T>
void
Tee<T>::do_conf(const nlohmann::json&)
{
  TLOG_DEBUG(2) << get_name() + " configured.";
}

template<class T>
void
Tee<T>::do_start(const nlohmann::json&)
{
  m_thread.start_working_thread("tctee");
  TLOG_DEBUG(2) << get_name() + " successfully started.";
}

template<class T>
void
Tee<T>::do_stop(const nlohmann::json&)
{
  m_thread.stop_working_thread();
  TLOG_DEBUG(2) << get_name() + " successfully stopped.";
}

template<class T>
void
Tee<T>::do_scrap(const nlohmann::json&)
{}

template<class T>
void
Tee<T>::do_work(std::atomic<bool>& running_flag)
{
  size_t n_objects = 0;
  
  while (true) {
    T object;
    try {
      object = m_input_queue->receive(std::chrono::milliseconds(100));
      ++n_objects;
    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
      // The condition to exit the loop is that we've been stopped and
      // there's nothing left on the input queue
      if (!running_flag.load()) {
        break;
      } else {
        continue;
      }
    }

    size_t timeout_ms = 20;
    try {
      T object1(object);
      m_output_queue1->send(std::move(object1), std::chrono::milliseconds(timeout_ms));
    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
      ers::warning(dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), "push to output queue 1", timeout_ms));
    }
    try {
      T object2(object);
      m_output_queue2->send(std::move(object2), std::chrono::milliseconds(timeout_ms));
    } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
      ers::warning(dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), "push to output queue 2", timeout_ms));
    }
  }

  TLOG() << get_name() << ": Exiting do_work() method after receiving " << n_objects << " objects";
}

} // namespace trigger
} // namespace dunedaq

