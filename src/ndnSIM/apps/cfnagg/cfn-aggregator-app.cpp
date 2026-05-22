#include "cfn-aggregator-app.hpp"
#include "helper/ndn-fib-helper.hpp"
#include "ndn-cxx/name.hpp"
#include "ndn-cxx/data.hpp"
#include "ndn-cxx/interest.hpp"
#include "helper/ndn-stack-helper.hpp"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/names.h"
#include "ns3/log.h"
#include <arpa/inet.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
#include "straggler-manager.hpp"
#include "trace-collector.hpp"
#include "fl-payload.hpp"
#include "ns3/core-module.h"  // Include for StringValue, DoubleValue, etc.

// Byte order conversion helpers (same as in producer app)
#ifndef htobe64
inline uint64_t htobe64(uint64_t host64) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(host64);
#else
  return host64;
#endif
}
inline uint64_t be64toh(uint64_t net64) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(net64);
#else
  return net64;
#endif
}
#endif

NS_LOG_COMPONENT_DEFINE("CFNAggregatorApp");

namespace ns3 {

namespace {

void
AccumulatePayload(AggregationBuffer& buf, const ndn::Data& data)
{
  cfnagg::FlPayload flPayload;
  if (cfnagg::DecodeFlPayload(data.getContent().value(), data.getContent().value_size(), flPayload)) {
    if (!buf.hasFlPayload) {
      buf.hasFlPayload = true;
      buf.flWeightedSum.assign(flPayload.values.size(), 0.0);
    }
    if (buf.flWeightedSum.size() == flPayload.values.size()) {
      const uint32_t sampleCount = std::max<uint32_t>(1, flPayload.sampleCount);
      buf.flSampleCount += sampleCount;
      for (size_t i = 0; i < flPayload.values.size(); ++i) {
        buf.flWeightedSum[i] += flPayload.values[i] * static_cast<double>(sampleCount);
      }
    }
    return;
  }

  // Legacy fallback: first 8 bytes are a network-order uint64.
  if (data.getContent().value_size() >= 8) {
    uint64_t netVal;
    std::memcpy(&netVal, data.getContent().value(), 8);
    uint64_t val = be64toh(netVal);
    buf.partialSum += val;
  } else if (data.getContent().value_size() > 0) {
    uint64_t val = 0;
    const uint8_t* contentData = data.getContent().value();
    for (size_t i = 0; i < data.getContent().value_size(); ++i) {
      val = (val << 8) | contentData[i];
    }
    buf.partialSum += val;
  }
}

std::vector<uint8_t>
BuildAggregatedContent(const AggregationBuffer& buf)
{
  if (buf.hasFlPayload) {
    cfnagg::FlPayload payload;
    payload.sampleCount = static_cast<uint32_t>(std::min<uint64_t>(
      buf.flSampleCount, std::numeric_limits<uint32_t>::max()));
    payload.values.assign(buf.flWeightedSum.size(), 0.0);
    const double divisor = static_cast<double>(std::max<uint64_t>(1, buf.flSampleCount));
    for (size_t i = 0; i < buf.flWeightedSum.size(); ++i) {
      payload.values[i] = buf.flWeightedSum[i] / divisor;
    }
    return cfnagg::EncodeFlPayload(payload);
  }

  std::vector<uint8_t> content(sizeof(uint64_t));
  uint64_t netSum = htobe64(buf.partialSum);
  std::memcpy(content.data(), &netSum, sizeof(netSum));
  return content;
}

uint64_t
GetTraceScalar(const AggregationBuffer& buf)
{
  if (buf.hasFlPayload) {
    return buf.flSampleCount;
  }
  return buf.partialSum;
}

} // namespace

NS_OBJECT_ENSURE_REGISTERED(CFNAggregatorApp);

TypeId 
CFNAggregatorApp::GetTypeId() {
  static TypeId tid = TypeId("ns3::CFNAggregatorApp")
    .SetParent<ndn::App>()
    .AddConstructor<CFNAggregatorApp>()
    .AddAttribute("Prefix", "NDN prefix handled by this Aggregator",
                  StringValue("/"), MakeStringAccessor(&CFNAggregatorApp::m_prefix),
                  MakeStringChecker())
    .AddAttribute("ChildTimeout", "Maximum wait time for child Data (seconds)",
                  DoubleValue(1.0), MakeDoubleAccessor(&CFNAggregatorApp::m_childTimeout),
                  MakeDoubleChecker<double>());
  return tid;
}

CFNAggregatorApp::CFNAggregatorApp()
  : m_childTimeout(1.0) {
}

void 
CFNAggregatorApp::SetChildren(const std::vector<std::string>& children) {
  m_children = children;
}

void
CFNAggregatorApp::StartApplication() {
  ndn::App::StartApplication();
  if (m_prefix.empty()) {
    NS_LOG_WARN("CFNAggregatorApp has no prefix configured");
    return;
  }
  
  // Register exact prefix route
  ndn::FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
  std::cout << "Aggregator " << Names::FindName(GetNode())
            << ": added FIB route for exact prefix " << m_prefix 
            << " via local face " << m_face->getId() << std::endl;
            
  // Register wildcard route - CRITICAL PART
  ndn::Name wildcardName(m_prefix);
  wildcardName.append(ndn::name::Component::fromEscapedString("*"));
  ndn::FibHelper::AddRoute(GetNode(), wildcardName, m_face, 1);
  
  std::cout << "Aggregator " << Names::FindName(GetNode())
            << ": CRITICAL - Explicitly added interest filters for " 
            << m_prefix << " and " << wildcardName << std::endl;
  
  // Initialize random nonce generator
  m_rand = CreateObject<UniformRandomVariable>();
  NS_LOG_INFO("CFNAggregatorApp started on node " << Names::FindName(GetNode())
              << " [prefix=" << m_prefix << ", children=" << m_children.size() << "]");
}


void 
CFNAggregatorApp::StopApplication() {
  // Cancel any pending timeouts
  for (auto& entry : m_buffers) {
    if (entry.second.timeoutEvent.IsRunning()) {
      StragglerManager::Cancel(entry.second.timeoutEvent);
    }
  }
  m_buffers.clear();
  ndn::App::StopApplication();
}

void 
CFNAggregatorApp::OnInterest(std::shared_ptr<const ndn::Interest> interest) {
  std::cout << "====== INTEREST ARRIVED ======" << std::endl;
  // This method is invoked when an Interest for this aggregator's prefix arrives (from parent)
  ndn::App::OnInterest(interest); // trace
  NS_LOG_INFO("Aggregator [" << Names::FindName(GetNode()) << "] received Interest: " 
              << interest->getName());

  // Parse sequence number from the Interest name (assuming the last name component is a sequence number)
  ndn::Name interestName = interest->getName();
  int seq = -1;
  if (interestName.size() > 0) {
    std::string seqStr = interestName.get(-1).toUri();
    try {
      seq = std::stoi(seqStr);
    } catch (...) {
      seq = -1;
    }
  }
  if (seq < 0) {
    NS_LOG_ERROR("Aggregator could not parse sequence from Interest name " << interestName);
    return;
  }

  // Create a new AggregationBuffer for this sequence
  AggregationBuffer buf(m_children.size());
  buf.parentInterestName = interestName;
  buf.partialSum = 0;
  buf.receivedCount = 0;
  // (childrenReceived vector is initialized to false by AggregationBuffer constructor)
  m_buffers[seq] = buf;

  // Forward an Interest to each child
  for (size_t i = 0; i < m_children.size(); ++i) {
    const std::string& childName = m_children[i];
    std::string childInterestName = "/" + childName + "/" + std::to_string(seq);
    auto childInterest = std::make_shared<ndn::Interest>(childInterestName);
    childInterest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
    childInterest->setInterestLifetime(ndn::time::milliseconds(static_cast<int>(m_childTimeout * 1000)));

    NS_LOG_INFO("Aggregator forwarding Interest " << childInterest->getName() 
                << " to child [" << childName << "]");
    m_transmittedInterests(childInterest, this, m_face);
    m_appLink->onReceiveInterest(*childInterest);
  }

  // Schedule a straggler timeout to finalize this aggregation after ChildTimeout seconds
  m_buffers[seq].timeoutEvent = StragglerManager::ScheduleAggregator(this, seq, m_childTimeout);
}

void 
CFNAggregatorApp::OnData(std::shared_ptr<const ndn::Data> data) {
  // This is called for Data coming from one of the children
  NS_LOG_INFO("Aggregator [" << Names::FindName(GetNode()) << "] received Data: " 
              << data->getName());
  // Determine which sequence this data corresponds to
  ndn::Name dataName = data->getName();
  int seq = -1;
  if (dataName.size() > 0) {
    std::string seqStr = dataName.get(-1).toUri();
    try {
      seq = std::stoi(seqStr);
    } catch (...) {
      seq = -1;
    }
  }
  if (seq < 0) {
    NS_LOG_WARN("Aggregator couldn't parse sequence from Data name " << dataName);
    return;
  }

  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    // This might occur if data arrives after finalization (late straggler)
    NS_LOG_WARN("Aggregator received unexpected Data for seq " << seq << " (ignored)");
    return;
  }
  AggregationBuffer& buf = it->second;

