#include "cfn-root-app.hpp"
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
#include <cmath>
#include <cstring>
#include "straggler-manager.hpp"
#include "trace-collector.hpp"
#include "fl-payload.hpp"
#include "ns3/core-module.h"  // Include for StringValue, DoubleValue, etc.
#include <algorithm>
#include <limits>

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

NS_LOG_COMPONENT_DEFINE("CFNRootApp");

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

NS_OBJECT_ENSURE_REGISTERED(CFNRootApp);

TypeId 
CFNRootApp::GetTypeId() {
  static TypeId tid = TypeId("ns3::CFNRootApp")
    .SetParent<ndn::App>()
    .AddConstructor<CFNRootApp>()
    .AddAttribute("Prefix", "Identity prefix of the root (optional, for logging)",
                  StringValue("/"), MakeStringAccessor(&CFNRootApp::m_prefix),
                  MakeStringChecker())
    .AddAttribute("ChildTimeout", "Timeout waiting for all children data (seconds)",
                  DoubleValue(1.0), MakeDoubleAccessor(&CFNRootApp::m_childTimeout),
                  MakeDoubleChecker<double>())
    .AddAttribute("CongestionControl", "Congestion control algorithm (AIMD, CUBIC, BBR)",
                  StringValue("AIMD"), MakeStringAccessor(&CFNRootApp::m_ccName),
                  MakeStringChecker())
    .AddAttribute("MaxSeq", "Maximum number of aggregation rounds; 0 means unlimited",
                  UintegerValue(0), MakeUintegerAccessor(&CFNRootApp::m_maxSeq),
                  MakeUintegerChecker<uint32_t>())
    .AddAttribute("FlOutputDir", "Directory to write root aggregate payloads as aggregate-<seq>.wfl",
                  StringValue(""), MakeStringAccessor(&CFNRootApp::m_flOutputDir),
                  MakeStringChecker());
  return tid;
}

CFNRootApp::CFNRootApp()
  : m_childTimeout(1.0)
  , m_nextSeq(0)
  , m_maxSeq(0) {
}

void 
CFNRootApp::SetChildren(const std::vector<std::string>& children) {
  m_children = children;
}

void 
CFNRootApp::SetCongestionControl(const std::string& algName) {
  m_ccName = algName;
}

void 
CFNRootApp::StartApplication() {
  ndn::App::StartApplication();
  // (Root's prefix registration is not strictly needed unless external consumer interests target the root)
  if (!m_prefix.empty()) {
    ndn::FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
  }

  // Instantiate the chosen congestion control algorithm
  if (m_ccName == "CUBIC" || m_ccName == "cubic") {
    m_congestionCtrl = std::make_unique<CongestionCubic>();
  } else if (m_ccName == "BBR" || m_ccName == "bbr") {
    m_congestionCtrl = std::make_unique<CongestionBBR>();
  } else {
    // Default to AIMD if unspecified or unknown
    m_congestionCtrl = std::make_unique<CongestionAIMD>();
  }
  NS_LOG_INFO("CFNRootApp started on node " << Names::FindName(GetNode()) 
              << " [CongestionControl=" << m_ccName << "]");

  m_nextSeq = 0;
  // Send initial interests up to the initial congestion window size
  int initialWindow = m_congestionCtrl->GetCwnd();
  for (int i = 0; i < initialWindow; ++i) {
    if (m_maxSeq > 0 && static_cast<uint32_t>(m_nextSeq) >= m_maxSeq) {
      break;
    }
    m_nextSeq++;
    // Stagger the initial sends by a tiny delta to avoid simultaneous send at t=0
    Simulator::Schedule(Seconds(1.0 + 0.001 * i), &CFNRootApp::SendInterest, this, m_nextSeq);
  }
}

void 
CFNRootApp::StopApplication() {
  // Cancel any outstanding events (e.g., straggler timeouts)
  for (auto& entry : m_buffers) {
    if (entry.second.timeoutEvent.IsRunning()) {
      StragglerManager::Cancel(entry.second.timeoutEvent);
    }
  }
  m_buffers.clear();
  ndn::App::StopApplication();
}

