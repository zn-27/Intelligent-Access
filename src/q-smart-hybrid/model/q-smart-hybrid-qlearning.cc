/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025 Q-Smart-Hybrid Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "q-smart-hybrid-qlearning.h"
#include "ns3/log.h"
#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("QSmartHybridQlearning");

namespace qSmartHybrid
{

// Static threshold definitions
const double QLearning::SPEED_THRESHOLDS[4] = {1.0, 2.0, 3.0, 4.0};      // m/s
const double QLearning::NCR_THRESHOLDS[4] = {0.2, 0.5, 1.0, 2.0};        // neighbors/s
const double QLearning::PDR_THRESHOLDS[4] = {0.6, 0.75, 0.85, 0.95};     // ratio
const double QLearning::SNR_VAR_THRESHOLDS[4] = {2.0, 5.0, 10.0, 20.0};  // dB^2
const double QLearning::QUEUE_THRESHOLDS[4] = {10, 30, 60, 100};         // packets

QLearning::QLearning (double alpha, double gamma, double epsilon)
  : m_alpha (alpha),
    m_gamma (gamma),
    m_epsilon (epsilon),
    m_switchThreshold (0.1)
{
  NS_LOG_FUNCTION (this << alpha << gamma << epsilon);
  m_random = CreateObject<UniformRandomVariable> ();
}

QLearning::~QLearning ()
{
  NS_LOG_FUNCTION (this);
}

Action
QLearning::ChooseAction (const QState& state)
{
  NS_LOG_FUNCTION (this);

  // Epsilon-greedy exploration
  if (m_random->GetValue (0.0, 1.0) < m_epsilon)
  {
    // Random exploration
    Action action = static_cast<Action> (m_random->GetInteger (0, NUM_ACTIONS - 1));
    NS_LOG_DEBUG ("Exploration: choosing random action " << action);
    return action;
  }
  else
  {
    // Exploitation: choose best action
    return ChooseBestAction (state);
  }
}

Action
QLearning::ChooseBestAction (const QState& state) const
{
  NS_LOG_FUNCTION (this);

  return GetBestAction (state);
}

bool
QLearning::ShouldSwitchAction (Action currentAction, Action candidateAction,
                               double qCandidate, double qCurrent) const
{
  NS_LOG_FUNCTION (this << currentAction << candidateAction << qCandidate << qCurrent);

  // Don't switch to same action
  if (currentAction == candidateAction)
  {
    return false;
  }

  // Hysteresis: only switch if improvement exceeds threshold
  return (qCandidate - qCurrent) > m_switchThreshold;
}

void
QLearning::Update (const QState& state, Action action, const QState& nextState, double reward)
{
  NS_LOG_FUNCTION (this << action << reward);

  uint64_t key = EncodeStateAction (state, action);
  double currentQ = GetQValue (state, action);
  double maxNextQ = GetMaxQValue (nextState);

  // Q-learning update: Q(s,a) = Q(s,a) + alpha * [r + gamma * max(Q(s',a')) - Q(s,a)]
  double newQ = currentQ + m_alpha * (reward + m_gamma * maxNextQ - currentQ);

  m_qTable[key] = newQ;

  NS_LOG_DEBUG ("Q-value update: state=" << state.Encode ()
                << " action=" << action
                << " oldQ=" << currentQ
                << " reward=" << reward
                << " maxNextQ=" << maxNextQ
                << " newQ=" << newQ);
}

double
QLearning::GetQValue (const QState& state, Action action) const
{
  uint64_t key = EncodeStateAction (state, action);
  auto it = m_qTable.find (key);
  if (it != m_qTable.end ())
  {
    return it->second;
  }
  return 0.0;  // Default Q-value
}

void
QLearning::SetQValue (const QState& state, Action action, double value)
{
  uint64_t key = EncodeStateAction (state, action);
  m_qTable[key] = value;
}

double
QLearning::CalculateReward (const PerformanceMetrics& metrics) const
{
  NS_LOG_FUNCTION (this);

  // Normalize PDR (0-1)
  double pdrScore = metrics.pdr;
  if (pdrScore > 1.0) pdrScore = 1.0;
  if (pdrScore < 0.0) pdrScore = 0.0;

  // Normalize delay (assume max 1 second)
  double delayScore = metrics.avgDelay / 1.0;
  if (delayScore > 1.0) delayScore = 1.0;
  if (delayScore < 0.0) delayScore = 0.0;

  // Normalize overhead (assume max 10000 packets)
  double overheadScore = metrics.controlPackets / 10000.0;
  if (overheadScore > 1.0) overheadScore = 1.0;
  if (overheadScore < 0.0) overheadScore = 0.0;

  // Weighted reward: PDR positive, delay and overhead negative
  double reward = 0.5 * pdrScore - 0.3 * delayScore - 0.2 * overheadScore;

  NS_LOG_DEBUG ("Reward calculation: pdr=" << pdrScore
                << " delay=" << delayScore
                << " overhead=" << overheadScore
                << " reward=" << reward);

  return reward;
}

uint8_t
QLearning::Discretize (double value, const double thresholds[4])
{
  for (uint8_t i = 0; i < 4; ++i)
  {
    if (value < thresholds[i])
    {
      return i;
    }
  }
  return 4;  // Highest level
}

QState
QLearning::CreateState (double nodeSpeed, double neighborChangeRate,
                        double pdr, double snrVariance, uint32_t queueLength)
{
  return QState (
    Discretize (nodeSpeed, SPEED_THRESHOLDS),
    Discretize (neighborChangeRate, NCR_THRESHOLDS),
    Discretize (pdr, PDR_THRESHOLDS),
    Discretize (snrVariance, SNR_VAR_THRESHOLDS),
    Discretize (static_cast<double> (queueLength), QUEUE_THRESHOLDS)
  );
}

double
QLearning::GetMaxQValue (const QState& state) const
{
  double maxQ = 0.0;
  for (int a = 0; a < NUM_ACTIONS; ++a)
  {
    double q = GetQValue (state, static_cast<Action> (a));
    if (q > maxQ)
    {
      maxQ = q;
    }
  }
  return maxQ;
}

Action
QLearning::GetBestAction (const QState& state) const
{
  Action bestAction = A2_WEAK_PROACTIVE;  // Default: conservative start
  double bestQ = GetQValue (state, bestAction);

  for (int a = 0; a < NUM_ACTIONS; ++a)
  {
    double q = GetQValue (state, static_cast<Action> (a));
    if (q > bestQ)
    {
      bestQ = q;
      bestAction = static_cast<Action> (a);
    }
  }

  NS_LOG_DEBUG ("Best action for state " << state.Encode ()
                << " is " << bestAction << " with Q=" << bestQ);

  return bestAction;
}

uint64_t
QLearning::EncodeStateAction (const QState& state, Action action) const
{
  // State can be up to 3125 (5^5), action 0-3
  // Total: 3125 * 4 = 12500 possible keys
  return static_cast<uint64_t> (state.Encode ()) * NUM_ACTIONS + static_cast<uint64_t> (action);
}

int64_t
QLearning::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_random->SetStream (stream);
  return 1;
}

void
QLearning::Clear ()
{
  NS_LOG_FUNCTION (this);
  m_qTable.clear ();
}

} // namespace qSmartHybrid
} // namespace ns3
