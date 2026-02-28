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

#include "q-smart-hybrid-routing-protocol.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac.h"
#include "ns3/pointer.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include <algorithm>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("QSmartHybridRoutingProtocol");

namespace qSmartHybrid
{

NS_OBJECT_ENSURE_REGISTERED (RoutingProtocol);

const uint32_t RoutingProtocol::QSH_PORT = 654;

TypeId
RoutingProtocol::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::qSmartHybrid::RoutingProtocol")
    .SetParent<Ipv4RoutingProtocol> ()
    .SetGroupName ("QSmartHybrid")
    .AddConstructor<RoutingProtocol> ()
    .AddAttribute ("HelloInterval", "HELLO messages emission interval.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&RoutingProtocol::m_helloInterval),
                   MakeTimeChecker ())
    .AddAttribute ("RreqRetries", "Maximum number of retransmissions of RREQ.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::m_rreqRetries),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RreqRateLimit", "Maximum number of RREQ per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::m_rreqRateLimit),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("RerrRateLimit", "Maximum number of RERR per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::m_rerrRateLimit),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("NodeTraversalTime", "Conservative estimate of average one hop traversal time.",
                   TimeValue (MilliSeconds (40)),
                   MakeTimeAccessor (&RoutingProtocol::m_nodeTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("NextHopWait", "Timeout for RREP_ACK reception.",
                   TimeValue (MilliSeconds (50)),
                   MakeTimeAccessor (&RoutingProtocol::m_nextHopWait),
                   MakeTimeChecker ())
    .AddAttribute ("ActiveRouteTimeout", "Period of time during which the route is valid.",
                   TimeValue (Seconds (3)),
                   MakeTimeAccessor (&RoutingProtocol::m_activeRouteTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("NetDiameter", "Maximum possible number of hops between two nodes.",
                   UintegerValue (35),
                   MakeUintegerAccessor (&RoutingProtocol::m_netDiameter),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxQueueLen", "Maximum number of packets to buffer.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&RoutingProtocol::m_maxQueueLen),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxQueueTime", "Maximum period of time to buffer packets.",
                   TimeValue (Seconds (30)),
                   MakeTimeAccessor (&RoutingProtocol::m_maxQueueTime),
                   MakeTimeChecker ())
    .AddAttribute ("DestinationOnly", "Indicates only the destination may respond to RREQ.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RoutingProtocol::m_destinationOnly),
                   MakeBooleanChecker ())
    .AddAttribute ("GratuitousReply", "Indicates whether gratuitous RREP should be sent.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::m_gratuitousReply),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableHello", "Indicates whether hello messages are enabled.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::m_enableHello),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableBroadcast", "Indicates whether broadcast data forwarding is enabled.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::m_enableBroadcast),
                   MakeBooleanChecker ())
    .AddAttribute ("QlearningInterval", "Q-Learning decision interval.",
                   TimeValue (Seconds (5)),
                   MakeTimeAccessor (&RoutingProtocol::m_qlearningInterval),
                   MakeTimeChecker ())
    .AddAttribute ("UniformRandomVariable", "Uniform random variable for jitter.",
                   PointerValue (),
                   MakePointerAccessor (&RoutingProtocol::m_uniformRandomVariable),
                   MakePointerChecker<UniformRandomVariable> ())
    .AddTraceSource ("Tx", "Packet transmission trace.",
                     MakeTraceSourceAccessor (&RoutingProtocol::m_txPacketTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("Rx", "Packet reception trace.",
                     MakeTraceSourceAccessor (&RoutingProtocol::m_rxPacketTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("RoutingTableChanged", "Routing table changed.",
                     MakeTraceSourceAccessor (&RoutingProtocol::m_routingTableChanged),
                     "ns3::RoutingTableChangeTracedCallback")
    .AddTraceSource ("ActionChanged", "Q-Learning action changed.",
                     MakeTraceSourceAccessor (&RoutingProtocol::m_actionChanged),
                     "ns3::qSmartHybrid::RoutingProtocol::ActionChangeTracedCallback");
  return tid;
}

//-----------------------------------------------------------------------------
// IdCache implementation
//-----------------------------------------------------------------------------
bool
IdCache::IsDuplicate (Ipv4Address addr, uint32_t id)
{
  Purge ();
  for (auto it = m_idCache.begin (); it != m_idCache.end (); ++it)
  {
    if (it->m_context == addr && it->m_id == id)
    {
      return true;
    }
  }
  UniqueId entry;
  entry.m_context = addr;
  entry.m_id = id;
  entry.m_expire = Simulator::Now () + m_lifetime;
  m_idCache.push_back (entry);
  return false;
}

//-----------------------------------------------------------------------------
// Neighbors implementation
//-----------------------------------------------------------------------------
Neighbors::Neighbors (Time delay)
  : m_delay (delay)
{
  NS_LOG_FUNCTION (this << delay);
  m_ntimer.SetFunction (&Neighbors::Purge, this);
  m_ntimer.Schedule ();
}

bool
Neighbors::IsNeighbor (Ipv4Address addr) const
{
  for (const auto& nb : m_nb)
  {
    if (nb.m_neighborAddress == addr)
    {
      return true;
    }
  }
  return false;
}

void
Neighbors::Update (Ipv4Address addr, Time expiry)
{
  NS_LOG_FUNCTION (this << addr << expiry);
  for (auto& nb : m_nb)
  {
    if (nb.m_neighborAddress == addr)
    {
      nb.m_expire = expiry;
      return;
    }
  }
  Neighbor nb;
  nb.m_neighborAddress = addr;
  nb.m_expire = expiry;
  nb.m_lastSnr = 0;
  m_nb.push_back (nb);
}

void
Neighbors::UpdateSnr (Ipv4Address addr, double snr)
{
  NS_LOG_FUNCTION (this << addr << snr);
  for (auto& nb : m_nb)
  {
    if (nb.m_neighborAddress == addr)
    {
      nb.m_lastSnr = snr;
      nb.m_snrHistory.push_back (snr);
      if (nb.m_snrHistory.size () > Neighbor::MAX_SNR_HISTORY)
      {
        nb.m_snrHistory.erase (nb.m_snrHistory.begin ());
      }
      return;
    }
  }
}

double
Neighbors::GetSnr (Ipv4Address addr) const
{
  for (const auto& nb : m_nb)
  {
    if (nb.m_neighborAddress == addr)
    {
      return nb.m_lastSnr;
    }
  }
  return 0;
}

double
Neighbors::GetMeanSnr (Ipv4Address addr) const
{
  for (const auto& nb : m_nb)
  {
    if (nb.m_neighborAddress == addr)
    {
      if (nb.m_snrHistory.empty ())
      {
        return nb.m_lastSnr;
      }
      double sum = 0;
      for (double snr : nb.m_snrHistory)
      {
        sum += snr;
      }
      return sum / nb.m_snrHistory.size ();
    }
  }
  return 0;
}

double
Neighbors::GetSnrStdDev (Ipv4Address addr) const
{
  for (const auto& nb : m_nb)
  {
    if (nb.m_neighborAddress == addr)
    {
      if (nb.m_snrHistory.size () < 2)
      {
        return 0;
      }
      double mean = GetMeanSnr (addr);
      double sumSq = 0;
      for (double snr : nb.m_snrHistory)
      {
        sumSq += (snr - mean) * (snr - mean);
      }
      return std::sqrt (sumSq / (nb.m_snrHistory.size () - 1));
    }
  }
  return 0;
}

void
Neighbors::Purge ()
{
  m_nb.erase (std::remove_if (m_nb.begin (), m_nb.end (), IsExpired ()), m_nb.end ());
}

void
Neighbors::ScheduleTimer ()
{
  m_ntimer.Schedule ();
}

std::vector<Ipv4Address>
Neighbors::GetNeighbors () const
{
  std::vector<Ipv4Address> neighbors;
  for (const auto& nb : m_nb)
  {
    neighbors.push_back (nb.m_neighborAddress);
  }
  return neighbors;
}

//-----------------------------------------------------------------------------
// RequestQueue implementation
//-----------------------------------------------------------------------------
bool
RequestQueue::Enqueue (Ipv4Header header, Ptr<const Packet> packet,
                       UnicastForwardCallback ucb, ErrorCallback ecb)
{
  Purge ();
  if (m_queue.size () >= m_maxLen)
  {
    return false;
  }

  QueueEntry entry;
  entry.m_header = header;
  entry.m_packet = packet;
  entry.m_ucb = ucb;
  entry.m_ecb = ecb;
  entry.m_expire = Simulator::Now () + m_queueTimeout;
  m_queue.push_back (entry);
  return true;
}

void
RequestQueue::Dequeue (Ipv4Address dst,
                       std::vector<std::tuple<Ipv4Header, Ptr<const Packet>,
                                              UnicastForwardCallback, ErrorCallback>>& entries)
{
  Purge ();
  entries.clear ();

  for (auto it = m_queue.begin (); it != m_queue.end (); )
  {
    if (it->m_header.GetDestination () == dst)
    {
      entries.push_back (std::make_tuple (it->m_header, it->m_packet,
                                          it->m_ucb, it->m_ecb));
      it = m_queue.erase (it);
    }
    else
    {
      ++it;
    }
  }
}

bool
RequestQueue::Find (Ipv4Address dst) const
{
  for (const auto& entry : m_queue)
  {
    if (entry.m_header.GetDestination () == dst)
    {
      return true;
    }
  }
  return false;
}

void
RequestQueue::Purge ()
{
  m_queue.erase (std::remove_if (m_queue.begin (), m_queue.end (),
                                 [] (const QueueEntry& e) { return e.IsExpired (); }),
                 m_queue.end ());
}

//-----------------------------------------------------------------------------
// RoutingProtocol implementation
//-----------------------------------------------------------------------------
RoutingProtocol::RoutingProtocol ()
  : m_routingTable (Seconds (30)),
    m_queue (64, Seconds (30)),
    m_nb (Seconds (3)),
    m_rreqIdCache (Seconds (30)),
    m_qlearning (0.1, 0.9, 0.1),
    m_txDataPackets (0),
    m_rxDataPackets (0),
    m_txControlPackets (0),
    m_totalDelay (0.0),
    m_prevNeighborCount (0),
    m_neighborChangeRate (0.0)
{
  NS_LOG_FUNCTION (this);
  m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();

  m_netTraversalTime = Seconds (2 * m_netDiameter * m_nodeTraversalTime.GetMilliSeconds () / 1000.0);
  m_pathDiscoveryTime = Seconds (2 * m_netTraversalTime.GetSeconds ());
  m_myRouteTimeout = Seconds (2 * m_activeRouteTimeout.GetSeconds ());
  m_lastBcastTime = Seconds (0);
  m_lastMetricUpdate = Seconds (0);
  m_lastNeighborCheck = Seconds (0);
}

RoutingProtocol::~RoutingProtocol ()
{
  NS_LOG_FUNCTION (this);
}

void
RoutingProtocol::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_routingTable.Clear ();
  m_queue.Purge ();
  m_htimer.Cancel ();
  m_qlearningTimer.Cancel ();
  m_transitionTimer.Cancel ();
  Ipv4RoutingProtocol::DoDispose ();
}

void
RoutingProtocol::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
  Ipv4RoutingProtocol::DoInitialize ();

  // Start timers
  m_htimer.SetFunction (&RoutingProtocol::HelloTimerExpire, this);
  m_htimer.Schedule (MicroSeconds (m_uniformRandomVariable->GetInteger (0, 100000)));

  m_qlearningTimer.SetFunction (&RoutingProtocol::QlearningTimerExpire, this);
  m_qlearningTimer.Schedule (m_qlearningInterval);

  m_transitionTimer.SetFunction (&RoutingProtocol::TransitionTimerExpire, this);
  m_transitionTimer.Schedule (Seconds (1));

  m_rreqRateLimitTimer.SetFunction (&RoutingProtocol::RreqRateLimitTimerExpire, this);
  m_rreqRateLimitTimer.Schedule (Seconds (1));

  m_rerrRateLimitTimer.SetFunction (&RoutingProtocol::RerrRateLimitTimerExpire, this);
  m_rerrRateLimitTimer.Schedule (Seconds (1));
}

void
RoutingProtocol::Start ()
{
  NS_LOG_FUNCTION (this);
  // Initial state
  m_currentDecision.baseAction = A2_WEAK_PROACTIVE;
  m_currentDecision.targetAction = A2_WEAK_PROACTIVE;
  m_currentDecision.transitionFactor = 1.0f;
}

void
RoutingProtocol::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_LOG_FUNCTION (this << ipv4);
  NS_ASSERT (ipv4 != 0);
  NS_ASSERT (m_ipv4 == 0);
  m_ipv4 = ipv4;

  // Create sockets
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces (); ++i)
  {
    NotifyInterfaceUp (i);
  }
}

void
RoutingProtocol::NotifyInterfaceUp (uint32_t interface)
{
  NS_LOG_FUNCTION (this << interface);
  if (m_ipv4->GetNAddresses (interface) > 1)
  {
    for (uint32_t j = 0; j < m_ipv4->GetNAddresses (interface); ++j)
    {
      NotifyAddAddress (interface, m_ipv4->GetAddress (interface, j));
    }
  }
}

void
RoutingProtocol::NotifyInterfaceDown (uint32_t interface)
{
  NS_LOG_FUNCTION (this << interface);
  // Remove sockets
  for (auto it = m_socketAddresses.begin (); it != m_socketAddresses.end (); )
  {
    int32_t ifIndex = m_ipv4->GetInterfaceForAddress (it->second.GetLocal ());
    if (ifIndex >= 0 && static_cast<uint32_t> (ifIndex) == interface)
    {
      it->first->Close ();
      it = m_socketAddresses.erase (it);
    }
    else
    {
      ++it;
    }
  }
  m_routingTable.DeleteAllRoutesFromInterface (m_ipv4->GetAddress (interface, 0));
}

void
RoutingProtocol::NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << interface << address);

  // Skip loopback address
  if (address.GetLocal () == Ipv4Address ("127.0.0.1"))
  {
    return;
  }

  // Check if we already have a socket for this interface
  Ptr<Socket> existingSocket = FindSocketWithInterfaceAddress (address);
  if (existingSocket)
  {
    NS_LOG_DEBUG ("Socket already exists for " << address.GetLocal ());
    return;
  }

  NS_LOG_UNCOND ("Creating socket for " << address.GetLocal () << " on interface " << interface);

  // Create a socket to listen on this interface
  Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (), UdpSocketFactory::GetTypeId ());
  NS_ASSERT (socket != 0);
  socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvQsh, this));
  socket->BindToNetDevice (m_ipv4->GetNetDevice (interface));
  socket->Bind (InetSocketAddress (address.GetLocal (), QSH_PORT));
  socket->SetAllowBroadcast (true);
  m_socketAddresses.insert (std::make_pair (socket, address));

  NS_LOG_UNCOND ("Socket bound to " << address.GetLocal () << " port " << QSH_PORT);

  // Create also a subnet directed broadcast socket
  Ptr<Socket> broadcastSocket = Socket::CreateSocket (GetObject<Node> (), UdpSocketFactory::GetTypeId ());
  NS_ASSERT (broadcastSocket != 0);
  broadcastSocket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvQsh, this));
  broadcastSocket->BindToNetDevice (m_ipv4->GetNetDevice (interface));
  broadcastSocket->Bind (InetSocketAddress (address.GetBroadcast (), QSH_PORT));
  broadcastSocket->SetAllowBroadcast (true);
  m_socketSubnetBroadcastAddresses.insert (std::make_pair (broadcastSocket, address));

  NS_LOG_UNCOND ("Broadcast socket bound to " << address.GetBroadcast () << " port " << QSH_PORT);

  // Set up MAC layer callbacks for cross-layer feedback
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (interface);
  if (dev)
  {
    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice> (dev);
    if (wifiDev)
    {
      // Connect to PHY Rx stats for RSSI/SNR
      Ptr<WifiPhy> phy = wifiDev->GetPhy ();
      if (phy)
      {
        phy->TraceConnectWithoutContext ("MonitorSnifferRx",
          MakeCallback (&RoutingProtocol::PhyRxStats, this));
      }

      // Connect to MAC Tx failed for link failure detection
      Ptr<WifiMac> mac = wifiDev->GetMac ();
      if (mac)
      {
        mac->TraceConnectWithoutContext ("DroppedMpdu",
          MakeCallback (&RoutingProtocol::NotifyTxError, this));
      }
    }
  }
}

