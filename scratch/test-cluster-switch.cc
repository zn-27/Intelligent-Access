/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV-V2 Cluster Switch Test
 *
 * 50 nodes, 5 clusters, 10 nodes per cluster
 * Tests cluster-based routing and switching functionality
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/smart-aodv-v2-helper.h"
#include "ns3/smart-aodv-v2-routing-protocol.h"
#include "ns3/cluster-control-app.h"

using namespace ns3;
using namespace ns3::smartAodvV2;

NS_LOG_COMPONENT_DEFINE ("ClusterSwitchTest");

int main (int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 50;
  uint32_t numClusters = 5;
  double totalTime = 100.0;
  double step = 30.0;  // Distance between nodes in grid
  bool verbose = false;

  CommandLine cmd;
  cmd.AddValue ("n", "Number of nodes", numNodes);
  cmd.AddValue ("c", "Number of clusters", numClusters);
  cmd.AddValue ("t", "Simulation time (seconds)", totalTime);
  cmd.AddValue ("d", "Distance between nodes", step);
  cmd.AddValue ("v", "Enable verbose logging", verbose);
  cmd.Parse (argc, argv);

  // Set random seed for reproducibility
  RngSeedManager::SetSeed (42);
  RngSeedManager::SetRun (1);

  // Enable logging if verbose
  if (verbose)
    {
      LogComponentEnable ("ClusterSwitchTest", LOG_LEVEL_ALL);
      LogComponentEnable ("SmartAodvV2Cluster", LOG_LEVEL_ALL);
      LogComponentEnable ("ClusterControlApp", LOG_LEVEL_ALL);
    }

  NS_LOG_UNCOND ("=== Smart-AODV-V2 Cluster Switch Test ===");
  NS_LOG_UNCOND ("Nodes: " << numNodes);
  NS_LOG_UNCOND ("Clusters: " << numClusters);
  NS_LOG_UNCOND ("Simulation time: " << totalTime << " seconds");
  NS_LOG_UNCOND ("");

  // Create nodes
  NodeContainer nodes;
  nodes.Create (numNodes);

  // Grid layout (5 columns x 10 rows)
  // This creates 5 clusters, each with 10 nodes
  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                "MinX", DoubleValue (0.0),
                                "MinY", DoubleValue (0.0),
                                "DeltaX", DoubleValue (step),
                                "DeltaY", DoubleValue (step),
                                "GridWidth", UintegerValue (5),
                                "LayoutType", StringValue ("RowFirst"));
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (nodes);

  // WiFi configuration
  WifiHelper wifi;
  wifi.SetRemoteStationManager ("ns3::ArfWifiManager");

  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");

  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  wifiPhy.SetChannel (wifiChannel.Create ());
  wifiPhy.Set ("TxPowerStart", DoubleValue (20.0));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (20.0));
  wifiPhy.Set ("RxGain", DoubleValue (0));
  wifiPhy.Set ("RxNoiseFigure", DoubleValue (7));

  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, nodes);

  // Install Internet stack with Smart-AODV-V2
  InternetStackHelper internet;
  SmartAodvV2Helper smartAodv2;
  internet.SetRoutingHelper (smartAodv2);
  internet.Install (nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase ("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // Assign cluster IDs and cluster heads based on position
  uint32_t nodesPerCluster = numNodes / numClusters;
  NS_ASSERT (nodesPerCluster * numClusters == numNodes);

  NS_LOG_UNCOND ("Cluster Assignment:");
  for (uint32_t c = 1; c <= numClusters; ++c)
    {
      uint32_t startIdx = (c - 1) * nodesPerCluster;
      uint32_t endIdx = startIdx + nodesPerCluster - 1;
      uint32_t headIdx = startIdx;  // First node in cluster is head

      NS_LOG_UNCOND ("  Cluster " << c << ": Nodes " << startIdx << "-" << endIdx
                    << ", Head: Node " << headIdx);
    }
  NS_LOG_UNCOND ("");

  // Configure each node's cluster settings
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Node> node = nodes.Get (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();

      // Get the routing protocol
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
      uint32_t clusterId = (i / nodesPerCluster) + 1;
      routing->SetLocalClusterId (clusterId);

      // Set cluster head
      uint32_t headIdx = ((clusterId - 1) * nodesPerCluster);
      Ipv4Address headAddr = interfaces.GetAddress (headIdx);
      routing->SetClusterHead (headAddr);

      // Set cluster mode (default: SELF_ORG)
      routing->SetClusterMode (MODE_SELF_ORG);

      NS_LOG_DEBUG ("Node " << i << " (" << interfaces.GetAddress (i)
                   << ") assigned to Cluster " << clusterId
                   << ", Head: " << headAddr);
    }

  // Install ClusterControlApp on all nodes for dynamic control
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<ClusterControlApp> clusterApp = CreateObject<ClusterControlApp> ();
      nodes.Get (i)->AddApplication (clusterApp);
      clusterApp->SetStartTime (Seconds (1.0));
      clusterApp->SetStopTime (Seconds (totalTime));
    }

  // Test Scenario 1: Intra-cluster communication (Node 1 -> Node 2, same cluster)
  // Use nodes that always exist (indices 1 and 2)
  uint32_t intraClusterClient = 1;
  uint32_t intraClusterServer = std::min ((uint32_t)2, nodesPerCluster - 1);  // Within first cluster

  NS_LOG_UNCOND ("Test Scenario 1: Intra-cluster communication");
  NS_LOG_UNCOND ("  Node " << intraClusterClient << " -> Node " << intraClusterServer
                << " (same cluster, Cluster 1)");

  uint16_t port1 = 9;
  UdpEchoServerHelper echoServer1 (port1);
  ApplicationContainer serverApps1 = echoServer1.Install (nodes.Get (intraClusterServer));
  serverApps1.Start (Seconds (2.0));
  serverApps1.Stop (Seconds (totalTime - 1.0));

  UdpEchoClientHelper echoClient1 (interfaces.GetAddress (intraClusterServer), port1);
  echoClient1.SetAttribute ("MaxPackets", UintegerValue (50));
  echoClient1.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  echoClient1.SetAttribute ("PacketSize", UintegerValue (1024));

  ApplicationContainer clientApps1 = echoClient1.Install (nodes.Get (intraClusterClient));
  clientApps1.Start (Seconds (3.0));
  clientApps1.Stop (Seconds (totalTime - 2.0));

  // Test Scenario 2: Inter-cluster communication (Node 0 -> last node, different clusters)
  // Only run if there are multiple clusters
  uint32_t interClusterClient = 0;
  uint32_t interClusterServer = numNodes - 1;  // Last node

  NS_LOG_UNCOND ("Test Scenario 2: Inter-cluster communication");
  if (numClusters > 1)
    {
      NS_LOG_UNCOND ("  Node " << interClusterClient << " (Cluster 1) -> Node "
                    << interClusterServer << " (Cluster " << numClusters << ")");

      uint16_t port2 = 10;
      UdpEchoServerHelper echoServer2 (port2);
      ApplicationContainer serverApps2 = echoServer2.Install (nodes.Get (interClusterServer));
      serverApps2.Start (Seconds (2.0));
      serverApps2.Stop (Seconds (totalTime - 1.0));

      UdpEchoClientHelper echoClient2 (interfaces.GetAddress (interClusterServer), port2);
      echoClient2.SetAttribute ("MaxPackets", UintegerValue (50));
      echoClient2.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
      echoClient2.SetAttribute ("PacketSize", UintegerValue (1024));

      ApplicationContainer clientApps2 = echoClient2.Install (nodes.Get (interClusterClient));
      clientApps2.Start (Seconds (5.0));
      clientApps2.Stop (Seconds (totalTime - 2.0));
    }
  else
    {
      NS_LOG_UNCOND ("  Skipped: Only 1 cluster configured");
    }

  NS_LOG_UNCOND ("");

  // Install FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  // Run simulation
  NS_LOG_UNCOND ("Starting simulation...");
  Simulator::Stop (Seconds (totalTime));
  Simulator::Run ();

  // Collect and print statistics
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Flow Statistics ===");

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin ();
       i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);

      std::string flowType;
      if (t.destinationAddress == interfaces.GetAddress (intraClusterServer))
        {
          flowType = "Intra-cluster (Cluster 1)";
        }
      else if (numClusters > 1 && t.destinationAddress == interfaces.GetAddress (interClusterServer))
        {
          flowType = "Inter-cluster (Cluster 1 -> Cluster " + std::to_string(numClusters) + ")";
        }
      else
        {
          flowType = "Unknown";
        }

      NS_LOG_UNCOND ("Flow " << i->first << " (" << flowType << ")");
      NS_LOG_UNCOND ("  Source: " << t.sourceAddress << " -> Destination: " << t.destinationAddress);
      NS_LOG_UNCOND ("  Tx Packets: " << i->second.txPackets);
      NS_LOG_UNCOND ("  Rx Packets: " << i->second.rxPackets);
      NS_LOG_UNCOND ("  Lost Packets: " << (i->second.txPackets - i->second.rxPackets));
      NS_LOG_UNCOND ("  Packet Loss Rate: "
                    << (i->second.txPackets > 0 ?
                        100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets :
                        0.0) << "%");

      if (i->second.rxPackets > 0)
        {
          NS_LOG_UNCOND ("  Average Delay: "
                        << i->second.delaySum.GetSeconds () / i->second.rxPackets * 1000 << " ms");
          NS_LOG_UNCOND ("  Throughput: "
                        << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () -
                                                      i->second.timeFirstTxPacket.GetSeconds ()) / 1024
                        << " Kbps");
        }
      NS_LOG_UNCOND ("");
    }

  // Print expected behavior summary
  NS_LOG_UNCOND ("=== Expected Behavior ===");
  NS_LOG_UNCOND ("1. Intra-cluster traffic (Node 1 -> Node 9):");
  NS_LOG_UNCOND ("   - Should succeed in SELF_ORG mode");
  NS_LOG_UNCOND ("   - Direct route within cluster");
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("2. Inter-cluster traffic (Node 0 -> Node 49):");
  NS_LOG_UNCOND ("   - Should go through cluster heads");
  NS_LOG_UNCOND ("   - Cluster 1 Head -> ... -> Cluster 5 Head -> Destination");
  NS_LOG_UNCOND ("   - May experience higher latency due to multi-hop path");
  NS_LOG_UNCOND ("");

  Simulator::Destroy ();

  NS_LOG_UNCOND ("Simulation completed successfully.");

  return 0;
}
