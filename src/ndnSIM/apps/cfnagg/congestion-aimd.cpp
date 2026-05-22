#include "congestion-control.hpp"
#include <algorithm>

CongestionAIMD::CongestionAIMD()
  : m_cwnd(1)
  , m_ssthresh(10000)
  , m_ackCount(0) {
}

void CongestionAIMD::OnData() {
  // Increase congestion window
  if (m_cwnd < m_ssthresh) {
    // Slow start: exponential growth
    m_cwnd += 1;
  } else {
    // Congestion avoidance: linear growth (increment by 1 per cwnd acknowledgements)
    m_ackCount += 1;
    if (m_ackCount >= m_cwnd) {
      m_cwnd += 1;
      m_ackCount = 0;
    }
  }
}

void CongestionAIMD::OnTimeout() {
  // Timeout indicates loss: multiplicative decrease
  m_ssthresh = std::max(1, m_cwnd / 2);
  m_cwnd = 1;
  m_ackCount = 0;
}

int CongestionAIMD::GetCwnd() const {
  // Ensure at least one interest can be sent
  return std::max(m_cwnd, 1);
}