void
RoutingProtocol::NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << interface << address);
  // Remove socket for this address
  for (auto it = m_socketAddresses.begin (); it != m_socketAddresses.end (); )
  {
    if (it->second == address)
    {
      it->first->Close ();
      it = m_socketAddresses.erase (it);
      break;
    }
    else
    {
      ++it;
    }
  }
}

Ptr<Ipv4Route>
RoutingProtocol::RouteOutput (Ptr<Packet> p, const Ipv4Header& header,
                              Ptr<NetDevice> oif, Socket::SocketErrno& sockerr)
{
  NS_LOG_FUNCTION (this << header << oif);
  sockerr = Socket::ERROR_NOTERROR;
  Ptr<Ipv4Route> route;

  Ipv4Address dst = header.GetDestination ();

  // Check for broadcast/multicast
  if (dst.IsBroadcast () || dst.IsMulticast ())
  {
    route = Create<Ipv4Route> ();
    route->SetDestination (dst);
    route->SetGateway (Ipv4Address::GetZero ());
    if (oif)
    {
      route->SetOutputDevice (oif);
    }
    else
    {
      route->SetOutputDevice (m_ipv4->GetNetDevice (0));
    }
    return route;
  }

  // Check for subnet broadcast
  for (auto& sa : m_socketAddresses)
  {
    if (dst == sa.second.GetBroadcast ())
    {
      route = Create<Ipv4Route> ();
      route->SetDestination (dst);
      route->SetGateway (Ipv4Address::GetZero ());
      if (oif)
      {
        route->SetOutputDevice (oif);
      }
      else
      {
        route->SetOutputDevice (m_ipv4->GetNetDevice (0));
      }
      return route;
    }
  }

  // Check own address
  if (IsMyOwnAddress (dst))
  {
    return LoopbackRoute (header, oif);
  }

  // Lookup route
  RoutingTableEntry rt;
  if (m_routingTable.SelectBestRoute (dst, rt))
  {
    if (rt.GetFlag () == VALID)
    {
      route = rt.GetRoute ();
      rt.IncrementUsageCount ();
      m_routingTable.Update (rt);
      NS_LOG_DEBUG ("Route found for " << dst << " via " << rt.GetNextHop ());
      return route;
    }
  }

  // No valid route - buffer packet and start route discovery
  NS_LOG_DEBUG ("No route to " << dst << ", starting route discovery");
  if (!m_queue.Find (dst))
  {
    SendRequest (dst);
  }
  sockerr = Socket::ERROR_NOROUTETOHOST;

  return route;
}

