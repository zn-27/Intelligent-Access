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

#ifndef Q_SMART_HYBRID_ROUTING_PROTOCOL_H
#define Q_SMART_HYBRID_ROUTING_PROTOCOL_H

#include "q-smart-hybrid-rtable.h"
#include "q-smart-hybrid-qlearning.h"
#include "q-smart-hybrid-packet.h"
#include "ns3/node.h"
#include "ns3/random-variable-stream.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-tx-vector.h"
#include "ns3/wifi-phy-common.h"
#include "ns3/wifi-mac-queue-item.h"
#include "ns3/timer.h"
#include "ns3/traced-callback.h"
#include <map>
#include <list>
#include <deque>

namespace ns3
{

class WifiMacQueueItem;
enum WifiMacDropReason : uint8_t;

namespace olsr
{
class RoutingProtocol;  // Forward declaration for OLSR wrapper
}

namespace qSmartHybrid
{

// Callback type definitions from Ipv4RoutingProtocol
typedef Ipv4RoutingProtocol::UnicastForwardCallback UnicastForwardCallback;
typedef Ipv4RoutingProtocol::MulticastForwardCallback MulticastForwardCallback;
typedef Ipv4RoutingProtocol::LocalDeliverCallback LocalDeliverCallback;
typedef Ipv4RoutingProtocol::ErrorCallback ErrorCallback;

/**
 * \ingroup qSmartHybrid
 * \brief ID Cache for duplicate detection
 */
class IdCache
{
public:
  /**
   * \brief constructor
   * \param lifetime lifetime of entries
   */
  IdCache (Time lifetime) : m_lifetime (lifetime) {}

  /**
   * \brief Check if entry is duplicate
   * \param id the ID to check
   * \return true if duplicate
   */
  bool IsDuplicate (Ipv4Address addr, uint32_t id);

  /**
   * \brief Get size of cache
   * \return size
   */
  size_t GetSize ()
  {
    Purge ();
    return m_idCache.size ();
  }

private:
  struct UniqueId
  {
    Ipv4Address m_context;
    uint32_t m_id;
    Time m_expire;
  };

  struct IsExpired
  {
    bool operator() (const UniqueId& id) const
    {
      return id.m_expire < Simulator::Now ();
    }
  };

  void Purge ()
  {
    m_idCache.remove_if (IsExpired ());
  }

  std::list<UniqueId> m_idCache;
  Time m_lifetime;
};

/**
 * \ingroup qSmartHybrid
 * \brief Neighbor structure for link sensing
 */
class Neighbors
{
public:
  /**
   * \brief constructor
   * \param delay the delay time for neighbor expiration
   */
  Neighbors (Time delay);

  /**
   * \brief Check if neighbor exists
   * \param addr neighbor address
   * \return true if exists
   */
  bool IsNeighbor (Ipv4Address addr) const;

  /**
   * \brief Update neighbor expiry time
   * \param addr neighbor address
   * \param expiry expiry time
   */
  void Update (Ipv4Address addr, Time expiry);

  /**
   * \brief Update SNR for neighbor
   * \param addr neighbor address
   * \param snr SNR value
   */
  void UpdateSnr (Ipv4Address addr, double snr);

  /**
   * \brief Get SNR for neighbor
   * \param addr neighbor address
   * \return SNR value (0 if not found)
   */
  double GetSnr (Ipv4Address addr) const;

  /**
   * \brief Get mean SNR (sliding window)
   * \param addr neighbor address
   * \return mean SNR
   */
  double GetMeanSnr (Ipv4Address addr) const;

  /**
   * \brief Get SNR standard deviation
   * \param addr neighbor address
   * \return SNR standard deviation
   */
  double GetSnrStdDev (Ipv4Address addr) const;

  /**
   * \brief Purge expired neighbors
   */
  void Purge ();

  /**
   * \brief Schedule timer for purge
   */
  void ScheduleTimer ();

  /**
   * \brief Get neighbor list
   * \return list of neighbor addresses
   */
  std::vector<Ipv4Address> GetNeighbors () const;

