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
 *      NS-2 HARP model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      HARP-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/HARP-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
/* #define NS_LOG_APPEND_CONTEXT                                   \
   if (m_ipv4) { std::clog << "[node " << m_ipv4->GetObject<Node> ()->GetId () << "] "; }
*/

#include "harp-routing-protocol.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-header.h"
#include "ns3/wifi-net-device.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/wifi-mac-queue-item.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include <algorithm>
#include <limits>

namespace ns3
{

  NS_LOG_COMPONENT_DEFINE("HarpRoutingProtocol");

  namespace harp
  {
    NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

    /// UDP Port for HARP control traffic

    /**
     * \ingroup harp
     * \brief Tag used by HARP implementation
     */
    class DeferredRouteOutputTag : public Tag
    {

    public:
      /**
       * \brief Constructor
       * \param o the output interface
       */
      DeferredRouteOutputTag(int32_t o = -1) : Tag(),
                                               m_oif(o)
      {
      }

      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId()
      {
        static TypeId tid = TypeId("ns3::harp::DeferredRouteOutputTag")
                                .SetParent<Tag>()
                                .SetGroupName("Harp")
                                .AddConstructor<DeferredRouteOutputTag>();
        return tid;
      }

      TypeId GetInstanceTypeId() const
      {
        return GetTypeId();
      }

      /**
       * \brief Get the output interface
       * \return the output interface
       */
      int32_t GetInterface() const
      {
        return m_oif;
      }

      /**
       * \brief Set the output interface
       * \param oif the output interface
       */
      void SetInterface(int32_t oif)
      {
        m_oif = oif;
      }

      uint32_t GetSerializedSize() const
      {
        return sizeof(int32_t);
      }

      void Serialize(TagBuffer i) const
      {
        i.WriteU32(m_oif);
      }

      void Deserialize(TagBuffer i)
      {
        m_oif = i.ReadU32();
      }

      void Print(std::ostream &os) const
      {
        os << "DeferredRouteOutputTag: output interface = " << m_oif;
      }

    private:
      /// Positive if output device is fixed in RouteOutput
      int32_t m_oif;
    };

    NS_OBJECT_ENSURE_REGISTERED(DeferredRouteOutputTag);

    //-----------------------------------------------------------------------------
    RoutingProtocol::RoutingProtocol()
        : m_rreqRetries(2),
          m_ttlStart(1),
          m_ttlIncrement(2),
          m_ttlThreshold(7),
          m_timeoutBuffer(2),
          m_rreqRateLimit(10),
          m_rerrRateLimit(10),
          m_activeRouteTimeout(Seconds(3)),
          m_netDiameter(35),
          m_nodeTraversalTime(MilliSeconds(40)),
          m_netTraversalTime(Time((2 * m_netDiameter) * m_nodeTraversalTime)),
          m_pathDiscoveryTime(Time(2 * m_netTraversalTime)),
          m_myRouteTimeout(Time(2 * std::max(m_pathDiscoveryTime, m_activeRouteTimeout))),
          m_helloInterval(Seconds(1)),
          m_allowedHelloLoss(2),
          m_deletePeriod(Time(5 * std::max(m_activeRouteTimeout, m_helloInterval))),
          m_nextHopWait(m_nodeTraversalTime + MilliSeconds(10)),
          m_blackListTimeout(Time(m_rreqRetries * m_netTraversalTime)),
          m_maxQueueLen(64),
          m_maxQueueTime(Seconds(30)),
          m_destinationOnly(false),
          m_gratuitousReply(true),
          m_enableHello(false),
          m_enableBroadcast(false),
          m_routingTable(m_deletePeriod),
          m_queue(m_maxQueueLen, m_maxQueueTime),
          m_requestId(0),
          m_seqNo(0),
          m_rreqIdCache(m_pathDiscoveryTime),
          m_dpd(m_pathDiscoveryTime),
          m_nb(m_helloInterval),
          m_rreqCount(0),
          m_rerrCount(0),
          m_currentQueueLen(0),

          m_uniformRandomVariable(CreateObject<UniformRandomVariable>()),
          m_lastBcastTime(Seconds(0))
    {
      m_nb.SetCallback(MakeCallback(&RoutingProtocol::SendRerrWhenBreaksLinkToNextHop, this));
    }

    // m_avgSnr (0.0),
    // m_packetArrivalRate (0.0),
    // m_currentMode (MODE_SLEEP),
    // m_logicCycleTimer (),
    // m_packetsSent (0),
    // m_packetsReceived (0),
    // m_accumulatedDelay (Seconds (0)),