bool
RoutingProtocol::RouteInput (Ptr<const Packet> p, const Ipv4Header& header,
                             Ptr<const NetDevice> idev, UnicastForwardCallback ucb,
                             MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                             ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this << header << idev);

  Ipv4Address dst = header.GetDestination ();
  Ipv4Address src = header.GetSource ();

  // Update neighbor info
  for (auto& sa : m_socketAddresses)
  {
    if (sa.second.GetLocal () == header.GetDestination ())
    {
      UpdateRouteToNeighbor (src, header.GetDestination ());
    }
  }

  // Check for local delivery
  if (IsMyOwnAddress (dst))
  {
    lcb (p, header, m_ipv4->GetInterfaceForAddress (dst));
    return true;
  }

  // Check for broadcast (including subnet broadcast) - deliver locally
  bool isSubnetBroadcast = false;
  for (auto& sa : m_socketAddresses)
  {
    if (dst == sa.second.GetBroadcast ())
    {
      isSubnetBroadcast = true;
      break;
    }
  }

  if (dst.IsBroadcast () || dst.IsMulticast () || isSubnetBroadcast)
  {
    // Deliver to local stack
    lcb (p, header, m_ipv4->GetInterfaceForDevice (idev));
    return true;
  }

  // Lookup route
  RoutingTableEntry rt;
  if (m_routingTable.SelectBestRoute (dst, rt))
  {
    if (rt.GetFlag () == VALID)
    {
      // Forward packet
      Ptr<Ipv4Route> route = rt.GetRoute ();
      rt.IncrementUsageCount ();
      m_routingTable.Update (rt);
      ucb (route, p, header);
      return true;
    }
  }

  // No route - send RERR
  SendRerrWhenNoRouteToForward (dst, 0, src);
  ecb (p, header, Socket::ERROR_NOROUTETOHOST);
  return false;
}