  /**
   * \brief Get neighbor count
   * \return number of neighbors
   */
  uint32_t GetNbrCount () const
  {
    return m_nb.size ();
  }

private:
  struct Neighbor
  {
    Ipv4Address m_neighborAddress;
    Time m_expire;
    double m_lastSnr;
    std::vector<double> m_snrHistory;  // Sliding window for SNR stats
    static const size_t MAX_SNR_HISTORY = 10;
  };

  struct IsExpired
  {
    bool operator() (const Neighbor& nb) const
    {
      return nb.m_expire < Simulator::Now ();
    }
  };

  std::vector<Neighbor> m_nb;
  Timer m_ntimer;
  Time m_delay;
};

/**
 * \ingroup qSmartHybrid
 * \brief Request queue for buffering packets during route discovery
 */
class RequestQueue
{
public:
  /**
   * \brief constructor
   * \param maxLen maximum queue length
   * \param routeToQueueTimeout timeout for queue entries
   */
  RequestQueue (uint32_t maxLen, Time routeToQueueTimeout)
    : m_maxLen (maxLen), m_queueTimeout (routeToQueueTimeout)
  {
  }

  /**
   * \brief Check if queue is full
   * \return true if full
   */
  bool IsFull () const
  {
    return m_queue.size () >= m_maxLen;
  }

  /**
   * \brief Enqueue a packet
   * \param entry the queue entry
   * \return true if enqueued
   */
  bool Enqueue (Ipv4Header header, Ptr<const Packet> packet,
                UnicastForwardCallback ucb, ErrorCallback ecb);

  /**
   * \brief Dequeue packets for a destination
   * \param dst the destination
   * \param entries list of entries to fill
   */
  void Dequeue (Ipv4Address dst, std::vector<std::tuple<Ipv4Header, Ptr<const Packet>,
                                                        UnicastForwardCallback, ErrorCallback>>& entries);

  /**
   * \brief Check if entry exists for destination
   * \param dst the destination
   * \return true if exists
   */
  bool Find (Ipv4Address dst) const;

  /**
   * \brief Get queue size
   * \return size
   */
  uint32_t GetSize () const
  {
    return m_queue.size ();
  }

  /**
   * \brief Purge expired entries
   */
  void Purge ();

private:
  struct QueueEntry
  {
    Ipv4Header m_header;
    Ptr<const Packet> m_packet;
    UnicastForwardCallback m_ucb;
    ErrorCallback m_ecb;
    Time m_expire;

    bool IsExpired () const
    {
      return m_expire < Simulator::Now ();
    }
  };

  std::deque<QueueEntry> m_queue;
  uint32_t m_maxLen;
  Time m_queueTimeout;
};

/**
 * \ingroup qSmartHybrid
 * \brief Q-Smart-Hybrid routing protocol
 *
 * Main routing protocol that combines:
 * - OLSR (proactive routing) with adjustable frequency
 * - Smart-AODV (reactive routing) with link quality awareness
 * - Q-Learning for intelligent mode switching
 * - MAC layer cross-layer feedback
 */
class RoutingProtocol : public Ipv4RoutingProtocol
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId ();

  static const uint32_t QSH_PORT;

  /// constructor
  RoutingProtocol ();
  virtual ~RoutingProtocol ();
  virtual void DoDispose ();

