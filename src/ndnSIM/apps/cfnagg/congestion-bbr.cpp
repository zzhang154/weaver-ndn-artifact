#include "congestion-control.hpp"
#include <cmath>
#include <algorithm>
#include "ns3/simulator.h"

CongestionBBR::CongestionBBR()
  : m_cwnd(1)
  , m_minRtt(1e9)
  , m_bandwidthEstimate(0.0)
  , m_lastSampleTime(0.0)
  , m_deliveredBytes(0.0) {
}

void CongestionBBR::OnData() {
  double now = ns3::Simulator::Now().GetSeconds();
  double interval = now - m_lastSampleTime;
  if (interval > 0.0) {
    // Calculate delivery rate in bytes/sec over this interval
    double deliveryRate = m_deliveredBytes / interval;
    if (deliveryRate > m_bandwidthEstimate) {
      m_bandwidthEstimate = deliveryRate;
      // If RTT is known, set cwnd to bandwidth * minRTT (estimated BDP)
      if (m_minRtt < 1e9) {
        int targetCwnd = (int)std::ceil(m_bandwidthEstimate * m_minRtt);
        if (targetCwnd > m_cwnd) {
          m_cwnd = targetCwnd;
        }
      } else {
        // If no RTT estimate yet, slowly increase
        m_cwnd += 1;
      }
    } 
    // If delivery rate did not increase, maintain current cwnd (BBR would probe periodically)
  }
  // Reset delivery measurement
  m_deliveredBytes = 0.0;
  m_lastSampleTime = now;
}

void CongestionBBR::OnTimeout() {
  // On loss, BBR generally does not drastically cut cwnd; here we reduce slightly
  m_cwnd = std::max(1, m_cwnd / 2);
  // In a complete BBR, we might also reset bandwidth estimate or enter a recovery mode
}

int CongestionBBR::GetCwnd() const {
  return std::max(m_cwnd, 1);
}