void
RoutingProtocol::SetMaxQueueTime (Time t)
{
  m_maxQueueTime = t;
}

void
RoutingProtocol::SetMaxQueueLen (uint32_t len)
{
  m_maxQueueLen = len;
}

void
RoutingProtocol::SetQlearningInterval (Time interval)
{
  m_qlearningInterval = interval;
}

int64_t
RoutingProtocol::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uniformRandomVariable->SetStream (stream);
  m_qlearning.AssignStreams (stream + 1);
  return 2;
}

// ===== MAC Layer Cross-layer =====

void
RoutingProtocol::NotifyTxError (WifiMacDropReason reason, Ptr<const WifiMacQueueItem> mpdu)
{
  NS_LOG_FUNCTION (this << static_cast<uint32_t> (reason));
  // Get next hop from MPDU
  // This is a simplified implementation - in practice would extract from MAC header
  NS_LOG_DEBUG ("MAC layer transmission error detected");
}

void
RoutingProtocol::PhyRxStats (Ptr<const Packet> packet, uint16_t channelFreqMhz,
                             WifiTxVector txVector, MpduInfo mpduInfo,
                             SignalNoiseDbm snr, uint16_t staId)
{
  NS_LOG_FUNCTION (this << snr.signal << snr.noise);

  // Calculate SNR in dB
  double snrDb = snr.signal - snr.noise;

  // Update neighbor SNR info
  // In practice, would need to map packet to source address
  // For now, this updates statistics for tracking purposes
  NS_LOG_DEBUG ("PHY Rx stats: signal=" << snr.signal << "dBm noise=" << snr.noise
                << "dBm SNR=" << snrDb << "dB");
}

bool
RoutingProtocol::ShouldForceInvalidateRoute (Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this << nextHop);

  // Get SNR statistics
  double meanSnr = m_nb.GetMeanSnr (nextHop);
  double stdSnr = m_nb.GetSnrStdDev (nextHop);
  double currentSnr = m_nb.GetSnr (nextHop);

  // Dynamic adaptive threshold: current < mean - 2*std
  bool rssiTrigger = false;
  if (stdSnr > 0 && currentSnr < meanSnr - 2 * stdSnr)
  {
    rssiTrigger = true;
  }

  // Check consecutive failures
  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (nextHop, rt))
  {
    bool failureTrigger = rt.GetConsecutiveTxFailures () >= 3;
    return rssiTrigger || failureTrigger;
  }

  return rssiTrigger;
}

void
RoutingProtocol::OnMacLayerFailure (Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this << nextHop);

  if (ShouldForceInvalidateRoute (nextHop))
  {
    NS_LOG_DEBUG ("Force invalidating routes via " << nextHop);

    // Get all destinations using this next hop
    std::map<Ipv4Address, uint32_t> unreachable;
    m_routingTable.GetListOfDestinationWithNextHop (nextHop, unreachable);

    // Invalidate routes
    m_routingTable.InvalidateRoutesWithDst (unreachable);

    // Send preemptive RREQ for affected destinations
    for (const auto& entry : unreachable)
    {
      SendPreemptiveRreq (entry.first);
    }
  }
}

void
RoutingProtocol::SendPreemptiveRreq (Ipv4Address destination)
{
  NS_LOG_FUNCTION (this << destination);
  NS_LOG_DEBUG ("Sending preemptive RREQ for " << destination);
  SendRequest (destination);
}

Time
RoutingProtocol::PredictLinkExpiry (double rssi)
{
  NS_LOG_FUNCTION (this << rssi);
  // Simple prediction based on RSSI threshold
  // In practice, this would use more sophisticated prediction
  if (rssi < -90)
  {
    return Seconds (1);
  }
  else if (rssi < -80)
  {
    return Seconds (5);
  }
  else if (rssi < -70)
  {
    return Seconds (15);
  }
  else
  {
    return Seconds (30);
  }
}

// ===== OLSR Integration =====

Time
RoutingProtocol::GetActualHelloInterval () const
{
  return GetActualInterval (m_currentDecision.baseAction,
                            m_currentDecision.targetAction,
                            m_currentDecision.transitionFactor, true);
}

Time
RoutingProtocol::GetActualTcInterval () const
{
  return GetActualInterval (m_currentDecision.baseAction,
                            m_currentDecision.targetAction,
                            m_currentDecision.transitionFactor, false);
}

void
RoutingProtocol::ApplyAction (const QLearningDecision& decision)
{
  NS_LOG_FUNCTION (this << decision.baseAction << decision.targetAction);

  ActionConfig config = GetActionConfig (decision.baseAction);

  // Update hello interval
  m_helloInterval = GetActualHelloInterval ();

  NS_LOG_DEBUG ("Applied action: HelloInterval=" << m_helloInterval.GetSeconds () << "s");
}

void
RoutingProtocol::UpdateTransition ()
{
  if (m_currentDecision.InTransition ())
  {
    m_currentDecision.transitionFactor -= 0.1f;
    if (m_currentDecision.transitionFactor <= 0)
    {
      m_currentDecision.baseAction = m_currentDecision.targetAction;
      m_currentDecision.transitionFactor = 1.0f;
      NS_LOG_DEBUG ("Transition complete, now at action " << m_currentDecision.baseAction);
    }
  }
}

