#ifndef WEAVER_MININDN_CONGESTION_CONTROL_HPP
#define WEAVER_MININDN_CONGESTION_CONTROL_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace weaver {

class CongestionControl
{
public:
  virtual ~CongestionControl() = default;
  virtual void onData() = 0;
  virtual void onTimeout() = 0;
  virtual int getCwnd() const = 0;
  virtual std::string name() const = 0;
};

class CongestionAimd final : public CongestionControl
{
public:
  CongestionAimd();
  void onData() override;
  void onTimeout() override;
  int getCwnd() const override;
  std::string name() const override;

private:
  int m_cwnd;
  int m_ssthresh;
  int m_ackCount;
};

class CongestionCubic final : public CongestionControl
{
public:
  CongestionCubic();
  void onData() override;
  void onTimeout() override;
  int getCwnd() const override;
  std::string name() const override;

private:
  std::chrono::steady_clock::time_point m_epochStart;
  int m_cwnd;
  double m_ssthresh;
  double m_wMax;
  const double m_c;
  const double m_beta;
};

class CongestionBbr final : public CongestionControl
{
public:
  CongestionBbr();
  void onData() override;
  void onTimeout() override;
  int getCwnd() const override;
  std::string name() const override;

private:
  int m_cwnd;
  double m_bandwidthEstimate;
  double m_deliveredRounds;
  std::chrono::steady_clock::time_point m_lastSample;
};

std::unique_ptr<CongestionControl>
makeCongestionControl(const std::string& name);

} // namespace weaver

#endif // WEAVER_MININDN_CONGESTION_CONTROL_HPP

