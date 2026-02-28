/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Based on
 *      NS-2 AODV model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      AODV-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/AODV-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
#ifndef AODV_V2ROUTINGPROTOCOL_H
#define AODV_V2ROUTINGPROTOCOL_H

#include "smart-aodv-v2-rtable.h"
#include "smart-aodv-v2-rqueue.h"
#include "smart-aodv-v2-packet.h"
#include "smart-aodv-v2-neighbor.h"
#include "smart-aodv-v2-dpd.h"
#include "smart-aodv-v2-qlearning.h"
#include "smart-aodv-v2-cluster.h"
#include "ns3/node.h"
#include "ns3/random-variable-stream.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-tx-vector.h"
#include <map>
// 确保包含此头文件以识别 MpduInfo 和 SignalNoiseDbm
#include "ns3/wifi-phy-common.h"

namespace ns3
{

  class WifiMacQueueItem;
  enum WifiMacDropReason : uint8_t; // opaque enum declaration

  namespace smartAodvV2
  {
    /**
     * \ingroup aodv
     *
     * \brief AODV routing protocol
     */
    class RoutingProtocol : public Ipv4RoutingProtocol
    {
    public:
      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId(void);
      static const uint32_t AODV_PORT;

      /// constructor
      RoutingProtocol();
      virtual ~RoutingProtocol();
      virtual void DoDispose();

      // Inherited from Ipv4RoutingProtocol
      Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
      bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                      UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                      LocalDeliverCallback lcb, ErrorCallback ecb);
      virtual void NotifyInterfaceUp(uint32_t interface);
      virtual void NotifyInterfaceDown(uint32_t interface);
      virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
      virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);
      virtual void SetIpv4(Ptr<Ipv4> ipv4);
      virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;

      // Handle protocol parameters
      /**
       * Get maximum queue time
       * \returns the maximum queue time
       */
      Time GetMaxQueueTime() const
      {
        return m_maxQueueTime;
      }
      /**
       * Set the maximum queue time
       * \param t the maximum queue time
       */
      void SetMaxQueueTime(Time t);
      /**
       * Get the maximum queue length
       * \returns the maximum queue length
       */
      uint32_t GetMaxQueueLen() const
      {
        return m_maxQueueLen;
      }
      /**
       * Set the maximum queue length
       * \param len the maximum queue length
       */
      void SetMaxQueueLen(uint32_t len);
      /**
       * Get destination only flag
       * \returns the destination only flag
       */
      bool GetDestinationOnlyFlag() const
      {
        return m_destinationOnly;
      }
      /**
       * Set destination only flag
       * \param f the destination only flag
       */
      void SetDestinationOnlyFlag(bool f)
      {
        m_destinationOnly = f;
      }
      /**
       * Get gratuitous reply flag
       * \returns the gratuitous reply flag
       */
      bool GetGratuitousReplyFlag() const
      {
        return m_gratuitousReply;
      }
      /**
       * Set gratuitous reply flag
       * \param f the gratuitous reply flag
       */
      void SetGratuitousReplyFlag(bool f)
      {
        m_gratuitousReply = f;
      }
      /**
       * Set hello enable
       * \param f the hello enable flag
       */
      void SetHelloEnable(bool f)
      {
        m_enableHello = f;
      }
      /**
       * Get hello enable flag
       * \returns the enable hello flag
       */
      bool GetHelloEnable() const
      {
        return m_enableHello;
      }
      /**
       * Set broadcast enable flag
       * \param f enable broadcast flag
       */
      void SetBroadcastEnable(bool f)
      {
        m_enableBroadcast = f;
      }
      /**
       * Get broadcast enable flag
       * \returns the broadcast enable flag
       */
      bool GetBroadcastEnable() const
      {
        return m_enableBroadcast;
      }

      /**
       * Assign a fixed random variable stream number to the random variables
       * used by this model.  Return the number of streams (possibly zero) that
       * have been assigned.
       *
       * \param stream first stream index to use
       * \return the number of stream indices assigned by this model
       */
      int64_t AssignStreams(int64_t stream);