// ===== Q-Learning =====

QState
RoutingProtocol::CollectState ()
{
  NS_LOG_FUNCTION (this);

  // Get node speed (would need mobility model access)
  double nodeSpeed = 0.0; // Placeholder

  // Calculate neighbor change rate
  double currentNeighborCount = m_nb.GetNbrCount ();
  Time now = Simulator::Now ();
  double timeDelta = (now - m_lastNeighborCheck).GetSeconds ();

  if (timeDelta > 0)
  {
    m_neighborChangeRate = std::abs (currentNeighborCount - m_prevNeighborCount) / timeDelta;
  }
  m_prevNeighborCount = currentNeighborCount;
  m_lastNeighborCheck = now;

  // Calculate PDR
  double pdr = 0.0;
  if (m_txDataPackets > 0)
  {
    pdr = static_cast<double> (m_rxDataPackets) / m_txDataPackets;
  }

  // Calculate SNR variance (average across neighbors)
  double snrVariance = 0.0;
  auto neighbors = m_nb.GetNeighbors ();
  if (!neighbors.empty ())
  {
    for (const auto& addr : neighbors)
    {
      snrVariance += m_nb.GetSnrStdDev (addr);
    }
    snrVariance /= neighbors.size ();
  }

  // Get queue length
  uint32_t queueLength = m_queue.GetSize ();

  return QLearning::CreateState (nodeSpeed, m_neighborChangeRate, pdr,
                                 snrVariance, queueLength);
}

PerformanceMetrics
RoutingProtocol::CollectMetrics ()
{
  NS_LOG_FUNCTION (this);

  PerformanceMetrics metrics;

  // PDR
  if (m_txDataPackets > 0)
  {
    metrics.pdr = static_cast<double> (m_rxDataPackets) / m_txDataPackets;
  }

  // Average delay
  if (m_rxDataPackets > 0)
  {
    metrics.avgDelay = m_totalDelay / m_rxDataPackets;
  }

  // Control packets
  metrics.controlPackets = m_txControlPackets;

  return metrics;
}

void
RoutingProtocol::QlearningTimerExpire ()
{
  NS_LOG_FUNCTION (this);

  // Collect current state
  QState state = CollectState ();

  // Get performance metrics
  PerformanceMetrics metrics = CollectMetrics ();

  // Calculate reward
  double reward = m_qlearning.CalculateReward (metrics);

  // Update Q-value for previous state-action pair
  QState nextState = state;  // For simplicity, assume state doesn't change immediately
  m_qlearning.Update (m_prevState, m_currentDecision.baseAction, nextState, reward);
  m_prevState = state;

  // Choose action
  Action newAction = m_qlearning.ChooseAction (state);
  double newQ = m_qlearning.GetQValue (state, newAction);
  double currentQ = m_qlearning.GetQValue (state, m_currentDecision.baseAction);

  // Check if should switch (with hysteresis)
  if (m_qlearning.ShouldSwitchAction (m_currentDecision.baseAction, newAction, newQ, currentQ))
  {
    NS_LOG_DEBUG ("Action switch: " << m_currentDecision.baseAction << " -> " << newAction);

    // Start transition
    m_currentDecision.targetAction = newAction;
    m_currentDecision.transitionFactor = 1.0f;

    // Fire trace
    m_actionChanged (m_currentDecision.baseAction, newAction, m_currentDecision.transitionFactor);
  }

  // Apply current action
  ApplyAction (m_currentDecision);

  // Schedule next Q-Learning decision
  m_qlearningTimer.Schedule (m_qlearningInterval);
}

void
RoutingProtocol::UpdatePerformanceStats ()
{
  // Reset counters periodically
  m_txDataPackets = 0;
  m_rxDataPackets = 0;
  m_txControlPackets = 0;
  m_totalDelay = 0;
}

void
RoutingProtocol::TransitionTimerExpire ()
{
  UpdateTransition ();
  m_transitionTimer.Schedule (Seconds (1));
}

// ===== Timers =====

void
RoutingProtocol::HelloTimerExpire ()
{
  NS_LOG_FUNCTION (this);
  SendHello ();
  m_htimer.Schedule (GetActualHelloInterval ());
}

void
RoutingProtocol::RreqRateLimitTimerExpire ()
{
  m_rreqCount = 0;
  m_rreqRateLimitTimer.Schedule (Seconds (1));
}

void
RoutingProtocol::RerrRateLimitTimerExpire ()
{
  m_rerrCount = 0;
  m_rerrRateLimitTimer.Schedule (Seconds (1));
}

// ===== Helper functions =====

bool
RoutingProtocol::IsMyOwnAddress (Ipv4Address src)
{
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces (); ++i)
  {
    for (uint32_t j = 0; j < m_ipv4->GetNAddresses (i); ++j)
    {
      if (m_ipv4->GetAddress (i, j).GetLocal () == src)
      {
        return true;
      }
    }
  }
  return false;
}

Ptr<Socket>
RoutingProtocol::FindSocketWithInterfaceAddress (Ipv4InterfaceAddress iface) const
{
  for (auto it = m_socketAddresses.begin (); it != m_socketAddresses.end (); ++it)
  {
    if (it->second == iface)
    {
      return it->first;
    }
  }
  return nullptr;
}

Ptr<Socket>
RoutingProtocol::FindSubnetBroadcastSocketWithInterfaceAddress (Ipv4InterfaceAddress iface) const
{
  for (auto it = m_socketSubnetBroadcastAddresses.begin ();
       it != m_socketSubnetBroadcastAddresses.end (); ++it)
  {
    if (it->second == iface)
    {
      return it->first;
    }
  }
  return nullptr;
}

Ptr<Ipv4Route>
RoutingProtocol::LoopbackRoute (const Ipv4Header& header, Ptr<NetDevice> oif) const
{
  NS_LOG_FUNCTION (this << header << oif);
  Ptr<Ipv4Route> route = Create<Ipv4Route> ();
  route->SetDestination (header.GetDestination ());
  route->SetSource (header.GetSource ());
  route->SetOutputDevice (m_lo);
  return route;
}

void
RoutingProtocol::UpdateRouteToNeighbor (Ipv4Address sender, Ipv4Address receiver)
{
  NS_LOG_FUNCTION (this << sender << receiver);
  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (sender, rt))
  {
    rt.SetLifeTime (m_activeRouteTimeout);
    rt.SetFlag (VALID);
    m_routingTable.Update (rt);
  }
  else
  {
    // Create new entry
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
    RoutingTableEntry newEntry (dev, sender, false, 0,
                                m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
                                1, sender, m_activeRouteTimeout);
    newEntry.SetProtocolSource (REACTIVE_SAODV);
    m_routingTable.AddRoute (newEntry);
  }

  m_nb.Update (sender, Simulator::Now () + m_activeRouteTimeout);
}

