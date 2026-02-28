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
 *
 * Authors: Q-Smart-Hybrid Implementation
 */

#ifndef Q_SMART_HYBRID_QLEARNING_H
#define Q_SMART_HYBRID_QLEARNING_H

#include <stdint.h>
#include <map>
#include <array>
#include <cmath>
#include "ns3/nstime.h"
#include "ns3/ipv4-address.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include "ns3/ptr.h"

namespace ns3
{
namespace qSmartHybrid
{

/**
 * \ingroup qSmartHybrid
 * \brief Action enumeration for Q-Learning
 *
 * Four discrete actions controlling OLSR behavior:
 * - A1: Full proactive (Hello=1s, TC=3s)
 * - A2: Weak proactive (Hello=2s, TC=8s)
 * - A3: Local proactive (Hello=5s, TC=30s)
 * - A4: Pure reactive (Stop OLSR, only MPR for RREQ)
 */
enum Action
{
  A1_FULL_PROACTIVE = 0,    ///< Complete OLSR mode
  A2_WEAK_PROACTIVE = 1,    ///< Reduced OLSR frequency
  A3_LOCAL_PROACTIVE = 2,   ///< Minimal OLSR (1-hop only)
  A4_PURE_REACTIVE = 3      ///< Pure Smart-AODV mode
};

/// Number of actions
const int NUM_ACTIONS = 4;

/**
 * \ingroup qSmartHybrid
 * \brief Q-Learning state representation (5D state space)
 *
 * State is defined by:
 * - nodeSpeed: Node movement speed (0-4 levels)
 * - neighborChangeRate: Rate of neighbor changes (0-4 levels)
 * - currentPdr: Current PDR (0-4 levels)
 * - snrVariance: SNR variance/stability (0-4 levels)
 * - queueLength: Interface queue length (0-4 levels)
 *
 * Total state space: 5^5 = 3125 states
 */
struct QState
{
  uint8_t nodeSpeed;           ///< Node speed level (0-4)
  uint8_t neighborChangeRate;  ///< Neighbor change rate level (0-4)
  uint8_t currentPdr;          ///< PDR level (0-4)
  uint8_t snrVariance;         ///< SNR variance level (0-4)
  uint8_t queueLength;         ///< Queue length level (0-4)

  /**
   * \brief Default constructor
   */
  QState ()
    : nodeSpeed (0), neighborChangeRate (0), currentPdr (0), snrVariance (0), queueLength (0)
  {
  }

  /**
   * \brief Constructor with parameters
   * \param speed Node speed level
   * \param ncr Neighbor change rate level
   * \param pdr PDR level
   * \param snrVar SNR variance level
   * \param queue Queue length level
   */
  QState (uint8_t speed, uint8_t ncr, uint8_t pdr, uint8_t snrVar, uint8_t queue)
    : nodeSpeed (speed < 5 ? speed : 4),
      neighborChangeRate (ncr < 5 ? ncr : 4),
      currentPdr (pdr < 5 ? pdr : 4),
      snrVariance (snrVar < 5 ? snrVar : 4),
      queueLength (queue < 5 ? queue : 4)
  {
  }

  /**
   * \brief Encode state to a single integer for Q-table indexing
   * \return Encoded state value
   */
  uint32_t Encode () const
  {
    return nodeSpeed * 625 + neighborChangeRate * 125 + currentPdr * 25 + snrVariance * 5 + queueLength;
  }

  /**
   * \brief Comparison operator for map key
   * \param other Other QState to compare
   * \return true if this < other
   */
  bool operator< (const QState& other) const
  {
    return Encode () < other.Encode ();
  }

  /**
   * \brief Equality operator
   * \param other Other QState to compare
   * \return true if equal
   */
  bool operator== (const QState& other) const
  {
    return nodeSpeed == other.nodeSpeed &&
           neighborChangeRate == other.neighborChangeRate &&
           currentPdr == other.currentPdr &&
           snrVariance == other.snrVariance &&
           queueLength == other.queueLength;
  }
};

/**
 * \ingroup qSmartHybrid
 * \brief Performance metrics for reward calculation
 */
struct PerformanceMetrics
{
  double pdr;              ///< Packet delivery ratio [0,1]
  double avgDelay;         ///< Average end-to-end delay (seconds)
  uint32_t controlPackets; ///< Number of control packets sent

