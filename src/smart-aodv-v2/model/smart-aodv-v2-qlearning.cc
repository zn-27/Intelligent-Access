/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 NUS
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
 *
 * Authors: Q-Learning Enhancement for Smart-AODV-V2
 */

#include "smart-aodv-v2-qlearning.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include <algorithm>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("SmartAodvV2QLearning");

namespace smartAodvV2
{

// SNR thresholds for discretization (in dB)
static const double SNR_POOR_THRESHOLD = 10.0;   // Below 10 dB is poor
static const double SNR_GOOD_THRESHOLD = 20.0;   // Above 20 dB is good

// Reward constants
static const double REWARD_SUCCESS = 10.0;       // Base reward for successful transmission
static const double REWARD_HIGH_SNR = 5.0;       // Bonus for high SNR (>= 20 dB)
static const double REWARD_LOW_HOPS = 8.0;       // Base for low hop count
static const double PENALTY_FAILURE = -20.0;     // Penalty for failed transmission
static const double PENALTY_DELAY = -1.0;        // Penalty per ms of delay

QLearning::QLearning (double alpha, double gamma, double epsilon)
  : m_alpha (alpha),
    m_gamma (gamma),
    m_epsilon (epsilon)
{
  NS_LOG_FUNCTION (this << alpha << gamma << epsilon);
  m_random = CreateObject<UniformRandomVariable> ();
}

QLearning::~QLearning ()
{
  NS_LOG_FUNCTION (this);
}

int
QLearning::ChooseAction (const QState& state, int numActions)
{
  NS_LOG_FUNCTION (this << (uint32_t)state.snrLevel << (uint32_t)state.hopCount << numActions);

  if (numActions <= 0)
    {
      NS_LOG_WARN ("No actions available");
      return 0;
    }

  // ε-greedy exploration
  double explore = m_random->GetValue ();

  if (explore < m_epsilon)
    {
      // Exploration: random action
      int action = m_random->GetInteger (0, numActions - 1);
      NS_LOG_DEBUG ("Exploration: chose random action " << action);
      return action;
    }
  else
    {
      // Exploitation: best action
      return ChooseBestAction (state, numActions);
    }
}

int
QLearning::ChooseBestAction (const QState& state, int numActions)
{
  NS_LOG_FUNCTION (this << (uint32_t)state.snrLevel << (uint32_t)state.hopCount << numActions);

  if (numActions <= 0)
    {
      return 0;
    }

  // Find action with maximum Q-value
  int bestAction = 0;
  double bestQ = GetQValue (state, 0);

  for (int a = 1; a < numActions; ++a)
    {
      double q = GetQValue (state, a);
      if (q > bestQ)
        {
          bestQ = q;
          bestAction = a;
        }
    }

  NS_LOG_DEBUG ("Exploitation: chose best action " << bestAction << " with Q-value " << bestQ);
  return bestAction;
}

void
QLearning::Update (const QState& state, int action, const QState& nextState, double reward)
{
  NS_LOG_FUNCTION (this << (uint32_t)state.snrLevel << (uint32_t)state.hopCount
                       << action << reward);

  if (action < 0)
    {
      NS_LOG_WARN ("Invalid action for Q-update");
      return;
    }

  // Get current Q-value
  double currentQ = GetQValue (state, action);

  // Calculate max Q-value for next state
  // We assume at least one action available (will return 0 if no actions)
  double maxNextQ = GetMaxQValue (nextState, 8); // Assume up to 8 actions for next state

  // Q-learning update rule:
  // Q(s,a) = Q(s,a) + α * [r + γ * max(Q(s',a')) - Q(s,a)]
  double newQ = currentQ + m_alpha * (reward + m_gamma * maxNextQ - currentQ);

  SetQValue (state, action, newQ);

  NS_LOG_DEBUG ("Q-update: Q(" << state.Encode () << "," << action << ") = "
              << currentQ << " -> " << newQ << " (reward=" << reward
              << ", maxNextQ=" << maxNextQ << ")");
}

double
QLearning::GetQValue (const QState& state, int action) const
{
  uint64_t key = EncodeStateAction (state, action);
  std::map<uint64_t, double>::const_iterator it = m_qTable.find (key);

  if (it != m_qTable.end ())
    {
      return it->second;
    }

  // Default Q-value with small penalty for unknown routes
  // This prevents unexplored routes from appearing better than explored ones with failures
  return -5.0;
}

void
QLearning::SetQValue (const QState& state, int action, double value)
{
  uint64_t key = EncodeStateAction (state, action);
  m_qTable[key] = value;
}

double
QLearning::CalculateReward (const TransmissionStats& stats) const
{
  double reward = 0.0;

  if (stats.success)
    {
      // Base reward for successful transmission
      reward += REWARD_SUCCESS;

      // Bonus for high SNR
      if (stats.snr >= SNR_GOOD_THRESHOLD)
        {
          reward += REWARD_HIGH_SNR;
        }

      // Bonus for low hop count (fewer hops = better)
      if (stats.hops < 8)
        {
          reward += (REWARD_LOW_HOPS - stats.hops);
        }

      // Small penalty for delay (per ms)
      double delayMs = stats.delay * 1000.0;
      if (delayMs > 0)
        {
          reward += PENALTY_DELAY * (delayMs / 10.0); // -0.1 per 10ms delay
        }
    }
  else
    {
      // Penalty for failed transmission
      reward = PENALTY_FAILURE;
    }

  NS_LOG_DEBUG ("Reward calculation: success=" << stats.success
              << ", snr=" << stats.snr
              << ", hops=" << (uint32_t)stats.hops
              << ", delay=" << stats.delay
              << " -> reward=" << reward);

  return reward;
}

uint8_t
QLearning::DiscretizeSnr (double snr)
{
  if (snr < SNR_POOR_THRESHOLD)
    {
      return 0; // Poor
    }
  else if (snr < SNR_GOOD_THRESHOLD)
    {
      return 1; // Medium
    }
  else
    {
      return 2; // Good
    }
}

QState
QLearning::CreateState (double snr, uint8_t hops)
{
  return QState (DiscretizeSnr (snr), hops);
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

double
QLearning::GetMaxQValue (const QState& state, int numActions) const
{
  double maxQ = 0.0; // Default to 0 if no entries exist

  for (int a = 0; a < numActions; ++a)
    {
      double q = GetQValue (state, a);
      if (q > maxQ)
        {
          maxQ = q;
        }
    }

  return maxQ;
}

uint64_t
QLearning::EncodeStateAction (const QState& state, int action) const
{
  // State is encoded in upper 8 bits, action in lower bits
  // State space: 24 values (3 SNR levels x 8 hop counts)
  // Action space: up to 256 actions
  return (static_cast<uint64_t> (state.Encode ()) << 8) | static_cast<uint64_t> (action & 0xFF);
}

} // namespace smartAodvV2
} // namespace ns3