    protected:
      virtual void DoInitialize(void);

    private:
      /**
       * Notify that an MPDU was dropped.
       *
       * \param reason the reason why the MPDU was dropped
       * \param mpdu the dropped MPDU
       */
      void NotifyTxError(WifiMacDropReason reason, Ptr<const WifiMacQueueItem> mpdu);

      // Protocol parameters.
      uint32_t m_rreqRetries;    ///< Maximum number of retransmissions of RREQ with TTL = NetDiameter to discover a route
      uint16_t m_ttlStart;       ///< Initial TTL value for RREQ.
      uint16_t m_ttlIncrement;   ///< TTL increment for each attempt using the expanding ring search for RREQ dissemination.
      uint16_t m_ttlThreshold;   ///< Maximum TTL value for expanding ring search, TTL = NetDiameter is used beyond this value.
      uint16_t m_timeoutBuffer;  ///< Provide a buffer for the timeout.
      uint16_t m_rreqRateLimit;  ///< Maximum number of RREQ per second.
      uint16_t m_rerrRateLimit;  ///< Maximum number of REER per second.
      Time m_activeRouteTimeout; ///< Period of time during which the route is considered to be valid.
      uint32_t m_netDiameter;    ///< Net diameter measures the maximum possible number of hops between two nodes in the network
      /**
       *  NodeTraversalTime is a conservative estimate of the average one hop traversal time for packets
       *  and should include queuing delays, interrupt processing times and transfer times.
       */
      Time m_nodeTraversalTime;
      Time m_netTraversalTime;  ///< Estimate of the average net traversal time.
      Time m_pathDiscoveryTime; ///< Estimate of maximum time needed to find route in network.
      Time m_myRouteTimeout;    ///< Value of lifetime field in RREP generating by this node.
      /**
       * Every HelloInterval the node checks whether it has sent a broadcast  within the last HelloInterval.
       * If it has not, it MAY broadcast a  Hello message
       */
      Time m_helloInterval;
      uint32_t m_allowedHelloLoss; ///< Number of hello messages which may be loss for valid link
      /**
       * DeletePeriod is intended to provide an upper bound on the time for which an upstream node A
       * can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D.
       */
      Time m_deletePeriod;
      Time m_nextHopWait;      ///< Period of our waiting for the neighbour's RREP_ACK
      Time m_blackListTimeout; ///< Time for which the node is put into the blacklist
      uint32_t m_maxQueueLen;  ///< The maximum number of packets that we allow a routing protocol to buffer.
      Time m_maxQueueTime;     ///< The maximum period of time that a routing protocol is allowed to buffer a packet for.
      bool m_destinationOnly;  ///< Indicates only the destination may respond to this RREQ.
      bool m_gratuitousReply;  ///< Indicates whether a gratuitous RREP should be unicast to the node originated route discovery.
      bool m_enableHello;      ///< Indicates whether a hello messages enable
      bool m_enableBroadcast;  ///< Indicates whether a a broadcast data packets forwarding enable
      //\}

      /// IP protocol
      Ptr<Ipv4> m_ipv4;
      /// Raw unicast socket per each IP interface, map socket -> iface address (IP + mask)
      std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
      /// Raw subnet directed broadcast socket per each IP interface, map socket -> iface address (IP + mask)
      std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketSubnetBroadcastAddresses;
      /// Loopback device used to defer RREQ until packet will be fully formed
      Ptr<NetDevice> m_lo;

      /// Routing table
      RoutingTable m_routingTable;
      /// A "drop-front" queue used by the routing layer to buffer packets to which it does not have a route.
      RequestQueue m_queue;
      /// Broadcast ID
      uint32_t m_requestId;
      /// Request sequence number
      uint32_t m_seqNo;
      /// Handle duplicated RREQ
      IdCache m_rreqIdCache;
      /// Handle duplicated broadcast/multicast packets
      DuplicatePacketDetection m_dpd;
      /// Handle neighbors
      Neighbors m_nb;
      /// Number of RREQs used for RREQ rate control
      uint16_t m_rreqCount;
      /// Number of RERRs used for RERR rate control
      uint16_t m_rerrCount;

