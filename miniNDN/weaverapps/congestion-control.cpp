#include "congestion-control.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace weaver {

CongestionAimd::CongestionAimd()
  : m_cwnd(1)
  , m_ssthresh(32)
  , m_ackCount(0)
{
}

void
CongestionAimd::onData()
{
  if (m_cwnd < m_ssthresh) {
    ++m_cwnd;
    return;
  }

  ++m_ackCount;
  if (m_ackCount >= m_cwnd) {
    ++m_cwnd;
    m_ackCount = 0;
  }
}

void
CongestionAimd::onTimeout()
{
  m_ssthresh = std::max(2, m_cwnd / 2);
  m_cwnd = 1;
  m_ackCount = 0;
}

int
CongestionAimd::getCwnd() const
{
  return std::max(1, m_cwnd);
}

std::string
CongestionAimd::name() const
{
  return "AIMD";
}

CongestionCubic::CongestionCubic()
  : m_epochStart(std::chrono::steady_clock::now())
  , m_cwnd(1)
  , m_ssthresh(32.0)
  , m_wMax(1.0)
  , m_c(0.4)
  , m_beta(0.7)
{
}

void
CongestionCubic::onData()
{
  if (m_cwnd < m_ssthresh) {
    ++m_cwnd;
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto t = std::chrono::duration<double>(now - m_epochStart).count();
  const double k = std::cbrt((m_wMax * (1.0 - m_beta)) / m_c);
  const double target = m_c * std::pow(t - k, 3) + m_wMax;
  if (target > m_cwnd) {
    ++m_cwnd;
  }
}

void
CongestionCubic::onTimeout()
{
  m_wMax = std::max(1, m_cwnd);
  m_ssthresh = std::max(2.0, m_cwnd * m_beta);
  m_cwnd = std::max(1, static_cast<int>(m_ssthresh));
  m_epochStart = std::chrono::steady_clock::now();
}

int
CongestionCubic::getCwnd() const
{
  return std::max(1, m_cwnd);
}

std::string
CongestionCubic::name() const
{
  return "CUBIC";
}

CongestionBbr::CongestionBbr()
  : m_cwnd(2)
  , m_bandwidthEstimate(1.0)
  , m_deliveredRounds(0.0)
  , m_lastSample(std::chrono::steady_clock::now())
{
}

void
CongestionBbr::onData()
{
  ++m_deliveredRounds;
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration<double>(now - m_lastSample).count();
  if (elapsed < 0.2) {
    return;
  }

  const double sampleRate = m_deliveredRounds / elapsed;
  if (sampleRate >= m_bandwidthEstimate) {
    m_bandwidthEstimate = sampleRate;
    m_cwnd = std::max(m_cwnd, static_cast<int>(std::ceil(m_bandwidthEstimate * 2.0)));
  }
  else {
    m_cwnd = std::max(1, static_cast<int>(std::ceil(0.9 * m_cwnd)));
  }

  m_deliveredRounds = 0.0;
  m_lastSample = now;
}

void
CongestionBbr::onTimeout()
{
  m_cwnd = std::max(1, static_cast<int>(std::ceil(0.9 * m_cwnd)));
}

int
CongestionBbr::getCwnd() const
{
  return std::max(1, m_cwnd);
}

std::string
CongestionBbr::name() const
{
  return "BBR";
}

std::unique_ptr<CongestionControl>
makeCongestionControl(const std::string& ccName)
{
  std::string lower;
  lower.reserve(ccName.size());
  for (char ch : ccName) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }

  if (lower == "aimd" || lower == "reno") {
    return std::make_unique<CongestionAimd>();
  }
  if (lower == "cubic") {
    return std::make_unique<CongestionCubic>();
  }
  if (lower == "bbr") {
    return std::make_unique<CongestionBbr>();
  }

  throw std::invalid_argument("unknown congestion control: " + ccName);
}

} // namespace weaver