  /**
   * \brief Default constructor
   */
  PerformanceMetrics ()
    : pdr (0.0), avgDelay (0.0), controlPackets (0)
  {
  }
};

/**
 * \ingroup qSmartHybrid
 * \brief Q-Learning decision structure
 *
 * Contains the current action state including transition information
 * for smooth action switching.
 */
struct QLearningDecision
{
  Action baseAction;        ///< Current base action
  Action targetAction;      ///< Target action (for transition)
  float transitionFactor;   ///< Transition factor [0,1], 1 = fully at baseAction

  /**
   * \brief Default constructor - starts at A2 (weak proactive)
   */
  QLearningDecision ()
    : baseAction (A2_WEAK_PROACTIVE),
      targetAction (A2_WEAK_PROACTIVE),
      transitionFactor (1.0f)
  {
  }

  /**
   * \brief Check if in transition
   * \return true if transitioning between actions
   */
  bool InTransition () const
  {
    return baseAction != targetAction;
  }
};

/**
 * \ingroup qSmartHybrid
 * \brief Q-Learning agent for hybrid routing decisions
 *
 * Implements Q-learning algorithm with:
 * - 5D state space
 * - 4 discrete actions with smooth transition
 * - Threshold-based action switching (hysteresis)
 * - Weighted normalized reward function
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
   * \brief Choose action using epsilon-greedy policy
   * \param state Current state
   * \return Selected action
   */
  Action ChooseAction (const QState& state);

  /**
   * \brief Choose best action (no exploration, for production use)
   * \param state Current state
   * \return Best action
   */
  Action ChooseBestAction (const QState& state) const;

  /**
   * \brief Check if should switch action (with hysteresis)
   * \param currentAction Current action
   * \param candidateAction Candidate new action
   * \param qCandidate Q-value of candidate
   * \param qCurrent Q-value of current
   * \return true if should switch
   */
  bool ShouldSwitchAction (Action currentAction, Action candidateAction,
                           double qCandidate, double qCurrent) const;

  /**
   * \brief Update Q-value using Q-learning update rule
   * Q(s,a) = Q(s,a) + alpha * [r + gamma * max(Q(s',a')) - Q(s,a)]
   * \param state Current state
   * \param action Action taken
   * \param nextState Resulting state
   * \param reward Reward received
   */
  void Update (const QState& state, Action action, const QState& nextState, double reward);

  /**
   * \brief Get Q-value for state-action pair
   * \param state State
   * \param action Action
   * \return Q-value
   */
  double GetQValue (const QState& state, Action action) const;

  /**
   * \brief Set Q-value for state-action pair
   * \param state State
   * \param action Action
   * \param value Q-value to set
   */
  void SetQValue (const QState& state, Action action, double value);

  /**
   * \brief Calculate reward based on performance metrics
   * Weighted: PDR 50% - Delay 30% - Overhead 20%
   * \param metrics Performance metrics
   * \return Calculated reward value
   */
  double CalculateReward (const PerformanceMetrics& metrics) const;

  /**
   * \brief Discretize continuous value to level (0-4)
   * \param value Continuous value
   * \param thresholds Array of 4 threshold values
   * \return Discrete level (0-4)
   */
  static uint8_t Discretize (double value, const double thresholds[4]);

  /**
   * \brief Create QState from continuous values
   * \param nodeSpeed Node speed in m/s
   * \param neighborChangeRate Neighbor change rate
   * \param pdr PDR value [0,1]
   * \param snrVariance SNR variance
   * \param queueLength Queue length
   * \return QState object
   */
  static QState CreateState (double nodeSpeed, double neighborChangeRate,
                             double pdr, double snrVariance, uint32_t queueLength);

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
   * \brief Set switch threshold (hysteresis)
   * \param threshold New threshold
   */
  void SetSwitchThreshold (double threshold) { m_switchThreshold = threshold; }

  /**
   * \brief Get switch threshold
   * \return Current switch threshold
   */
  double GetSwitchThreshold () const { return m_switchThreshold; }

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
   * \return Maximum Q-value
   */
  double GetMaxQValue (const QState& state) const;

  /**
   * \brief Get action with maximum Q-value for a state
   * \param state State to query
   * \return Action with maximum Q-value
   */
  Action GetBestAction (const QState& state) const;

