/**
 * @file TokenManager_test.cxx  TokenManager class Unit Tests
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "trigger/LivetimeCounter.hpp"
#include "trigger/TokenManager.hpp"

#include "iomanager/IOManager.hpp"
#include "logging/Logging.hpp"

/**
 * @brief Name of this test module
 */
#define BOOST_TEST_MODULE TokenManager_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>

using namespace dunedaq;

BOOST_AUTO_TEST_SUITE(BOOST_TEST_MODULE)

/**
 * @brief Initializes the IOManager
 */
struct IOManagerTestFixture
{
  IOManagerTestFixture()
  {
    dunedaq::iomanager::ConnectionIds_t connections;
    dunedaq::iomanager::ConnectionId cid;
    cid.service_type = dunedaq::iomanager::ServiceType::kNetReceiver;
    cid.uid = "foo";
    cid.uri = "inproc://foo";
    cid.data_type = "dfmessages::TriggerDecisionToken";
    connections.push_back(cid);
    cid.service_type = dunedaq::iomanager::ServiceType::kNetSender;
    cid.uid = "foo_s";
    connections.push_back(cid);
    get_iomanager()->configure(connections);
  }
  ~IOManagerTestFixture()
  {
    get_iomanager()->reset();
  }

  IOManagerTestFixture(IOManagerTestFixture const&) = default;
  IOManagerTestFixture(IOManagerTestFixture&&) = default;
  IOManagerTestFixture& operator=(IOManagerTestFixture const&) = default;
  IOManagerTestFixture& operator=(IOManagerTestFixture&&) = default;
};

BOOST_TEST_GLOBAL_FIXTURE(IOManagerTestFixture);

BOOST_AUTO_TEST_CASE(Basics)
{
  using namespace std::chrono_literals;

  int initial_tokens = 10;
  daqdataformats::run_number_t run_number = 1;
  auto livetime_counter = std::make_shared<trigger::LivetimeCounter>(trigger::LivetimeCounter::State::kPaused);
  trigger::TokenManager tm("foo", initial_tokens, run_number, livetime_counter);

  BOOST_CHECK_EQUAL(tm.get_n_tokens(), initial_tokens);
  BOOST_CHECK_EQUAL(tm.triggers_allowed(), true);

  for (int i = 0; i < initial_tokens - 1; ++i) {
    tm.trigger_sent(i);
    BOOST_CHECK_EQUAL(tm.get_n_tokens(), initial_tokens - i - 1);
    BOOST_CHECK_EQUAL(tm.triggers_allowed(), true);
  }

  tm.trigger_sent(initial_tokens);
  BOOST_CHECK_EQUAL(tm.get_n_tokens(), 0);
  BOOST_CHECK_EQUAL(tm.triggers_allowed(), false);

  // Send a token and check that triggers become allowed again
  dfmessages::TriggerDecisionToken token;
  token.run_number = run_number;
  token.trigger_number = 1;
  get_iom_sender<dfmessages::TriggerDecisionToken>("foo_s")->send(std::move(token), std::chrono::milliseconds(10));

  // Give TokenManager a little time to pop the token off the queue
  std::this_thread::sleep_for(100ms);
  BOOST_CHECK_EQUAL(tm.get_n_tokens(), 1);
  BOOST_CHECK_EQUAL(tm.triggers_allowed(), true);
}

BOOST_AUTO_TEST_SUITE_END()
