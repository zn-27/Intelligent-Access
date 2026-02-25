/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV-V2 Cluster Mode Switch Demo
 *
 * Demonstrates runtime switching between SELF_ORG and CENTRALIZED modes.
 * - 12 nodes, 3 clusters, 4 nodes per cluster
 * - RandomDirection mobility model
 * - Scheduled mode switches at specific times
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
#include "ns3/cluster-demo-helper.h"
#include <map>
#include <iomanip>

using namespace ns3;
using namespace ns3::smartAodvV2;

NS_LOG_COMPONENT_DEFINE ("ClusterModeSwitchDemo");

/**
 * \brief Callback for mode switch events
 */
void
ModeSwitchedCallback (Ptr<Node> node, ClusterMode oldMode, ClusterMode newMode)
{
  NS_LOG_UNCOND (">>> [t=" << std::fixed << std::setprecision (1)
                << Simulator::Now ().GetSeconds () << "s] MODE SWITCH: Node " << node->GetId ()
                << " -> " << (newMode == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED"));
}

/**
 * \brief Switch all nodes to a new cluster mode
 */
void
SwitchAllNodesMode (NodeContainer& nodes, ClusterMode newMode)
{
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== SWITCHING ALL NODES TO "
                << (newMode == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED")
                << " MODE at t=" << std::fixed << std::setprecision (1)
                << Simulator::Now ().GetSeconds () << "s ===");
  NS_LOG_UNCOND ("");

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Ipv4> ipv4 = nodes.Get (i)->GetObject<Ipv4> ();
      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing)
        {
          ClusterMode oldMode = routing->GetClusterMode ();
          routing->SetClusterMode (newMode);
          ModeSwitchedCallback (nodes.Get (i), oldMode, newMode);
        }
    }
}

/**
 * \brief Print current mode of all nodes
 */
void
PrintModeStatus (NodeContainer& nodes)
{
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("--- Mode Status at t=" << std::fixed << std::setprecision (1)
                << Simulator::Now ().GetSeconds () << "s ---");

  uint32_t selfOrgCount = 0;
  uint32_t centralizedCount = 0;

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Ipv4> ipv4 = nodes.Get (i)->GetObject<Ipv4> ();
      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing)
        {
          ClusterMode mode = routing->GetClusterMode ();
          if (mode == MODE_SELF_ORG)
            selfOrgCount++;
          else
            centralizedCount++;
        }
    }

  NS_LOG_UNCOND ("  SELF_ORG: " << selfOrgCount << " nodes");
  NS_LOG_UNCOND ("  CENTRALIZED: " << centralizedCount << " nodes");
}

/**
 * \brief Periodic status printer
 */
void
PeriodicStatusPrint (NodeContainer& nodes, double interval, double duration)
{
  double currentTime = Simulator::Now ().GetSeconds ();

  if (currentTime > duration)
    {
      return;
    }

  PrintModeStatus (nodes);

  Simulator::Schedule (Seconds (interval), &PeriodicStatusPrint,
                       std::ref (nodes), interval, duration);
}

