/**
 * @file Set.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_SET_HPP_
#define TRIGGER_INCLUDE_TRIGGER_SET_HPP_

#include "daqdataformats/SourceID.hpp"
#include "daqdataformats/Types.hpp"
#include "detdataformats/trigger/Types.hpp"

#include <cstdint>
#include <vector>

namespace dunedaq::trigger {

/**
 * @brief A set of TPs or TAs in a given time window, defined by its start and end times
 */
template<class T>
class Set
{
public:
  using element_t = T;
  using origin_t = daqdataformats::SourceID;
  using timestamp_t = daqdataformats::timestamp_t;

  enum Type
  {
    kUnknown = 0,
    kPayload = 1,
    kHeartbeat = 2
  };

  // An incremental count of how many Sets have been produced by this source
  // At TPSet rates seen in one protodune felix link, 32bits overflows in a week.
  using seqno_t = uint64_t; // NOLINT(build/unsigned)
  seqno_t seqno{ 0 };

  // The current run number
  daqdataformats::run_number_t run_number{ 0 };
  
  // Identify the instance creator/stream/source of this set.
  origin_t origin{ daqdataformats::SourceID(daqdataformats::SourceID::Subsystem::kTRG,
                                         daqdataformats::SourceID::s_invalid_id) };

  // Whether this Set is a regular bag-of-objects or a heartbeat
  Type type{ kUnknown };

  // The earliest timestamp inspected to form this Set
  timestamp_t start_time{ 0 };

  // The latest timestamp inspected to form this Set
  timestamp_t end_time{ 0 };

  // The TPs/TAs themselves. Needs a better name!
  std::vector<T> objects;
};

} // namespace dunedaq::trigger

#endif // TRIGGER_INCLUDE_TRIGGER_SET_HPP_

// Local Variables:
// c-basic-offset: 2
// End:
