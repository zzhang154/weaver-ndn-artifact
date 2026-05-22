#ifndef WEAVER_MININDN_AGGREGATION_BUFFER_HPP
#define WEAVER_MININDN_AGGREGATION_BUFFER_HPP

#include <ndn-cxx/name.hpp>
#include <ndn-cxx/util/scheduler.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace weaver {

struct AggregationBuffer
{
  uint64_t seq = 0;
  ndn::Name responseName;
  size_t expectedChildren = 0;
  uint64_t partialSum = 0;
  uint64_t flSampleCount = 0;
  std::vector<double> flWeightedSum;
  bool hasFlPayload = false;
  std::set<std::string> receivedChildren;
  std::set<std::string> failedChildren;
  ndn::scheduler::EventId timeoutEvent;

  bool
  isComplete() const
  {
    return receivedChildren.size() == expectedChildren;
  }

  bool
  isSettled() const
  {
    return receivedChildren.size() + failedChildren.size() >= expectedChildren;
  }
};

} // namespace weaver

#endif // WEAVER_MININDN_AGGREGATION_BUFFER_HPP
