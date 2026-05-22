#ifndef CONGESTION_CONTROL_HPP
#define CONGESTION_CONTROL_HPP

#include <cstdint>

/** CongestionControl: Abstract base class for congestion control algorithms. */
class CongestionControl {
public:
  virtual ~CongestionControl() = default;
  // Called when a Data (acknowledgement of an interest) is received successfully
  virtual void OnData() = 0;
  // Called when an interest times out (loss event)
  virtual void OnTimeout() = 0;
  // Get current congestion window (number of parallel interests allowed)
  virtual int GetCwnd() const = 0;
};

/** AIMD (TCP Reno/NewReno-like) congestion control implementation. */
class CongestionAIMD : public CongestionControl {
public:
  CongestionAIMD();
  void OnData() override;
  void OnTimeout() override;
  int GetCwnd() const override;
private:
  int m_cwnd;
  int m_ssthresh;
  int m_ackCount;
};

/** Cubic congestion control implementation (simplified). */
class CongestionCubic : public CongestionControl {
public:
  CongestionCubic();
  void OnData() override;
  void OnTimeout() override;
  int GetCwnd() const override;
private:
  double m_epochStartTime;
  double m_lastDropTime;
  int m_cwnd;
  double m_ssthresh;
  int m_ackCount;
  double m_Wmax;
  // Cubic parameters
  const double m_C;
  const double m_beta;
};

/** BBR congestion control implementation (simplified). */
class CongestionBBR : public CongestionControl {
public:
  CongestionBBR();
  void OnData() override;
  void OnTimeout() override;
  int GetCwnd() const override;
  // (In a full implementation, methods to record RTT and delivered bytes would be added)
private:
  int m_cwnd;
  double m_minRtt;
  double m_bandwidthEstimate;
  double m_lastSampleTime;
  double m_deliveredBytes;
};

#endif // CONGESTION_CONTROL_HPP
