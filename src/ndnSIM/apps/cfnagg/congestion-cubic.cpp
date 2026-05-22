#include "congestion-control.hpp"
#include <cmath>
#include <algorithm>
#include "ns3/simulator.h"

CongestionCubic::CongestionCubic()
  : m_epochStartTime(0.0)
  , m_lastDropTime(0.0)
  , m_cwnd(1)
  , m_ssthresh(10000)
  , m_ackCount(0)
  , m_Wmax(0.0)
  , m_C(0.4)
  , m_beta(0.7) {
}

void CongestionCubic::OnData() {
  double now = ns3::Simulator::Now().GetSeconds();
  if (m_cwnd < m_ssthresh) {
    // Slow start phase
    m_cwnd += 1;
    return;
  }
  if (m_epochStartTime == 0.0) {
    m_epochStartTime = now;
    if (m_Wmax < m_cwnd) {
      m_Wmax = m_cwnd;
    }
  }
  double t = now - m_epochStartTime;
  // Calculate Cubic target window W(t)
  double K = cbrt(m_Wmax * (1 - m_beta) / m_C);
  double target;
  if (t < K) {
    target = m_Wmax - m_C * std::pow(K - t, 3);
  } else {
    target = m_Wmax + m_C * std::pow(t - K, 3);
  }
  if (target < m_cwnd) {
    // TCP-friendly region: do linear increase
    m_ackCount++;
    if (m_ackCount >= m_cwnd) {
      m_cwnd += 1;
      m_ackCount = 0;
    }
  } else {
    // Cubic region: increase cwnd if target > current cwnd
    m_cwnd += 1;
  }
}

void CongestionCubic::OnTimeout() {
  // On loss event, update Wmax and reduce cwnd
  m_lastDropTime = ns3::Simulator::Now().GetSeconds();
  m_Wmax = m_cwnd * 1.0;  // record last cwnd
  m_cwnd = std::max(1, (int)std::floor(m_cwnd * m_beta));
  m_ssthresh = std::max(1.0, m_Wmax * m_beta);
  m_ackCount = 0;
  m_epochStartTime = 0.0; // reset epoch
}

int CongestionCubic::GetCwnd() const {
  return std::max(m_cwnd, 1);
}
