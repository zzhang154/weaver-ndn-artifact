#include "trace-collector.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace weaver {

TraceCollector::TraceCollector(const std::string& path)
{
  if (path.empty()) {
    return;
  }

  m_os.open(path, std::ios::out | std::ios::app);
  if (!m_os) {
    std::cerr << "WARN: cannot open trace file " << path << "\n";
    return;
  }

  if (m_os.tellp() == 0) {
    m_os << "time_ms,event,node,seq,detail\n";
  }
}

TraceCollector::~TraceCollector()
{
  if (m_os) {
    m_os.flush();
  }
}

void
TraceCollector::log(const std::string& event, const std::string& node, uint64_t seq,
                    const std::string& detail)
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

  std::ostringstream escaped;
  for (char ch : detail) {
    if (ch == '"') {
      escaped << "\"\"";
    }
    else {
      escaped << ch;
    }
  }

  if (m_os) {
    m_os << ms << ',' << event << ',' << node << ',' << seq << ",\"" << escaped.str() << "\"\n";
    m_os.flush();
  }

  std::cerr << '[' << event << "] node=" << node << " seq=" << seq << ' ' << detail << '\n';
}

} // namespace weaver

