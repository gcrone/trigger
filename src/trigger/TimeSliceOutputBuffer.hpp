/**
 * @file TimeSliceOutputBuffer.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_TIMESLICEOUTPUTBUFFER_HPP_
#define TRIGGER_SRC_TRIGGER_TIMESLICEOUTPUTBUFFER_HPP_

#include "trigger/Issues.hpp"
#include "trigger/Set.hpp"

#include "logging/Logging.hpp"

#include <queue>
#include <string>
#include <vector>

namespace dunedaq::trigger {

// TODO Benjamin Land <BenLand100@github.com> June-01-2021: would be nice if the T (TriggerPrimative, etc) included a natural ordering with operator<()
template<class T>
struct time_start_greater_t
{
  bool operator()(const T& a, const T& b) { return a.time_start > b.time_start; }
};

// TODO Philip Rodrigues rodriges@fnal.gov 2022-04-27: Same comment as above, plus we should really fix the time_start/start_time inconsistency
template<class T>
struct start_time_greater_t
{
  bool operator()(const T& a, const T& b) { return a.start_time > b.start_time; }
};

// When writing Set<T> to a queue, we want to buffer all T with the same for
// some time, to ensure that Set<T> are generated with all T from that window,
// assuming that the T may be generated in some arbitrary, but not too tardy,
// order. Finally, emit Set<T> for completed windows, and warn for any late
// arriving T.
// This class encapsulates that logic.
template<class T>
class TimeSliceOutputBuffer
{
public:
  // Parameters for ers warning metadata and config
  TimeSliceOutputBuffer(const std::string& name,
                        const std::string& algorithm,
                        const daqdataformats::timestamp_t buffer_time = 0,
                        const daqdataformats::timestamp_t window_time = 625000)
    : m_name(name)
    , m_algorithm(algorithm)
    , m_next_window_start(0)
    , m_buffer_time(buffer_time)
    , m_window_time(window_time)
    , m_largest_time(0)
  {}

  // Add a new vector<T> to the buffer.
  void buffer(const std::vector<T>& in)
  {
    if (m_next_window_start == 0) {
      // Window start time is unknown. pick it as the window that contains the
      // first element of in. Window start time must be multiples of m_window_time
      m_next_window_start = (in.front().time_start / m_window_time) * m_window_time;
    }
    for (const T& x : in) {
      if (x.time_start < m_next_window_start) {
        ers::warning(TardyOutputError(ERS_HERE, m_name, m_algorithm, x.time_start, m_next_window_start));
        // x is discarded
      } else {
        m_buffer.push(x);
        if (m_largest_time < x.time_start) {
          m_largest_time = x.time_start;
        }
      }
    }
  }

  // Add a new heartbeat Set to the buffer
  void buffer_heartbeat(const Set<T> heartbeat)
  {
    if (m_next_window_start == 0) {
      // Window start time is unknown. pick it as the window that contains the
      // first element of in. Window start time must be multiples of m_window_time
      m_next_window_start = heartbeat.start_time;
    }

    if (heartbeat.start_time < m_next_window_start) {
      ers::warning(TardyOutputError(ERS_HERE, m_name, m_algorithm, heartbeat.start_time, m_next_window_start));
      // heartbeat is discarded
    } else if (heartbeat.start_time % m_window_time != 0) {
      ers::warning(UnalignedHeartbeat(ERS_HERE, m_name, m_algorithm, heartbeat.start_time, m_window_time));
      // heartbeat is discarded
    } else {
      m_heartbeat_buffer.push(heartbeat);
      if (m_largest_time < heartbeat.start_time) {
        m_largest_time = heartbeat.start_time;
      }
    }
  }

  void reset() { m_next_window_start = 0; }

  void set_window_time(const daqdataformats::timestamp_t window_time)
  {
    m_window_time = window_time;
    // next window start must technically be realigned to the new multiple.
    // this probably never matters, because m_next_window_start is 0 at conf time
    m_next_window_start = (m_next_window_start / m_window_time) * m_window_time;
  }

  // Set the time to wait after a window before a window is emitted in ticks
  void set_buffer_time(const daqdataformats::timestamp_t buffer_time) { m_buffer_time = buffer_time; }

  // True if this buffer has gone m_buffer_time past the end of the first window
  bool ready()
  {
    if (empty()) {
      return false;
    } else {
      return m_largest_time - (m_next_window_start + m_window_time) > m_buffer_time;
    }
  }

  bool empty() { return m_buffer.empty() && m_heartbeat_buffer.empty(); }

  // Fills out_set with the contents of the buffer that fall within
  // the first window, or with the next buffered heartbeat Set, if it
  // should be output next . This removes the contents that are added
  // to out_set from the buffer, and moves to the next window. Call
  // when ready() is true for full windows, or whenever to drain this
  // buffer.
  void flush(Set<T>& out_set)
  {
    // Heartbeats have no duration and live at window boundaries. If
    // there's a heartbeat at the start of our time window, we send it
    // out and don't advance the window: there might be objects in the
    // window that we want. We'll get those next time we call flush(),
    // because the heartbeat will have been popped from the heartbeat
    // buffer
    if (!m_heartbeat_buffer.empty() && m_heartbeat_buffer.top().start_time == m_next_window_start) {
      auto& hb = m_heartbeat_buffer.top();
      TLOG_DEBUG(4) << "Flushing heartbeat with start time " << hb.start_time;
      out_set.start_time = hb.start_time;
      out_set.end_time = hb.end_time;
      out_set.origin = hb.origin;
      out_set.type = Set<T>::Type::kHeartbeat;
      m_heartbeat_buffer.pop();
      return;
    }

    out_set.type = Set<T>::Type::kPayload;
    out_set.start_time = m_next_window_start;
    out_set.end_time = m_next_window_start + m_window_time;
    m_next_window_start = m_next_window_start + m_window_time;
    while (!m_buffer.empty() && m_buffer.top().time_start <= out_set.end_time) {
      if (m_buffer.top().time_start < out_set.start_time) {
        ers::warning(WindowlessOutputError(ERS_HERE, m_name, m_algorithm));
        // top is discarded
      } else {
        out_set.objects.emplace_back(m_buffer.top());
      }
      m_buffer.pop();
    }
    TLOG_DEBUG(4) << "Filled payload from " << out_set.start_time << " to " << out_set.end_time << " with " << out_set.objects.size() << " objects";
  }

private:
  std::priority_queue<T, std::vector<T>, time_start_greater_t<T>> m_buffer;
  std::priority_queue<Set<T>, std::vector<Set<T>>, start_time_greater_t<Set<T>>> m_heartbeat_buffer;
  const std::string &m_name, &m_algorithm;
  daqdataformats::timestamp_t m_next_window_start; // tick start of next window, or 0 if not yet known
  daqdataformats::timestamp_t m_buffer_time;       // ticks to buffer after a window before a window is valid
  daqdataformats::timestamp_t m_window_time;       // width of output windows in ticks
  daqdataformats::timestamp_t m_largest_time;      // larges observed timestamp
};

} // namespace dunedaq::trigger

#endif // TRIGGER_SRC_TRIGGER_TIMESLICEOUTPUTBUFFER_HPP_
