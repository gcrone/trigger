/**
 * @file Tee.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TEE_HPP_
#define TRIGGER_PLUGINS_TEE_HPP_

#include "appfwk/DAQModule.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "utilities/WorkerThread.hpp"

namespace dunedaq {
namespace trigger {
template<class T>
class Tee : public dunedaq::appfwk::DAQModule
{
public:
  explicit Tee(const std::string& name);

  Tee(const Tee&) = delete;
  Tee& operator=(const Tee&) = delete;
  Tee(Tee&&) = delete;
  Tee& operator=(Tee&&) = delete;

  void init(const nlohmann::json& iniobj) override;

private:
  void do_conf(const nlohmann::json& config);
  void do_start(const nlohmann::json& obj);
  void do_stop(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);
  void do_work(std::atomic<bool>&);

  dunedaq::utilities::WorkerThread m_thread;

  using source_t = dunedaq::iomanager::ReceiverConcept<T>;
  std::shared_ptr<source_t> m_input_queue;
  using sink_t = dunedaq::iomanager::SenderConcept<T>;
  std::shared_ptr<sink_t> m_output_queue1;
  std::shared_ptr<sink_t> m_output_queue2;

};
} // namespace trigger
} // namespace dunedaq

#include "Tee.hxx"

#endif // TRIGGER_PLUGINS_TEE_HPP_