  // Mark one child response received
  buf.receivedCount++;
  // Identify which child responded (based on prefix in Data name) and mark as received
  if (dataName.size() > 0) {
    std::string childPrefix = dataName.get(0).toUri();
    for (size_t j = 0; j < m_children.size(); ++j) {
      if (m_children[j] == childPrefix) {
        buf.childrenReceived[j] = true;
        break;
      }
    }
  }

  // Extract either a WFL1 model/gradient vector or the legacy numeric value.
  AccumulatePayload(buf, *data);

  // Check if all children have responded
  if (buf.receivedCount >= buf.expectedCount) {
    // All expected child data received: cancel the straggler timeout
    if (buf.timeoutEvent.IsRunning()) {
      StragglerManager::Cancel(buf.timeoutEvent);
    }
    // Aggregate complete: produce Data to satisfy parent's Interest
    ndn::Name parentName = buf.parentInterestName;
    auto outData = std::make_shared<ndn::Data>(parentName);
    outData->setFreshnessPeriod(ndn::time::seconds(1));
    std::vector<uint8_t> content = BuildAggregatedContent(buf);
    outData->setContent(content.data(), content.size());
    ndn::StackHelper::getKeyChain().sign(*outData);

    NS_LOG_INFO("Aggregator sending aggregated Data " << outData->getName() 
                << " [legacySum=" << buf.partialSum
                << ", flSamples=" << buf.flSampleCount
                << ", flDim=" << buf.flWeightedSum.size() << "]");
    m_transmittedDatas(outData, this, m_face);
    m_appLink->onReceiveData(*outData);

    // Log the aggregation completion
    TraceCollector::LogAggregate(Names::FindName(GetNode()), seq, GetTraceScalar(buf),
                                 buf.expectedCount, buf.receivedCount);

    // Clean up the buffer
    m_buffers.erase(it);
  }
  // else: still waiting for other children, do nothing until timeout or all arrive
}