  // Inherited from Ipv4RoutingProtocol
  Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, const Ipv4Header& header,
                              Ptr<NetDevice> oif, Socket::SocketErrno& sockerr);
  bool RouteInput (Ptr<const Packet> p, const Ipv4Header& header,
                   Ptr<const NetDevice> idev, UnicastForwardCallback ucb,
                   MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                   ErrorCallback ecb);
  virtual void NotifyInterfaceUp (uint32_t interface);
  virtual void NotifyInterfaceDown (uint32_t interface);
  virtual void NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address);
  virtual void SetIpv4 (Ptr<Ipv4> ipv4);
  virtual void PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;

  // Protocol parameters
  /**
   * \brief Get maximum queue time
   * \return The maximum queue time
   */
  Time GetMaxQueueTime () const { return m_maxQueueTime; }

  /**
   * \brief Set maximum queue time
   * \param t The maximum queue time
   */
  void SetMaxQueueTime (Time t);

  /**
   * \brief Get maximum queue length
   * \return The maximum queue length
   */
  uint32_t GetMaxQueueLen () const { return m_maxQueueLen; }

  /**
   * \brief Set maximum queue length
   * \param len The maximum queue length
   */
  void SetMaxQueueLen (uint32_t len);

  /**
   * \brief Get Q-Learning decision interval
   * \return The decision interval
   */
  Time GetQlearningInterval () const { return m_qlearningInterval; }

  /**
   * \brief Set Q-Learning decision interval
   * \param interval The decision interval
   */
  void SetQlearningInterval (Time interval);

  /**
   * \brief Assign random variable stream
   * \param stream first stream index
   * \return number of streams assigned
   */
  int64_t AssignStreams (int64_t stream);

  /**
   * \brief Get current action
   * \return Current action
   */
  Action GetCurrentAction () const { return m_currentDecision.baseAction; }

  /**
   * \brief Get current transition factor
   * \return Transition factor
   */
  float GetTransitionFactor () const { return m_currentDecision.transitionFactor; }

protected:
  virtual void DoInitialize (void);

