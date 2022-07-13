/**
 * @file BufferManager_test.cxx  BufferManager class Unit Tests
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "../src/trigger/TPSetBuffer.hpp" // NOLINT

#include "dfmessages/DataRequest.hpp"

/**
 * @brief Name of this test module
 */
#define BOOST_TEST_MODULE BufferManager_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <random>

using namespace dunedaq;

BOOST_AUTO_TEST_SUITE(BOOST_TEST_MODULE)

BOOST_AUTO_TEST_CASE(Basics)
{
  size_t buffer_size = 10;
  trigger::TPSetBuffer bm(buffer_size);
  trigger::TPSet input_tpset;

  // Generate a random TPSet
  std::default_random_engine generator;
  std::uniform_int_distribution<int> uniform(0, 100);

  input_tpset.start_time = 1000 + uniform(generator);
  input_tpset.end_time = input_tpset.start_time + uniform(generator);

  // Store a TPSet
  BOOST_CHECK_EQUAL(bm.add(input_tpset), true);

  // Store another TPSet
  input_tpset.start_time = 2000 + uniform(generator);
  input_tpset.end_time = input_tpset.start_time + uniform(generator);
  BOOST_CHECK_EQUAL(bm.add(input_tpset), true);
  BOOST_CHECK_EQUAL(bm.add(input_tpset), false);

  // Generate a data request, but not available in buffer anymore
  trigger::TPSetBuffer::DataRequestOutput requested_tpset;
  dfmessages::DataRequest input_data_request;
  input_data_request.request_information.window_begin = 100;
  input_data_request.request_information.window_end = 120;
  requested_tpset = bm.get_txsets_in_window(input_data_request.request_information.window_begin,
                                            input_data_request.request_information.window_end);
  BOOST_CHECK_EQUAL(requested_tpset.ds_outcome, trigger::TPSetBuffer::kEmpty);

  // Generate a data request, available in buffer
  input_data_request.request_information.window_begin = 100;
  input_data_request.request_information.window_end = 3000;
  requested_tpset = bm.get_txsets_in_window(input_data_request.request_information.window_begin,
                                            input_data_request.request_information.window_end);
  BOOST_CHECK_EQUAL(requested_tpset.ds_outcome, trigger::TPSetBuffer::kSuccess);

  BOOST_CHECK_EQUAL(requested_tpset.txsets_in_window.size(), 2);

  // Generate a data request, but not available in buffer yet
  input_data_request.request_information.window_begin = 3000;
  input_data_request.request_information.window_end = 4000;
  requested_tpset = bm.get_txsets_in_window(input_data_request.request_information.window_begin,
                                            input_data_request.request_information.window_end);
  BOOST_CHECK_EQUAL(requested_tpset.ds_outcome, trigger::TPSetBuffer::kLate);

  // Fill buffer beyond its capacity
  input_tpset.start_time = 3001;
  input_tpset.end_time = input_tpset.start_time + uniform(generator);
  bm.add(input_tpset);
  int ntps = buffer_size - 1;
  for (int i = 0; i < ntps; i++) {
    input_tpset.start_time = input_tpset.end_time + uniform(generator);
    input_tpset.end_time = input_tpset.start_time + uniform(generator);
    bm.add(input_tpset);
  }
  BOOST_CHECK_EQUAL(bm.get_stored_size(), buffer_size);

  input_data_request.request_information.window_begin = 0;
  input_data_request.request_information.window_end = 100000;
  requested_tpset = bm.get_txsets_in_window(input_data_request.request_information.window_begin,
                                            input_data_request.request_information.window_end);

  BOOST_CHECK_EQUAL(requested_tpset.txsets_in_window.size(), buffer_size);
  BOOST_CHECK_GT(requested_tpset.txsets_in_window.at(0).start_time, 3000);
  BOOST_CHECK_LT(requested_tpset.txsets_in_window.at(0).start_time, 3002);
}

BOOST_AUTO_TEST_SUITE_END()
