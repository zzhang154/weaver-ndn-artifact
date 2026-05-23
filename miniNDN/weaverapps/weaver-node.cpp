#include "weaver-node.hpp"

#include <ndn-cxx/encoding/block-helpers.hpp>
#include <ndn-cxx/lp/nack.hpp>
#include <ndn-cxx/util/span.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "fl-payload.hpp"
#include "quic-packet.hpp"

namespace weaver {

namespace {

std::string
nodeLabel(const std::string& prefix)
{
  if (!prefix.empty() && prefix.front() == '/') {
    return prefix.substr(1);
  }
  return prefix;
}

std::string
componentToPlainString(const ndn::name::Component& component)
{
  std::string text = component.toUri(ndn::name::UriFormat::CANONICAL);
  const auto eq = text.find('=');
  if (eq != std::string::npos) {
    text = text.substr(eq + 1);
  }
  return text;
}

std::string
joinChildren(const std::vector<std::string>& children)
{
  std::ostringstream os;
  for (size_t i = 0; i < children.size(); ++i) {
    if (i > 0) {
      os << ';';
    }
    os << children[i];
  }
  return os.str();
}

uint64_t
parseContentBytes(const uint8_t* data, size_t size)
{
  std::string text(reinterpret_cast<const char*>(data), size);
  if (text.empty()) {
    return 0;
  }
  return std::stoull(text);
}

void
accumulateFlPayload(AggregationBuffer& buffer, const FlPayload& payload)
{
  if (!buffer.hasFlPayload) {
    buffer.hasFlPayload = true;
    buffer.flWeightedSum.assign(payload.values.size(), 0.0);
  }
  if (buffer.flWeightedSum.size() == payload.values.size()) {
    const uint32_t sampleCount = std::max<uint32_t>(1, payload.sampleCount);
    buffer.flSampleCount += sampleCount;
    for (size_t i = 0; i < payload.values.size(); ++i) {
      buffer.flWeightedSum[i] += payload.values[i] * static_cast<double>(sampleCount);
    }
  }
}

void
accumulatePayload(AggregationBuffer& buffer, const ndn::Data& data)
{
  const auto& content = data.getContent();
  const uint8_t* contentValue = content.value();
  const size_t contentSize = content.value_size();

  FlPayload payload;
  std::vector<uint8_t> quicPayload;
  if (decodeFlPayload(contentValue, contentSize, payload)) {
    accumulateFlPayload(buffer, payload);
    return;
  }

  if (decodeQuicAggregationPacket(contentValue, contentSize, quicPayload)) {
    if (decodeFlPayload(quicPayload.data(), quicPayload.size(), payload)) {
      accumulateFlPayload(buffer, payload);
      return;
    }

    buffer.partialSum += parseContentBytes(quicPayload.data(), quicPayload.size());
    return;
  }

  buffer.partialSum += parseContentValue(data);
}

std::vector<uint8_t>
wrapOutgoingContent(const NodeOptions& options, uint64_t seq, const std::vector<uint8_t>& content)
{
  if (options.payloadFormat == "quic") {
    if (seq > std::numeric_limits<uint16_t>::max()) {
      throw std::runtime_error("QUIC aggregation packet iteration exceeds uint16_t");
    }
    return encodeQuicAggregationPacket(static_cast<uint16_t>(seq), content);
  }
  return content;
}

std::vector<uint8_t>
buildAggregatedContent(const AggregationBuffer& buffer)
{
  if (buffer.hasFlPayload) {
    FlPayload payload;
    payload.sampleCount = static_cast<uint32_t>(std::min<uint64_t>(
      buffer.flSampleCount, std::numeric_limits<uint32_t>::max()));
    payload.values.assign(buffer.flWeightedSum.size(), 0.0);
    const double divisor = static_cast<double>(std::max<uint64_t>(1, buffer.flSampleCount));
    for (size_t i = 0; i < buffer.flWeightedSum.size(); ++i) {
      payload.values[i] = buffer.flWeightedSum[i] / divisor;
    }
    return encodeFlPayload(payload);
  }

  std::string value = std::to_string(buffer.partialSum);
  return std::vector<uint8_t>(value.begin(), value.end());
}

} // namespace

std::string
normalizePrefix(const std::string& prefix)
{
  if (prefix.empty()) {
    return "/";
  }
  if (prefix.front() == '/') {
    return prefix;
  }
  return "/" + prefix;
}

std::vector<std::string>
splitChildren(const std::string& children)
{
  std::vector<std::string> result;
  std::stringstream ss(children);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char ch) {
      return !std::isspace(ch);
    }));
    item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char ch) {
      return !std::isspace(ch);
    }).base(), item.end());
    if (!item.empty()) {
      result.push_back(normalizePrefix(item));
    }
  }
  return result;
}

uint64_t
parseSequence(const ndn::Name& name)
{
  if (name.empty()) {
    throw std::runtime_error("cannot parse sequence from empty name");
  }
  return std::stoull(componentToPlainString(name[-1]));
}

uint64_t
parseContentValue(const ndn::Data& data)
{
  const auto& block = data.getContent();
  return parseContentBytes(block.value(), block.value_size());
}

ProducerNode::ProducerNode(const NodeOptions& options, ndn::Face& face, ndn::KeyChain& keyChain,
                           TraceCollector& trace)
  : m_options(options)
  , m_face(face)
  , m_keyChain(keyChain)
  , m_trace(trace)
{
  m_options.prefix = normalizePrefix(m_options.prefix);
}

void
ProducerNode::run()
{
  const ndn::Name prefix(m_options.prefix);
  m_face.setInterestFilter(prefix,
                           [this] (const auto&, const ndn::Interest& interest) {
                             onInterest(interest);
                           },
                           [] (const ndn::Name& failedPrefix, const std::string& reason) {
                             std::cerr << "ERROR: failed to register " << failedPrefix
                                       << ": " << reason << "\n";
                           });

  m_trace.log("ProducerStart", nodeLabel(m_options.prefix), 0,
              "prefix=" + m_options.prefix + " value=" + std::to_string(m_options.value));
  m_face.processEvents();
}

void
ProducerNode::onInterest(const ndn::Interest& interest)
{
  uint64_t seq = 0;
  try {
    seq = parseSequence(interest.getName());
  }
  catch (const std::exception& e) {
    m_trace.log("ProducerBadInterest", nodeLabel(m_options.prefix), 0,
                interest.getName().toUri() + " error=" + e.what());
    return;
  }

  auto data = std::make_shared<ndn::Data>(interest.getName());
  data->setFreshnessPeriod(ndn::time::milliseconds(m_options.freshnessMs));

  std::vector<uint8_t> content;
  if (!m_options.payloadDir.empty()) {
    const std::string path = buildNodeRoundPayloadPath(m_options.payloadDir, m_options.prefix, seq);
    if (!readFlPayloadFile(path, content)) {
      FlPayload payload;
      payload.sampleCount = 1;
      payload.values.push_back(static_cast<double>(m_options.value));
      content = encodeFlPayload(payload);
      m_trace.log("ProducerPayloadFallback", nodeLabel(m_options.prefix), seq,
                  "missing=" + path);
    }
  }
  else {
    FlPayload payload;
    payload.sampleCount = 1;
    payload.values.push_back(static_cast<double>(m_options.value));
    content = encodeFlPayload(payload);
  }

  content = wrapOutgoingContent(m_options, seq, content);
  data->setContent(ndn::make_span(content.data(), content.size()));
  m_keyChain.sign(*data);
  m_face.put(*data);

  m_trace.log("ProducerData", nodeLabel(m_options.prefix), seq,
              "name=" + data->getName().toUri() + " value=" + std::to_string(m_options.value));
}

AggregatorNode::AggregatorNode(const NodeOptions& options, ndn::Face& face, ndn::KeyChain& keyChain,
                               TraceCollector& trace)
  : m_options(options)
  , m_face(face)
  , m_keyChain(keyChain)
  , m_trace(trace)
  , m_scheduler(m_face.getIoContext())
{
  m_options.prefix = normalizePrefix(m_options.prefix);
}

void
AggregatorNode::run()
{
  if (m_options.children.empty()) {
    throw std::runtime_error("aggregator requires --children");
  }

  const ndn::Name prefix(m_options.prefix);
  m_face.setInterestFilter(prefix,
                           [this] (const auto&, const ndn::Interest& interest) {
                             onInterest(interest);
                           },
                           [] (const ndn::Name& failedPrefix, const std::string& reason) {
                             std::cerr << "ERROR: failed to register " << failedPrefix
                                       << ": " << reason << "\n";
                           });

  m_trace.log("AggregatorStart", nodeLabel(m_options.prefix), 0,
              "prefix=" + m_options.prefix + " children=" + joinChildren(m_options.children));
  m_face.processEvents();
}

void
AggregatorNode::onInterest(const ndn::Interest& interest)
{
  uint64_t seq = 0;
  try {
    seq = parseSequence(interest.getName());
  }
  catch (const std::exception& e) {
    m_trace.log("AggregatorBadInterest", nodeLabel(m_options.prefix), 0,
                interest.getName().toUri() + " error=" + e.what());
    return;
  }

  if (m_buffers.count(seq) > 0) {
    m_trace.log("AggregatorDuplicateInterest", nodeLabel(m_options.prefix), seq,
                "name=" + interest.getName().toUri());
    return;
  }

  AggregationBuffer buffer;
  buffer.seq = seq;
  buffer.responseName = interest.getName();
  buffer.expectedChildren = m_options.children.size();
  buffer.timeoutEvent = m_scheduler.schedule(ndn::time::milliseconds(m_options.timeoutMs),
                                             [this, seq] { onStragglerTimeout(seq); });
  m_buffers.emplace(seq, std::move(buffer));

  m_trace.log("AggregatorInterest", nodeLabel(m_options.prefix), seq,
              "fromParent=" + interest.getName().toUri());

  for (const auto& child : m_options.children) {
    requestChild(seq, child);
  }
}

void
AggregatorNode::requestChild(uint64_t seq, const std::string& child)
{
  ndn::Interest interest(ndn::Name(child).append(std::to_string(seq)));
  interest.setCanBePrefix(false);
  interest.setMustBeFresh(true);
  interest.setInterestLifetime(ndn::time::milliseconds(m_options.interestLifetimeMs));

  m_face.expressInterest(interest,
                         [this, seq, child] (const ndn::Interest&, const ndn::Data& data) {
                           onChildData(seq, child, data);
                         },
                         [this, seq, child] (const ndn::Interest&, const ndn::lp::Nack& nack) {
                           onChildFailure(seq, child, "nack:" + std::to_string(static_cast<int>(nack.getReason())));
                         },
                         [this, seq, child] (const ndn::Interest&) {
                           onChildFailure(seq, child, "interest-timeout");
                         });

  m_trace.log("AggregatorSendInterest", nodeLabel(m_options.prefix), seq,
              "child=" + child + " name=" + interest.getName().toUri());
}

void
AggregatorNode::onChildData(uint64_t seq, const std::string& child, const ndn::Data& data)
{
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return;
  }

  auto& buffer = it->second;
  if (buffer.receivedChildren.count(child) > 0 || buffer.failedChildren.count(child) > 0) {
    return;
  }

  buffer.receivedChildren.insert(child);
  try {
    accumulatePayload(buffer, data);
  }
  catch (const std::exception& e) {
    onChildFailure(seq, child, std::string("bad-data:") + e.what());
    return;
  }
  m_trace.log("AggregatorRecvData", nodeLabel(m_options.prefix), seq,
              "child=" + child + " bytes=" + std::to_string(data.getContent().value_size()));

  if (buffer.isComplete()) {
    finishRound(seq, false, "all-children");
  }
}

void
AggregatorNode::onChildFailure(uint64_t seq, const std::string& child, const std::string& reason)
{
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return;
  }

  auto& buffer = it->second;
  if (buffer.receivedChildren.count(child) > 0 || buffer.failedChildren.count(child) > 0) {
    return;
  }

  buffer.failedChildren.insert(child);
  m_trace.log("AggregatorChildFailure", nodeLabel(m_options.prefix), seq,
              "child=" + child + " reason=" + reason);

  if (buffer.isSettled()) {
    finishRound(seq, !buffer.isComplete(), reason);
  }
}

void
AggregatorNode::onStragglerTimeout(uint64_t seq)
{
  if (m_buffers.count(seq) == 0) {
    return;
  }
  finishRound(seq, true, "straggler-timeout");
}

void
AggregatorNode::finishRound(uint64_t seq, bool partial, const std::string& reason)
{
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return;
  }

  AggregationBuffer buffer = std::move(it->second);
  m_buffers.erase(it);
  buffer.timeoutEvent.cancel();

  auto data = std::make_shared<ndn::Data>(buffer.responseName);
  data->setFreshnessPeriod(ndn::time::milliseconds(m_options.freshnessMs));
  std::vector<uint8_t> content = wrapOutgoingContent(m_options, seq, buildAggregatedContent(buffer));
  data->setContent(ndn::make_span(content.data(), content.size()));
  m_keyChain.sign(*data);
  m_face.put(*data);

  m_trace.log(partial ? "AggregatorPartial" : "AggregatorComplete",
              nodeLabel(m_options.prefix), seq,
              "sum=" + std::to_string(buffer.partialSum) +
                " flSamples=" + std::to_string(buffer.flSampleCount) +
                " flDim=" + std::to_string(buffer.flWeightedSum.size()) +
                " received=" + std::to_string(buffer.receivedChildren.size()) +
                "/" + std::to_string(buffer.expectedChildren) +
                " reason=" + reason);
}

RootNode::RootNode(const NodeOptions& options, ndn::Face& face, TraceCollector& trace)
  : m_options(options)
  , m_face(face)
  , m_trace(trace)
  , m_scheduler(m_face.getIoContext())
  , m_cc(makeCongestionControl(options.cc))
{
  m_options.prefix = normalizePrefix(m_options.prefix);
}

void
RootNode::run()
{
  if (m_options.children.empty()) {
    throw std::runtime_error("root requires --children");
  }

  m_trace.log("RootStart", nodeLabel(m_options.prefix), 0,
              "children=" + joinChildren(m_options.children) +
                " cc=" + m_cc->name() +
                " iterations=" + std::to_string(m_options.iterations));

  m_scheduler.schedule(ndn::time::milliseconds(m_options.startDelayMs),
                       [this] { trySendNext(); });
  m_face.processEvents();
}

void
RootNode::trySendNext()
{
  while (m_nextSeq <= m_options.iterations &&
         m_inFlightRounds < static_cast<uint64_t>(m_cc->getCwnd())) {
    sendRound(m_nextSeq++);
  }

  shutdownIfDone();
}

void
RootNode::sendRound(uint64_t seq)
{
  AggregationBuffer buffer;
  buffer.seq = seq;
  buffer.expectedChildren = m_options.children.size();
  buffer.timeoutEvent = m_scheduler.schedule(ndn::time::milliseconds(m_options.timeoutMs),
                                             [this, seq] { onStragglerTimeout(seq); });
  m_buffers.emplace(seq, std::move(buffer));
  ++m_inFlightRounds;

  m_trace.log("RootRoundStart", nodeLabel(m_options.prefix), seq,
              "cwnd=" + std::to_string(m_cc->getCwnd()));

  for (const auto& child : m_options.children) {
    requestChild(seq, child);
  }
}

void
RootNode::requestChild(uint64_t seq, const std::string& child)
{
  ndn::Interest interest(ndn::Name(child).append(std::to_string(seq)));
  interest.setCanBePrefix(false);
  interest.setMustBeFresh(true);
  interest.setInterestLifetime(ndn::time::milliseconds(m_options.interestLifetimeMs));

  m_face.expressInterest(interest,
                         [this, seq, child] (const ndn::Interest&, const ndn::Data& data) {
                           onChildData(seq, child, data);
                         },
                         [this, seq, child] (const ndn::Interest&, const ndn::lp::Nack& nack) {
                           onChildFailure(seq, child, "nack:" + std::to_string(static_cast<int>(nack.getReason())));
                         },
                         [this, seq, child] (const ndn::Interest&) {
                           onChildFailure(seq, child, "interest-timeout");
                         });

  m_trace.log("RootSendInterest", nodeLabel(m_options.prefix), seq,
              "child=" + child + " name=" + interest.getName().toUri());
}

void
RootNode::onChildData(uint64_t seq, const std::string& child, const ndn::Data& data)
{
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return;
  }

  auto& buffer = it->second;
  if (buffer.receivedChildren.count(child) > 0 || buffer.failedChildren.count(child) > 0) {
    return;
  }

  buffer.receivedChildren.insert(child);
  try {
    accumulatePayload(buffer, data);
  }
  catch (const std::exception& e) {
    onChildFailure(seq, child, std::string("bad-data:") + e.what());
    return;
  }
  m_trace.log("RootRecvData", nodeLabel(m_options.prefix), seq,
              "child=" + child + " bytes=" + std::to_string(data.getContent().value_size()));

  if (buffer.isComplete()) {
    finishRound(seq, false, "all-children");
  }
}

void
RootNode::onChildFailure(uint64_t seq, const std::string& child, const std::string& reason)
{
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return;
  }

  auto& buffer = it->second;
  if (buffer.receivedChildren.count(child) > 0 || buffer.failedChildren.count(child) > 0) {
    return;
  }

  buffer.failedChildren.insert(child);
  m_trace.log("RootChildFailure", nodeLabel(m_options.prefix), seq,
              "child=" + child + " reason=" + reason);

  if (buffer.isSettled()) {
    finishRound(seq, !buffer.isComplete(), reason);
  }
}

void
RootNode::onStragglerTimeout(uint64_t seq)
{
  if (m_buffers.count(seq) == 0) {
    return;
  }
  finishRound(seq, true, "straggler-timeout");
}

void
RootNode::finishRound(uint64_t seq, bool partial, const std::string& reason)
{
  auto it = m_buffers.find(seq);
  if (it == m_buffers.end()) {
    return;
  }

  AggregationBuffer buffer = std::move(it->second);
  m_buffers.erase(it);
  buffer.timeoutEvent.cancel();

  if (m_inFlightRounds > 0) {
    --m_inFlightRounds;
  }
  ++m_completedRounds;

  if (partial) {
    m_cc->onTimeout();
  }
  else {
    m_cc->onData();
  }

  m_trace.log(partial ? "RootPartial" : "RootComplete",
              nodeLabel(m_options.prefix), seq,
              "sum=" + std::to_string(buffer.partialSum) +
                " flSamples=" + std::to_string(buffer.flSampleCount) +
                " flDim=" + std::to_string(buffer.flWeightedSum.size()) +
                " received=" + std::to_string(buffer.receivedChildren.size()) +
                "/" + std::to_string(buffer.expectedChildren) +
                " cwnd=" + std::to_string(m_cc->getCwnd()) +
                " reason=" + reason);

  if (!m_options.outputDir.empty()) {
    std::vector<uint8_t> content = buildAggregatedContent(buffer);
    const std::string path = buildNodeRoundPayloadPath(m_options.outputDir, "aggregate", seq);
    if (!writeFlPayloadFile(path, content)) {
      m_trace.log("RootOutputError", nodeLabel(m_options.prefix), seq,
                  "path=" + path);
    }
  }

  trySendNext();
}

void
RootNode::shutdownIfDone()
{
  if (m_completedRounds < m_options.iterations || !m_buffers.empty()) {
    return;
  }

  m_trace.log("RootDone", nodeLabel(m_options.prefix), m_completedRounds,
              "iterations=" + std::to_string(m_options.iterations));
  m_scheduler.schedule(ndn::time::milliseconds(100), [this] {
    m_face.shutdown();
  });
}

} // namespace weaver
