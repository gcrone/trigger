/**
 * @file TCSet.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_TCSET_HPP_
#define TRIGGER_INCLUDE_TRIGGER_TCSET_HPP_

#include "dfmessages/SourceID_serialization.hpp"
#include "serialization/Serialization.hpp"
#include "trigger/Set.hpp"
#include "trigger/TriggerCandidate_serialization.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

namespace dunedaq::trigger {

using TCSet = Set<triggeralgs::TriggerCandidate>;

} // namespace dunedaq::trigger

MSGPACK_ADD_ENUM(dunedaq::trigger::TCSet::Type)
DUNE_DAQ_SERIALIZE_NON_INTRUSIVE(dunedaq::trigger, TCSet, seqno, run_number, origin, type, start_time, end_time, objects)

#endif // TRIGGER_INCLUDE_TRIGGER_TCSET_HPP_