    TypeId
    RoutingProtocol::GetTypeId(void)
    {
      static TypeId tid = TypeId("ns3::harp::RoutingProtocol")
                              .SetParent<Ipv4RoutingProtocol>()
                              .SetGroupName("Harp")
                              .AddConstructor<RoutingProtocol>()
                              .AddAttribute("HelloInterval", "HELLO messages emission interval.",
                                            TimeValue(Seconds(1)),
                                            MakeTimeAccessor(&RoutingProtocol::m_helloInterval),
                                            MakeTimeChecker())
                              .AddAttribute("TtlStart", "Initial TTL value for RREQ.",
                                            UintegerValue(1),
                                            MakeUintegerAccessor(&RoutingProtocol::m_ttlStart),
                                            MakeUintegerChecker<uint16_t>())
                              .AddAttribute("TtlIncrement", "TTL increment for each attempt using the expanding ring search for RREQ dissemination.",
                                            UintegerValue(2),
                                            MakeUintegerAccessor(&RoutingProtocol::m_ttlIncrement),
                                            MakeUintegerChecker<uint16_t>())
                              .AddAttribute("TtlThreshold", "Maximum TTL value for expanding ring search, TTL = NetDiameter is used beyond this value.",
                                            UintegerValue(7),
                                            MakeUintegerAccessor(&RoutingProtocol::m_ttlThreshold),
                                            MakeUintegerChecker<uint16_t>())
                              .AddAttribute("TimeoutBuffer", "Provide a buffer for the timeout.",
                                            UintegerValue(2),
                                            MakeUintegerAccessor(&RoutingProtocol::m_timeoutBuffer),
                                            MakeUintegerChecker<uint16_t>())
                              .AddAttribute("RreqRetries", "Maximum number of retransmissions of RREQ to discover a route",
                                            UintegerValue(2),
                                            MakeUintegerAccessor(&RoutingProtocol::m_rreqRetries),
                                            MakeUintegerChecker<uint32_t>())
                              .AddAttribute("RreqRateLimit", "Maximum number of RREQ per second.",
                                            UintegerValue(10),
                                            MakeUintegerAccessor(&RoutingProtocol::m_rreqRateLimit),
                                            MakeUintegerChecker<uint32_t>())
                              .AddAttribute("RerrRateLimit", "Maximum number of RERR per second.",
                                            UintegerValue(10),
                                            MakeUintegerAccessor(&RoutingProtocol::m_rerrRateLimit),
                                            MakeUintegerChecker<uint32_t>())
                              .AddAttribute("NodeTraversalTime", "Conservative estimate of the average one hop traversal time for packets and should include "
                                                                 "queuing delays, interrupt processing times and transfer times.",
                                            TimeValue(MilliSeconds(40)),
                                            MakeTimeAccessor(&RoutingProtocol::m_nodeTraversalTime),
                                            MakeTimeChecker())
                              .AddAttribute("NextHopWait", "Period of our waiting for the neighbour's RREP_ACK = 10 ms + NodeTraversalTime",
                                            TimeValue(MilliSeconds(50)),
                                            MakeTimeAccessor(&RoutingProtocol::m_nextHopWait),
                                            MakeTimeChecker())
                              .AddAttribute("ActiveRouteTimeout", "Period of time during which the route is considered to be valid",
                                            TimeValue(Seconds(3)),
                                            MakeTimeAccessor(&RoutingProtocol::m_activeRouteTimeout),
                                            MakeTimeChecker())
                              .AddAttribute("MyRouteTimeout", "Value of lifetime field in RREP generating by this node = 2 * max(ActiveRouteTimeout, PathDiscoveryTime)",
                                            TimeValue(Seconds(11.2)),
                                            MakeTimeAccessor(&RoutingProtocol::m_myRouteTimeout),
                                            MakeTimeChecker())
                              .AddAttribute("BlackListTimeout", "Time for which the node is put into the blacklist = RreqRetries * NetTraversalTime",
                                            TimeValue(Seconds(5.6)),
                                            MakeTimeAccessor(&RoutingProtocol::m_blackListTimeout),
                                            MakeTimeChecker())
                              .AddAttribute("DeletePeriod", "DeletePeriod is intended to provide an upper bound on the time for which an upstream node A "
                                                            "can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D."
                                                            " = 5 * max (HelloInterval, ActiveRouteTimeout)",
                                            TimeValue(Seconds(15)),
                                            MakeTimeAccessor(&RoutingProtocol::m_deletePeriod),
                                            MakeTimeChecker())
                              .AddAttribute("NetDiameter", "Net diameter measures the maximum possible number of hops between two nodes in the network",
                                            UintegerValue(35),
                                            MakeUintegerAccessor(&RoutingProtocol::m_netDiameter),
                                            MakeUintegerChecker<uint32_t>())
                              .AddAttribute("NetTraversalTime", "Estimate of the average net traversal time = 2 * NodeTraversalTime * NetDiameter",
                                            TimeValue(Seconds(2.8)),
                                            MakeTimeAccessor(&RoutingProtocol::m_netTraversalTime),
                                            MakeTimeChecker())
                              .AddAttribute("PathDiscoveryTime", "Estimate of maximum time needed to find route in network = 2 * NetTraversalTime",
                                            TimeValue(Seconds(5.6)),
                                            MakeTimeAccessor(&RoutingProtocol::m_pathDiscoveryTime),
                                            MakeTimeChecker())
                              .AddAttribute("MaxQueueLen", "Maximum number of packets that we allow a routing protocol to buffer.",
                                            UintegerValue(64),
                                            MakeUintegerAccessor(&RoutingProtocol::SetMaxQueueLen,
                                                                 &RoutingProtocol::GetMaxQueueLen),
                                            MakeUintegerChecker<uint32_t>())
                              .AddAttribute("MaxQueueTime", "Maximum time packets can be queued (in seconds)",
                                            TimeValue(Seconds(30)),
                                            MakeTimeAccessor(&RoutingProtocol::SetMaxQueueTime,
                                                             &RoutingProtocol::GetMaxQueueTime),
                                            MakeTimeChecker())
                              .AddAttribute("AllowedHelloLoss", "Number of hello messages which may be loss for valid link.",
                                            UintegerValue(2),
                                            MakeUintegerAccessor(&RoutingProtocol::m_allowedHelloLoss),
                                            MakeUintegerChecker<uint16_t>())
                              .AddAttribute("GratuitousReply", "Indicates whether a gratuitous RREP should be unicast to the node originated route discovery.",
                                            BooleanValue(true),
                                            MakeBooleanAccessor(&RoutingProtocol::SetGratuitousReplyFlag,
                                                                &RoutingProtocol::GetGratuitousReplyFlag),
                                            MakeBooleanChecker())
                              .AddAttribute("DestinationOnly", "Indicates only the destination may respond to this RREQ.",
                                            BooleanValue(false),
                                            MakeBooleanAccessor(&RoutingProtocol::SetDestinationOnlyFlag,
                                                                &RoutingProtocol::GetDestinationOnlyFlag),
                                            MakeBooleanChecker())
                              .AddAttribute("EnableHello", "Indicates whether a hello messages enable.",
                                            BooleanValue(true),
                                            MakeBooleanAccessor(&RoutingProtocol::SetHelloEnable,
                                                                &RoutingProtocol::GetHelloEnable),
                                            MakeBooleanChecker())
                              .AddAttribute("EnableBroadcast", "Indicates whether a broadcast data packets forwarding enable.",
                                            BooleanValue(true),
                                            MakeBooleanAccessor(&RoutingProtocol::SetBroadcastEnable,
                                                                &RoutingProtocol::GetBroadcastEnable),
                                            MakeBooleanChecker())
                              .AddAttribute("UniformRv",
                                            "Access to the underlying UniformRandomVariable",
                                            StringValue("ns3::UniformRandomVariable"),
                                            MakePointerAccessor(&RoutingProtocol::m_uniformRandomVariable),
                                            MakePointerChecker<UniformRandomVariable>());
      return tid;
    }

    void
    RoutingProtocol::SetMaxQueueLen(uint32_t len)
    {
      m_maxQueueLen = len;
      m_queue.SetMaxQueueLen(len);
    }
    void
    RoutingProtocol::SetMaxQueueTime(Time t)
    {
      m_maxQueueTime = t;
      m_queue.SetQueueTimeout(t);
    }

    RoutingProtocol::~RoutingProtocol()
    {
    }

