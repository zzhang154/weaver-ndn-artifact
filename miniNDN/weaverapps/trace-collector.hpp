#ifndef WEAVER_MININDN_TRACE_COLLECTOR_HPP
#define WEAVER_MININDN_TRACE_COLLECTOR_HPP

#include <cstdint>
#include <fstream>
#include <string>

namespace weaver {

class TraceCollector
{
public:
  explicit TraceCollector(const std::string& path);
  ~TraceCollector();

  void log(const std::string& event, const std::string& node, uint64_t seq,
           const std::string& detail);

private:
  std::ofstream m_os;
};

} // namespace weaver

#endif // WEAVER_MININDN_TRACE_COLLECTOR_HPP