    private:
      /// Start protocol operation
      void Start();
      /**
       * Queue packet and send route request
       *
       * \param p the packet to route
       * \param header the IP header
       * \param ucb the UnicastForwardCallback function
       * \param ecb the ErrorCallback function
       */
      void DeferredRouteOutput(Ptr<const Packet> p, const Ipv4Header &header, UnicastForwardCallback ucb, ErrorCallback ecb);
      /**
       * If route exists and is valid, forward packet.
       *
       * \param p the packet to route
       * \param header the IP header
       * \param ucb the UnicastForwardCallback function
       * \param ecb the ErrorCallback function
       * \returns true if forwarded
       */
      bool Forwarding(Ptr<const Packet> p, const Ipv4Header &header, UnicastForwardCallback ucb, ErrorCallback ecb);
      /**
       * Repeated attempts by a source node at route discovery for a single destination
       * use the expanding ring search technique.
       * \param dst the destination IP address
       */
      void ScheduleRreqRetry(Ipv4Address dst);
      /**
       * Set lifetime field in routing table entry to the maximum of existing lifetime and lt, if the entry exists
       * \param addr - destination address
       * \param lt - proposed time for lifetime field in routing table entry for destination with address addr.
       * \return true if route to destination address addr exist
       */
      bool UpdateRouteLifeTime(Ipv4Address addr, Time lt);
      /**
       * Update neighbor record.
       * \param receiver is supposed to be my interface
       * \param sender is supposed to be IP address of my neighbor.
       */
      void UpdateRouteToNeighbor(Ipv4Address sender, Ipv4Address receiver);
      /**
       * Test whether the provided address is assigned to an interface on this node
       * \param src the source IP address
       * \returns true if the IP address is the node's IP address
       */
      bool IsMyOwnAddress(Ipv4Address src);
      /**
       * Find unicast socket with local interface address iface
       *
       * \param iface the interface
       * \returns the socket associated with the interface
       */
      Ptr<Socket> FindSocketWithInterfaceAddress(Ipv4InterfaceAddress iface) const;
      /**
       * Find subnet directed broadcast socket with local interface address iface
       *
       * \param iface the interface
       * \returns the socket associated with the interface
       */
      Ptr<Socket> FindSubnetBroadcastSocketWithInterfaceAddress(Ipv4InterfaceAddress iface) const;
      /**
       * Process hello message
       *
       * \param rrepHeader RREP message header
       * \param receiverIfaceAddr receiver interface IP address
       */
      void ProcessHello(RrepHeader const &rrepHeader, Ipv4Address receiverIfaceAddr);
      /**
       * Create loopback route for given header
       *
       * \param header the IP header
       * \param oif the output interface net device
       * \returns the route
       */
      Ptr<Ipv4Route> LoopbackRoute(const Ipv4Header &header, Ptr<NetDevice> oif) const;

