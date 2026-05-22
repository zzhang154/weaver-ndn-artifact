#ifndef STRAGGLER_MANAGER_HPP
#define STRAGGLER_MANAGER_HPP

#include "ns3/event-id.h"
#include "ns3/simulator.h"

namespace ns3 {

class CFNAggregatorApp;
class CFNRootApp;

/** StragglerManager: Utility to schedule and cancel aggregation timeout events. */
class StragglerManager {
public:
  static EventId ScheduleAggregator(CFNAggregatorApp* app, int seq, double delay);
  static EventId ScheduleRoot(CFNRootApp* app, int seq, double delay);
  static void Cancel(EventId& event);
};

} // namespace ns3

#endif // STRAGGLER_MANAGER_HPP
