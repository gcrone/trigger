/**
 * @file zipper.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef TRIGGER_PLUGINS_ZIPPER_HPP_
#define TRIGGER_PLUGINS_ZIPPER_HPP_

#include <chrono>
#include <queue>
#include <vector>
#include <functional>
#include <unordered_map>
#include <stdexcept>

namespace zipper {

/**
   Prototype node for tracking elements in the zipper queue.

   The "payload" is the user object to place in the queue.

   The "ordering" is a value used to order the the nodes.

   The "identity" is a value that can be hashed and is used define
   a "stream".  That is, all "payload" objects with a common
   "identity" are so grouped.

   The "debut" is the time point which the node entered the queue.
*/
template<typename Payload,
         typename Ordering = size_t,
         typename Identity = size_t,
         typename TimePoint = std::chrono::steady_clock::time_point>
struct Node
{
  using payload_t = Payload;
  using ordering_t = Ordering;
  using identity_t = Identity;
  using timepoint_t = TimePoint;

  payload_t payload;
  ordering_t ordering;
  identity_t identity;
  timepoint_t debut;

  // Partial order.
  bool operator<(const Node& rhs) const { return ordering < rhs.ordering; }
  bool operator>(const Node& rhs) const { return ordering > rhs.ordering; }
};

/**
   A k-way merge with ordering and optional latency guarantees.

   See @ref Node above for example of Node type.

   Note, the priority is in ASCENDING orderering, the queue inside
   merge is a min-heap.  If the reverse is true, you must provide
   a Node type with a "backwards" less-than operator.
*/
template<typename Node>
class merge : public std::priority_queue<Node, std::vector<Node>, std::greater<Node>>
{

public:
  using node_t = Node;
  using payload_t = typename Node::payload_t;
  using ordering_t = typename Node::ordering_t;
  using identity_t = typename Node::identity_t;
  using timepoint_t = typename Node::timepoint_t;
  using duration_t = typename timepoint_t::duration;
  using clock_t = typename timepoint_t::clock;

  /**
     Construct a zipper merge.

     See @ref set_cardinality() for the "k" parameter.

     A nonzero max_latency must be supplied to enable latency
     guarantees.
   */
  explicit merge(size_t k = 0, duration_t max_latency = duration_t::zero())
    : cardinality(k)
    , latency(max_latency)
    , origin(0) // ordering
  {}

  /**
      Set the expected number of identified streams.

      Cardinality is used to determine completeness.
      Completeness is achieved when the number of streams which
      are represented in the zipper matches the cardinality.

      Cardinality may be arbitrarily set anytime prior to the
      first drain.

      A subsequent increase in the expected number of streams
      may be accomodated by increasing the cardinality to match.

      Subsequent reduction of cardinality will result in
      undefined behavior which may drain output prematurely.
      Once elements causing "over completeness" are drained the
      zipper returns to expected behavior.

      To avoid this, at the cost of draining the entire merge
      buffer, see @ref clear();

      A cardinality of zero will cause the zipper always be
      considered complete and thus will exhibit permanent
      undefined behavior as described above.
  */
  void set_cardinality(size_t k) { cardinality = k; }

  /**
     Set the maximum latency
   */
  void set_max_latency(duration_t max_latency) { latency = max_latency; }

  ordering_t get_origin() const { return origin; }

  /**
     Clear the zipper merge buffer.
  */
  void clear()
  {
    std::vector<node_t> got;
    drain_full(std::back_inserter(got));
    streams.clear();
    origin = 0;
  }

  /**
     Feed a new node to the merge queue.

     Return true if it was accepted.  Rejection will occur if
     the node partial ordering places it "earlier" (smaller
     ordering value) than the last drained node.
  */
  bool feed(const node_t& node)
  {
    if (node.ordering < origin) {
      return false;
    }
    auto& s = streams[node.identity];
    s.occupancy += 1;
    s.last_seen = node.debut;
    this->push(node);
    return true;
  }

  /**
     Sugar to add a node to the queue from its constituents.
  */
  bool feed(const payload_t& pay,
            const ordering_t& ord,
            const identity_t& ident,
            const timepoint_t& debut = clock_t::now())
  {
    return feed(node_t{ pay, ord, ident, debut });
  }

