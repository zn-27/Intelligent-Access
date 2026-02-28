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

#ifndef SMART_AODV_QLEARNING_QLEARNING_H
#define SMART_AODV_QLEARNING_QLEARNING_H

#include <stdint.h>
#include <map>
#include <vector>
#include <cmath>
#include "ns3/nstime.h"
#include "ns3/ipv4-address.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include "ns3/ptr.h"

namespace ns3
{
namespace smartAodvQlearningV2
{

/**
 * \ingroup smartAodvQlearningV2
 * \brief Q-Learning state representation
 *
 * State is defined by SNR level (0=poor, 1=medium, 2=good) and hop count (0-7).
 * Total state space: 3 x 8 = 24 states.
 */
struct QState
{
  uint8_t snrLevel;   ///< SNR level: 0=poor (<10dB), 1=medium (10-20dB), 2=good (>20dB)
  uint8_t hopCount;   ///< Hop count (0-7, truncated if >7)

  /**
   * \brief Default constructor
   */
  QState () : snrLevel (0), hopCount (0) {}

  /**
   * \brief Constructor with parameters
   * \param snr SNR level
   * \param hops Hop count
   */
  QState (uint8_t snr, uint8_t hops) : snrLevel (snr), hopCount (hops < 8 ? hops : 7) {}

  /**
   * \brief Encode state to a single integer for Q-table indexing
   * \return Encoded state value
   */
  uint32_t Encode () const
  {
    return snrLevel * 8 + hopCount;
  }

  /**
   * \brief Comparison operator for map key
   * \param other Other QState to compare
   * \return true if this < other
   */
  bool operator< (const QState& other) const
  {
    if (snrLevel != other.snrLevel)
      return snrLevel < other.snrLevel;
    return hopCount < other.hopCount;
  }

  /**
   * \brief Equality operator
   * \param other Other QState to compare
   * \return true if equal
   */
  bool operator== (const QState& other) const
  {
    return snrLevel == other.snrLevel && hopCount == other.hopCount;
  }
};

/**
 * \ingroup smartAodvQlearningV2
 * \brief Transmission statistics for reward calculation
 */
struct TransmissionStats
{
  bool success;       ///< Whether transmission was successful
  double snr;         ///< SNR value in dB
  uint8_t hops;       ///< Number of hops
  double delay;       ///< End-to-end delay in seconds

  /**
   * \brief Default constructor
   */
  TransmissionStats () : success (false), snr (0.0), hops (0), delay (0.0) {}

  /**
   * \brief Constructor with parameters
   * \param s Success flag
   * \param snrVal SNR value
   * \param hopCount Hop count
   * \param del Delay
   */
  TransmissionStats (bool s, double snrVal, uint8_t hopCount, double del = 0.0)
    : success (s), snr (snrVal), hops (hopCount), delay (del) {}
};

/**
 * \ingroup smartAodvQlearningV2
 * \brief Q-Learning agent for intelligent routing decisions
 *
 * Implements Q-learning algorithm with ε-greedy action selection.
 * Used for both path-level and next-hop-level routing decisions.
 */
class QLearning
{
public:
  /**
   * \brief Constructor
   * \param alpha Learning rate (0-1)
   * \param gamma Discount factor (0-1)
   * \param epsilon Exploration rate (0-1)
   */
  QLearning (double alpha = 0.1, double gamma = 0.9, double epsilon = 0.1);

  /**
   * \brief Destructor
   */
  ~QLearning ();

  /**
   * \brief Choose action using ε-greedy policy
   * \param state Current state
   * \param numActions Number of available actions
   * \return Selected action index (0 to numActions-1)
   */
  int ChooseAction (const QState& state, int numActions);

  /**
   * \brief Choose best action (no exploration, for production use)
   * \param state Current state
   * \param numActions Number of available actions
   * \return Best action index
   */
  int ChooseBestAction (const QState& state, int numActions);

  /**
   * \brief Update Q-value using Q-learning update rule
   * Q(s,a) = Q(s,a) + α * [r + γ * max(Q(s',a')) - Q(s,a)]
   * \param state Current state
   * \param action Action taken
   * \param nextState Resulting state
   * \param reward Reward received
   */
  void Update (const QState& state, int action, const QState& nextState, double reward);

