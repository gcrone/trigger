/**
 * @file Issues.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_INCLUDE_TRIGGER_ISSUES_HPP_
#define TRIGGER_INCLUDE_TRIGGER_ISSUES_HPP_

#include "appfwk/DAQModule.hpp"
#include "daqdataformats/SourceID.hpp"
#include "daqdataformats/Types.hpp"
#include "ers/Issue.hpp"
#include "triggeralgs/Types.hpp"

#include <string>
#include <bitset>

// NOLINTNEXTLINE(build/define_used)
#define TLVL_ENTER_EXIT_METHODS 10
// NOLINTNEXTLINE(build/define_used)
#define TLVL_GENERATION 11
// NOLINTNEXTLINE(build/define_used)
#define TLVL_CANDIDATE 15

namespace dunedaq {

ERS_DECLARE_ISSUE(trigger, InvalidConfiguration, "An invalid configuration object was received", ERS_EMPTY)
ERS_DECLARE_ISSUE(trigger, TriggerActive, "Trigger is active now", ERS_EMPTY)
ERS_DECLARE_ISSUE(trigger, TriggerPaused, "Trigger is paused", ERS_EMPTY)
ERS_DECLARE_ISSUE(trigger, TriggerInhibited, "Trigger is inhibited in run " << runno, ((int64_t)runno))
ERS_DECLARE_ISSUE(trigger, TriggerStartOfRun, "Start of run " << runno, ((int64_t)runno))
ERS_DECLARE_ISSUE(trigger, TriggerEndOfRun, "End of run " << runno, ((int64_t)runno))

ERS_DECLARE_ISSUE(trigger, UnknownGeoID, "Unknown SourceID: " << source_id, ((daqdataformats::SourceID)source_id))
ERS_DECLARE_ISSUE(trigger, InvalidSystemType, "Unknown system type " << type, ((std::string)type))

ERS_DECLARE_ISSUE_BASE(trigger,
                       SignalTypeError,
                       appfwk::GeneralDAQModuleIssue,
                       "Signal type " << signal_type << " invalid.",
                       ((std::string)name),
                       ((uint32_t)signal_type)) // NOLINT(build/unsigned)

ERS_DECLARE_ISSUE_BASE(trigger,
                       InvalidQueueFatalError,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << queueType << " queue was not successfully created.",
                       ((std::string)name),
                       ((std::string)queueType))

ERS_DECLARE_ISSUE_BASE(trigger,
                       AlgorithmFatalError,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm << " failed to run.",
                       ((std::string)name),
                       ((std::string)algorithm))

ERS_DECLARE_ISSUE_BASE(trigger,
                       UnknownSetError,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm << " encountered an unknown Set type.",
                       ((std::string)name),
                       ((std::string)algorithm))

ERS_DECLARE_ISSUE_BASE(trigger,
                       InconsistentSetTimeError,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm << " maker encountered Sets with inconsistent start/end times.",
                       ((std::string)name),
                       ((std::string)algorithm))

// clang-format off
ERS_DECLARE_ISSUE_BASE(trigger,
                       TardyOutputError,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm << " maker generated a tardy output, which will be dropped."
                       << " Output's time is " << output_time << ", last sent time is " << last_sent_time,
                       ((std::string)name),
                       ((std::string)algorithm)
                       ((daqdataformats::timestamp_t)output_time)
                       ((daqdataformats::timestamp_t)last_sent_time))

ERS_DECLARE_ISSUE_BASE(trigger,
                       UnalignedHeartbeat,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm << " maker received a heartbeat with start time " << start_time
                       << ", not aligned to a window time boundary " << window_time,
                       ((std::string)name),
                       ((std::string)algorithm)
                       ((daqdataformats::timestamp_t)start_time)
                       ((daqdataformats::timestamp_t)window_time))

ERS_DECLARE_ISSUE_BASE(trigger,
                       TardyInputSet,
                       appfwk::GeneralDAQModuleIssue,
                       "Tardy input set from element " << element
                       << ". Set start time " << start_time << " but last sent time " << last_sent_time,
                       ((std::string)name),
                       ((uint32_t)element) // NOLINT(build/unsigned)
                       ((daqdataformats::timestamp_t)start_time)
                       ((daqdataformats::timestamp_t)last_sent_time))
// clang-format on

ERS_DECLARE_ISSUE_BASE(trigger,
                       OutOfOrderSets,
                       appfwk::GeneralDAQModuleIssue,
                       "Received sets with start_times out of order: previous was " << previous << " current is "
                                                                                    << current,
                       ((std::string)name),
                       ((triggeralgs::timestamp_t)previous)((triggeralgs::timestamp_t)current))

ERS_DECLARE_ISSUE_BASE(trigger,
                       AlgorithmFailedToSend,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm << " maker failed to add output to a queue, which will be dropped.",
                       ((std::string)name),
                       ((std::string)algorithm))

ERS_DECLARE_ISSUE_BASE(trigger,
                       AlgorithmFailedToHeartbeat,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm << " maker failed to add a heartbeat to a queue, which will be dropped.",
                       ((std::string)name),
                       ((std::string)algorithm))

ERS_DECLARE_ISSUE_BASE(trigger,
                       WindowlessOutputError,
                       appfwk::GeneralDAQModuleIssue,
                       "The " << algorithm
                              << " maker generated an output that was not in any input window, which will be dropped.",
                       ((std::string)name),
                       ((std::string)algorithm))

ERS_DECLARE_ISSUE_BASE(trigger,
                       BadTPInputFile,
                       appfwk::GeneralDAQModuleIssue,
                       "Problem opening file " << filename,
                       ((std::string)name),
                       ((std::string)filename))

ERS_DECLARE_ISSUE_BASE(trigger,
                       UnsortedTP,
                       appfwk::GeneralDAQModuleIssue,
                       "TP with time_start " << time_start
                                             << "is higher than time_start of last TP and will be ignored.",
                       ((std::string)name),
                       ((int64_t)time_start))

ERS_DECLARE_ISSUE_BASE(trigger,
                       BadTriggerBitmask,
                       appfwk::GeneralDAQModuleIssue,
                       "The trigger type contains high bits: " << trigger_type,
                       ((std::string)name),
                       ((std::bitset<16>)trigger_type))

ERS_DECLARE_ISSUE_BASE(trigger,
                       TCOutOfTimeout,
                       appfwk::GeneralDAQModuleIssue,
                       "TC of type " << tc_type << ", timestamp " << tc_timestamp << " overlaps with previous TD readout window: [" << td_start << ", " << td_end << "]",
                       ((std::string)name),
                       ((int)tc_type)
                       ((triggeralgs::timestamp_t)tc_timestamp)
                       ((triggeralgs::timestamp_t)td_start)
                       ((triggeralgs::timestamp_t)td_end))

ERS_DECLARE_ISSUE_BASE(trigger,
                       InvalidHSIEventRunNumber,
                       appfwk::GeneralDAQModuleIssue,
                       "An invalid run number was received in an HSIEvent, "
                         << "received=" << received << ", expected=" << expected << ", timestamp=" << ts
                         << ", sequence_count=" << seq,
                       ((std::string)name),
                       ((size_t)received)((size_t)expected)((size_t)ts)((size_t)seq))

} // namespace dunedaq

#endif // TRIGGER_INCLUDE_TRIGGER_ISSUES_HPP_
