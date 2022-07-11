/**
 * @file TxSet_test.cxx  Unit tests for TxSet family
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "trigger/TPSet.hpp"

/**
 * @brief Name of this test module
 */
#define BOOST_TEST_MODULE TxSet_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>

using namespace dunedaq;

BOOST_AUTO_TEST_SUITE(BOOST_TEST_MODULE)

/**
 * @brief Initializes the QueueRegistry
 */
struct TxSetTestFixture
{
  TxSetTestFixture() {}

  void setup() {}
};

BOOST_TEST_GLOBAL_FIXTURE(TxSetTestFixture);

BOOST_AUTO_TEST_CASE(Initialization)
{
  trigger::TPSet tpset;
  BOOST_CHECK_EQUAL(tpset.origin.subsystem, daqdataformats::SourceID::Subsystem::kTRG);
}

BOOST_AUTO_TEST_SUITE_END()