  /** Unconditionally pop and return the top node.

      Throws if queue is empty but otherwise does not care about
      completeness.

      Cardinality may be set any time prior to the first drain.
   */
  node_t next()
  {
    if (this->empty()) {
      throw std::out_of_range("attempt to drain empty queue");
    }
    auto node = this->top(); // copy
    this->pop();

    auto& s = streams.at(node.identity);
    s.occupancy -= 1;
    origin = node.ordering;

    return node;
  }

  /**
     Return all nodes, unconditionally.
  */
  template<typename OutputIterator>
  OutputIterator drain_full(OutputIterator result)
  {
    while (!this->empty()) {
      *result = next(); // hey, dev: do not forget back_inserter
      ++result;
    }
    return result;
  }

  /**
     Return available nodes, maintaining latency guaratee.

     Calling this may lead to subsequent input nodes which are
     tardy to be rejected when fed to the merge.

     Note: if max latecy is zero, this is equivalent to calling
     @ref drain_waiting().
  */
  template<typename OutputIterator>
  OutputIterator drain_prompt(OutputIterator result, const timepoint_t& now = clock_t::now())
  {
    while (complete(now)) {
      *result = next(); // hey, dev: do not forget back_inserter
      ++result;
    }
    return result;
  }

  /**
     Return available nodes, maintaining completeness.

     This will preserve ability to accept from future tardy
     streams but may lead to unbound latency.
  */
  template<typename OutputIterator>
  OutputIterator drain_waiting(OutputIterator result)
  {
    while (complete()) {
      *result = next(); // hey, dev: do not forget back_inserter
      ++result;
    }
    return result;
  }

  /**
     Return the next top node without removal.

     Throws if queue is empty.
  */
  const node_t& peek() const
  {
    if (this->empty()) {
      throw std::out_of_range("attempt to peek empty queue");
    }
    return this->top();
  }

  /**
     Return true if queue is "complete".

     If a non-minimal "now" time is given then an unrepresented
     but stale stream will not degrade completeness.
   */
  bool complete(const timepoint_t& now = timepoint_t::min()) const
  {
    if (this->empty()) { 
      return false;
    }

    const size_t target_cardinality = streams.size();

    if (target_cardinality < cardinality) { // absent streams
      if (latency == duration_t::zero()) { // unbound latency
        return false;
      }
    }

    size_t completeness = 0;

    const auto top_ident = this->top().identity;

    // check each known stream to see if it is "represented"
    for (const auto& sit : streams) {
      const auto& ident = sit.first;
      auto have = sit.second.occupancy;

      // Do not count the top node.
      if (top_ident == ident) {
        have -= 1;
      }
      if (have > 0) {
        ++completeness;
        continue; // stream is represented
      }

      // unbound latency, wait as long as needed
      if (latency == duration_t::zero()) {
        // std::cerr << "no latency " << ident << std::endl;
        return false;
      }

      if (now == timepoint_t::min()) { // my clock is broken
        // std::cerr << "clock broken " << ident << std::endl;
        return false;
      }

      const auto& last_seen = sit.second.last_seen;
      auto delta = now - last_seen;
      auto delta_us = std::chrono::duration_cast<std::chrono::microseconds>(delta);
      if (delta < latency) {
        // std::cerr << "still active " << ident
        //           << " [" << completeness
        //           << "] " << delta_us.count() << " us"
        //           << std::endl;
        return false;
      }

      // std::cerr << "missing but stale " << ident
      //           << " [" << completeness
      //           << "] " << delta_us.count() << " us"
      //           << std::endl;
      // To preserve max latency we will not consider this
      // stale "unrepresented" to cause incompleteness.
      ++completeness;
    }

    // Currently, the cardinality is set to 2 even when there is
    // only 1 (active) input stream during FW TP Generation. We'll temporarily
    // reduce the cardinality condition to see if this resolves FW TPG
    // runs, and then if it does, rethink the cardinality/completeness
    // logic.
    return completeness >= target_cardinality;
  }

private:
  size_t cardinality;
  duration_t latency{ 0 };
  ordering_t origin;
  struct Stream
  {
    size_t occupancy{ 0 };
    timepoint_t last_seen{ duration_t::min() };
  };
  std::unordered_map<identity_t, Stream> streams;
};

} // namespace zipper
#endif // TRIGGER_PLUGINS_ZIPPER_HPP_
