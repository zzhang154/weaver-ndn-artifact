#ifndef CFN_PRODUCER_APP_HPP
#define CFN_PRODUCER_APP_HPP

#include "../ndn-app.hpp"
#include "ns3/core-module.h"

namespace ns3 {

/** CFNProducerApp: Leaf node application that produces data for incoming Interests. */
class CFNProducerApp : public ndn::App {
public:
  static TypeId GetTypeId();
  CFNProducerApp();

  virtual void StartApplication();
  virtual void StopApplication();
  virtual void OnInterest(std::shared_ptr<const ndn::Interest> interest);

private:
  std::string m_prefix;      // Namespace prefix this producer serves
  uint32_t m_payloadSize;    // Size of the data payload in bytes
  uint64_t m_value;          // Value to include in the data content (e.g., sensor reading)
  std::string m_payloadFormat; // legacy or fl
  std::string m_flPayloadDir;  // directory with <producer>-<seq>.wfl files
  uint32_t m_sampleCount;      // fallback FL sample count when no file is available
};

} // namespace ns3

#endif // CFN_PRODUCER_APP_HPP
