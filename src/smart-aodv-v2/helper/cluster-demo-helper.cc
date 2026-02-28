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
 * Helper utilities for cluster demonstration scripts
 */

#include "cluster-demo-helper.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ipv4.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/enum.h"
#include "ns3/mobility-module.h"
#include "ns3/smart-aodv-v2-routing-protocol.h"
#include "cluster-control-app.h"
#include <iomanip>
#include <sstream>

namespace ns3 {
namespace smartAodvV2 {

NS_LOG_COMPONENT_DEFINE ("ClusterDemoHelper");

ClusterDemoHelper::ClusterDemoHelper ()
{
}

ClusterDemoHelper::~ClusterDemoHelper ()
{
}

NodeContainer
ClusterDemoHelper::CreateMobileNodes (uint32_t numNodes,
                                       uint32_t numClusters,
                                       Rectangle bounds,
                                       double speedMin,
                                       double speedMax)
{
  NS_LOG_FUNCTION (numNodes << numClusters << bounds << speedMin << speedMax);

  NodeContainer nodes;
  nodes.Create (numNodes);

  // Calculate grid layout
  uint32_t nodesPerCluster = numNodes / numClusters;
  uint32_t cols = nodesPerCluster;  // Nodes per row in each cluster

  // Position allocator: spread nodes across the simulation area
  MobilityHelper mobility;

  std::stringstream ssSpeed;
  ssSpeed << "ns3::UniformRandomVariable[Min=" << speedMin << "|Max=" << speedMax << "]";

  // Use RandomWalk2d mobility model
  mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                            "Mode", EnumValue (RandomWalk2dMobilityModel::MODE_TIME),
                            "Time", TimeValue (Seconds (5.0)),
                            "Speed", StringValue (ssSpeed.str ()),
                            "Bounds", RectangleValue (bounds));

  // Set initial positions in a grid pattern
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                "MinX", DoubleValue (bounds.xMin + 10.0),
                                "MinY", DoubleValue (bounds.yMin + 10.0),
                                "DeltaX", DoubleValue (30.0),
                                "DeltaY", DoubleValue (30.0),
                                "GridWidth", UintegerValue (cols),
                                "LayoutType", StringValue ("RowFirst"));

  mobility.Install (nodes);

  return nodes;
}

void
ClusterDemoHelper::ConfigureClusters (NodeContainer& nodes,
                                       Ipv4InterfaceContainer& interfaces,
                                       uint32_t numClusters,
                                       std::vector<uint32_t>& clusterHeads)
{
  NS_LOG_FUNCTION (nodes.GetN () << numClusters);

  uint32_t numNodes = nodes.GetN ();
  uint32_t nodesPerCluster = numNodes / numClusters;

  clusterHeads.clear ();

  NS_LOG_UNCOND ("Cluster Configuration:");
  for (uint32_t c = 1; c <= numClusters; ++c)
    {
      uint32_t startIdx = (c - 1) * nodesPerCluster;
      uint32_t endIdx = (c == numClusters) ? (numNodes - 1) : (startIdx + nodesPerCluster - 1);
      uint32_t headIdx = startIdx;

      clusterHeads.push_back (headIdx);

      NS_LOG_UNCOND ("  Cluster " << c << ": Nodes " << startIdx << "-" << endIdx
                    << ", Head: Node " << headIdx << " (" << interfaces.GetAddress (headIdx) << ")");
    }

  // Configure each node's cluster settings
  for (uint32_t i = 0; i < numNodes; ++i)
    {
      Ptr<Node> node = nodes.Get (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();

      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      if (!rp)
        {
          NS_LOG_ERROR ("No routing protocol found for node " << i);
          continue;
        }

      Ptr<RoutingProtocol> routing = rp->GetObject<RoutingProtocol> ();
      if (!routing)
        {
          NS_LOG_ERROR ("Routing protocol is not SmartAodvV2 for node " << i);
          continue;
        }

      // Calculate cluster ID (1-indexed)
      uint32_t clusterId = std::min ((i / nodesPerCluster) + 1, numClusters);
      routing->SetLocalClusterId (clusterId);

      // Set cluster head
      uint32_t headIdx = clusterHeads[clusterId - 1];
      Ipv4Address headAddr = interfaces.GetAddress (headIdx);
      routing->SetClusterHead (headAddr);

      // Mark cluster head
      if (i == headIdx)
        {
          // Node is cluster head - this is handled internally
        }

      // Set default mode
      routing->SetClusterMode (MODE_SELF_ORG);
    }
}

ApplicationContainer
ClusterDemoHelper::InstallClusterApps (NodeContainer& nodes,
                                        Time startTime,
                                        Time stopTime)
{
  NS_LOG_FUNCTION (nodes.GetN () << startTime << stopTime);

  ApplicationContainer apps;

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<ClusterControlApp> app = CreateObject<ClusterControlApp> ();
      nodes.Get (i)->AddApplication (app);
      app->SetStartTime (startTime);
      app->SetStopTime (stopTime);
      apps.Add (app);
    }

  return apps;
}

