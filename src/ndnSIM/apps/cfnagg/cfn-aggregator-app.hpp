#ifndef CFN_AGGREGATOR_APP_HPP
#define CFN_AGGREGATOR_APP_HPP

#include "../ndn-app.hpp"
#include "ns3/random-variable-stream.h"  // Add this include for UniformRandomVariable
#include <map>
#include <vector>
#include <string>
#include "aggregation-buffer.hpp"

namespace ns3 {

/** CFNAggregatorApp: Aggregation node application that collects data from children and aggregates it. */
class CFNAggregatorApp : public ndn::App {
public:
  static TypeId GetTypeId();
  CFNAggregatorApp();

  virtual void StartApplication();
  virtual void StopApplication();

  // Called when an Interest arrives from this node's parent (or root consumer)
  virtual void OnInterest(std::shared_ptr<const ndn::Interest> interest);

  // Called when a Data packet arrives from a child node
  virtual void OnData(std::shared_ptr<const ndn::Data> data);

  // Callback for straggler timeout (when some children did not respond in time)
  void OnStragglerTimeout(int seq);

  // Configure the list of child node prefixes for this aggregator
  void SetChildren(const std::vector<std::string>& children);

private:
  std::string m_prefix;                       // Prefix identifying this aggregator node
  std::vector<std::string> m_children;        // List of child node names (prefixes)
  double m_childTimeout;                      // Timeout (seconds) to wait for children data
  std::map<int, AggregationBuffer> m_buffers; // Active aggregation buffers indexed by sequence number
  Ptr<UniformRandomVariable> m_rand;          // RNG for Interest nonces
};

} // namespace ns3

#endif // CFN_AGGREGATOR_APP_HPP