private:
  // ===== MAC Layer Cross-layer Callbacks =====
  /**
   * \brief Notify that an MPDU was dropped
   * \param reason the reason why the MPDU was dropped
   * \param mpdu the dropped MPDU
   */
  void NotifyTxError (WifiMacDropReason reason, Ptr<const WifiMacQueueItem> mpdu);

  /**
   * \brief Monitor physical layer reception to get RSSI/SNR
   * \param packet the received packet
   * \param channelFreqMhz channel frequency in MHz
   * \param txVector transmit vector
   * \param mpduInfo MPDU info
   * \param snr signal and noise values
   * \param staId station ID
   */
  void PhyRxStats (Ptr<const Packet> packet, uint16_t channelFreqMhz,
                   WifiTxVector txVector, MpduInfo mpduInfo,
                   SignalNoiseDbm snr, uint16_t staId);

  // ===== OLSR Integration =====
  /**
   * \brief Get actual Hello interval based on current action
   * \return Hello interval
   */
  Time GetActualHelloInterval () const;

  /**
   * \brief Get actual TC interval based on current action
   * \return TC interval
   */
  Time GetActualTcInterval () const;

  /**
   * \brief Apply action to OLSR parameters
   * \param decision The Q-Learning decision
   */
  void ApplyAction (const QLearningDecision& decision);

  /**
   * \brief Update transition state
   */
  void UpdateTransition ();

  // ===== Q-Learning =====
  /**
   * \brief Collect current state for Q-Learning
   * \return Current QState
   */
  QState CollectState ();

  /**
   * \brief Collect performance metrics
   * \return PerformanceMetrics
   */
  PerformanceMetrics CollectMetrics ();

  /**
   * \brief Q-Learning decision timer handler
   */
  void QlearningTimerExpire ();

  /**
   * \brief Update performance statistics
   */
  void UpdatePerformanceStats ();

  // ===== MAC Layer Cross-layer =====
  /**
   * \brief Check if route should be force invalidated
   * \param nextHop Next hop address
   * \return true if should invalidate
   */
  bool ShouldForceInvalidateRoute (Ipv4Address nextHop);

  /**
   * \brief Handle MAC layer failure
   * \param nextHop Next hop address
   */
  void OnMacLayerFailure (Ipv4Address nextHop);

  /**
   * \brief Send preemptive RREQ
   * \param destination Destination address
   */
  void SendPreemptiveRreq (Ipv4Address destination);

  /**
   * \brief Predict link expiry time based on RSSI
   * \param rssi RSSI value
   * \return Predicted expiry time
   */
  Time PredictLinkExpiry (double rssi);

  // ===== AODV Core Functions =====
  /**
   * \brief Start protocol operation
   */
  void Start ();

  /**
   * \brief Deferred route output
   */
  void DeferredRouteOutput (Ptr<const Packet> p, const Ipv4Header& header,
                            UnicastForwardCallback ucb, ErrorCallback ecb);

  /**
   * \brief Forward packet
   */
  bool Forwarding (Ptr<const Packet> p, const Ipv4Header& header,
                   UnicastForwardCallback ucb, ErrorCallback ecb);

  /**
   * \brief Schedule RREQ retry
   */
  void ScheduleRreqRetry (Ipv4Address dst);

  /**
   * \brief Update route lifetime
   */
  bool UpdateRouteLifeTime (Ipv4Address addr, Time lt);

  /**
   * \brief Update route to neighbor
   */
  void UpdateRouteToNeighbor (Ipv4Address sender, Ipv4Address receiver);

  /**
   * \brief Check if own address
   */
  bool IsMyOwnAddress (Ipv4Address src);

  /**
   * \brief Find socket with interface address
   */
  Ptr<Socket> FindSocketWithInterfaceAddress (Ipv4InterfaceAddress iface) const;

  /**
   * \brief Find subnet broadcast socket
   */
  Ptr<Socket> FindSubnetBroadcastSocketWithInterfaceAddress (Ipv4InterfaceAddress iface) const;

  /**
   * \brief Process hello message
   */
  void ProcessHello (RrepHeader const& rrepHeader, Ipv4Address receiverIfaceAddr);

  /**
   * \brief Create loopback route
   */
  Ptr<Ipv4Route> LoopbackRoute (const Ipv4Header& header, Ptr<NetDevice> oif) const;

  // ===== Receive handlers =====
  /**
   * \brief Receive and process control packet
   */
  void RecvQsh (Ptr<Socket> socket);

  /**
   * \brief Receive RREQ
   */
  void RecvRequest (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);

  /**
   * \brief Receive RREP
   */
  void RecvReply (Ptr<Packet> p, Ipv4Address my, Ipv4Address src);

  /**
   * \brief Receive RREP_ACK
   */
  void RecvReplyAck (Ipv4Address neighbor);

  /**
   * \brief Receive RERR
   */
  void RecvError (Ptr<Packet> p, Ipv4Address src);

  // ===== Send handlers =====
  /**
   * \brief Send packet from queue
   */
  void SendPacketFromQueue (Ipv4Address dst, Ptr<Ipv4Route> route);

  /**
   * \brief Send hello
   */
  void SendHello ();

  /**
   * \brief Send RREQ
   */
  void SendRequest (Ipv4Address dst);

  /**
   * \brief Send RREP
   */
  void SendReply (RreqHeader const& rreqHeader, RoutingTableEntry const& toOrigin);

  /**
   * \brief Send RREP by intermediate node
   */
  void SendReplyByIntermediateNode (RoutingTableEntry& toDst, RoutingTableEntry& toOrigin, bool gratRep);

  /**
   * \brief Send RREP_ACK
   */
  void SendReplyAck (Ipv4Address neighbor);

  /**
   * \brief Send RERR when link breaks
   */
  void SendRerrWhenBreaksLinkToNextHop (Ipv4Address nextHop);

  /**
   * \brief Send RERR message
   */
  void SendRerrMessage (Ptr<Packet> packet, std::vector<Ipv4Address> precursors);

  /**
   * \brief Send RERR when no route
   */
  void SendRerrWhenNoRouteToForward (Ipv4Address dst, uint32_t dstSeqNo, Ipv4Address origin);

  /**
   * \brief Send to socket
   */
  void SendTo (Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination);

  // ===== Timers =====
  /// Hello timer
  Timer m_htimer;
  void HelloTimerExpire ();

  /// RREQ rate limit timer
  Timer m_rreqRateLimitTimer;
  void RreqRateLimitTimerExpire ();

  /// RERR rate limit timer
  Timer m_rerrRateLimitTimer;
  void RerrRateLimitTimerExpire ();

  /// Route request timer map
  std::map<Ipv4Address, Timer> m_addressReqTimer;
  void RouteRequestTimerExpire (Ipv4Address dst);

  /// ACK timer
  void AckTimerExpire (Ipv4Address neighbor, Time blacklistTimeout);

  /// Q-Learning timer
  Timer m_qlearningTimer;

  /// Transition update timer
  Timer m_transitionTimer;
  void TransitionTimerExpire ();

  // ===== Protocol Parameters =====
  uint32_t m_rreqRetries;       ///< Maximum RREQ retries
  uint16_t m_ttlStart;          ///< Initial TTL
  uint16_t m_ttlIncrement;      ///< TTL increment
  uint16_t m_ttlThreshold;      ///< TTL threshold
  uint16_t m_timeoutBuffer;     ///< Timeout buffer
  uint16_t m_rreqRateLimit;     ///< RREQ rate limit
  uint16_t m_rerrRateLimit;     ///< RERR rate limit
  Time m_activeRouteTimeout;    ///< Active route timeout
  uint32_t m_netDiameter;       ///< Network diameter
  Time m_nodeTraversalTime;     ///< Node traversal time
  Time m_netTraversalTime;      ///< Network traversal time
  Time m_pathDiscoveryTime;     ///< Path discovery time
  Time m_myRouteTimeout;        ///< My route timeout
  Time m_helloInterval;         ///< Hello interval (base)
  uint32_t m_allowedHelloLoss;  ///< Allowed hello loss
  Time m_deletePeriod;          ///< Delete period
  Time m_nextHopWait;           ///< Next hop wait time
  Time m_blackListTimeout;      ///< Blacklist timeout
  uint32_t m_maxQueueLen;       ///< Maximum queue length
  Time m_maxQueueTime;          ///< Maximum queue time
  bool m_destinationOnly;       ///< Destination only flag
  bool m_gratuitousReply;       ///< Gratuitous reply flag
  bool m_enableHello;           ///< Enable hello messages
  bool m_enableBroadcast;       ///< Enable broadcast
  Time m_qlearningInterval;     ///< Q-Learning decision interval

  // ===== Core Components =====
  Ptr<Ipv4> m_ipv4;             ///< IPv4 object
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketSubnetBroadcastAddresses;
  Ptr<NetDevice> m_lo;          ///< Loopback device

  RoutingTable m_routingTable;  ///< Unified routing table
  RequestQueue m_queue;         ///< Request queue
  Neighbors m_nb;               ///< Neighbors
  IdCache m_rreqIdCache;        ///< RREQ ID cache

  uint32_t m_requestId;         ///< Request ID counter
  uint32_t m_seqNo;             ///< Sequence number
  uint16_t m_rreqCount;         ///< RREQ count
  uint16_t m_rerrCount;         ///< RERR count

  // ===== Q-Learning Components =====
  QLearning m_qlearning;                ///< Q-Learning engine
  QLearningDecision m_currentDecision;  ///< Current decision
  QState m_prevState;                   ///< Previous state for Q-update

  // ===== Performance Statistics =====
  uint32_t m_txDataPackets;     ///< Transmitted data packets
  uint32_t m_rxDataPackets;     ///< Received data packets
  uint32_t m_txControlPackets;  ///< Transmitted control packets
  double m_totalDelay;          ///< Total delay sum
  Time m_lastMetricUpdate;      ///< Last metrics update time

  // ===== State Tracking =====
  double m_prevNeighborCount;   ///< Previous neighbor count
  Time m_lastNeighborCheck;     ///< Last neighbor check time
  double m_neighborChangeRate;  ///< Neighbor change rate

  // ===== Random Variable =====
  Ptr<UniformRandomVariable> m_uniformRandomVariable;
  Time m_lastBcastTime;

  // ===== Traced Callbacks =====
  /// Trace callback for packet Tx/Rx
  TracedCallback <Ptr<const Packet>> m_txPacketTrace;
  TracedCallback <Ptr<const Packet>> m_rxPacketTrace;
  /// Trace callback for routing table changes
  TracedCallback <uint32_t> m_routingTableChanged;
  /// Trace callback for action changes
  TracedCallback <Action, Action, float> m_actionChanged;
};

} // namespace qSmartHybrid
} // namespace ns3

#endif /* Q_SMART_HYBRID_ROUTING_PROTOCOL_H */
