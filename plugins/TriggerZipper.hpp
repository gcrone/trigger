/**
 * @file TriggerZipper.hpp TriggerZipper is an appfwk::DAQModule that runs zipper::merge
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_TRIGGERZIPPER_HPP_
#define TRIGGER_PLUGINS_TRIGGERZIPPER_HPP_

#include "zipper.hpp"

#include "trigger/Issues.hpp"
#include "trigger/triggerzipper/Nljs.hpp"

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQModuleHelper.hpp"
#include "daqdataformats/SourceID.hpp"
#include "iomanager/IOManager.hpp"
#include "iomanager/Receiver.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"
#include "utilities/WorkerThread.hpp"

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

const char* inqs_name = "inputs";
const char* outq_name = "output";

namespace dunedaq::trigger {

// template<typename Payload,
//          typename Ordering = size_t,
//          typename Identity = size_t,
//          typename TimePoint = std::chrono::steady_clock::time_point,
//          typename
// >

size_t
zipper_stream_id(const daqdataformats::SourceID& sourceid)
{
  return (0xffff000000000000 & (static_cast<size_t>(sourceid.subsystem) << 48)) | (0x00000000ffffffff & sourceid.id);
}

template<typename TSET>
class TriggerZipper : public dunedaq::appfwk::DAQModule
{

public:
  // Derived types
  using tset_type = TSET;
  using ordering_type = typename TSET::timestamp_t;
  using origin_type = typename TSET::origin_t; // GeoID
  using seqno_type = typename TSET::seqno_t;   // GeoID

  using cache_type = std::list<TSET>;
  using payload_type = typename cache_type::iterator;
  using identity_type = size_t;

  using node_type = zipper::Node<payload_type>;
  using zm_type = zipper::merge<node_type>;
  zm_type m_zm;

  // queues
  using source_t = iomanager::ReceiverConcept<TSET>;
  using sink_t = iomanager::SenderConcept<TSET>;
  std::shared_ptr<source_t> m_inq{};
  std::shared_ptr<sink_t> m_outq{};

  using cfg_t = triggerzipper::ConfParams;
  cfg_t m_cfg;

  std::thread m_thread;
  std::atomic<bool> m_running{ false };

  // We store input TSETs in a list and send iterator though the
  // zipper as payload so as to not suffer copy overhead.
  cache_type m_cache;
  seqno_type m_next_seqno{ 0 };

  size_t m_n_received{ 0 };
  size_t m_n_sent{ 0 };
  size_t m_n_tardy{ 0 };
  std::map<daqdataformats::SourceID, size_t> m_tardy_counts;

  explicit TriggerZipper(const std::string& name)
    : DAQModule(name)
    , m_zm()
  {
    // clang-format off
        register_command("conf",   &TriggerZipper<TSET>::do_configure);
        register_command("start",  &TriggerZipper<TSET>::do_start);
        register_command("stop",   &TriggerZipper<TSET>::do_stop);
        register_command("scrap",  &TriggerZipper<TSET>::do_scrap);
    // clang-format on
  }

  void init(const nlohmann::json& ini)
  {
    set_input(appfwk::connection_inst(ini, "input").uid);
    set_output(appfwk::connection_inst(ini, "output").uid);
  }
  void set_input(const std::string& name)
  {
    m_inq = get_iom_receiver<TSET>(name);
  }
  void set_output(const std::string& name)
  {
    m_outq = get_iom_sender<TSET>(name);
  }

  void do_configure(const nlohmann::json& cfgobj)
  {
    m_cfg = cfgobj.get<cfg_t>();
    m_zm.set_max_latency(std::chrono::milliseconds(m_cfg.max_latency_ms));
    m_zm.set_cardinality(m_cfg.cardinality);
  }

  void do_scrap(const nlohmann::json& /*stopobj*/)
  {
    m_cfg = cfg_t{};
    m_zm.set_cardinality(0);
  }

  void do_start(const nlohmann::json& /*startobj*/)
  {
    m_n_received = 0;
    m_n_sent = 0;
    m_n_tardy = 0;
    m_tardy_counts.clear();
    m_running.store(true);
    m_thread = std::thread(&TriggerZipper::worker, this);
  }

  void do_stop(const nlohmann::json& /*stopobj*/)
  {
    m_running.store(false);
    m_thread.join();
    flush();
    m_zm.clear();
    TLOG() << "Received " << m_n_received << " Sets. Sent " << m_n_sent << " Sets. " << m_n_tardy << " were tardy";
    std::stringstream ss;
    ss << std::endl;
    for (auto& [id, n] : m_tardy_counts) {
      ss << id << "\t" << n << std::endl;
    }
    TLOG_DEBUG(1) << "Tardy counts:" << ss.str();
  }

  // thread worker
  void worker()
  {
    while (true) {
      // Once we've received a stop command, keep reading the input
      // queue until there's nothing left on it
      if (!proc_one() && !m_running.load()) {
        break;
      }
    }
  }

  bool proc_one()
  {
    m_cache.emplace_front(); // to be filled
    auto& tset = m_cache.front();
    std::optional<TSET> opt_tset= m_inq->try_receive(std::chrono::milliseconds(10));
    if (opt_tset.has_value()) {
      tset = *opt_tset;
      ++m_n_received;
    }
    else {
      m_cache.pop_front(); // vestigial
      drain();
      return false;
    }

    if (!m_tardy_counts.count(tset.origin))
      m_tardy_counts[tset.origin] = 0;

    // P. Rodrigues 2022-03-03 This is a bit of a hack to ensure that
    // heartbeat TSETs with the same start_time as payload TSETs will
    // be output _before_ the payload. I think this is what we want
    // (and without it, we get out-of-order errors from
    // TriggerGenericMaker). Consider the following case:
    //
    // * We have a heartbeat with start_time (and end_time) 100
    // * We have payload TSETs with start_time 100 and end_time 200
    //
    // The heartbeat TSET encodes the information "you have seen all
    // TSETs with start times earlier than 100 (but none later)", so
    // it must be output before the payload TSETs with the same start
    // time.
    //
    // Alternative reasoning: On receipt of a heartbeat,
    // TriggerGenericMaker flushes its input buffer and processes the
    // items from the buffer. If we sent the heartbeat _after_ the
    // payload TSETs in the example above, we would be flushing items
    // up to the end_time, 200, but the heartbeat only says we've seen
    // up to timestamp 100
    ordering_type sort_value = tset.start_time << 1;
    if (tset.type != TSET::Type::kHeartbeat)
      sort_value |= 0x1;

    bool accepted = m_zm.feed(m_cache.begin(), sort_value, zipper_stream_id(tset.origin));

    if (!accepted) {
      ++m_n_tardy;
      ++m_tardy_counts[tset.origin];

      ers::warning(TardyInputSet(
                                 ERS_HERE, get_name(), tset.origin.id, tset.start_time, m_zm.get_origin() >> 1));
      m_cache.pop_front(); // vestigial
    }
    drain();
    return true;
  }

  void send_out(std::vector<node_type>& got)
  {
    for (auto& node : got) {
      payload_type lit = node.payload;
      auto& tset = *lit; // list iterator

      // tell consumer "where" the set was produced
      tset.origin.id = m_cfg.element_id;
      tset.seqno = m_next_seqno;
      ++m_next_seqno;

      try {
        m_outq->send(std::move(tset), std::chrono::milliseconds(10));
        ++m_n_sent;
      } catch (const iomanager::TimeoutExpired& err) {
        // our output queue is stuffed.  should more be done
        // here than simply complain and drop?
        ers::error(err);
      }
      m_cache.erase(lit);
    }
  }

  // Maybe drain and send to out queue
  void drain()
  {
    std::vector<node_type> got;
    if (m_cfg.max_latency_ms) {
      m_zm.drain_prompt(std::back_inserter(got));
    } else {
      m_zm.drain_waiting(std::back_inserter(got));
    }
    send_out(got);
  }

  // Fully drain and send to out queue
  void flush()
  {
    std::vector<node_type> got;
    m_zm.drain_full(std::back_inserter(got));
    send_out(got);
  }
};
} // namespace dunedaq::trigger

/// Need one of these in a .cpp for each concrete TSET type
// DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TriggerZipper<TSET>)

#endif // TRIGGER_PLUGINS_TRIGGERZIPPER_HPP_
// Local Variables:
// c-basic-offset: 2
// End:
