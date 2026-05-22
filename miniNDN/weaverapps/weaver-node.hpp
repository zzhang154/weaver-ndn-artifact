#ifndef WEAVER_MININDN_WEAVER_NODE_HPP
#define WEAVER_MININDN_WEAVER_NODE_HPP

#include "aggregation-buffer.hpp"
#include "congestion-control.hpp"
#include "trace-collector.hpp"

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/scheduler.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace weaver {

struct NodeOptions
{
  std::string role;
  std::string prefix;
  std::vector<std::string> children;
  std::string cc = "AIMD";
  std::string traceFile;
  std::string payloadDir;
  std::string outputDir;
  uint64_t value = 1;
  uint64_t iterations = 100;
  int timeoutMs = 1000;
  int interestLifetimeMs = 1000;
  int freshnessMs = 10000;
  int startDelayMs = 1000;
};

std::string normalizePrefix(const std::string& prefix);
std::vector<std::string> splitChildren(const std::string& children);
uint64_t parseSequence(const ndn::Name& name);
uint64_t parseContentValue(const ndn::Data& data);

class ProducerNode
{
public:
  ProducerNode(const NodeOptions& options, ndn::Face& face, ndn::KeyChain& keyChain,
               TraceCollector& trace);
  void run();

private:
  void onInterest(const ndn::Interest& interest);

private:
  NodeOptions m_options;
  ndn::Face& m_face;
  ndn::KeyChain& m_keyChain;
  TraceCollector& m_trace;
};

class AggregatorNode
{
public:
  AggregatorNode(const NodeOptions& options, ndn::Face& face, ndn::KeyChain& keyChain,
                 TraceCollector& trace);
  void run();

private:
  void onInterest(const ndn::Interest& interest);
  void requestChild(uint64_t seq, const std::string& child);
  void onChildData(uint64_t seq, const std::string& child, const ndn::Data& data);
  void onChildFailure(uint64_t seq, const std::string& child, const std::string& reason);
  void onStragglerTimeout(uint64_t seq);
  void finishRound(uint64_t seq, bool partial, const std::string& reason);

private:
  NodeOptions m_options;
  ndn::Face& m_face;
  ndn::KeyChain& m_keyChain;
  TraceCollector& m_trace;
  ndn::Scheduler m_scheduler;
  std::map<uint64_t, AggregationBuffer> m_buffers;
};

class RootNode
{
public:
  RootNode(const NodeOptions& options, ndn::Face& face, TraceCollector& trace);
  void run();

private:
  void trySendNext();
  void sendRound(uint64_t seq);
  void requestChild(uint64_t seq, const std::string& child);
  void onChildData(uint64_t seq, const std::string& child, const ndn::Data& data);
  void onChildFailure(uint64_t seq, const std::string& child, const std::string& reason);
  void onStragglerTimeout(uint64_t seq);
  void finishRound(uint64_t seq, bool partial, const std::string& reason);
  void shutdownIfDone();

private:
  NodeOptions m_options;
  ndn::Face& m_face;
  TraceCollector& m_trace;
  ndn::Scheduler m_scheduler;
  std::unique_ptr<CongestionControl> m_cc;
  std::map<uint64_t, AggregationBuffer> m_buffers;
  uint64_t m_nextSeq = 1;
  uint64_t m_completedRounds = 0;
  uint64_t m_inFlightRounds = 0;
};

} // namespace weaver

#endif // WEAVER_MININDN_WEAVER_NODE_HPP
