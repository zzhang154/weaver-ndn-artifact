#ifndef CFN_ROOT_APP_HPP
#define CFN_ROOT_APP_HPP

#include "../ndn-app.hpp"
#include <map>
#include <vector>
#include <string>
#include "aggregation-buffer.hpp"
#include "congestion-control.hpp"

namespace ns3 {

/** CFNRootApp: Root aggregator that initiates Interests and uses congestion control. */
class CFNRootApp : public ndn::App {
public:
  static TypeId GetTypeId();
  CFNRootApp();

  virtual void StartApplication();
  virtual void StopApplication();

  // Called when Data arrives from a child node
  virtual void OnData(std::shared_ptr<const ndn::Data> data);

  // Callback for straggler timeout for a given sequence
  void OnStragglerTimeout(int seq);

  // Set the list of child node prefixes (direct children of the root in the tree)
  void SetChildren(const std::vector<std::string>& children);

  // Set the congestion control algorithm by name ("AIMD", "CUBIC", or "BBR")
  void SetCongestionControl(const std::string& algName);

private:
  // Helper to send Interests to all children for a new sequence
  void SendInterest(int seq);
  // Check congestion window and send further Interests if window allows
  void TrySendNext();
  void WriteOutputPayload(int seq, const AggregationBuffer& buf);

  std::string m_prefix;                        // Prefix (identity) of root node (optional)
  std::vector<std::string> m_children;         // List of child node names (prefixes)
  double m_childTimeout;                       // Timeout for children data
  std::map<int, AggregationBuffer> m_buffers;  // Buffers for active sequences
  std::string m_ccName;                        // Name of congestion control algorithm
  std::unique_ptr<CongestionControl> m_congestionCtrl; // Active congestion control instance
  int m_nextSeq;                               // Sequence number to use for next new interest
  uint32_t m_maxSeq;                           // optional cap for FL training rounds; 0 means unlimited
  std::string m_flOutputDir;                   // writes root aggregate as aggregate-<seq>.wfl
};

} // namespace ns3

#endif // CFN_ROOT_APP_HPP