void 
CFNRootApp::SendInterest(int seq) {
  if (m_maxSeq > 0 && static_cast<uint32_t>(seq) > m_maxSeq) {
    return;
  }
  if (m_children.empty()) {
    NS_LOG_ERROR("CFNRootApp: no children to send interests to");
    return;
  }
  // Create a new aggregation buffer for this sequence
  AggregationBuffer buf(m_children.size());
  buf.parentInterestName = ndn::Name(m_prefix.empty() ? "/" : m_prefix).append(std::to_string(seq));
  buf.partialSum = 0;
  buf.receivedCount = 0;
  m_buffers[seq] = buf;

  // Send an Interest to each direct child
  Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
  for (const std::string& childName : m_children) {
      // Construct the Interest name as "/<childName>/<seq>" 
      std::string interestName = "/" + childName + "/" + std::to_string(seq);
      auto interest = std::make_shared<ndn::Interest>(interestName);
      interest->setNonce(rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
      interest->setInterestLifetime(ndn::time::milliseconds(static_cast<int>(m_childTimeout * 1000)));

      NS_LOG_INFO("Root sending Interest " << interest->getName() 
                  << " to child [" << childName << "]");
      m_transmittedInterests(interest, this, m_face);
      
      // CRITICAL FIX: Use the correct method to send interests
      // m_face->sendInterest(*interest);  // Use this method instead of m_appLink->onReceiveInterest
      m_appLink->onReceiveInterest(*interest);
  }

  // Schedule a timeout for this sequence aggregation
  m_buffers[seq].timeoutEvent = StragglerManager::ScheduleRoot(this, seq, m_childTimeout);

  // Log the interest dispatch event (time, node, seq)
  TraceCollector::LogInterest(Names::FindName(GetNode()), seq, Simulator::Now().GetSeconds());
}

void 
CFNRootApp::OnData(std::shared_ptr<const ndn::Data> data) {
  // Called when a Data packet from a child is received
  NS_LOG_INFO("Root received Data: " << data->getName());
  // Determine sequence number from Data name
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
    NS_LOG_WARN("Root could not parse sequence from Data name");
    return;
  }
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    // Could be a late packet after finalization
    NS_LOG_WARN("Root received Data for unknown/finished seq " << seq);
    return;
  }
  AggregationBuffer& buf = it->second;

  // Mark which child responded
  if (dataName.size() > 0) {
    std::string childPrefix = dataName.get(0).toUri();
    for (size_t j = 0; j < m_children.size(); ++j) {
      if (m_children[j] == childPrefix) {
        buf.childrenReceived[j] = true;
        break;
      }
    }
  }
  buf.receivedCount++;

  // Accumulate either a WFL1 model/gradient vector or the legacy numeric value.
  AccumulatePayload(buf, *data);

  // If all children have responded for this sequence, finalize the aggregation
  if (buf.receivedCount >= buf.expectedCount) {
    if (buf.timeoutEvent.IsRunning()) {
      StragglerManager::Cancel(buf.timeoutEvent);
    }
    // Treat this as a successfully completed round for congestion control
    if (buf.receivedCount > 0) {
      m_congestionCtrl->OnData();
    } else {
      m_congestionCtrl->OnTimeout();
    }
    // Log the final aggregated result
    WriteOutputPayload(seq, buf);
    TraceCollector::LogAggregate(Names::FindName(GetNode()), seq, GetTraceScalar(buf),
                                 buf.expectedCount, buf.receivedCount);
    // Remove buffer
    m_buffers.erase(it);
    // Try to send new Interests if window allows
    TrySendNext();
  }
  // Else: waiting for more children of this sequence (partial data arrived, but not done)
}

void 
CFNRootApp::OnStragglerTimeout(int seq) {
  // Timeout: not all children responded in time for this sequence
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return;
  }
  AggregationBuffer& buf = it->second;
  NS_LOG_INFO("Root straggler timeout for seq " << seq 
              << " (received " << buf.receivedCount << "/" << buf.expectedCount << ")");
  // Congestion control reaction: consider this round done (partial or failed)
  if (buf.receivedCount > 0) {
    m_congestionCtrl->OnData();
  } else {
    m_congestionCtrl->OnTimeout();
  }
  // Log the partial aggregate outcome
  WriteOutputPayload(seq, buf);
  TraceCollector::LogAggregate(Names::FindName(GetNode()), seq, GetTraceScalar(buf),
                               buf.expectedCount, buf.receivedCount);
  // Remove the buffer
  m_buffers.erase(it);
  // Send next interest(s) if window allows
  TrySendNext();
}

void 
CFNRootApp::TrySendNext() {
  if (m_maxSeq > 0 && static_cast<uint32_t>(m_nextSeq) >= m_maxSeq) {
    return;
  }
  // Check current number of in-flight aggregation rounds vs congestion window
  int inFlight = m_buffers.size();
  int cwnd = m_congestionCtrl->GetCwnd();
  if (inFlight < cwnd) {
    // Launch new aggregation rounds until the window is full
    while (inFlight < cwnd) {
      if (m_maxSeq > 0 && static_cast<uint32_t>(m_nextSeq) >= m_maxSeq) {
        break;
      }
      m_nextSeq++;
      SendInterest(m_nextSeq);
      inFlight++;
    }
  }
}

void
CFNRootApp::WriteOutputPayload(int seq, const AggregationBuffer& buf) {
  if (m_flOutputDir.empty()) {
    return;
  }

  std::vector<uint8_t> content = BuildAggregatedContent(buf);
  const std::string path = cfnagg::BuildNodeRoundPayloadPath(m_flOutputDir, "aggregate", seq);
  if (!cfnagg::WriteFlPayloadFile(path, content)) {
    NS_LOG_WARN("Root failed to write FL aggregate payload to " << path);
  } else {
    NS_LOG_INFO("Root wrote FL aggregate payload to " << path
                << " [bytes=" << content.size()
                << ", flSamples=" << buf.flSampleCount
                << ", flDim=" << buf.flWeightedSum.size() << "]");
  }
}

} // namespace ns3
