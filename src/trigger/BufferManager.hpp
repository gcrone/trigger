/**
 * @file BufferManager.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_SRC_TRIGGER_BUFFERMANAGER_HPP_
#define TRIGGER_SRC_TRIGGER_BUFFERMANAGER_HPP_

#include "daqdataformats/Types.hpp"

#include <atomic>
#include <set>
#include <vector>
#include <cstddef>

namespace dunedaq {
namespace trigger {

/**
 * @brief BufferManager description.
 *
 */
template<typename BSET>
class BufferManager
{
public:
  BufferManager();
  explicit BufferManager(size_t buffer_size)
    : m_buffer_max_size(buffer_size)
    , m_buffer_earliest_start_time(0)
    , m_buffer_latest_end_time(0)
  {}

  virtual ~BufferManager() {}

  void set_buffer_size(size_t size) { m_buffer_max_size = size; }
  void clear_buffer() { m_txset_buffer.clear(); }
  size_t get_buffer_size() { return m_buffer_max_size; }
  size_t get_stored_size() { return m_txset_buffer.size(); }

  BufferManager(BufferManager const&) = delete;
  BufferManager(BufferManager&&) = default;
  BufferManager& operator=(BufferManager const&) = delete;
  BufferManager& operator=(BufferManager&&) = default;

  /**
   *  add a TxSet to the buffer. Remove oldest TxSets from buffer if we are at maximum size
   */
  bool add(BSET& txs)
  {
    if (m_txset_buffer.size() >=
        m_buffer_max_size) // delete oldest TxSet if buffer full (and updating earliest start time) -> circular buffer
    {
      auto firstIt = m_txset_buffer.begin();
      m_txset_buffer.erase(firstIt);
      m_buffer_earliest_start_time = m_txset_buffer.begin()->start_time;
    }
    if ((m_buffer_earliest_start_time == 0) || (txs.start_time < m_buffer_earliest_start_time))
      m_buffer_earliest_start_time = txs.start_time;

    if ((m_buffer_latest_end_time == 0) || (txs.end_time > m_buffer_latest_end_time))
      m_buffer_latest_end_time = txs.end_time;

    return m_txset_buffer.insert(txs).second; // false if txs with same start_time already exists
  }

  enum DataRequestOutcome
  {
    kEmpty,
    kLate,
    kSuccess
  };

  struct DataRequestOutput
  {
    typename std::vector<BSET> txsets_in_window;
    DataRequestOutcome ds_outcome;
  };

  /**
   * return a vector of all the TxSets in the buffer that overlap with [start_time, end_time]
   */
  DataRequestOutput get_txsets_in_window(daqdataformats::timestamp_t start_time, daqdataformats::timestamp_t end_time)
  {
    BufferManager::DataRequestOutput ds_out;
    std::vector<BSET> txsets_output;

    if (end_time < m_buffer_earliest_start_time) {
      ds_out.txsets_in_window = txsets_output;
      ds_out.ds_outcome = BufferManager::kEmpty;
      return ds_out;
    }

    if (start_time > m_buffer_latest_end_time) {
      ds_out.txsets_in_window = txsets_output;
      ds_out.ds_outcome = BufferManager::kLate;
      return ds_out;
    }

    BSET txset_low, txset_up;
    txset_low.start_time = start_time;
    txset_up.start_time = end_time;

    typename std::set<BSET, TxSetCmp>::iterator it, it_low, it_up;

    // checking first and last TxSet of buffer that have a start_time within data request limits
    it_low = m_txset_buffer.lower_bound(txset_low);
    it_up = m_txset_buffer.upper_bound(txset_up);
    it = it_low;

    // checking if previous TxSet has a end_time that is after the data request's start time
    if (!(it == m_txset_buffer.begin())) {
      it--;
      if ((*it).end_time > start_time)
        txsets_output.push_back(*it);
      it++;
    }

    // loading TxSets
    while (it != it_up) {
      txsets_output.push_back(*it);
      it++;
    }

    ds_out.txsets_in_window = txsets_output;
    ds_out.ds_outcome = BufferManager::kSuccess;

    return ds_out;
  }

  daqdataformats::timestamp_t get_earliest_start_time() const { return m_buffer_earliest_start_time; }
  daqdataformats::timestamp_t get_latest_end_time() const { return m_buffer_latest_end_time; }

private:
  // Buffer contains TxSet ordered by start_time
  struct TxSetCmp
  {
    bool operator()(const BSET& ltps, const BSET& rtps) const
    {
      daqdataformats::timestamp_t const LTPS = ltps.start_time;
      daqdataformats::timestamp_t const RTPS = rtps.start_time;
      return LTPS < RTPS;
    }
  };

  // Where the TxSet will be buffered
  std::set<BSET, TxSetCmp> m_txset_buffer;

  // Buffer maximum size.
  std::atomic<size_t> m_buffer_max_size;

  // Earliest start time stored in the buffer
  daqdataformats::timestamp_t m_buffer_earliest_start_time;

  // Latest end time stored in the buffer
  daqdataformats::timestamp_t m_buffer_latest_end_time;
};

} // namespace trigger
} // namespace dunedaq

#endif // TRIGGER_SRC_TRIGGER_BUFFERMANAGER_HPP_
