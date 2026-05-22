#include "cfn-producer-app.hpp"
#include "helper/ndn-fib-helper.hpp"
#include "ndn-cxx/name.hpp"
#include "ndn-cxx/data.hpp"
#include "ndn-cxx/interest.hpp"
#include "helper/ndn-stack-helper.hpp"
#include "fl-payload.hpp"
#include <algorithm>
#include <cstring>   // for std::memcpy
#include <arpa/inet.h> // for byte-order functions if needed

// Helper for 64-bit host/network byte order conversion (if not provided by system)
#ifndef htobe64
inline uint64_t htobe64(uint64_t host64) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(host64);
#else
  return host64;
#endif
}
#endif

NS_LOG_COMPONENT_DEFINE("CFNProducerApp");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(CFNProducerApp);

TypeId 
CFNProducerApp::GetTypeId() {
  static TypeId tid = TypeId("ns3::CFNProducerApp")
    .SetParent<ndn::App>()
    .AddConstructor<CFNProducerApp>()
    .AddAttribute("Prefix", "NDN prefix that this Producer will serve",
                  StringValue("/"), MakeStringAccessor(&CFNProducerApp::m_prefix),
                  MakeStringChecker())
    .AddAttribute("PayloadSize", "Size of Data payload in bytes",
                  UintegerValue(8), MakeUintegerAccessor(&CFNProducerApp::m_payloadSize),
                  MakeUintegerChecker<uint32_t>())
    .AddAttribute("Value", "64-bit integer value to include in Data content",
                  UintegerValue(1), MakeUintegerAccessor(&CFNProducerApp::m_value),
                  MakeUintegerChecker<uint64_t>())
    .AddAttribute("PayloadFormat", "Payload format: legacy or fl",
                  StringValue("legacy"), MakeStringAccessor(&CFNProducerApp::m_payloadFormat),
                  MakeStringChecker())
    .AddAttribute("FlPayloadDir", "Directory containing <producer>-<seq>.wfl FL payload files",
                  StringValue(""), MakeStringAccessor(&CFNProducerApp::m_flPayloadDir),
                  MakeStringChecker())
    .AddAttribute("SampleCount", "Fallback FL sample count represented by this producer",
                  UintegerValue(1), MakeUintegerAccessor(&CFNProducerApp::m_sampleCount),
                  MakeUintegerChecker<uint32_t>());
  return tid;
}

CFNProducerApp::CFNProducerApp()
  : m_payloadSize(8)
  , m_value(1)
  , m_payloadFormat("legacy")
  , m_sampleCount(1) {
}

void 
CFNProducerApp::StartApplication() {
  ndn::App::StartApplication();
  if (m_prefix.empty()) {
    NS_LOG_WARN("CFNProducerApp has no prefix configured");
    return;
  }
  // Register prefix with local NFD (so Interests for this prefix are routed to this app)
  ndn::FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
  NS_LOG_INFO("CFNProducerApp started on node " << GetNode()->GetId() 
              << " [prefix=" << m_prefix << "]");
}

void 
CFNProducerApp::StopApplication() {
  // Unregisters the prefix and stops the app
  ndn::App::StopApplication();
}

void 
CFNProducerApp::OnInterest(std::shared_ptr<const ndn::Interest> interest) {
  // Log and trace the incoming Interest
  ndn::App::OnInterest(interest); // (for tracing)
  NS_LOG_INFO("Producer received Interest for " << interest->getName());

  // Create Data packet with the same name as the Interest
  auto data = std::make_shared<ndn::Data>(interest->getName());
  data->setFreshnessPeriod(ndn::time::seconds(1));  // e.g., 1 second freshness

  int seq = 0;
  if (interest->getName().size() > 0) {
    try {
      seq = std::stoi(interest->getName().get(-1).toUri());
    } catch (...) {
      seq = 0;
    }
  }

  std::vector<uint8_t> content;
  if (m_payloadFormat == "fl" || !m_flPayloadDir.empty()) {
    const std::string payloadPath =
      cfnagg::BuildNodeRoundPayloadPath(m_flPayloadDir, m_prefix, seq);
    if (!m_flPayloadDir.empty() && cfnagg::ReadFlPayloadFile(payloadPath, content)) {
      NS_LOG_INFO("Producer loaded FL payload from " << payloadPath
                  << " [size=" << content.size() << " bytes]");
    }
    else {
      cfnagg::FlPayload payload;
      payload.sampleCount = m_sampleCount;
      payload.values.push_back(static_cast<double>(m_value));
      content = cfnagg::EncodeFlPayload(payload);
      NS_LOG_WARN("Producer using fallback FL payload for " << m_prefix
                  << " seq=" << seq << " because file is unavailable: " << payloadPath);
    }
  }
  else {
    // Prepare content buffer of length m_payloadSize
    content.assign(m_payloadSize, 0);
    // Embed m_value (64-bit) into the first 8 bytes of content (network byte order)
    uint64_t netValue = htobe64(m_value);
    size_t copySize = std::min((size_t)m_payloadSize, sizeof(netValue));
    std::memcpy(content.data(), &netValue, copySize);
  }

  data->setContent(content.data(), content.size());

  // Sign the Data packet with the default ndnSIM key (required by NDN)
  ndn::StackHelper::getKeyChain().sign(*data);

  NS_LOG_INFO("Producer sending Data for " << data->getName() 
              << " [value=" << m_value << ", size=" << content.size()
              << " bytes, format=" << m_payloadFormat << "]");

  // Send Data packet out
  m_transmittedDatas(data, this, m_face);
  m_appLink->onReceiveData(*data);
}

} // namespace ns3
