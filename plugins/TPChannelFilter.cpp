/**
 * @file TPChannelFilter.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "TPChannelFilter.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "iomanager/IOManager.hpp"
#include "triggeralgs/TriggerPrimitive.hpp"

#include <string>

namespace dunedaq {
namespace trigger {
TPChannelFilter::TPChannelFilter(const std::string& name)
  : DAQModule(name)
  , m_thread(std::bind(&TPChannelFilter::do_work, this, std::placeholders::_1))
  , m_input_queue(nullptr)
  , m_output_queue(nullptr)
  , m_queue_timeout(100)
{

  register_command("conf", &TPChannelFilter::do_conf);
  register_command("start", &TPChannelFilter::do_start);
  register_command("stop", &TPChannelFilter::do_stop);
  register_command("scrap", &TPChannelFilter::do_scrap);
}

void
TPChannelFilter::init(const nlohmann::json& iniobj)
{
  try {
    m_input_queue = get_iom_receiver<TPSet>(appfwk::connection_inst(iniobj, "tpset_source"));
    m_output_queue = get_iom_sender<TPSet>(appfwk::connection_inst(iniobj, "tpset_sink"));
  } catch (const ers::Issue& excpt) {
    throw dunedaq::trigger::InvalidQueueFatalError(ERS_HERE, get_name(), "input/output", excpt);
  }
}

void
TPChannelFilter::get_info(opmonlib::InfoCollector& /* ci */, int /*level*/)
{}

void
TPChannelFilter::do_conf(const nlohmann::json& conf_arg)
{
  m_conf = conf_arg.get<dunedaq::trigger::tpchannelfilter::Conf>();
  m_channel_map = dunedaq::detchannelmaps::make_map(m_conf.channel_map_name);
}

void
TPChannelFilter::do_start(const nlohmann::json&)
{
  m_thread.start_working_thread("channelfilter");
  TLOG_DEBUG(2) << get_name() + " successfully started.";
}

void
TPChannelFilter::do_stop(const nlohmann::json&)
{
  m_thread.stop_working_thread();
  TLOG_DEBUG(2) << get_name() + " successfully stopped.";
}

void
TPChannelFilter::do_scrap(const nlohmann::json&)
{}

bool
TPChannelFilter::channel_should_be_removed(int channel) const
{
  // The plane numbering convention is found in detchannelmaps/plugins/VDColdboxChannelMap.cpp and is:
  // U (induction) = 0, Y (induction) = 1, Z (induction) = 2, unconnected channel = 9999
  uint plane = m_channel_map->get_plane_from_offline_channel(channel);
  // Check for collection
  if (plane == 0 || plane == 1) {
    return false;
  }
  // Check for induction
  if (plane == 2) {
    return !m_conf.keep_collection;
  }
  // Always remove unconnected channels
  if (plane == 9999 ) {
    return true;
  }
  // Unknown plane?!
  TLOG() << "Encountered unexpected plane " << plane << " from channel " << channel << ", check channel map?";
  return false;
}

void
TPChannelFilter::do_work(std::atomic<bool>& running_flag)
{
  while (true) {
    std::optional<TPSet> tpset = m_input_queue->try_receive(m_queue_timeout);;

    if (!tpset.has_value()) {
      // The condition to exit the loop is that we've been stopped and
      // there's nothing left on the input queue
      if (!running_flag.load()) {
        break;
      } else {
        continue;
      }
    }

    // If we got here, we got a TPSet
    
    // Actually do the removal for payload TPSets. Leave heartbeat TPSets unmolested

    if (tpset->type == TPSet::kPayload) {
      size_t n_before = tpset->objects.size();
      auto it = std::remove_if(tpset->objects.begin(), tpset->objects.end(), [this](triggeralgs::TriggerPrimitive p) {
        return channel_should_be_removed(p.channel);
      });
      tpset->objects.erase(it, tpset->objects.end());
      size_t n_after = tpset->objects.size();
      TLOG_DEBUG(2) << "Removed " << (n_before - n_after) << " TPs out of " << n_before;
    }

    // The rule is that we don't send empty TPSets, so ensure that
    if (!tpset->objects.empty()) {
      try {
        m_output_queue->send(std::move(*tpset), m_queue_timeout);
      } catch (const dunedaq::iomanager::TimeoutExpired& excpt) {
        std::ostringstream oss_warn;
        oss_warn << "push to output queue \"" << m_output_queue->get_name() << "\"";
        ers::warning(
          dunedaq::iomanager::TimeoutExpired(ERS_HERE, get_name(), oss_warn.str(), m_queue_timeout.count()));
      }
    }

  } // while(true)
  TLOG_DEBUG(2) << "Exiting do_work() method";
}

} // namespace trigger
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TPChannelFilter)
