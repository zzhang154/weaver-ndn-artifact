#ifndef TRACE_COLLECTOR_HPP
#define TRACE_COLLECTOR_HPP

#include <fstream>
#include <string>
#include <cstdint>

/** TraceCollector: Utility for logging events (Interests, Data, aggregations) to a file. */
class TraceCollector {
public:
  // Open the log file (writes header line)
  static void Open(const std::string& filename);
  // Close the log file
  static void Close();
  // Log an interest sent event (node, sequence, time)
  static void LogInterest(const std::string& nodeName, int seq, double time);
  // Log a data received event (node, sequence, child, time)
  static void LogData(const std::string& nodeName, int seq, const std::string& childName, double time);
  // Log an aggregation result (complete or partial) event (node, sequence, expected children, received children, result)
  static void LogAggregate(const std::string& nodeName, int seq, uint64_t result, uint32_t expected, uint32_t received);

private:
  static std::ofstream m_logFile;
};

#endif // TRACE_COLLECTOR_HPP