void
ClusterDemoHelper::PrintClusterStatus (NodeContainer& nodes,
                                        Ipv4InterfaceContainer& interfaces)
{
  NS_LOG_FUNCTION (nodes.GetN ());

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Cluster Status at t=" << std::fixed << std::setprecision (1)
                << Simulator::Now ().GetSeconds () << "s ===");

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Node> node = nodes.Get (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();

      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing)
        {
          uint32_t clusterId = routing->GetLocalClusterId ();
          ClusterMode mode = routing->GetClusterMode ();
          bool isHead = routing->IsClusterHead ();
          Ipv4Address headAddr = routing->GetClusterHeadAddress ();

          NS_LOG_UNCOND ("  Node " << std::setw (2) << i << " ("
                        << interfaces.GetAddress (i) << "): "
                        << "Cluster " << clusterId
                        << (isHead ? " [HEAD]" : "")
                        << ", Mode: " << (mode == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED")
                        << ", Head: " << headAddr);
        }
    }
}

void
ClusterDemoHelper::SetClusterMode (NodeContainer& nodes, ClusterMode mode)
{
  NS_LOG_FUNCTION (nodes.GetN () << mode);

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Node> node = nodes.Get (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();

      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing)
        {
          routing->SetClusterMode (mode);
        }
    }

  NS_LOG_UNCOND (">>> MODE SWITCH: All nodes -> "
                << (mode == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED"));
}

void
ClusterDemoHelper::PrintHeader (const std::string& title,
                                 uint32_t numNodes,
                                 uint32_t numClusters,
                                 double simTime)
{
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("================================================");
  NS_LOG_UNCOND ("  " << title);
  NS_LOG_UNCOND ("================================================");
  NS_LOG_UNCOND ("  Nodes: " << numNodes);
  NS_LOG_UNCOND ("  Clusters: " << numClusters);
  NS_LOG_UNCOND ("  Simulation Time: " << simTime << " seconds");
  NS_LOG_UNCOND ("================================================");
  NS_LOG_UNCOND ("");
}

void
ClusterDemoHelper::SchedulePeriodicStatusPrint (NodeContainer& nodes,
                                                 Ipv4InterfaceContainer& interfaces,
                                                 Time interval,
                                                 Time duration)
{
  NS_LOG_FUNCTION (interval << duration);

  Time currentTime = Seconds (0);

  while (currentTime < duration)
    {
      Simulator::Schedule (currentTime,
                          &ClusterDemoHelper::PeriodicStatusCallback,
                          &nodes,
                          &interfaces);
      currentTime += interval;
    }
}

void
ClusterDemoHelper::PeriodicStatusCallback (NodeContainer* nodes,
                                            Ipv4InterfaceContainer* interfaces)
{
  NS_LOG_FUNCTION (nodes->GetN ());

  double time = Simulator::Now ().GetSeconds ();

  // Count nodes per cluster
  std::map<uint32_t, std::vector<uint32_t>> clusterMembers;

  for (uint32_t i = 0; i < nodes->GetN (); ++i)
    {
      Ptr<Node> node = nodes->Get (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();

      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing)
        {
          uint32_t clusterId = routing->GetLocalClusterId ();
          clusterMembers[clusterId].push_back (i);
        }
    }

  NS_LOG_UNCOND ("[t=" << std::fixed << std::setprecision (1) << time << "s] Cluster Summary:");
  for (auto& kv : clusterMembers)
    {
      NS_LOG_UNCOND ("  Cluster " << kv.first << ": " << kv.second.size () << " nodes");
    }
}

uint32_t
ClusterDemoHelper::GetNodeClusterId (Ptr<Node> node)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
  Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

  return routing ? routing->GetLocalClusterId () : 0;
}

ClusterMode
ClusterDemoHelper::GetNodeClusterMode (Ptr<Node> node)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
  Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

  return routing ? routing->GetClusterMode () : MODE_SELF_ORG;
}

bool
ClusterDemoHelper::IsNodeClusterHead (Ptr<Node> node)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
  Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

  return routing ? routing->IsClusterHead () : false;
}

} // namespace smartAodvV2
} // namespace ns3
