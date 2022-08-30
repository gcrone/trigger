/**
 * @file check_fragment_TPs.cxx Read TP fragments from file and check that they have start times within the request window
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "CLI/CLI.hpp"

#include "detdataformats/trigger/TriggerPrimitive.hpp"
#include "hdf5libs/HDF5RawDataFile.hpp"

#include <daqdataformats/Fragment.hpp>
#include <daqdataformats/FragmentHeader.hpp>
#include <daqdataformats/TriggerRecordHeader.hpp>
#include <daqdataformats/Types.hpp>
#include <iostream>

int
main(int argc, char** argv)
{
  CLI::App app{ "App description" };

  std::string filename;
  app.add_option("-f,--file", filename, "Input HDF5 file");

  CLI11_PARSE(app, argc, argv);

  dunedaq::hdf5libs::HDF5RawDataFile decoder(filename);

  auto trigger_record_ids = decoder.get_all_trigger_record_ids();
  
  for (auto trigger_record_id : trigger_record_ids){
    auto trigger_number = trigger_record_id.first;

    auto header = decoder.get_trh_ptr(trigger_number);
    std::cout << "Trigger record " << trigger_number << " has type 0x" << std::hex << header->get_trigger_type() << std::dec << std::endl;
  }
}