  /**
   * \brief Encode state-action pair for Q-table key
   * \param state State
   * \param action Action
   * \return Encoded key
   */
  uint64_t EncodeStateAction (const QState& state, Action action) const;

  double m_alpha;            ///< Learning rate
  double m_gamma;            ///< Discount factor
  double m_epsilon;          ///< Exploration rate for epsilon-greedy
  double m_switchThreshold;  ///< Threshold for action switching (hysteresis)

  /// Q-table: maps encoded (state, action) pairs to Q-values
  std::map<uint64_t, double> m_qTable;

  /// Random variable for exploration
  Ptr<UniformRandomVariable> m_random;

  // Discretization thresholds
  static const double SPEED_THRESHOLDS[4];          ///< Speed thresholds (m/s)
  static const double NCR_THRESHOLDS[4];            ///< Neighbor change rate thresholds
  static const double PDR_THRESHOLDS[4];            ///< PDR thresholds
  static const double SNR_VAR_THRESHOLDS[4];        ///< SNR variance thresholds
  static const double QUEUE_THRESHOLDS[4];          ///< Queue length thresholds
};

/**
 * \ingroup qSmartHybrid
 * \brief Action configuration structure
 *
 * Contains the OLSR parameter settings for each action.
 */
struct ActionConfig
{
  Time helloInterval;  ///< Hello message interval
  Time tcInterval;     ///< TC message interval
  bool mprEnabled;     ///< Whether MPR is enabled
  bool tcEnabled;      ///< Whether TC messages are sent

  /**
   * \brief Default constructor
   */
  ActionConfig ()
    : helloInterval (Seconds (2)), tcInterval (Seconds (8)), mprEnabled (true), tcEnabled (true)
  {
  }
};

/**
 * \ingroup qSmartHybrid
 * \brief Get action configuration for a given action
 * \param action The action
 * \return Configuration for the action
 */
inline ActionConfig GetActionConfig (Action action)
{
  ActionConfig config;
  switch (action)
  {
    case A1_FULL_PROACTIVE:
      config.helloInterval = Seconds (1);
      config.tcInterval = Seconds (3);
      config.mprEnabled = true;
      config.tcEnabled = true;
      break;
    case A2_WEAK_PROACTIVE:
      config.helloInterval = Seconds (2);
      config.tcInterval = Seconds (8);
      config.mprEnabled = true;
      config.tcEnabled = true;
      break;
    case A3_LOCAL_PROACTIVE:
      config.helloInterval = Seconds (5);
      config.tcInterval = Seconds (30);
      config.mprEnabled = true;
      config.tcEnabled = true;
      break;
    case A4_PURE_REACTIVE:
      config.helloInterval = Time::Max ();  // Stop Hello
      config.tcInterval = Time::Max ();     // Stop TC
      config.mprEnabled = true;             // Keep MPR for RREQ
      config.tcEnabled = false;
      break;
  }
  return config;
}

/**
 * \ingroup qSmartHybrid
 * \brief Calculate actual interval with transition factor
 * \param baseAction Base action
 * \param targetAction Target action
 * \param transitionFactor Transition factor [0,1]
 * \param isHello True for Hello interval, false for TC
 * \return Actual interval
 */
inline Time GetActualInterval (Action baseAction, Action targetAction,
                               float transitionFactor, bool isHello)
{
  ActionConfig baseConfig = GetActionConfig (baseAction);
  ActionConfig targetConfig = GetActionConfig (targetAction);

  Time baseInterval = isHello ? baseConfig.helloInterval : baseConfig.tcInterval;
  Time targetInterval = isHello ? targetConfig.helloInterval : targetConfig.tcInterval;

  // Handle Time::Max() case
  if (baseInterval == Time::Max () && targetInterval == Time::Max ())
  {
    return Time::Max ();
  }
  if (baseInterval == Time::Max ())
  {
    baseInterval = Seconds (10);  // Use a large value for interpolation
  }
  if (targetInterval == Time::Max ())
  {
    targetInterval = Seconds (10);
  }

  double actual = baseInterval.GetSeconds () * transitionFactor +
                  targetInterval.GetSeconds () * (1.0 - transitionFactor);

  return Seconds (actual);
}

} // namespace qSmartHybrid
} // namespace ns3

#endif /* Q_SMART_HYBRID_QLEARNING_H */
