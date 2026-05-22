#include "trace-collector.hpp"
#include "ns3/simulator.h"
#include <iostream>
#include <iomanip>

std::ofstream TraceCollector::m_logFile;

void TraceCollector::Open(const std::string& filename) {
  m_logFile.open(filename);
  if (m_logFile.is_open()) {
    // Write CSV header
    m_logFile << "Time(s),Event,Node,Seq,Child,Expected,Received,Result\n";
  } else {
    std::cerr << "TraceCollector: Failed to open log file " << filename << std::endl;
  }
}

void TraceCollector::Close() {
  if (m_logFile.is_open()) {
    m_logFile.close();
  }
}

void TraceCollector::LogInterest(const std::string& nodeName, int seq, double time) {
  if (!m_logFile.is_open()) return;
  m_logFile << std::fixed << std::setprecision(3)
            << time << ",InterestSent," << nodeName << "," << seq 
            << ",,,," << "\n";
}

void TraceCollector::LogData(const std::string& nodeName, int seq, const std::string& childName, double time) {
  if (!m_logFile.is_open()) return;
  m_logFile << std::fixed << std::setprecision(3)
            << time << ",DataReceived," << nodeName << "," << seq 
            << "," << childName << ",,,\n";
}

void TraceCollector::LogAggregate(const std::string& nodeName, int seq, uint64_t result, uint32_t expected, uint32_t received) {
  if (!m_logFile.is_open()) return;
  std::string eventType = (received == expected) ? "AggregateComplete" : "AggregatePartial";
  m_logFile << std::fixed << std::setprecision(3)
            << ns3::Simulator::Now().GetSeconds() << "," << eventType << "," 
            << nodeName << "," << seq << ",," << expected << "," 
            << received << "," << result << "\n";
}