      ///\name Receive control packets
      //\{
      /**
       * Receive and process control packet
       * \param socket input socket
       */
      void RecvAodv(Ptr<Socket> socket);
      /**
       * Receive RREQ
       * \param p packet
       * \param receiver receiver address
       * \param src sender address
       */
      void RecvRequest(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
      /**
       * Receive RREP
       * \param p packet
       * \param my destination address
       * \param src sender address
       */
      void RecvReply(Ptr<Packet> p, Ipv4Address my, Ipv4Address src);
      /**
       * Receive RREP_ACK
       * \param neighbor neighbor address
       */
      void RecvReplyAck(Ipv4Address neighbor);
      /**
       * Receive RERR
       * \param p packet
       * \param src sender address
       */
      /// Receive  from node with address src
      void RecvError(Ptr<Packet> p, Ipv4Address src);
      //\}

      ///\name Send
      //\{
      /** Forward packet from route request queue
       * \param dst destination address
       * \param route route to use
       */
      void SendPacketFromQueue(Ipv4Address dst, Ptr<Ipv4Route> route);
      /// Send hello
      void SendHello();
      /** Send RREQ
       * \param dst destination address
       */
      void SendRequest(Ipv4Address dst);
      /** Send RREP
       * \param rreqHeader route request header
       * \param toOrigin routing table entry to originator
       */
      void SendReply(RreqHeader const &rreqHeader, RoutingTableEntry const &toOrigin);
      /** Send RREP by intermediate node
       * \param toDst routing table entry to destination
       * \param toOrigin routing table entry to originator
       * \param gratRep indicates whether a gratuitous RREP should be unicast to destination
       */
      void SendReplyByIntermediateNode(RoutingTableEntry &toDst, RoutingTableEntry &toOrigin, bool gratRep);
      /** Send RREP_ACK
       * \param neighbor neighbor address
       */
      void SendReplyAck(Ipv4Address neighbor);
      /** Initiate RERR
       * \param nextHop next hop address
       */
      void SendRerrWhenBreaksLinkToNextHop(Ipv4Address nextHop);
      /** Forward RERR
       * \param packet packet
       * \param precursors list of addresses of the visited nodes
       */
      void SendRerrMessage(Ptr<Packet> packet, std::vector<Ipv4Address> precursors);
      /**
       * Send RERR message when no route to forward input packet. Unicast if there is reverse route to originating node, broadcast otherwise.
       * \param dst - destination node IP address
       * \param dstSeqNo - destination node sequence number
       * \param origin - originating node IP address
       */
      void SendRerrWhenNoRouteToForward(Ipv4Address dst, uint32_t dstSeqNo, Ipv4Address origin);
      /// @}

      /**
       * Send packet to destination scoket
       * \param socket - destination node socket
       * \param packet - packet to send
       * \param destination - destination node IP address
       */
      void SendTo(Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination);

      /// Hello timer
      Timer m_htimer;
      /// Schedule next send of hello message
      void HelloTimerExpire();
      /// RREQ rate limit timer
      Timer m_rreqRateLimitTimer;
      /// Reset RREQ count and schedule RREQ rate limit timer with delay 1 sec.
      void RreqRateLimitTimerExpire();
      /// RERR rate limit timer
      Timer m_rerrRateLimitTimer;
      /// Reset RERR count and schedule RERR rate limit timer with delay 1 sec.
      void RerrRateLimitTimerExpire();
      /// Map IP address + RREQ timer.
      std::map<Ipv4Address, Timer> m_addressReqTimer;
      /**
       * Handle route discovery process
       * \param dst the destination IP address
       */
      void RouteRequestTimerExpire(Ipv4Address dst);
      /**
       * Mark link to neighbor node as unidirectional for blacklistTimeout
       *
       * \param neighbor the IP address of the neightbor node
       * \param blacklistTimeout the black list timeout time
       */
      void AckTimerExpire(Ipv4Address neighbor, Time blacklistTimeout);

      /// Provides uniform random variables.
      Ptr<UniformRandomVariable> m_uniformRandomVariable;
      /// Keep track of the last bcast time
      Time m_lastBcastTime;

      // SmartAodv: Link quality monitoring methods
      /**
       * \brief Monitor physical layer reception to get RSSI/SNR
       * \param packet the received packet
       * \param channelFreqMhz channel frequency in MHz
       * \param txVector transmit vector
       * \param signalDbm signal strength in dBm
       * \param noiseDbm noise floor in dBm
       */

      // ... 在 class RoutingProtocol 内部 ...
      // 修改前的参数可能是 (Ptr<const Packet>, uint16_t, WifiTxVector, double, double)
      void PhyRxStats(Ptr<const Packet> packet,
                      uint16_t channelFreqMhz,
                      WifiTxVector txVector,
                      MpduInfo mpduInfo,  // ns-3.34 新增
                      SignalNoiseDbm snr, // ns-3.34 替代了之前的 Power/Noise double
                      uint16_t staId);    // ns-3.34 新增

      /**
       * \brief Send preemptive RREQ before link failure
       * \param destination the destination address
       */
      void SendPreemptiveRreq(Ipv4Address destination);

      /**
       * \brief Predict link expiry time based on RSSI and movement
       * \param rssi current RSSI value in dBm
       * \return predicted link expiry time
       */
      Time PredictLinkExpiry(double rssi);

      // Q-Learning methods
      /**
       * \brief Initialize Q-learning modules
       */
      void InitializeQLearning ();

      /**
       * \brief Get Q-learning state for a route
       * \param rt Routing table entry
       * \return QState representing the route state
       */
      QState GetRouteQState (const RoutingTableEntry& rt) const;

      /**
       * \brief Get Q-learning state for a neighbor
       * \param neighbor Neighbor address
       * \return QState representing the neighbor state
       */
      QState GetNeighborQState (Ipv4Address neighbor) const;

      /**
       * \brief Select best route using Q-learning
       * \param dst Destination address
       * \param rt Output routing table entry
       * \return true if route found
       */
      bool QLearningRouteSelection (Ipv4Address dst, RoutingTableEntry& rt);

      /**
       * \brief Select best next-hop using Q-learning
       * \param candidates Vector of candidate neighbors
       * \return Index of selected neighbor
       */
      int QLearningNextHopSelection (const std::vector<Ipv4Address>& candidates);

      /**
       * \brief Update Q-values after transmission result
       * \param dst Destination address
       * \param success Whether transmission was successful
       * \param snr SNR value of the transmission
       * \param hops Number of hops
       */
      void UpdateQValues (Ipv4Address dst, bool success, double snr, uint8_t hops);

      /**
       * \brief Store Q-context for later update
       * \param dst Destination address
       * \param state Q-state before action
       * \param action Action taken
       */
      void StoreQContext (Ipv4Address dst, const QState& state, int action);

      /**
       * \brief Get stored Q-context
       * \param dst Destination address
       * \return QContext for the destination
       */
      QContext GetQContext (Ipv4Address dst) const;

      /**
       * \brief Enable/disable Q-learning
       * \param enable true to enable
       */
      void SetQLearningEnable (bool enable)
      {
        m_qLearningEnabled = enable;
      }

      /**
       * \brief Check if Q-learning is enabled
       * \return true if enabled
       */
      bool IsQLearningEnabled () const
      {
        return m_qLearningEnabled;
      }

      /**
       * \brief Set Q-learning alpha parameter
       * \param alpha Learning rate
       */
      void SetQAlpha (double alpha);

      /**
       * \brief Get Q-learning alpha parameter
       * \return Learning rate
       */
      double GetQAlpha () const;

      /**
       * \brief Set Q-learning gamma parameter
       * \param gamma Discount factor
       */
      void SetQGamma (double gamma);

      /**
       * \brief Get Q-learning gamma parameter
       * \return Discount factor
       */
      double GetQGamma () const;

      /**
       * \brief Set Q-learning epsilon parameter
       * \param epsilon Exploration rate
       */
      void SetQEpsilon (double epsilon);

      /**
       * \brief Get Q-learning epsilon parameter
       * \return Exploration rate
       */
      double GetQEpsilon () const;

    public:
      // ========== Cluster Management Methods ==========
      /**
       * \brief Set local cluster ID
       * \param clusterId The cluster ID to assign
       */
      void SetLocalClusterId (uint32_t clusterId);

      /**
       * \brief Get local cluster ID
       * \return Current cluster ID
       */
      uint32_t GetLocalClusterId () const;

      /**
       * \brief Set cluster mode
       * \param mode Cluster mode (SELF_ORG or CENTRALIZED)
       */
      void SetClusterMode (ClusterMode mode);

      /**
       * \brief Get cluster mode
       * \return Current cluster mode
       */
      ClusterMode GetClusterMode () const;

      /**
       * \brief Set cluster head address
       * \param head Cluster head IP address
       */
      void SetClusterHead (Ipv4Address head);

      /**
       * \brief Get cluster head address
       * \return Cluster head IP address
       */
      Ipv4Address GetClusterHeadAddress () const;

      /**
       * \brief Check if this node is cluster head
       * \return true if cluster head
       */
      bool IsClusterHead () const;

      /**
       * \brief Get number of known cluster members
       * \return Number of members in the local cluster
       */
      uint32_t GetClusterMemberCount () const;

      /**
       * \brief Check if an address is in the same cluster
       * \param addr Address to check
       * \return true if in same cluster or clustering disabled
       */
      bool IsInSameCluster (Ipv4Address addr) const;

      /**
       * \brief Check if traffic should go via cluster head
       * \param dst Destination address
       * \return true if should forward via cluster head
       */
      bool ShouldForwardViaClusterHead (Ipv4Address dst) const;

      /**
       * \brief Update neighbor RSSI in cluster table
       * \param addr Neighbor address
       * \param rssi RSSI value in dBm
       */
      void UpdateNeighborRssi (Ipv4Address addr, double rssi);

      /**
       * \brief Get neighbor RSSI from cluster table
       * \param addr Neighbor address
       * \return RSSI value in dBm
       */
      double GetNeighborRssi (Ipv4Address addr) const;

      /**
       * \brief Set cluster ID for a neighbor
       * \param addr Neighbor address
       * \param clusterId Cluster ID
       */
      void SetNeighborClusterId (Ipv4Address addr, uint32_t clusterId);

      /**
       * \brief Get the cluster table
       * \return Reference to cluster table
       */
      const ClusterTable& GetClusterTable () const;

    private:
      /// Q-Learning module for path selection
      QLearning* m_pathQL;
      /// Q-Learning module for next-hop selection
      QLearning* m_nextHopQL;
      /// Q-Learning enabled flag
      bool m_qLearningEnabled;
      /// Q-Learning contexts for tracking state transitions
      std::map<Ipv4Address, QContext> m_qContexts;
      /// Q-Learning alpha (learning rate)
      double m_qAlpha;
      /// Q-Learning gamma (discount factor)
      double m_qGamma;
      /// Q-Learning epsilon (exploration rate)
      double m_qEpsilon;

      // ========== Cluster Management Members ==========
      /// Local cluster ID (0 = unassigned/clustering disabled)
      uint32_t m_localClusterId;
      /// Cluster head IP address
      Ipv4Address m_clusterHeadAddress;
      /// Whether this node is cluster head
      bool m_isClusterHead;
      /// Cluster mode (SELF_ORG or CENTRALIZED)
      ClusterMode m_clusterMode;
      /// Cluster table for neighbor management
      ClusterTable m_clusterTable;
      /// RSSI threshold to trigger cluster switch (dBm)
      double m_rssiSwitchThreshold;
      /// RSSI threshold to accept new cluster (dBm)
      double m_rssiAcceptThreshold;
      /// Periodic cluster check timer
      EventId m_clusterCheckEvent;

      // ========== Cluster Management Private Methods ==========
      /**
       * \brief Check if cluster switch is needed based on RSSI
       */
      void CheckClusterSwitch ();

      /**
       * \brief Perform cluster switch to new cluster
       * \param newClusterId Target cluster ID
       */
      void PerformClusterSwitch (uint32_t newClusterId);

      /**
       * \brief Get average RSSI for current cluster
       * \return Average RSSI in dBm
       */
      double GetCurrentClusterRssi () const;

      /**
       * \brief Find best cluster to switch to
       * \param bestRssi Output: best RSSI found
       * \return Best cluster ID, or 0 if none suitable
       */
      uint32_t FindBestCluster (double& bestRssi) const;
    };

  } // namespace smartAodvV2
} // namespace ns3

#endif /* AODV_V2ROUTINGPROTOCOL_H */