void 
CFNAggregatorApp::OnStragglerTimeout(int seq) {
  // Timeout triggered: not all children responded in time for this sequence
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return; // already finalized
  }
  AggregationBuffer& buf = it->second;
  NS_LOG_INFO("Aggregator straggler timeout for seq " << seq 
              << " (received " << buf.receivedCount << "/" << buf.expectedCount << " children)");

  // Create a Data with whatever partial aggregate we have
  ndn::Name parentName = buf.parentInterestName;
  auto outData = std::make_shared<ndn::Data>(parentName);
  outData->setFreshnessPeriod(ndn::time::seconds(1));
  std::vector<uint8_t> content = BuildAggregatedContent(buf);
  outData->setContent(content.data(), content.size());
  ndn::StackHelper::getKeyChain().sign(*outData);

  NS_LOG_INFO("Aggregator sending *partial* aggregated Data " << outData->getName() 
              << " [legacySum=" << buf.partialSum
              << ", flSamples=" << buf.flSampleCount
              << ", flDim=" << buf.flWeightedSum.size()
              << ", responded " << buf.receivedCount << "/" << buf.expectedCount << "]");

  m_transmittedDatas(outData, this, m_face);
  m_appLink->onReceiveData(*outData);

  // Log the partial aggregate event
  TraceCollector::LogAggregate(Names::FindName(GetNode()), seq, GetTraceScalar(buf),
                               buf.expectedCount, buf.receivedCount);

  // Clean up
  m_buffers.erase(it);
}

} // namespace ns3