    void
    RoutingProtocol::DoDispose()
    {
      m_ipv4 = 0;
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::iterator iter =
               m_socketAddresses.begin();
           iter != m_socketAddresses.end(); iter++)
      {
        iter->first->Close();
      }
      m_socketAddresses.clear();
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::iterator iter =
               m_socketSubnetBroadcastAddresses.begin();
           iter != m_socketSubnetBroadcastAddresses.end(); iter++)
      {
        iter->first->Close();
      }
      m_socketSubnetBroadcastAddresses.clear();
      Ipv4RoutingProtocol::DoDispose();
    }

    void
    RoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
    {
      *stream->GetStream() << "Node: " << m_ipv4->GetObject<Node>()->GetId()
                           << "; Time: " << Now().As(unit)
                           << ", Local time: " << m_ipv4->GetObject<Node>()->GetLocalTime().As(unit)
                           << ", HARP Routing table" << std::endl;

      m_routingTable.Print(stream, unit);
      *stream->GetStream() << std::endl;
    }

    int64_t
    RoutingProtocol::AssignStreams(int64_t stream)
    {
      NS_LOG_FUNCTION(this << stream);
      m_uniformRandomVariable->SetStream(stream);
      return 1;
    }

    void
    RoutingProtocol::Start()
    {
      NS_LOG_FUNCTION(this);
      if (m_enableHello)
      {
        m_nb.ScheduleTimer();
      }
      m_rreqRateLimitTimer.SetFunction(&RoutingProtocol::RreqRateLimitTimerExpire,
                                       this);
      m_rreqRateLimitTimer.Schedule(Seconds(1));

      m_rerrRateLimitTimer.SetFunction(&RoutingProtocol::RerrRateLimitTimerExpire,
                                       this);
      m_rerrRateLimitTimer.Schedule(Seconds(1));
    }

    Ptr<Ipv4Route>
    RoutingProtocol::RouteOutput(Ptr<Packet> p, const Ipv4Header &header,
                                 Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
    {
      NS_LOG_FUNCTION(this << header << (oif ? oif->GetIfIndex() : 0));
      if (!p)
      {
        NS_LOG_DEBUG("Packet is == 0");
        return LoopbackRoute(header, oif); // later
      }
      if (m_socketAddresses.empty())
      {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        NS_LOG_LOGIC("No harp interfaces");
        Ptr<Ipv4Route> route;
        return route;
      }
      sockerr = Socket::ERROR_NOTERROR;
      Ptr<Ipv4Route> route;
      Ipv4Address dst = header.GetDestination();
      RoutingTableEntry rt;
      if (m_routingTable.LookupValidRoute(dst, rt))
      {
        route = rt.GetRoute();
        NS_ASSERT(route != 0);
        NS_LOG_DEBUG("Exist route to " << route->GetDestination() << " from interface " << route->GetSource());
        if (oif != 0 && route->GetOutputDevice() != oif)
        {
          NS_LOG_DEBUG("Output device doesn't match. Dropped.");
          sockerr = Socket::ERROR_NOROUTETOHOST;
          return Ptr<Ipv4Route>();
        }

        if (rt.GetOrigin() != ORIGIN_PROACTIVE)
        {
          UpdateRouteLifeTime(dst, m_activeRouteTimeout);
        }
        UpdateRouteLifeTime(route->GetGateway(), m_activeRouteTimeout);
        return route;
      }

      // Valid route not found, in this case we return loopback.
      // Actual route request will be deferred until packet will be fully formed,
      // routed to loopback, received from loopback and passed to RouteInput (see below)
      uint32_t iif = (oif ? m_ipv4->GetInterfaceForDevice(oif) : -1);
      DeferredRouteOutputTag tag(iif);
      NS_LOG_DEBUG("Valid Route not found");
      if (!p->PeekPacketTag(tag))
      {
        p->AddPacketTag(tag);
      }
      return LoopbackRoute(header, oif);
    }

    void
    RoutingProtocol::DeferredRouteOutput(Ptr<const Packet> p, const Ipv4Header &header,
                                         UnicastForwardCallback ucb, ErrorCallback ecb)
    {
      NS_LOG_FUNCTION(this << p << header);
      NS_ASSERT(p != 0 && p != Ptr<Packet>());

      QueueEntry newEntry(p, header, ucb, ecb);
      bool result = m_queue.Enqueue(newEntry);
      if (result)
      {
        NS_LOG_LOGIC("Add packet " << p->GetUid() << " to queue. Protocol " << (uint16_t)header.GetProtocol());
        RoutingTableEntry rt;
        bool result = m_routingTable.LookupRoute(header.GetDestination(), rt);
        if (!result || ((rt.GetFlag() != IN_SEARCH) && result))
        {
          NS_LOG_LOGIC("Send new RREQ for outbound packet to " << header.GetDestination());
          SendRequest(header.GetDestination());
        }
      }
    }

    bool
    RoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header &header,
                                Ptr<const NetDevice> idev, UnicastForwardCallback ucb,
                                MulticastForwardCallback mcb, LocalDeliverCallback lcb, ErrorCallback ecb)
    {
      NS_LOG_FUNCTION(this << p->GetUid() << header.GetDestination() << idev->GetAddress());
      if (m_socketAddresses.empty())
      {
        NS_LOG_LOGIC("No harp interfaces");
        return false;
      }
      NS_ASSERT(m_ipv4 != 0);
      NS_ASSERT(p != 0);
      // Check if input device supports IP
      NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);
      int32_t iif = m_ipv4->GetInterfaceForDevice(idev);

      Ipv4Address dst = header.GetDestination();
      Ipv4Address origin = header.GetSource();

      // Deferred route request
      if (idev == m_lo)
      {
        DeferredRouteOutputTag tag;
        if (p->PeekPacketTag(tag))
        {
          DeferredRouteOutput(p, header, ucb, ecb);
          return true;
        }
      }

      // Duplicate of own packet
      if (IsMyOwnAddress(origin))
      {
        return true;
      }

      // HARP is not a multicast routing protocol
      if (dst.IsMulticast())
      {
        return false;
      }

      // Broadcast local delivery/forwarding
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
               m_socketAddresses.begin();
           j != m_socketAddresses.end(); ++j)
      {
        Ipv4InterfaceAddress iface = j->second;
        if (m_ipv4->GetInterfaceForAddress(iface.GetLocal()) == iif)
        {
          if (dst == iface.GetBroadcast() || dst.IsBroadcast())
          {
            if (m_dpd.IsDuplicate(p, header))
            {
              NS_LOG_DEBUG("Duplicated packet " << p->GetUid() << " from " << origin << ". Drop.");
              return true;
            }
            UpdateRouteLifeTime(origin, m_activeRouteTimeout);
            Ptr<Packet> packet = p->Copy();
            if (lcb.IsNull() == false)
            {
              NS_LOG_LOGIC("Broadcast local delivery to " << iface.GetLocal());
              lcb(p, header, iif);
              // Fall through to additional processing
            }
            else
            {
              NS_LOG_ERROR("Unable to deliver packet locally due to null callback " << p->GetUid() << " from " << origin);
              ecb(p, header, Socket::ERROR_NOROUTETOHOST);
            }
            if (!m_enableBroadcast)
            {
              return true;
            }
            if (header.GetProtocol() == UdpL4Protocol::PROT_NUMBER)
            {
              UdpHeader udpHeader;
              p->PeekHeader(udpHeader);
              if (udpHeader.GetDestinationPort() == HARP_PORT)
              {
                // HARP packets sent in broadcast are already managed
                return true;
              }
            }
            if (header.GetTtl() > 1)
            {
              NS_LOG_LOGIC("Forward broadcast. TTL " << (uint16_t)header.GetTtl());
              RoutingTableEntry toBroadcast;
              if (m_routingTable.LookupRoute(dst, toBroadcast))
              {
                Ptr<Ipv4Route> route = toBroadcast.GetRoute();
                ucb(route, packet, header);
              }
              else
              {
                NS_LOG_DEBUG("No route to forward broadcast. Drop packet " << p->GetUid());
              }
            }
            else
            {
              NS_LOG_DEBUG("TTL exceeded. Drop packet " << p->GetUid());
            }
            return true;
          }
        }
      }

      // Unicast local delivery
      if (m_ipv4->IsDestinationAddress(dst, iif))
      {
        UpdateRouteLifeTime(origin, m_activeRouteTimeout);
        RoutingTableEntry toOrigin;
        if (m_routingTable.LookupValidRoute(origin, toOrigin))
        {
          UpdateRouteLifeTime(toOrigin.GetNextHop(), m_activeRouteTimeout);
          m_nb.Update(toOrigin.GetNextHop(), m_activeRouteTimeout);
        }
        if (lcb.IsNull() == false)
        {
          NS_LOG_LOGIC("Unicast local delivery to " << dst);
          lcb(p, header, iif);
        }
        else
        {
          NS_LOG_ERROR("Unable to deliver packet locally due to null callback " << p->GetUid() << " from " << origin);
          ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        }
        return true;
      }

      // Check if input device supports IP forwarding
      if (m_ipv4->IsForwarding(iif) == false)
      {
        NS_LOG_LOGIC("Forwarding disabled for this interface");
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return true;
      }

      // Forwarding
      return Forwarding(p, header, ucb, ecb);
    }

    bool
    RoutingProtocol::Forwarding(Ptr<const Packet> p, const Ipv4Header &header,
                                UnicastForwardCallback ucb, ErrorCallback ecb)
    {
      NS_LOG_FUNCTION(this);
      Ipv4Address dst = header.GetDestination();
      Ipv4Address origin = header.GetSource();
      m_routingTable.Purge();
      RoutingTableEntry toDst;
      if (m_routingTable.LookupRoute(dst, toDst))
      {
        if (toDst.GetFlag() == VALID)
        {
          Ptr<Ipv4Route> route = toDst.GetRoute();
          NS_LOG_LOGIC(route->GetSource() << " forwarding to " << dst << " from " << origin << " packet " << p->GetUid());

          /*
           *  Each time a route is used to forward a data packet, its Active Route
           *  Lifetime field of the source, destination and the next hop on the
           *  path to the destination is updated to be no less than the current
           *  time plus ActiveRouteTimeout.
           */
          UpdateRouteLifeTime(origin, m_activeRouteTimeout);
          UpdateRouteLifeTime(dst, m_activeRouteTimeout);
          UpdateRouteLifeTime(route->GetGateway(), m_activeRouteTimeout);
          /*
           *  Since the route between each originator and destination pair is expected to be symmetric, the
           *  Active Route Lifetime for the previous hop, along the reverse path back to the IP source, is also updated
           *  to be no less than the current time plus ActiveRouteTimeout
           */
          RoutingTableEntry toOrigin;
          m_routingTable.LookupRoute(origin, toOrigin);
          UpdateRouteLifeTime(toOrigin.GetNextHop(), m_activeRouteTimeout);

          m_nb.Update(route->GetGateway(), m_activeRouteTimeout);
          m_nb.Update(toOrigin.GetNextHop(), m_activeRouteTimeout);

          ucb(route, p, header);
          return true;
        }
        else
        {
          if (toDst.GetValidSeqNo())
          {
            SendRerrWhenNoRouteToForward(dst, toDst.GetSeqNo(), origin);
            NS_LOG_DEBUG("Drop packet " << p->GetUid() << " because no route to forward it.");
            return false;
          }
        }
      }
      NS_LOG_LOGIC("route not found to " << dst << ". Send RERR message.");
      NS_LOG_DEBUG("Drop packet " << p->GetUid() << " because no route to forward it.");
      SendRerrWhenNoRouteToForward(dst, 0, origin);
      return false;
    }

    void
    RoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
    {
      NS_ASSERT(ipv4 != 0);
      NS_ASSERT(m_ipv4 == 0);

      m_ipv4 = ipv4;

      // Create lo route. It is asserted that the only one interface up for now is loopback
      NS_ASSERT(m_ipv4->GetNInterfaces() == 1 && m_ipv4->GetAddress(0, 0).GetLocal() == Ipv4Address("127.0.0.1"));
      m_lo = m_ipv4->GetNetDevice(0);
      NS_ASSERT(m_lo != 0);
      // Remember lo route
      RoutingTableEntry rt(/*device=*/m_lo, /*dst=*/Ipv4Address::GetLoopback(), /*know seqno=*/true, /*seqno=*/0,
                           /*iface=*/Ipv4InterfaceAddress(Ipv4Address::GetLoopback(), Ipv4Mask("255.0.0.0")),
                           /*hops=*/1, /*next hop=*/Ipv4Address::GetLoopback(),
                           /*lifetime=*/Simulator::GetMaximumSimulationTime());
      m_routingTable.AddRoute(rt);

      Simulator::ScheduleNow(&RoutingProtocol::Start, this);
    }

    void
    RoutingProtocol::NotifyInterfaceUp(uint32_t i)
    {
      NS_LOG_FUNCTION(this << m_ipv4->GetAddress(i, 0).GetLocal());
      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
      if (l3->GetNAddresses(i) > 1)
      {
        NS_LOG_WARN("HARP does not work with more then one address per each interface.");
      }
      Ipv4InterfaceAddress iface = l3->GetAddress(i, 0);
      if (iface.GetLocal() == Ipv4Address("127.0.0.1"))
      {
        return;
      }

      // Create a socket to listen only on this interface
      Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                                UdpSocketFactory::GetTypeId());
      NS_ASSERT(socket != 0);
      socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvHarp, this));
      socket->BindToNetDevice(l3->GetNetDevice(i));
      socket->Bind(InetSocketAddress(iface.GetLocal(), HARP_PORT));
      socket->SetAllowBroadcast(true);
      socket->SetIpRecvTtl(true);
      m_socketAddresses.insert(std::make_pair(socket, iface));

      // create also a subnet broadcast socket
      socket = Socket::CreateSocket(GetObject<Node>(),
                                    UdpSocketFactory::GetTypeId());
      NS_ASSERT(socket != 0);
      socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvHarp, this));
      socket->BindToNetDevice(l3->GetNetDevice(i));
      socket->Bind(InetSocketAddress(iface.GetBroadcast(), HARP_PORT));
      socket->SetAllowBroadcast(true);
      socket->SetIpRecvTtl(true);
      m_socketSubnetBroadcastAddresses.insert(std::make_pair(socket, iface));

      // Add local broadcast record to the routing table
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice(m_ipv4->GetInterfaceForAddress(iface.GetLocal()));
      RoutingTableEntry rt(/*device=*/dev, /*dst=*/iface.GetBroadcast(), /*know seqno=*/true, /*seqno=*/0, /*iface=*/iface,
                           /*hops=*/1, /*next hop=*/iface.GetBroadcast(), /*lifetime=*/Simulator::GetMaximumSimulationTime());
      m_routingTable.AddRoute(rt);

      if (l3->GetInterface(i)->GetArpCache())
      {
        m_nb.AddArpCache(l3->GetInterface(i)->GetArpCache());
      }

      // Allow neighbor manager use this interface for layer 2 feedback if possible
      Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice>();
      if (wifi == 0)
      {
        return;
      }
      Ptr<WifiMac> mac = wifi->GetMac();
      if (mac == 0)
      {
        return;
      }

      mac->TraceConnectWithoutContext("DroppedMpdu", MakeCallback(&RoutingProtocol::NotifyTxError, this));
    }

    void
    RoutingProtocol::NotifyTxError(WifiMacDropReason reason, Ptr<const WifiMacQueueItem> mpdu)
    {
      m_nb.GetTxErrorCallback()(mpdu->GetHeader());
    }

    void
    RoutingProtocol::NotifyInterfaceDown(uint32_t i)
    {
      NS_LOG_FUNCTION(this << m_ipv4->GetAddress(i, 0).GetLocal());

      // Disable layer 2 link state monitoring (if possible)
      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
      Ptr<NetDevice> dev = l3->GetNetDevice(i);
      Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice>();
      if (wifi != 0)
      {
        Ptr<WifiMac> mac = wifi->GetMac()->GetObject<AdhocWifiMac>();
        if (mac != 0)
        {
          mac->TraceDisconnectWithoutContext("DroppedMpdu",
                                             MakeCallback(&RoutingProtocol::NotifyTxError, this));
          m_nb.DelArpCache(l3->GetInterface(i)->GetArpCache());
        }
      }

      // Close socket
      Ptr<Socket> socket = FindSocketWithInterfaceAddress(m_ipv4->GetAddress(i, 0));
      NS_ASSERT(socket);
      socket->Close();
      m_socketAddresses.erase(socket);

      // Close socket
      socket = FindSubnetBroadcastSocketWithInterfaceAddress(m_ipv4->GetAddress(i, 0));
      NS_ASSERT(socket);
      socket->Close();
      m_socketSubnetBroadcastAddresses.erase(socket);

      if (m_socketAddresses.empty())
      {
        NS_LOG_LOGIC("No harp interfaces");
        m_htimer.Cancel();
        m_nb.Clear();
        m_routingTable.Clear();
        return;
      }
      m_routingTable.DeleteAllRoutesFromInterface(m_ipv4->GetAddress(i, 0));
    }

    void
    RoutingProtocol::NotifyAddAddress(uint32_t i, Ipv4InterfaceAddress address)
    {
      NS_LOG_FUNCTION(this << " interface " << i << " address " << address);
      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
      if (!l3->IsUp(i))
      {
        return;
      }
      if (l3->GetNAddresses(i) == 1)
      {
        Ipv4InterfaceAddress iface = l3->GetAddress(i, 0);
        Ptr<Socket> socket = FindSocketWithInterfaceAddress(iface);
        if (!socket)
        {
          if (iface.GetLocal() == Ipv4Address("127.0.0.1"))
          {
            return;
          }
          // Create a socket to listen only on this interface
          Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                                    UdpSocketFactory::GetTypeId());
          NS_ASSERT(socket != 0);
          socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvHarp, this));
          socket->BindToNetDevice(l3->GetNetDevice(i));
          socket->Bind(InetSocketAddress(iface.GetLocal(), HARP_PORT));
          socket->SetAllowBroadcast(true);
          m_socketAddresses.insert(std::make_pair(socket, iface));

          // create also a subnet directed broadcast socket
          socket = Socket::CreateSocket(GetObject<Node>(),
                                        UdpSocketFactory::GetTypeId());
          NS_ASSERT(socket != 0);
          socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvHarp, this));
          socket->BindToNetDevice(l3->GetNetDevice(i));
          socket->Bind(InetSocketAddress(iface.GetBroadcast(), HARP_PORT));
          socket->SetAllowBroadcast(true);
          socket->SetIpRecvTtl(true);
          m_socketSubnetBroadcastAddresses.insert(std::make_pair(socket, iface));

          // Add local broadcast record to the routing table
          Ptr<NetDevice> dev = m_ipv4->GetNetDevice(
              m_ipv4->GetInterfaceForAddress(iface.GetLocal()));
          RoutingTableEntry rt(/*device=*/dev, /*dst=*/iface.GetBroadcast(), /*know seqno=*/true,
                               /*seqno=*/0, /*iface=*/iface, /*hops=*/1,
                               /*next hop=*/iface.GetBroadcast(), /*lifetime=*/Simulator::GetMaximumSimulationTime());
          m_routingTable.AddRoute(rt);
        }
      }
      else
      {
        NS_LOG_LOGIC("HARP does not work with more then one address per each interface. Ignore added address");
      }
    }

    void
    RoutingProtocol::NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress address)
    {
      NS_LOG_FUNCTION(this);
      Ptr<Socket> socket = FindSocketWithInterfaceAddress(address);
      if (socket)
      {
        m_routingTable.DeleteAllRoutesFromInterface(address);
        socket->Close();
        m_socketAddresses.erase(socket);

        Ptr<Socket> unicastSocket = FindSubnetBroadcastSocketWithInterfaceAddress(address);
        if (unicastSocket)
        {
          unicastSocket->Close();
          m_socketAddresses.erase(unicastSocket);
        }

        Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
        if (l3->GetNAddresses(i))
        {
          Ipv4InterfaceAddress iface = l3->GetAddress(i, 0);
          // Create a socket to listen only on this interface
          Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                                    UdpSocketFactory::GetTypeId());
          NS_ASSERT(socket != 0);
          socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvHarp, this));
          // Bind to any IP address so that broadcasts can be received
          socket->BindToNetDevice(l3->GetNetDevice(i));
          socket->Bind(InetSocketAddress(iface.GetLocal(), HARP_PORT));
          socket->SetAllowBroadcast(true);
          socket->SetIpRecvTtl(true);
          m_socketAddresses.insert(std::make_pair(socket, iface));

          // create also a unicast socket
          socket = Socket::CreateSocket(GetObject<Node>(),
                                        UdpSocketFactory::GetTypeId());
          NS_ASSERT(socket != 0);
          socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvHarp, this));
          socket->BindToNetDevice(l3->GetNetDevice(i));
          socket->Bind(InetSocketAddress(iface.GetBroadcast(), HARP_PORT));
          socket->SetAllowBroadcast(true);
          socket->SetIpRecvTtl(true);
          m_socketSubnetBroadcastAddresses.insert(std::make_pair(socket, iface));

          // Add local broadcast record to the routing table
          Ptr<NetDevice> dev = m_ipv4->GetNetDevice(m_ipv4->GetInterfaceForAddress(iface.GetLocal()));
          RoutingTableEntry rt(/*device=*/dev, /*dst=*/iface.GetBroadcast(), /*know seqno=*/true, /*seqno=*/0, /*iface=*/iface,
                               /*hops=*/1, /*next hop=*/iface.GetBroadcast(), /*lifetime=*/Simulator::GetMaximumSimulationTime());
          m_routingTable.AddRoute(rt);
        }
        if (m_socketAddresses.empty())
        {
          NS_LOG_LOGIC("No harp interfaces");
          m_htimer.Cancel();
          m_nb.Clear();
          m_routingTable.Clear();
          return;
        }
      }
      else
      {
        NS_LOG_LOGIC("Remove address not participating in HARP operation");
      }
    }

    bool
    RoutingProtocol::IsMyOwnAddress(Ipv4Address src)
    {
      NS_LOG_FUNCTION(this << src);
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
               m_socketAddresses.begin();
           j != m_socketAddresses.end(); ++j)
      {
        Ipv4InterfaceAddress iface = j->second;
        if (src == iface.GetLocal())
        {
          return true;
        }
      }
      return false;
    }

    Ptr<Ipv4Route>
    RoutingProtocol::LoopbackRoute(const Ipv4Header &hdr, Ptr<NetDevice> oif) const
    {
      NS_LOG_FUNCTION(this << hdr);
      NS_ASSERT(m_lo != 0);
      Ptr<Ipv4Route> rt = Create<Ipv4Route>();
      rt->SetDestination(hdr.GetDestination());
      //
      // Source address selection here is tricky.  The loopback route is
      // returned when HARP does not have a route; this causes the packet
      // to be looped back and handled (cached) in RouteInput() method
      // while a route is found. However, connection-oriented protocols
      // like TCP need to create an endpoint four-tuple (src, src port,
      // dst, dst port) and create a pseudo-header for checksumming.  So,
      // HARP needs to guess correctly what the eventual source address
      // will be.
      //
      // For single interface, single address nodes, this is not a problem.
      // When there are possibly multiple outgoing interfaces, the policy
      // implemented here is to pick the first available HARP interface.
      // If RouteOutput() caller specified an outgoing interface, that
      // further constrains the selection of source address
      //
      std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin();
      if (oif)
      {
        // Iterate to find an address on the oif device
        for (j = m_socketAddresses.begin(); j != m_socketAddresses.end(); ++j)
        {
          Ipv4Address addr = j->second.GetLocal();
          int32_t interface = m_ipv4->GetInterfaceForAddress(addr);
          if (oif == m_ipv4->GetNetDevice(static_cast<uint32_t>(interface)))
          {
            rt->SetSource(addr);
            break;
          }
        }
      }
      else
      {
        rt->SetSource(j->second.GetLocal());
      }
      NS_ASSERT_MSG(rt->GetSource() != Ipv4Address(), "Valid HARP source address not found");
      rt->SetGateway(Ipv4Address("127.0.0.1"));
      rt->SetOutputDevice(m_lo);
      return rt;
    }

    void
    RoutingProtocol::SendRequest(Ipv4Address dst)
    {
      NS_LOG_FUNCTION(this << dst);
      // A node SHOULD NOT originate more than RREQ_RATELIMIT RREQ messages per second.
      if (m_rreqCount == m_rreqRateLimit)
      {
        Simulator::Schedule(m_rreqRateLimitTimer.GetDelayLeft() + MicroSeconds(100),
                            &RoutingProtocol::SendRequest, this, dst);
        return;
      }
      else
      {
        m_rreqCount++;
      }
      // Create RREQ header
      RreqHeader rreqHeader;
      rreqHeader.SetDst(dst);

      RoutingTableEntry rt;
      // Using the Hop field in Routing Table to manage the expanding ring search
      uint16_t ttl = m_ttlStart;
      if (m_routingTable.LookupRoute(dst, rt))
      {
        if (rt.GetFlag() != IN_SEARCH)
        {
          ttl = std::min<uint16_t>(rt.GetMetric() + m_ttlIncrement, m_netDiameter);
        }
        else
        {
          ttl = rt.GetMetric() + m_ttlIncrement;
          if (ttl > m_ttlThreshold)
          {
            ttl = m_netDiameter;
          }
        }
        if (ttl == m_netDiameter)
        {
          rt.IncrementRreqCnt();
        }
        if (rt.GetValidSeqNo())
        {
          rreqHeader.SetDstSeqno(rt.GetSeqNo());
        }
        else
        {
          rreqHeader.SetUnknownSeqno(true);
        }
        rt.SetMetric(ttl);
        rt.SetFlag(IN_SEARCH);
        rt.SetLifeTime(m_pathDiscoveryTime);
        m_routingTable.Update(rt);
      }
      else
      {
        rreqHeader.SetUnknownSeqno(true);
        Ptr<NetDevice> dev = 0;
        RoutingTableEntry newEntry(/*device=*/dev, /*dst=*/dst, /*validSeqNo=*/false, /*seqno=*/0,
                                   /*iface=*/Ipv4InterfaceAddress(), /*hop=*/ttl,
                                   /*nextHop=*/Ipv4Address(), /*lifeTime=*/m_pathDiscoveryTime);
        // Check if TtlStart == NetDiameter
        if (ttl == m_netDiameter)
        {
          newEntry.IncrementRreqCnt();
        }
        newEntry.SetFlag(IN_SEARCH);
        m_routingTable.AddRoute(newEntry);
      }

      if (m_gratuitousReply)
      {
        rreqHeader.SetGratuitousRrep(true);
      }
      if (m_destinationOnly)
      {
        rreqHeader.SetDestinationOnly(true);
      }

      m_seqNo++;
      rreqHeader.SetOriginSeqno(m_seqNo);
      m_requestId++;
      rreqHeader.SetId(m_requestId);

      // Send RREQ as subnet directed broadcast from each interface used by harp
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
               m_socketAddresses.begin();
           j != m_socketAddresses.end(); ++j)
      {
        Ptr<Socket> socket = j->first;
        Ipv4InterfaceAddress iface = j->second;

        rreqHeader.SetOrigin(iface.GetLocal());
        m_rreqIdCache.IsDuplicate(iface.GetLocal(), m_requestId);

        Ptr<Packet> packet = Create<Packet>();
        SocketIpTtlTag tag;
        tag.SetTtl(ttl);
        packet->AddPacketTag(tag);
        packet->AddHeader(rreqHeader);
        TypeHeader tHeader(HARPTYPE_RREQ);
        packet->AddHeader(tHeader);
        // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
        Ipv4Address destination;
        if (iface.GetMask() == Ipv4Mask::GetOnes())
        {
          destination = Ipv4Address("255.255.255.255");
        }
        else
        {
          destination = iface.GetBroadcast();
        }
        NS_LOG_DEBUG("Send RREQ with id " << rreqHeader.GetId() << " to socket");
        m_lastBcastTime = Simulator::Now();
        Simulator::Schedule(Time(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 10))), &RoutingProtocol::SendTo, this, socket, packet, destination);
      }
      ScheduleRreqRetry(dst);
    }

    void
    RoutingProtocol::SendTo(Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination)
    {
      socket->SendTo(packet, 0, InetSocketAddress(destination, HARP_PORT));
    }
    // void
    //  RoutingProtocol::SetOperationMode (OperationMode mode)
    //  {
    //    NS_LOG_FUNCTION (this << mode);
    //    if (m_currentMode == mode)
    //      {
    //        return;
    //      }
    //
    //    m_currentMode = mode;
    //
    //    if (mode == MODE_ACTIVE || mode == MODE_CRITICAL)
    //      {
    //        if (!m_htimer.IsRunning ())
    //          {
    //            m_htimer.SetFunction (&RoutingProtocol::HelloTimerExpire, this);
    //            uint32_t startTime = m_uniformRandomVariable->GetInteger (0, 100);
    //            m_htimer.Schedule (m_helloInterval + MilliSeconds (startTime));
    //            NS_LOG_DEBUG ("Started Hello timer in ACTIVE mode");
    //          }
    //      }
    //    else
    //      {
    //        if (m_htimer.IsRunning ())
    //          {
    //            m_htimer.Cancel ();
    //            NS_LOG_DEBUG ("Stopped Hello timer in SLEEP mode");
    //          }
    //      }
    //  }
    //
    //  void
    //  RoutingProtocol::RunLogicCycle ()
    //  {
    //    NS_LOG_FUNCTION (this);
    //
    //    OperationMode nextMode = GetModeByRules ();
    //
    //    if (nextMode != m_currentMode)
    //      {
    //        SetOperationMode (nextMode);
    //        NS_LOG_DEBUG ("Switched mode from " << m_currentMode << " to " << nextMode);
    //      }
    //
    //    Simulator::Schedule (Seconds (1.0), &RoutingProtocol::RunLogicCycle, this);
    //  }

    // RoutingProtocol::OperationMode
    // RoutingProtocol::GetModeByRules ()
    // {
    //   if (m_packetArrivalRate > 50)
    //     {
    //       return MODE_ACTIVE;
    //     }
    //   return MODE_SLEEP;
    // }
    //
    // double
    // RoutingProtocol::CalculateRecentPdr ()
    // {
    //   if (m_packetsSent == 0)
    //     {
    //       return 0.0;
    //     }
    //   return (double) m_packetsReceived / (double) m_packetsSent;
    // }
    //
    // double
    // RoutingProtocol::CalculateRecentDelay ()
    // {
    //   if (m_packetsReceived == 0)
    //     {
    //       return 0.0;
    //     }
    //   return m_accumulatedDelay.GetSeconds () / (double) m_packetsReceived;
    // }
    //
    // void
    // RoutingProtocol::RecvHello (HelloHeader const & helloHeader, Ipv4Address receiverIfaceAddr)
    // {
    //   NS_LOG_FUNCTION (this << helloHeader.GetNodeId ());
    //
    //   Ipv4Address sender = helloHeader.GetNodeId ();
    //   uint32_t seqNo = helloHeader.GetSeqNo ();
    //
    //   NS_LOG_DEBUG ("Received HELLO from " << sender << " with seqNo " << seqNo);
    //
    //   m_neighborLastSeen[sender] = Simulator::Now ();
    //
    //   std::vector<Ipv4Address> neighbors = helloHeader.GetNeighbors ();
    //
    //   RoutingTableEntry entry;
    //   if (m_routingTable.LookupRoute (sender, entry))
    //     {
    //       if (entry.GetFlag () == VALID && seqNo > entry.GetSeqNo ())
    //         {
    //           entry.SetSeqNo (seqNo);
    //           entry.SetOrigin (ORIGIN_PROACTIVE);
    //           entry.SetMetric (0.0);
    //           entry.SetLifeTime (Time (m_allowedHelloLoss * m_helloInterval));
    //           m_routingTable.Update (entry);
    //           NS_LOG_DEBUG ("Updated proactive route to " << sender << " with seqNo " << seqNo);
    //         }
    //     }
    //   else
    //     {
    //       Ipv4InterfaceAddress iface = m_socketAddresses.begin ()->second;
    //       RoutingTableEntry newEntry (0, sender, true, seqNo, iface,
    //                                    0.0, sender, Time (m_allowedHelloLoss * m_helloInterval),
    //                                    ORIGIN_PROACTIVE);
    //       m_routingTable.AddRoute (newEntry);
    //       NS_LOG_DEBUG ("Added proactive route to " << sender);
    //     }
    // }
    //
    // void
    // RoutingProtocol::CheckNeighbors ()
    // {
    //   NS_LOG_FUNCTION (this);
    //
    //   Time timeout = Time (3 * m_helloInterval);
    //   Time now = Simulator::Now ();
    //
    //   std::map<Ipv4Address, Time>::iterator i = m_neighborLastSeen.begin ();
    //   while (i != m_neighborLastSeen.end ())
    //     {
    //       Ipv4Address neighbor = i->first;
    //       Time lastSeen = i->second;
    //
    //       if (now - lastSeen > timeout)
    //         {
    //           NS_LOG_DEBUG ("Neighbor " << neighbor << " lost, invalidating routes");
    //           m_routingTable.InvalidateRoutesWithNextHop (neighbor);
    //           m_neighborLastSeen.erase (i++);
    //         }
    //       else
    //         {
    //           ++i;
    //         }
    //     }
    //
    //   Simulator::Schedule (m_helloInterval, &RoutingProtocol::CheckNeighbors, this);
    // }
    //
    // void
    // RoutingProtocol::DoInitialize (void)
    // {
    //   NS_LOG_FUNCTION (this);
    //
    //   if (m_enableHello)
    //     {
    //       m_htimer.SetFunction (&RoutingProtocol::HelloTimerExpire, this);
    //     }
    //
    //   Simulator::ScheduleNow (&RoutingProtocol::RunLogicCycle, this);
    //   Simulator::ScheduleNow (&RoutingProtocol::CheckNeighbors, this);

    //   Ipv4RoutingProtocol::DoInitialize ();
    // }

    void
    RoutingProtocol::DoInitialize(void)
    {
      NS_LOG_FUNCTION(this);
      Ipv4RoutingProtocol::DoInitialize();
    }

    void
    RoutingProtocol::RreqRateLimitTimerExpire()
    {
      NS_LOG_FUNCTION(this);
      m_rreqCount = 0;
    }

    void
    RoutingProtocol::RerrRateLimitTimerExpire()
    {
      NS_LOG_FUNCTION(this);
      m_rerrCount = 0;
    }

    bool
    RoutingProtocol::UpdateRouteLifeTime(Ipv4Address addr, Time lt)
    {
      NS_LOG_FUNCTION(this << addr << lt);
      RoutingTableEntry entry;
      if (m_routingTable.LookupValidRoute(addr, entry))
      {
        if (entry.GetLifeTime() < lt)
        {
          entry.SetLifeTime(lt);
          m_routingTable.Update(entry);
        }
      }
      return true;
    }

    void
    RoutingProtocol::SendRerrWhenBreaksLinkToNextHop(Ipv4Address nextHop)
    {
      NS_LOG_FUNCTION(this);
      RoutingTableEntry toNeighbor;
      if (m_routingTable.LookupValidRoute(nextHop, toNeighbor))
      {
        SendRerrWhenBreaksLinkToNextHop(nextHop);
      }
    }

    void
    RoutingProtocol::SendRerrWhenNoRouteToForward(Ipv4Address dst, uint32_t dstSeqNo, Ipv4Address origin)
    {
      NS_LOG_LOGIC("No route to forward to " << dst << " Drop packet");
    }

    void
    RoutingProtocol::RecvHarp(Ptr<Socket> socket)
    {
      NS_LOG_FUNCTION(this << socket);
      Ptr<Packet> packet;
      Address from;
      uint32_t flags = 0;
      while ((packet = socket->RecvFrom(0xFFFFFFFF, flags, from)))
      {
        InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom(from);
        Ipv4Address source = inetSourceAddr.GetIpv4();
        RecvError(packet, source);
      }
    }

    void
    RoutingProtocol::RecvError(Ptr<Packet> p, Ipv4Address src)
    {
      NS_LOG_FUNCTION(this << src << p);
    }

    Ptr<Socket>
    RoutingProtocol::FindSubnetBroadcastSocketWithInterfaceAddress(Ipv4InterfaceAddress addr) const
    {
      NS_LOG_FUNCTION(this << addr);
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
               m_socketSubnetBroadcastAddresses.begin();
           j != m_socketSubnetBroadcastAddresses.end(); ++j)
      {
        Ptr<Socket> socket = j->first;
        Ipv4InterfaceAddress iface = j->second;
        if (iface == addr)
        {
          return socket;
        }
      }
      return 0;
    }
    void
    RoutingProtocol::RouteRequestTimerExpire(Ipv4Address dst)
    {
      NS_LOG_FUNCTION(this << dst);

      // 1. 从定时器列表中移除该目标，因为定时器已经触发了
      m_addressReqTimer.erase(dst);

      // 2. 检查队列中是否有包在等待去往该目的地
      if (m_queue.Find(dst))
      {
        // 3. 再次尝试发起路由请求
        // SendRequest 内部会检查“重试次数” (RreqRetries)
        // 如果未达到上限，它会发送新的 RREQ；如果达到上限，它会丢包。
        SendRequest(dst);
      }
    }
    void
    RoutingProtocol::ScheduleRreqRetry(Ipv4Address dst)
    {
      NS_LOG_FUNCTION(this << dst);
      if (m_addressReqTimer.find(dst) == m_addressReqTimer.end())
      {
        Timer timer(Timer::CANCEL_ON_DESTROY);
        timer.SetFunction(&RoutingProtocol::RouteRequestTimerExpire, this);
        timer.SetArguments(dst);
        m_addressReqTimer[dst] = timer;
      }
      m_addressReqTimer[dst].Schedule(Time(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 10)))); // 引入少量抖动
    }
    Ptr<Socket>
    RoutingProtocol::FindSocketWithInterfaceAddress(Ipv4InterfaceAddress addr) const
    {
      NS_LOG_FUNCTION(this << addr);
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
               m_socketAddresses.begin();
           j != m_socketAddresses.end(); ++j)
      {
        Ptr<Socket> socket = j->first;
        Ipv4InterfaceAddress iface = j->second;
        if (iface == addr)
        {
          return socket;
        }
      }
      return 0;
    }

  } // namespace harp

} // namespace ns3
