#include "intelligent-access-algorithm.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("IntelligentAccessAlgorithm");
NS_OBJECT_ENSURE_REGISTERED(IntelligentAccessAlgorithm);

TypeId
IntelligentAccessAlgorithm::GetTypeId()
{
  static TypeId tid =
      TypeId("ns3::IntelligentAccessAlgorithm")
          .SetParent<Object>()
          .SetGroupName("Mymodule")
          .AddConstructor<IntelligentAccessAlgorithm>()
          .AddAttribute("QualityWeight", "Weight of normalized link quality.",
                        DoubleValue(0.40),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_qualityWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("HopWeight", "Penalty weight of gateway hops.",
                        DoubleValue(0.12),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_hopWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("LoadWeight", "Penalty weight of network load.",
                        DoubleValue(0.15),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_loadWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("EnergyWeight", "Weight of minimum residual energy.",
                        DoubleValue(0.13),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_energyWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("SecurityWeight", "Weight of advertised security capabilities.",
                        DoubleValue(0.10),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_securityWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("SwitchCostWeight", "Weight of handover cost.",
                        DoubleValue(0.10),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_switchCostWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("TrendWeight",
                        "Reward or penalty applied to the predicted signal trend.",
                        DoubleValue(0.10),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_trendWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("FreshnessWeight",
                        "Penalty applied as an observation approaches expiry.",
                        DoubleValue(0.08),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_freshnessWeight),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("EwmaAlpha", "EWMA coefficient for signal and SNR.",
                        DoubleValue(0.35),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_ewmaAlpha),
                        MakeDoubleChecker<double>(0.0, 1.0))
          .AddAttribute("Hysteresis", "Minimum score gain required before handover.",
                        DoubleValue(0.08),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_hysteresis),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("MinAdaptiveHysteresis",
                        "Lower bound for mobility-adaptive hysteresis.",
                        DoubleValue(0.005),
                        MakeDoubleAccessor(
                            &IntelligentAccessAlgorithm::m_minAdaptiveHysteresis),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute("MinSignalDbm", "Hard signal eligibility threshold.",
                        DoubleValue(-88.0),
                        MakeDoubleAccessor(&IntelligentAccessAlgorithm::m_minSignalDbm),
                        MakeDoubleChecker<double>(-150.0, 0.0))
          .AddAttribute("EmergencyPredictedSignalDbm",
                        "Predicted current signal that activates urgent handover.",
                        DoubleValue(-82.0),
                        MakeDoubleAccessor(
                            &IntelligentAccessAlgorithm::m_emergencyPredictedSignalDbm),
                        MakeDoubleChecker<double>(-150.0, 0.0))
          .AddAttribute("MaxAbsTrendDbPerSecond",
                        "Clamp applied to estimated signal slope.",
                        DoubleValue(12.0),
                        MakeDoubleAccessor(
                            &IntelligentAccessAlgorithm::m_maxAbsTrendDbPerSecond),
                        MakeDoubleChecker<double>(0.1))
          .AddAttribute("CandidateLifetime", "Maximum age of a candidate observation.",
                        TimeValue(Seconds(4.0)),
                        MakeTimeAccessor(&IntelligentAccessAlgorithm::m_candidateLifetime),
                        MakeTimeChecker())
          .AddAttribute("TimeToTrigger", "How long a target must remain better.",
                        TimeValue(Seconds(1.0)),
                        MakeTimeAccessor(&IntelligentAccessAlgorithm::m_timeToTrigger),
                        MakeTimeChecker())
          .AddAttribute("MinAdaptiveTimeToTrigger",
                        "Minimum time-to-trigger under urgent link degradation.",
                        TimeValue(MilliSeconds(250)),
                        MakeTimeAccessor(
                            &IntelligentAccessAlgorithm::m_minAdaptiveTimeToTrigger),
                        MakeTimeChecker())
          .AddAttribute("MaxAdaptiveTimeToTrigger",
                        "Maximum time-to-trigger for unstable or receding targets.",
                        TimeValue(Seconds(2.0)),
                        MakeTimeAccessor(
                            &IntelligentAccessAlgorithm::m_maxAdaptiveTimeToTrigger),
                        MakeTimeChecker())
          .AddAttribute("PredictionHorizon",
                        "How far ahead signal quality is predicted.",
                        TimeValue(Seconds(2.0)),
                        MakeTimeAccessor(
                            &IntelligentAccessAlgorithm::m_predictionHorizon),
                        MakeTimeChecker())
          .AddAttribute("TrendWindow",
                        "Recent observation window used for linear regression.",
                        TimeValue(Seconds(5.0)),
                        MakeTimeAccessor(&IntelligentAccessAlgorithm::m_trendWindow),
                        MakeTimeChecker())
          .AddAttribute("ConsecutiveBetter", "Required consecutive better evaluations.",
                        UintegerValue(2),
                        MakeUintegerAccessor(&IntelligentAccessAlgorithm::m_requiredConsecutiveBetter),
                        MakeUintegerChecker<uint32_t>(1))
          .AddAttribute("RequiredSecurityCapabilities",
                        "Bit mask of mandatory security capabilities.",
                        UintegerValue(0),
                        MakeUintegerAccessor(&IntelligentAccessAlgorithm::m_requiredSecurityCapabilities),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("RequireGateway", "Reject candidates without a reachable gateway.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&IntelligentAccessAlgorithm::m_requireGateway),
                        MakeBooleanChecker())
          .AddAttribute("RequireAddressService",
                        "Reject candidates without a usable address service.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&IntelligentAccessAlgorithm::m_requireAddressService),
                        MakeBooleanChecker());
  return tid;
}

IntelligentAccessAlgorithm::IntelligentAccessAlgorithm()
    : m_hasGuardTarget(false),
      m_betterSince(Seconds(0)),
      m_consecutiveBetter(0)
{
}

bool
IntelligentAccessAlgorithm::NetworkKey::operator<(const NetworkKey& other) const
{
  // Gateway/channel are mutable attributes of a network, not its identity.
  return std::tie(type, domainId, networkId) <
         std::tie(other.type, other.domainId, other.networkId);
}

bool
IntelligentAccessAlgorithm::NetworkKey::operator==(const NetworkKey& other) const
{
  return type == other.type && domainId == other.domainId &&
         networkId == other.networkId;
}

double
IntelligentAccessAlgorithm::Clamp01(double value)
{
  return std::max(0.0, std::min(1.0, value));
}

uint32_t
IntelligentAccessAlgorithm::CountBits(uint32_t value)
{
  uint32_t count = 0;
  while (value != 0)
    {
      count += value & 1u;
      value >>= 1;
    }
  return count;
}

void
IntelligentAccessAlgorithm::Update(const Observation& observation)
{
  CandidateState& state = m_candidates[observation.key];
  Candidate& candidate = state.candidate;
  if (candidate.sampleCount == 0)
    {
      candidate.ewmaSignalDbm = observation.signalDbm;
      candidate.ewmaSnrDb = observation.signalDbm - observation.noiseDbm;
    }
  else
    {
      candidate.ewmaSignalDbm =
          m_ewmaAlpha * observation.signalDbm +
          (1.0 - m_ewmaAlpha) * candidate.ewmaSignalDbm;
      candidate.ewmaSnrDb =
          m_ewmaAlpha * (observation.signalDbm - observation.noiseDbm) +
          (1.0 - m_ewmaAlpha) * candidate.ewmaSnrDb;
    }
  candidate.observation = observation;
  candidate.sampleCount++;
  state.lastSeen = observation.observedAt;
  state.signalHistory.emplace_back(observation.observedAt,
                                   candidate.ewmaSignalDbm);
  while (!state.signalHistory.empty() &&
         observation.observedAt - state.signalHistory.front().first >
             m_trendWindow)
    {
      state.signalHistory.pop_front();
    }
  while (state.signalHistory.size() > 32)
    {
      state.signalHistory.pop_front();
    }
  UpdatePrediction(state);
}

void
IntelligentAccessAlgorithm::UpdatePrediction(CandidateState& state)
{
  Candidate& candidate = state.candidate;
  candidate.signalTrendDbPerSecond = 0.0;
  if (state.signalHistory.size() >= 3 &&
      state.signalHistory.back().first - state.signalHistory.front().first >=
          Seconds(1.0))
    {
      const Time origin = state.signalHistory.front().first;
      double sumX = 0.0;
      double sumY = 0.0;
      double sumXX = 0.0;
      double sumXY = 0.0;
      double sumYY = 0.0;
      for (const auto& sample : state.signalHistory)
        {
          const double x = (sample.first - origin).GetSeconds();
          const double y = sample.second;
          sumX += x;
          sumY += y;
          sumXX += x * x;
          sumXY += x * y;
          sumYY += y * y;
        }
      const double count = static_cast<double>(state.signalHistory.size());
      const double denominator = count * sumXX - sumX * sumX;
      const double signalVariance = count * sumYY - sumY * sumY;
      if (std::abs(denominator) > 1e-9 && signalVariance > 1e-9)
        {
          const double covariance = count * sumXY - sumX * sumY;
          const double slope = covariance / denominator;
          const double correlation =
              covariance / std::sqrt(denominator * signalVariance);
          // Down-weight erratic scan samples; a clean monotonic trajectory
          // keeps its slope, while channel-hop/fading jitter approaches zero.
          candidate.signalTrendDbPerSecond =
              slope * std::abs(correlation);
        }
    }
  candidate.signalTrendDbPerSecond =
      std::max(-m_maxAbsTrendDbPerSecond,
               std::min(m_maxAbsTrendDbPerSecond,
                        candidate.signalTrendDbPerSecond));
  candidate.predictedSignalDbm =
      candidate.ewmaSignalDbm +
      candidate.signalTrendDbPerSecond * m_predictionHorizon.GetSeconds();
}

void
IntelligentAccessAlgorithm::Purge(Time now)
{
  for (auto it = m_candidates.begin(); it != m_candidates.end();)
    {
      if (now - it->second.lastSeen > m_candidateLifetime)
        {
          it = m_candidates.erase(it);
        }
      else
        {
          ++it;
        }
    }
}

bool
IntelligentAccessAlgorithm::IsEligible(const Candidate& candidate,
                                        std::string& reason) const
{
  const Observation& observation = candidate.observation;
  if (candidate.ewmaSignalDbm < m_minSignalDbm &&
      candidate.predictedSignalDbm < m_minSignalDbm)
    {
      reason = "signal below threshold";
      return false;
    }
  if ((observation.securityCapabilities & m_requiredSecurityCapabilities) !=
      m_requiredSecurityCapabilities)
    {
      reason = "mandatory security capability missing";
      return false;
    }
  if (m_requireGateway && !observation.gatewayReachable)
    {
      reason = "gateway unreachable";
      return false;
    }
  if (m_requireAddressService && !observation.addressServiceAvailable)
    {
      reason = "address service unavailable";
      return false;
    }
  return true;
}

double
IntelligentAccessAlgorithm::CalculateSwitchCost(const NetworkKey& target,
                                                 const NetworkKey* current) const
{
  if (current == nullptr || target == *current)
    {
      return 0.0;
    }
  if (target.domainId == current->domainId && target.type == current->type)
    {
      return 0.20;
    }
  if (target.type == current->type)
    {
      return 0.55;
    }
  return 1.0;
}

double
IntelligentAccessAlgorithm::CalculateScore(const Candidate& candidate,
                                            const NetworkKey* current) const
{
  const Observation& observation = candidate.observation;
  const double predictedDelta =
      candidate.predictedSignalDbm - candidate.ewmaSignalDbm;
  const double blendedSignal =
      0.4 * candidate.ewmaSignalDbm +
      0.6 * candidate.predictedSignalDbm;
  const double predictedSnr = candidate.ewmaSnrDb + predictedDelta;
  const double rssiNorm = Clamp01((blendedSignal + 90.0) / 60.0);
  const double snrNorm = Clamp01(predictedSnr / 40.0);
  const double qualityNorm = 0.6 * rssiNorm + 0.4 * snrNorm;
  const double trendNorm =
      std::max(-1.0, std::min(1.0,
          candidate.signalTrendDbPerSecond / m_maxAbsTrendDbPerSecond));
  const double hopNorm = Clamp01(static_cast<double>(
                                      std::min(observation.hopsToGateway, 8u)) /
                                  8.0);
  const double loadNorm = Clamp01(observation.load);
  const double energyNorm = Clamp01(observation.minEnergy);
  const double securityNorm =
      static_cast<double>(CountBits(observation.securityCapabilities &
                                    ((1u << 6) - 1))) /
      6.0;
  const double switchCost = CalculateSwitchCost(observation.key, current);
  const double freshnessPenalty =
      m_freshnessWeight *
      Clamp01(candidate.observationAgeSeconds /
              std::max(0.001, m_candidateLifetime.GetSeconds()));

  return m_qualityWeight * qualityNorm - m_hopWeight * hopNorm -
         m_loadWeight * loadNorm + m_energyWeight * energyNorm +
         m_securityWeight * securityNorm + m_trendWeight * trendNorm -
         m_switchCostWeight * switchCost - freshnessPenalty;
}

IntelligentAccessAlgorithm::Decision
IntelligentAccessAlgorithm::Evaluate(const NetworkKey* current,
                                     bool currentReady,
                                     Time now)
{
  Purge(now);
  Decision decision;
  decision.effectiveHysteresis = m_hysteresis;
  decision.effectiveTimeToTrigger = m_timeToTrigger;
  bool currentFound = false;
  const Candidate* currentCandidate = nullptr;

  for (auto& entry : m_candidates)
    {
      Candidate& candidate = entry.second.candidate;
      candidate.observationAgeSeconds =
          std::max(0.0, (now - entry.second.lastSeen).GetSeconds());
      std::string rejectReason;
      if (!IsEligible(candidate, rejectReason))
        {
          continue;
        }
      candidate.score = CalculateScore(candidate, current);
      if (current != nullptr && candidate.observation.key == *current)
        {
          decision.currentScore = candidate.score;
          currentFound = true;
          currentCandidate = &candidate;
        }
      if (!decision.hasCandidate || candidate.score > decision.best.score)
        {
          decision.hasCandidate = true;
          decision.best = candidate;
        }
    }

  if (!decision.hasCandidate)
    {
      ResetHandoverGuard();
      decision.reason = "no eligible candidate";
      return decision;
    }

  if (current == nullptr || !currentReady)
    {
      decision.shouldSwitch = true;
      decision.reason = "initial access";
      ResetHandoverGuard();
      return decision;
    }

  if (decision.best.observation.key == *current)
    {
      ResetHandoverGuard();
      decision.reason = "current network remains best";
      return decision;
    }

  if (!currentFound)
    {
      // A disappeared current network must not block emergency handover.
      decision.currentScore = -1e9;
    }

  const double currentDecline =
      currentCandidate
          ? Clamp01(-currentCandidate->signalTrendDbPerSecond /
                    m_maxAbsTrendDbPerSecond)
          : 1.0;
  const double targetApproach =
      Clamp01(decision.best.signalTrendDbPerSecond /
              m_maxAbsTrendDbPerSecond);
  const double targetReceding =
      Clamp01(-decision.best.signalTrendDbPerSecond /
              m_maxAbsTrendDbPerSecond);
  const bool emergency =
      !currentCandidate ||
      currentCandidate->predictedSignalDbm <=
          m_emergencyPredictedSignalDbm;
  decision.emergency = emergency;

  double hysteresisScale =
      1.0 - 0.60 * currentDecline - 0.25 * targetApproach +
      0.40 * targetReceding;
  if (emergency)
    {
      hysteresisScale *= 0.35;
    }
  decision.effectiveHysteresis =
      std::max(m_minAdaptiveHysteresis,
               m_hysteresis * std::max(0.1, hysteresisScale));

  double tttScale =
      1.0 - 0.65 * currentDecline - 0.25 * targetApproach +
      0.75 * targetReceding;
  if (emergency)
    {
      tttScale *= 0.30;
    }
  const double adaptiveTttSeconds = std::max(
      m_minAdaptiveTimeToTrigger.GetSeconds(),
      std::min(m_maxAdaptiveTimeToTrigger.GetSeconds(),
               m_timeToTrigger.GetSeconds() * std::max(0.1, tttScale)));
  decision.effectiveTimeToTrigger = Seconds(adaptiveTttSeconds);

  if (decision.best.score <=
      decision.currentScore + decision.effectiveHysteresis)
    {
      ResetHandoverGuard();
      decision.reason = "gain below hysteresis";
      return decision;
    }

  if (!m_hasGuardTarget || !(m_guardTarget == decision.best.observation.key))
    {
      m_guardTarget = decision.best.observation.key;
      m_hasGuardTarget = true;
      m_betterSince = now;
      m_consecutiveBetter = 1;
      decision.reason = "handover guard started";
      return decision;
    }

  ++m_consecutiveBetter;
  if (m_consecutiveBetter < m_requiredConsecutiveBetter ||
      now - m_betterSince < decision.effectiveTimeToTrigger)
    {
      decision.reason = "waiting for time-to-trigger";
      return decision;
    }

  decision.shouldSwitch = true;
  decision.reason = "candidate passed hysteresis and time-to-trigger";
  ResetHandoverGuard();
  return decision;
}

std::vector<IntelligentAccessAlgorithm::Candidate>
IntelligentAccessAlgorithm::GetCandidates() const
{
  std::vector<Candidate> result;
  result.reserve(m_candidates.size());
  for (const auto& entry : m_candidates)
    {
      result.push_back(entry.second.candidate);
    }
  return result;
}

void
IntelligentAccessAlgorithm::ResetHandoverGuard()
{
  m_hasGuardTarget = false;
  m_consecutiveBetter = 0;
  m_betterSince = Seconds(0);
}

} // namespace ns3
