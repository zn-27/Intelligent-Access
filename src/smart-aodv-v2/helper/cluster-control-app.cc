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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Cluster Control Application for Smart-AODV-V2
 */

#include "cluster-control-app.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ipv4.h"
#include "ns3/node.h"
#include "ns3/smart-aodv-v2-routing-protocol.h"

namespace ns3 {
namespace smartAodvV2 {

NS_LOG_COMPONENT_DEFINE ("ClusterControlApp");

NS_OBJECT_ENSURE_REGISTERED (ClusterControlApp);

TypeId
ClusterControlApp::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::smartAodvV2::ClusterControlApp")
    .SetParent<Application> ()
    .SetGroupName ("Network")
    .AddConstructor<ClusterControlApp> ()
    .AddTraceSource ("ModeSwitched",
                     "Cluster mode switched",
                     MakeTraceSourceAccessor (&ClusterControlApp::m_modeSwitchedTrace),
                     "ns3::smartAodvV2::ClusterControlApp::ModeSwitchedTracedCallback")
    .AddTraceSource ("ClusterChanged",
                     "Node changed cluster",
                     MakeTraceSourceAccessor (&ClusterControlApp::m_clusterChangedTrace),
                     "ns3::smartAodvV2::ClusterControlApp::ClusterChangedTracedCallback")
    .AddTraceSource ("MemberJoined",
                     "New member joined the cluster",
                     MakeTraceSourceAccessor (&ClusterControlApp::m_memberJoinedTrace),
                     "ns3::smartAodvV2::ClusterControlApp::MemberTracedCallback")
    .AddTraceSource ("MemberLeft",
                     "Member left the cluster",
                     MakeTraceSourceAccessor (&ClusterControlApp::m_memberLeftTrace),
                     "ns3::smartAodvV2::ClusterControlApp::MemberTracedCallback")
    ;
  return tid;
}

ClusterControlApp::ClusterControlApp ()
  : m_routing (0)
{
  NS_LOG_FUNCTION (this);
}

ClusterControlApp::~ClusterControlApp ()
{
  NS_LOG_FUNCTION (this);
}

void
ClusterControlApp::StartApplication ()
{
  NS_LOG_FUNCTION (this);

  // Get the routing protocol from the node
  Ptr<Ipv4> ipv4 = GetNode ()->GetObject<Ipv4> ();
  if (ipv4)
    {
      m_routing = ipv4->GetRoutingProtocol ()->GetObject<RoutingProtocol> ();
    }

  if (!m_routing)
    {
      NS_LOG_ERROR ("ClusterControlApp: Smart-AODV-V2 routing protocol not found!");
    }
  else
    {
      NS_LOG_DEBUG ("ClusterControlApp started on node " << GetNode ()->GetId ());
    }
}

void
ClusterControlApp::StopApplication ()
{
  NS_LOG_FUNCTION (this);
  m_routing = 0;
}

void
ClusterControlApp::SwitchMode (ClusterMode mode)
{
  NS_LOG_FUNCTION (this << mode);

  if (!m_routing)
    {
      NS_LOG_WARN ("Cannot switch mode: routing protocol not available");
      return;
    }

  ClusterMode oldMode = m_routing->GetClusterMode ();
  m_routing->SetClusterMode (mode);

  NS_LOG_UNCOND ("Node " << GetNode ()->GetId ()
                << " switched cluster mode: "
                << (oldMode == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED")
                << " -> "
                << (mode == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED"));

  // Fire trace source
  m_modeSwitchedTrace (m_routing->GetLocalClusterId (), mode);
}

ClusterMode
ClusterControlApp::GetMode () const
{
  return m_routing ? m_routing->GetClusterMode () : MODE_SELF_ORG;
}

uint32_t
ClusterControlApp::GetClusterId () const
{
  return m_routing ? m_routing->GetLocalClusterId () : 0;
}

void
ClusterControlApp::SetClusterId (uint32_t clusterId)
{
  if (m_routing)
    {
      m_routing->SetLocalClusterId (clusterId);
    }
}

Ipv4Address
ClusterControlApp::GetClusterHead () const
{
  return m_routing ? m_routing->GetClusterHeadAddress () : Ipv4Address::GetZero ();
}

void
ClusterControlApp::SetClusterHead (Ipv4Address head)
{
  if (m_routing)
    {
      m_routing->SetClusterHead (head);
    }
}

bool
ClusterControlApp::IsClusterHead () const
{
  return m_routing ? m_routing->IsClusterHead () : false;
}

uint32_t
ClusterControlApp::GetClusterSize () const
{
  return m_routing ? m_routing->GetClusterMemberCount () : 0;
}

void
ClusterControlApp::ScheduleModeSwitch (Time delay, ClusterMode mode)
{
  NS_LOG_FUNCTION (this << delay << mode);
  Simulator::Schedule (delay, &ClusterControlApp::SwitchMode, this, mode);
}

} // namespace smartAodvV2
} // namespace ns3
