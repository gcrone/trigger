/**
 * @file streamed_TPs_to_text.cxx Dump streamed TPs from an HDF5 into the text format used by trigger
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "CLI/CLI.hpp"

#include "detdataformats/trigger/TriggerObjectOverlay.hpp"
#include "detdataformats/trigger/TriggerPrimitive.hpp"
#include "hdf5libs/HDF5RawDataFile.hpp"

#include <daqdataformats/Fragment.hpp>
#include <daqdataformats/GeoID.hpp>
#include <daqdataformats/TimeSlice.hpp>
#include <daqdataformats/TimeSliceHeader.hpp>
#include <daqdataformats/Types.hpp>

#include <iostream>

using dunedaq::detdataformats::trigger::TriggerActivity;
using dunedaq::detdataformats::trigger::TriggerActivityData;
using dunedaq::detdataformats::trigger::TriggerCandidate;
using dunedaq::detdataformats::trigger::TriggerPrimitive;

void
print_tp(const TriggerPrimitive& prim, size_t offset = 0)
{
  for (size_t i = 0; i < offset; ++i)
    std::cout << "\t";
  std::cout << "\t" << prim.time_start << "\t" << prim.time_over_threshold << "\t" << prim.time_peak << "\t"
            << prim.channel << "\t" << prim.adc_integral << "\t" << prim.adc_peak << "\t" << prim.detid << "\t"
            << prim.type << std::endl;
}

void
print_tps(std::unique_ptr<dunedaq::daqdataformats::Fragment>&& frag, size_t offset = 0)
{
  size_t payload_size = frag->get_size() - sizeof(dunedaq::daqdataformats::FragmentHeader);
  size_t n_tps = payload_size / sizeof(TriggerPrimitive);
  size_t remainder = payload_size % sizeof(TriggerPrimitive);
  assert(remainder == 0);
  const TriggerPrimitive* prim = reinterpret_cast<TriggerPrimitive*>(frag->get_data());
  std::cout << "Trigger primitives for " << frag->get_element_id() << std::endl;
  for (size_t i = 0; i < n_tps; ++i) {
    print_tp(*prim, offset);
    ++prim;
  }
}

void
print_ta(const TriggerActivity& activity)
{
  std::cout << "\t" << activity.data.time_start << "\t" << activity.data.time_end << "\t" << activity.data.channel_start
            << "\t" << activity.data.channel_end << std::endl;
  std::cout << "\tInput TPs:" << std::endl;
  for (size_t i = 0; i < activity.n_inputs; ++i) {
    const TriggerPrimitive& prim = activity.inputs[i];
    print_tp(prim, 1);
  }
  std::cout << std::endl;
}

void
print_tas(std::unique_ptr<dunedaq::daqdataformats::Fragment>&& frag)
{
  size_t payload_size = frag->get_size() - sizeof(dunedaq::daqdataformats::FragmentHeader);

  std::cout << "Trigger activities for " << frag->get_element_id() << std::endl;
  // The fragment contains a number of variable-sized TAs stored
  // contiguously so we can't calculate the number of TAs a priori. We
  // have to do this pointer arithmetic business while looping over
  // the items in the fragment
  char* buffer = static_cast<char*>(frag->get_data());
  size_t offset = 0;
  const TriggerActivity* activity = reinterpret_cast<TriggerActivity*>(buffer);
  while (offset < payload_size) {
    print_ta(*activity);
    offset += sizeof(TriggerActivity) + activity->n_inputs * sizeof(TriggerActivity::input_t);
    activity = reinterpret_cast<TriggerActivity*>(buffer + offset);
  }
}

void
print_ta_data(const dunedaq::detdataformats::trigger::TriggerActivityData& ta_data)
{
  std::cout << "\t\t" << ta_data.time_start << "\t" << ta_data.time_end << "\t" << ta_data.channel_start << "\t"
            << ta_data.channel_end << std::endl;
}

void
print_tc(const TriggerCandidate& candidate)
{
  std::cout << "\t" << candidate.data.time_start << "\t" << candidate.data.time_end << std::endl;
  std::cout << "\tInput TAs:" << std::endl;
  for (size_t i = 0; i < candidate.n_inputs; ++i) {
    const TriggerActivityData& activity_data = candidate.inputs[i];
    print_ta_data(activity_data);
  }
  std::cout << std::endl;
}

void
print_tcs(std::unique_ptr<dunedaq::daqdataformats::Fragment>&& frag)
{
  size_t payload_size = frag->get_size() - sizeof(dunedaq::daqdataformats::FragmentHeader);

  std::cout << "Trigger candidates for " << frag->get_element_id() << std::endl;
  // The fragment contains a number of variable-sized TCs stored
  // contiguously so we can't calculate the number of TCs a priori. We
  // have to do this pointer arithmetic business while looping over
  // the items in the fragment
  char* buffer = static_cast<char*>(frag->get_data());
  size_t offset = 0;
  const TriggerCandidate* candidate = reinterpret_cast<TriggerCandidate*>(buffer);
  while (offset < payload_size) {
    print_tc(*candidate);
    offset += sizeof(TriggerCandidate) + candidate->n_inputs * sizeof(TriggerCandidate::input_t);
    candidate = reinterpret_cast<TriggerCandidate*>(buffer + offset);
  }
}

int
main(int argc, char** argv)
{
  CLI::App app{ "Print DS fragments from an HDF5 file" };

  std::string in_filename;
  app.add_option("-i,--input", in_filename, "Input HDF5 file");

  CLI11_PARSE(app, argc, argv);

  dunedaq::hdf5libs::HDF5RawDataFile hdf5file(in_filename);

  auto record_ids = hdf5file.get_all_trigger_record_ids();

  for (auto const& record_id : record_ids) {
    std::cout << "-----------------------------------------------------------------------------------" << std::endl;
    std::cout << "Trigger record " << record_id.first << std::endl;
    auto frag_paths =
      hdf5file.get_fragment_dataset_paths(record_id, dunedaq::daqdataformats::GeoID::SystemType::kDataSelection);
    for (auto const& frag_path : frag_paths) {
      auto frag_ptr = hdf5file.get_frag_ptr(frag_path);
      if (frag_ptr->get_fragment_type() == dunedaq::daqdataformats::FragmentType::kTriggerPrimitives) {
        print_tps(std::move(frag_ptr));
      }
      if (frag_ptr->get_fragment_type() == dunedaq::daqdataformats::FragmentType::kTriggerActivities) {
        print_tas(std::move(frag_ptr));
      }
      if (frag_ptr->get_fragment_type() == dunedaq::daqdataformats::FragmentType::kTriggerCandidates) {
        print_tcs(std::move(frag_ptr));
      }

      // std::cout << frag_path << std::endl;
    }
    std::cout << std::endl;
  }
}
