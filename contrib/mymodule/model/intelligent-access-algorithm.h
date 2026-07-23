#ifndef INTELLIGENT_ACCESS_ALGORITHM_H
#define INTELLIGENT_ACCESS_ALGORITHM_H

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace ns3 {

/**
 * Independent multi-attribute access decision module.
 *
 * Discovery and handover execution stay outside this class.  The algorithm
 * owns the time history needed for smoothing, time-to-trigger and ping-pong
 * suppression, so the same policy can be reused by AP and Ad-Hoc domains.
 */
class IntelligentAccessAlgorithm : public Object
{
public:
  enum NetworkType : uint8_t
  {
    INFRASTRUCTURE = 0,
    ADHOC = 1
  };

  enum SecurityCapability : uint32_t
  {
    SEC_INTEGRITY = 1u << 0,
    SEC_CONFIDENTIALITY = 1u << 1,
    SEC_MUTUAL_AUTH = 1u << 2,
    SEC_REPLAY_PROTECTION = 1u << 3,
    SEC_FORWARD_SECRECY = 1u << 4,
    SEC_DECENTRALIZED_AUTH = 1u << 5
  };

  struct NetworkKey
  {
    NetworkType type{INFRASTRUCTURE};
    uint32_t domainId{0};
    std::string networkId;
    Ipv4Address gateway{"0.0.0.0"};
    uint16_t channelFreqMhz{0};

    bool operator<(const NetworkKey& other) const;
    bool operator==(const NetworkKey& other) const;
  };

  struct Observation
  {
    NetworkKey key;
    double signalDbm{-100.0};
    double noiseDbm{-95.0};
    uint32_t hopsToGateway{99};
    double load{1.0};
    double minEnergy{0.0};
    uint32_t nodeCount{0};
    uint32_t securityCapabilities{0};
    bool gatewayReachable{false};
    bool addressServiceAvailable{false};
    Time observedAt{Seconds(0)};
  };

  struct Candidate
  {
    Observation observation;
    double ewmaSignalDbm{-100.0};
    double ewmaSnrDb{0.0};
    double predictedSignalDbm{-100.0};
    double signalTrendDbPerSecond{0.0};
    double observationAgeSeconds{0.0};
    double score{-1e9};
    uint32_t sampleCount{0};
  };

  struct Decision
  {
    bool hasCandidate{false};
    bool shouldSwitch{false};
    bool emergency{false};
    Candidate best;
    double currentScore{-1e9};
    double effectiveHysteresis{0.0};
    Time effectiveTimeToTrigger{Seconds(0)};
    std::string reason;
  };

  static TypeId GetTypeId();
  IntelligentAccessAlgorithm();

  void Update(const Observation& observation);
  void Purge(Time now);
  Decision Evaluate(const NetworkKey* current, bool currentReady, Time now);
  std::vector<Candidate> GetCandidates() const;
  double CalculateScore(const Candidate& candidate, const NetworkKey* current) const;
  void ResetHandoverGuard();

private:
  struct CandidateState
  {
    Candidate candidate;
    Time lastSeen{Seconds(0)};
    std::deque<std::pair<Time, double>> signalHistory;
  };

  bool IsEligible(const Candidate& candidate, std::string& reason) const;
  void UpdatePrediction(CandidateState& state);
  double CalculateSwitchCost(const NetworkKey& target, const NetworkKey* current) const;
  static double Clamp01(double value);
  static uint32_t CountBits(uint32_t value);

  std::map<NetworkKey, CandidateState> m_candidates;
  NetworkKey m_guardTarget;
  bool m_hasGuardTarget;
  Time m_betterSince;
  uint32_t m_consecutiveBetter;

  double m_qualityWeight;
  double m_hopWeight;
  double m_loadWeight;
  double m_energyWeight;
  double m_securityWeight;
  double m_switchCostWeight;
  double m_trendWeight;
  double m_freshnessWeight;
  double m_ewmaAlpha;
  double m_hysteresis;
  double m_minAdaptiveHysteresis;
  double m_minSignalDbm;
  double m_emergencyPredictedSignalDbm;
  double m_maxAbsTrendDbPerSecond;
  Time m_candidateLifetime;
  Time m_timeToTrigger;
  Time m_minAdaptiveTimeToTrigger;
  Time m_maxAdaptiveTimeToTrigger;
  Time m_predictionHorizon;
  Time m_trendWindow;
  uint32_t m_requiredConsecutiveBetter;
  uint32_t m_requiredSecurityCapabilities;
  bool m_requireGateway;
  bool m_requireAddressService;
};

} // namespace ns3

#endif