// ===== Receive handlers =====

void
RoutingProtocol::RecvQsh (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);

  Address srcAddr;
  Ptr<Packet> packet = socket->RecvFrom (srcAddr);

  if (!InetSocketAddress::IsMatchingType (srcAddr))
  {
    NS_LOG_DEBUG ("Source address is not InetSocketAddress");
    return;
  }

  InetSocketAddress inetAddr = InetSocketAddress::ConvertFrom (srcAddr);
  Ipv4Address src = inetAddr.GetIpv4 ();

  TypeHeader typeHeader;
  packet->RemoveHeader (typeHeader);
  if (!typeHeader.IsValid ())
  {
    NS_LOG_DEBUG ("Invalid packet type received");
    return;
  }

  NS_LOG_DEBUG ("Received " << typeHeader << " from " << src);
  m_rxPacketTrace (packet);

  // Get local address from socket map
  Ipv4Address localAddr;
  auto it = m_socketAddresses.find (socket);
  if (it != m_socketAddresses.end ())
  {
    localAddr = it->second.GetLocal ();
  }
  else
  {
    // Fallback: use first available address
    if (!m_socketAddresses.empty ())
    {
      localAddr = m_socketAddresses.begin ()->second.GetLocal ();
    }
  }

  switch (typeHeader.Get ())
  {
    case QSHHYBRIDTYPE_RREQ:
      RecvRequest (packet, localAddr, src);
      break;
    case QSHHYBRIDTYPE_RREP:
      RecvReply (packet, localAddr, src);
      break;
    case QSHHYBRIDTYPE_RERR:
      RecvError (packet, src);
      break;
    case QSHHYBRIDTYPE_RREP_ACK:
      RecvReplyAck (src);
      break;
    default:
      NS_LOG_DEBUG ("Unknown packet type");
  }
}

void
RoutingProtocol::RecvRequest (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)
{
  NS_LOG_FUNCTION (this << receiver << src);
  RreqHeader rreqHeader;
  p->RemoveHeader (rreqHeader);

  // Check for duplicate
  if (m_rreqIdCache.IsDuplicate (rreqHeader.GetOrigin (), rreqHeader.GetId ()))
  {
    NS_LOG_DEBUG ("Duplicate RREQ from " << rreqHeader.GetOrigin ());
    return;
  }

  // Use the origin address from the header as the actual source
  // (since socket's RecvFrom might return incorrect address for broadcast)
  Ipv4Address actualSrc = rreqHeader.GetOrigin ();
  if (rreqHeader.GetHopCount () > 0)
  {
    // If hop count > 0, the packet was forwarded, so we need to use the
    // immediate sender. For now, we'll use the origin address and fix this later.
    // In a proper implementation, we would get the immediate sender from the IP header.
    actualSrc = src;  // This is the immediate sender (might be incorrect)
  }

  NS_LOG_DEBUG ("RREQ from " << rreqHeader.GetOrigin () << " hop " << (uint32_t)rreqHeader.GetHopCount () << " src " << src);

  // Update SNR info
  double minSnr = rreqHeader.GetMinSnr ();
  m_nb.UpdateSnr (actualSrc, minSnr);

  // Update or create route to origin
  RoutingTableEntry toOrigin;
  if (!m_routingTable.LookupRoute (rreqHeader.GetOrigin (), toOrigin))
  {
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
    toOrigin = RoutingTableEntry (dev, rreqHeader.GetOrigin (), true,
                                  rreqHeader.GetOriginSeqno (),
                                  m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
                                  rreqHeader.GetHopCount () + 1, actualSrc, m_activeRouteTimeout);
    toOrigin.SetMinSnr (minSnr);
    toOrigin.SetProtocolSource (REACTIVE_SAODV);
    m_routingTable.AddRoute (toOrigin);
    NS_LOG_DEBUG ("Added route to " << rreqHeader.GetOrigin () << " via " << actualSrc);
  }
  else
  {
    // Update if newer
    if (rreqHeader.GetOriginSeqno () > toOrigin.GetSeqNo () ||
        (rreqHeader.GetOriginSeqno () == toOrigin.GetSeqNo () &&
         rreqHeader.GetHopCount () + 1 < toOrigin.GetHop ()))
    {
      toOrigin.SetSeqNo (rreqHeader.GetOriginSeqno ());
      toOrigin.SetHop (rreqHeader.GetHopCount () + 1);
      toOrigin.SetNextHop (actualSrc);
      toOrigin.SetLifeTime (m_activeRouteTimeout);
      toOrigin.SetMinSnr (std::min (minSnr, toOrigin.GetMinSnr ()));
      m_routingTable.Update (toOrigin);
      NS_LOG_DEBUG ("Updated route to " << rreqHeader.GetOrigin () << " via " << actualSrc);
    }
  }

  NS_LOG_DEBUG ("RREQ looking for dst " << rreqHeader.GetDst () << " my address is " << receiver);

  // Check if destination is this node
  if (IsMyOwnAddress (rreqHeader.GetDst ()))
  {
    NS_LOG_DEBUG ("I am the destination, sending RREP");
    // Send RREP
    SendReply (rreqHeader, toOrigin);
    return;
  }

  // Check if we have a route to destination
  RoutingTableEntry toDst;
  if (m_routingTable.LookupValidRoute (rreqHeader.GetDst (), toDst))
  {
    NS_LOG_DEBUG ("Have valid route to " << rreqHeader.GetDst () << ", sending intermediate RREP");
    // Intermediate node with valid route
    SendReplyByIntermediateNode (toDst, toOrigin, rreqHeader.GetGratuitousRrep ());
    return;
  }

  NS_LOG_DEBUG ("Forwarding RREQ for " << rreqHeader.GetDst ());
  // Forward RREQ
  rreqHeader.SetHopCount (rreqHeader.GetHopCount () + 1);
  rreqHeader.SetMinSnr (std::min (minSnr, m_nb.GetSnr (src)));

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rreqHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RREQ);
  packet->AddHeader (typeHeader);

  // Broadcast
  for (auto& sa : m_socketAddresses)
  {
    Ptr<Socket> socket = sa.first;
    SendTo (socket, packet, sa.second.GetBroadcast ());
  }

  m_txControlPackets++;
}

