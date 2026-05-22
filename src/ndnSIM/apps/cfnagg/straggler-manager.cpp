#include "straggler-manager.hpp"
#include "cfn-aggregator-app.hpp"
#include "cfn-root-app.hpp"

namespace ns3 {

EventId StragglerManager::ScheduleAggregator(CFNAggregatorApp* app, int seq, double delay) {
  // Schedule CFNAggregatorApp::OnStragglerTimeout(seq) to be called after 'delay' seconds
  return Simulator::Schedule(Seconds(delay), &CFNAggregatorApp::OnStragglerTimeout, app, seq);
}

EventId StragglerManager::ScheduleRoot(CFNRootApp* app, int seq, double delay) {
  // Schedule CFNRootApp::OnStragglerTimeout(seq) after 'delay' seconds
  return Simulator::Schedule(Seconds(delay), &CFNRootApp::OnStragglerTimeout, app, seq);
}

void StragglerManager::Cancel(EventId& event) {
  if (event.IsRunning()) {
    Simulator::Cancel(event);
  }
}

} // namespace ns3