int
main (int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 12;
  uint32_t numClusters = 3;
  double totalTime = 90.0;
  bool verbose = false;

  // Mode switch times
  double firstSwitchTime = 30.0;   // Switch to CENTRALIZED
  double secondSwitchTime = 60.0;  // Switch back to SELF_ORG

  CommandLine cmd;
  cmd.AddValue ("n", "Number of nodes", numNodes);
  cmd.AddValue ("c", "Number of clusters", numClusters);
  cmd.AddValue ("t", "Simulation time (seconds)", totalTime);
  cmd.AddValue ("v", "Enable verbose logging", verbose);
  cmd.Parse (argc, argv);

  // Set random seed
  RngSeedManager::SetSeed (42);
  RngSeedManager::SetRun (1);

  // Enable logging
  if (verbose)
    {
      LogComponentEnable ("ClusterModeSwitchDemo", LOG_LEVEL_ALL);
      LogComponentEnable ("SmartAodvV2Cluster", LOG_LEVEL_ALL);
      LogComponentEnable ("ClusterControlApp", LOG_LEVEL_ALL);
    }

  // Print header
  ClusterDemoHelper::PrintHeader ("Cluster Mode Switch Demo",
                                   numNodes, numClusters, totalTime);
  NS_LOG_UNCOND ("Scenario:");
  NS_LOG_UNCOND ("  - 0-30s:  MODE_SELF_ORG (self-organizing mode)");
  NS_LOG_UNCOND ("  - 30-60s: MODE_CENTRALIZED (centralized mode)");
  NS_LOG_UNCOND ("  - 60-90s: MODE_SELF_ORG (back to self-organizing)");
  NS_LOG_UNCOND ("");

  // Create nodes
  NodeContainer nodes;
  nodes.Create (numNodes);

  // Mobility: Static positions (to avoid boundary issues with RandomDirection)
  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                "MinX", DoubleValue (0.0),
                                "MinY", DoubleValue (0.0),
                                "DeltaX", DoubleValue (50.0),
                                "DeltaY", DoubleValue (50.0),
                                "GridWidth", UintegerValue (4),
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

  // Configure clusters
  std::vector<uint32_t> clusterHeads;
  ClusterDemoHelper::ConfigureClusters (nodes, interfaces, numClusters, clusterHeads);

  // Install ClusterControlApp
  ApplicationContainer clusterApps = ClusterDemoHelper::InstallClusterApps (
      nodes, Seconds (1.0), Seconds (totalTime));

  // Set initial mode to SELF_ORG
  ClusterDemoHelper::SetClusterMode (nodes, MODE_SELF_ORG);

  // Create cross-cluster traffic: Node 1 (Cluster 1) -> Node 5 (Cluster 2)
  uint16_t port = 9;
  UdpEchoServerHelper echoServer (port);
  ApplicationContainer serverApps = echoServer.Install (nodes.Get (5));
  serverApps.Start (Seconds (2.0));
  serverApps.Stop (Seconds (totalTime - 1.0));

  UdpEchoClientHelper echoClient (interfaces.GetAddress (5), port);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (100));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (0.5)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

  ApplicationContainer clientApps = echoClient.Install (nodes.Get (1));
  clientApps.Start (Seconds (3.0));
  clientApps.Stop (Seconds (totalTime - 2.0));

  NS_LOG_UNCOND ("Traffic: Node 1 (Cluster 1) -> Node 5 (Cluster 2)");
  NS_LOG_UNCOND ("");

  // Schedule mode switches
  Simulator::Schedule (Seconds (firstSwitchTime), &SwitchAllNodesMode,
                       std::ref (nodes), MODE_CENTRALIZED);
  Simulator::Schedule (Seconds (secondSwitchTime), &SwitchAllNodesMode,
                       std::ref (nodes), MODE_SELF_ORG);

  // Schedule periodic status printing
  Simulator::Schedule (Seconds (10.0), &PeriodicStatusPrint,
                       std::ref (nodes), 10.0, totalTime);

  // Install FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  // Print initial status
  NS_LOG_UNCOND ("Initial Mode Status:");
  PrintModeStatus (nodes);

  // Run simulation
  NS_LOG_UNCOND ("Starting simulation...");
  Simulator::Stop (Seconds (totalTime));
  Simulator::Run ();

  // Print final statistics
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Flow Statistics ===");

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  for (const auto& stat : stats)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (stat.first);
      NS_LOG_UNCOND ("Flow " << stat.first << ": " << t.sourceAddress
                    << " -> " << t.destinationAddress);
      NS_LOG_UNCOND ("  Tx Packets: " << stat.second.txPackets);
      NS_LOG_UNCOND ("  Rx Packets: " << stat.second.rxPackets);

      if (stat.second.rxPackets > 0)
        {
          double avgDelay = stat.second.delaySum.GetSeconds () / stat.second.rxPackets * 1000;
          double throughput = stat.second.rxBytes * 8.0 /
              (stat.second.timeLastRxPacket.GetSeconds () -
               stat.second.timeFirstTxPacket.GetSeconds ()) / 1024;

          NS_LOG_UNCOND ("  Avg Delay: " << std::fixed << std::setprecision (2) << avgDelay << " ms");
          NS_LOG_UNCOND ("  Throughput: " << std::setprecision (2) << throughput << " Kbps");
        }
      NS_LOG_UNCOND ("");
    }

  NS_LOG_UNCOND ("=== Expected Behavior ===");
  NS_LOG_UNCOND ("- SELF_ORG mode: Intra-cluster direct, inter-cluster via head");
  NS_LOG_UNCOND ("- CENTRALIZED mode: All traffic via cluster head for audit");

  Simulator::Destroy ();

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Simulation completed successfully.");

  return 0;
}