void
RoutingProtocol::RecvReply (Ptr<Packet> p, Ipv4Address my, Ipv4Address src)
{
  NS_LOG_FUNCTION (this << my << src);
  RrepHeader rrepHeader;
  p->RemoveHeader (rrepHeader);

  // Check if this is a Hello message (dst == origin and hopCount == 0)
  if (rrepHeader.GetDst () == rrepHeader.GetOrigin () && rrepHeader.GetHopCount () == 0)
  {
    NS_LOG_DEBUG ("Received Hello from " << src);
    ProcessHello (rrepHeader, my);
    return;
  }

  // Update route to destination
  RoutingTableEntry rt;
  if (!m_routingTable.LookupRoute (rrepHeader.GetDst (), rt))
  {
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (my));
    rt = RoutingTableEntry (dev, rrepHeader.GetDst (), true, rrepHeader.GetDstSeqno (),
                            m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (my), 0),
                            rrepHeader.GetHopCount (), src, rrepHeader.GetLifeTime ());
    rt.SetProtocolSource (REACTIVE_SAODV);
    m_routingTable.AddRoute (rt);
  }
  else
  {
    rt.SetSeqNo (rrepHeader.GetDstSeqno ());
    rt.SetHop (rrepHeader.GetHopCount ());
    rt.SetNextHop (src);
    rt.SetLifeTime (rrepHeader.GetLifeTime ());
    rt.SetFlag (VALID);
    m_routingTable.Update (rt);
  }

  // Send buffered packets
  Ptr<Ipv4Route> route = rt.GetRoute ();
  SendPacketFromQueue (rrepHeader.GetDst (), route);

  // If not destination, forward RREP
  if (!IsMyOwnAddress (rrepHeader.GetOrigin ()))
  {
    RoutingTableEntry toOrigin;
    if (m_routingTable.LookupValidRoute (rrepHeader.GetOrigin (), toOrigin))
    {
      rrepHeader.SetHopCount (rrepHeader.GetHopCount () + 1);
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (rrepHeader);
      TypeHeader typeHeader (QSHHYBRIDTYPE_RREP);
      packet->AddHeader (typeHeader);

      Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigin.GetInterface ());
      SendTo (socket, packet, toOrigin.GetNextHop ());
      m_txControlPackets++;
    }
  }
}

void
RoutingProtocol::RecvReplyAck (Ipv4Address neighbor)
{
  NS_LOG_FUNCTION (this << neighbor);
  // Mark link as bidirectional
  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (neighbor, rt))
  {
    rt.SetUnidirectional (false);
    m_routingTable.Update (rt);
  }
}

void
RoutingProtocol::RecvError (Ptr<Packet> p, Ipv4Address src)
{
  NS_LOG_FUNCTION (this << src);
  RerrHeader rerrHeader;
  p->RemoveHeader (rerrHeader);

  // Process unreachable destinations
  std::pair<Ipv4Address, uint32_t> un;
  while (rerrHeader.RemoveUnDestination (un))
  {
    m_routingTable.SetEntryState (un.first, INVALID);
  }
}

// ===== Send handlers =====

void
RoutingProtocol::SendHello ()
{
  NS_LOG_FUNCTION (this);
  if (!m_enableHello)
  {
    return;
  }

  // Create RREP as Hello
  RrepHeader helloHeader;
  helloHeader.SetHello (m_ipv4->GetAddress (1, 0).GetLocal (), m_seqNo, m_activeRouteTimeout);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (helloHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RREP);
  packet->AddHeader (typeHeader);

  for (auto& sa : m_socketAddresses)
  {
    SendTo (sa.first, packet->Copy (), sa.second.GetBroadcast ());
  }

  m_txControlPackets++;
  m_seqNo++;
}

void
RoutingProtocol::SendRequest (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);

  // Rate limiting
  if (m_rreqCount >= m_rreqRateLimit)
  {
    NS_LOG_DEBUG ("RREQ rate limit exceeded");
    return;
  }
  m_rreqCount++;

  RreqHeader rreqHeader;
  rreqHeader.SetDst (dst);
  rreqHeader.SetDstSeqno (0);
  rreqHeader.SetOrigin (m_ipv4->GetAddress (1, 0).GetLocal ());
  rreqHeader.SetOriginSeqno (m_seqNo);
  rreqHeader.SetId (m_requestId++);
  rreqHeader.SetHopCount (0);
  rreqHeader.SetMinSnr (30.0);  // Start with max SNR

  if (m_destinationOnly)
  {
    rreqHeader.SetDestinationOnly (true);
  }
  if (m_gratuitousReply)
  {
    rreqHeader.SetGratuitousRrep (true);
  }

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rreqHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RREQ);
  packet->AddHeader (typeHeader);

  for (auto& sa : m_socketAddresses)
  {
    SendTo (sa.first, packet->Copy (), sa.second.GetBroadcast ());
  }

  m_txControlPackets++;
  m_seqNo++;
}

void
RoutingProtocol::SendReply (RreqHeader const& rreqHeader, RoutingTableEntry const& toOrigin)
{
  NS_LOG_FUNCTION (this);

  RrepHeader rrepHeader;
  rrepHeader.SetDst (rreqHeader.GetDst ());
  rrepHeader.SetDstSeqno (m_seqNo);
  rrepHeader.SetOrigin (rreqHeader.GetOrigin ());
  rrepHeader.SetHopCount (0);
  rrepHeader.SetLifeTime (m_myRouteTimeout);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rrepHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RREP);
  packet->AddHeader (typeHeader);

  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigin.GetInterface ());
  SendTo (socket, packet, toOrigin.GetNextHop ());

  m_txControlPackets++;
  m_seqNo++;
}

void
RoutingProtocol::SendReplyByIntermediateNode (RoutingTableEntry& toDst,
                                               RoutingTableEntry& toOrigin, bool gratRep)
{
  NS_LOG_FUNCTION (this);

  RrepHeader rrepHeader;
  rrepHeader.SetDst (toDst.GetDestination ());
  rrepHeader.SetDstSeqno (toDst.GetSeqNo ());
  rrepHeader.SetOrigin (toOrigin.GetDestination ());
  rrepHeader.SetHopCount (toDst.GetHop ());
  rrepHeader.SetLifeTime (toDst.GetLifeTime ());

  // Update hop counts
  toDst.SetHop (toDst.GetHop () + 1);
  toOrigin.SetHop (toOrigin.GetHop () + 1);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rrepHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RREP);
  packet->AddHeader (typeHeader);

  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigin.GetInterface ());
  SendTo (socket, packet, toOrigin.GetNextHop ());

  m_txControlPackets++;
}