  /**
   * \brief Get Q-value for state-action pair
   * \param state State
   * \param action Action
   * \return Q-value
   */
  double GetQValue (const QState& state, int action) const;

  /**
   * \brief Set Q-value for state-action pair
   * \param state State
   * \param action Action
   * \param value Q-value to set
   */
  void SetQValue (const QState& state, int action, double value);

  /**
   * \brief Calculate reward based on transmission statistics
   * \param stats Transmission statistics
   * \return Calculated reward value
   */
  double CalculateReward (const TransmissionStats& stats) const;

  /**
   * \brief Discretize SNR value to level
   * \param snr SNR value in dB
   * \return SNR level (0=poor, 1=medium, 2=good)
   */
  static uint8_t DiscretizeSnr (double snr);

  /**
   * \brief Create QState from continuous values
   * \param snr SNR value in dB
   * \param hops Hop count
   * \return QState object
   */
  static QState CreateState (double snr, uint8_t hops);

  /**
   * \brief Set learning rate
   * \param alpha New learning rate
   */
  void SetAlpha (double alpha) { m_alpha = alpha; }

  /**
   * \brief Get learning rate
   * \return Current learning rate
   */
  double GetAlpha () const { return m_alpha; }

  /**
   * \brief Set discount factor
   * \param gamma New discount factor
   */
  void SetGamma (double gamma) { m_gamma = gamma; }

  /**
   * \brief Get discount factor
   * \return Current discount factor
   */
  double GetGamma () const { return m_gamma; }

  /**
   * \brief Set exploration rate
   * \param epsilon New exploration rate
   */
  void SetEpsilon (double epsilon) { m_epsilon = epsilon; }

  /**
   * \brief Get exploration rate
   * \return Current exploration rate
   */
  double GetEpsilon () const { return m_epsilon; }

  /**
   * \brief Assign random variable stream
   * \param stream Stream number
   * \return Number of streams assigned
   */
  int64_t AssignStreams (int64_t stream);

  /**
   * \brief Clear Q-table
   */
  void Clear ();

  /**
   * \brief Get Q-table size
   * \return Number of entries in Q-table
   */
  size_t GetQTableSize () const { return m_qTable.size (); }

private:
  /**
   * \brief Get maximum Q-value for a state
   * \param state State to query
   * \param numActions Number of available actions
   * \return Maximum Q-value
   */
  double GetMaxQValue (const QState& state, int numActions) const;

  /**
   * \brief Encode state-action pair for Q-table key
   * \param state State
   * \param action Action
   * \return Encoded key
   */
  uint64_t EncodeStateAction (const QState& state, int action) const;

  double m_alpha;      ///< Learning rate
  double m_gamma;      ///< Discount factor
  double m_epsilon;    ///< Exploration rate for ε-greedy

  /// Q-table: maps encoded (state, action) pairs to Q-values
  std::map<uint64_t, double> m_qTable;

  /// Random variable for exploration
  Ptr<UniformRandomVariable> m_random;
};

/**
 * \ingroup smartAodvQlearningV2
 * \brief Q-Learning context for tracking state transitions
 *
 * Tracks the previous state and action for proper Q-value updates
 * when the result of an action is received.
 */
struct QContext
{
  QState previousState;   ///< State before action
  int previousAction;     ///< Action taken
  Ipv4Address destination;///< Destination address
  Time timestamp;         ///< When the action was taken

  /**
   * \brief Default constructor
   */
  QContext () : previousState (), previousAction (-1), destination (Ipv4Address ()), timestamp (Seconds (0)) {}

  /**
   * \brief Constructor with parameters
   * \param state Previous state
   * \param action Action taken
   * \param dst Destination address
   */
  QContext (const QState& state, int action, Ipv4Address dst)
    : previousState (state), previousAction (action), destination (dst), timestamp (Simulator::Now ()) {}

  /**
   * \brief Check if context is valid
   * \return true if valid
   */
  bool IsValid () const
  {
    return previousAction >= 0 && destination != Ipv4Address ();
  }

  /**
   * \brief Clear context
   */
  void Clear ()
  {
    previousState = QState ();
    previousAction = -1;
    destination = Ipv4Address ();
    timestamp = Seconds (0);
  }
};

} // namespace smartAodvQlearningV2
} // namespace ns3

#endif /* SMART_AODV_QLEARNING_QLEARNING_H */