void
RoutingProtocol::SendReplyAck (Ipv4Address neighbor)
{
  NS_LOG_FUNCTION (this << neighbor);

  RrepAckHeader ackHeader;
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (ackHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RREP_ACK);
  packet->AddHeader (typeHeader);

  for (auto& sa : m_socketAddresses)
  {
    RoutingTableEntry rt;
    if (m_routingTable.LookupRoute (neighbor, rt))
    {
      if (rt.GetInterface () == sa.second)
      {
        SendTo (sa.first, packet, neighbor);
        m_txControlPackets++;
        break;
      }
    }
  }
}

void
RoutingProtocol::SendRerrWhenBreaksLinkToNextHop (Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this << nextHop);

  std::map<Ipv4Address, uint32_t> unreachable;
  m_routingTable.GetListOfDestinationWithNextHop (nextHop, unreachable);

  if (unreachable.empty ())
  {
    return;
  }

  RerrHeader rerrHeader;
  for (const auto& entry : unreachable)
  {
    rerrHeader.AddUnDestination (entry.first, entry.second);
  }

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rerrHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RERR);
  packet->AddHeader (typeHeader);

  // Broadcast RERR
  for (auto& sa : m_socketAddresses)
  {
    SendTo (sa.first, packet->Copy (), sa.second.GetBroadcast ());
  }

  m_txControlPackets++;
  m_routingTable.InvalidateRoutesWithDst (unreachable);
}

void
RoutingProtocol::SendRerrMessage (Ptr<Packet> packet, std::vector<Ipv4Address> precursors)
{
  NS_LOG_FUNCTION (this);

  if (precursors.empty ())
  {
    return;
  }

  for (const auto& precursor : precursors)
  {
    RoutingTableEntry rt;
    if (m_routingTable.LookupRoute (precursor, rt))
    {
      Ptr<Socket> socket = FindSocketWithInterfaceAddress (rt.GetInterface ());
      SendTo (socket, packet->Copy (), precursor);
    }
  }
  m_txControlPackets++;
}

void
RoutingProtocol::SendRerrWhenNoRouteToForward (Ipv4Address dst, uint32_t dstSeqNo, Ipv4Address origin)
{
  NS_LOG_FUNCTION (this << dst << origin);

  if (m_rerrCount >= m_rerrRateLimit)
  {
    return;
  }
  m_rerrCount++;

  RerrHeader rerrHeader;
  rerrHeader.AddUnDestination (dst, dstSeqNo);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rerrHeader);
  TypeHeader typeHeader (QSHHYBRIDTYPE_RERR);
  packet->AddHeader (typeHeader);

  for (auto& sa : m_socketAddresses)
  {
    SendTo (sa.first, packet->Copy (), sa.second.GetBroadcast ());
  }

  m_txControlPackets++;
}

void
RoutingProtocol::SendPacketFromQueue (Ipv4Address dst, Ptr<Ipv4Route> route)
{
  NS_LOG_FUNCTION (this << dst);

  std::vector<std::tuple<Ipv4Header, Ptr<const Packet>,
                         UnicastForwardCallback, ErrorCallback>> entries;
  m_queue.Dequeue (dst, entries);

  for (auto& entry : entries)
  {
    Ipv4Header header = std::get<0> (entry);
    Ptr<const Packet> packet = std::get<1> (entry);
    UnicastForwardCallback ucb = std::get<2> (entry);
    ErrorCallback ecb = std::get<3> (entry);

    ucb (route, packet, header);
    m_txDataPackets++;
  }
}

void
RoutingProtocol::SendTo (Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination)
{
  NS_LOG_FUNCTION (this << destination);
  socket->SendTo (packet, 0, InetSocketAddress (destination, QSH_PORT));
  m_txPacketTrace (packet);
}

void
RoutingProtocol::ProcessHello (RrepHeader const& rrepHeader, Ipv4Address receiverIfaceAddr)
{
  NS_LOG_FUNCTION (this);
  UpdateRouteToNeighbor (rrepHeader.GetDst (), receiverIfaceAddr);
}

void
RoutingProtocol::ScheduleRreqRetry (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);

  Timer& timer = m_addressReqTimer[dst];
  timer.SetFunction (&RoutingProtocol::RouteRequestTimerExpire, this);
  timer.SetArguments (dst);
  timer.Schedule (m_netTraversalTime);
}

void
RoutingProtocol::RouteRequestTimerExpire (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);

  RoutingTableEntry rt;
  if (m_routingTable.LookupValidRoute (dst, rt))
  {
    SendPacketFromQueue (dst, rt.GetRoute ());
    m_addressReqTimer.erase (dst);
    return;
  }

  if (rt.GetRreqCnt () < m_rreqRetries)
  {
    rt.IncrementRreqCnt ();
    m_routingTable.Update (rt);
    SendRequest (dst);
    ScheduleRreqRetry (dst);
  }
  else
  {
    m_routingTable.DeleteRoute (dst);
    m_addressReqTimer.erase (dst);
  }
}

bool
RoutingProtocol::UpdateRouteLifeTime (Ipv4Address addr, Time lt)
{
  NS_LOG_FUNCTION (this << addr << lt);
  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (addr, rt))
  {
    rt.SetLifeTime (std::max (rt.GetLifeTime (), lt));
    m_routingTable.Update (rt);
    return true;
  }
  return false;
}

void
RoutingProtocol::AckTimerExpire (Ipv4Address neighbor, Time blacklistTimeout)
{
  NS_LOG_FUNCTION (this << neighbor);
  m_routingTable.MarkLinkAsUnidirectional (neighbor, blacklistTimeout);
}

void
RoutingProtocol::PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  m_routingTable.Print (stream, unit);
}

void
RoutingProtocol::DeferredRouteOutput (Ptr<const Packet> p, const Ipv4Header& header,
                                      UnicastForwardCallback ucb, ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this << header.GetDestination ());
  m_queue.Enqueue (header, p, ucb, ecb);
}

bool
RoutingProtocol::Forwarding (Ptr<const Packet> p, const Ipv4Header& header,
                             UnicastForwardCallback ucb, ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this << header.GetDestination ());

  Ipv4Address dst = header.GetDestination ();
  RoutingTableEntry rt;

  if (m_routingTable.SelectBestRoute (dst, rt))
  {
    if (rt.GetFlag () == VALID)
    {
      Ptr<Ipv4Route> route = rt.GetRoute ();
      rt.IncrementUsageCount ();
      m_routingTable.Update (rt);
      ucb (route, p, header);
      return true;
    }
  }

  return false;
}

} // namespace qSmartHybrid
} // namespace ns3
