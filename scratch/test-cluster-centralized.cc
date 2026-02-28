/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV-V2 Centralized Mode Demo
 *
 * Demonstrates centralized mode where all traffic goes through cluster heads.
 * - 12 nodes, 3 clusters, 4 nodes per cluster
 * - RandomWalk2D mobility
 * - Shows traffic routing path in centralized mode
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
#include <iomanip>

using namespace ns3;
using namespace ns3::smartAodvV2;

NS_LOG_COMPONENT_DEFINE ("ClusterCentralizedDemo");

/**
 * \brief Log packet forwarding in centralized mode
 */
void
TxPacketCallback (std::string context, Ptr<const Packet> packet)
{
  NS_LOG_UNCOND ("[Centralized] Packet transmitted: " << packet->GetSize () << " bytes");
}

/**
 * \brief Print routing table for a node
 */
void
PrintNodeRoute (Ptr<Node> node, Ipv4Address dest)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
  Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

  if (routing)
    {
      NS_LOG_UNCOND ("  Node " << node->GetId ()
                    << " - Cluster: " << routing->GetLocalClusterId ()
                    << ", Mode: " << (routing->GetClusterMode () == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED")
                    << ", Head: " << routing->GetClusterHeadAddress ());
    }
}

/**
 * \brief Print centralized mode routing summary
 */
void
PrintCentralizedRoutingSummary (NodeContainer& nodes, Ipv4InterfaceContainer& interfaces)
{
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Centralized Mode Routing Summary ===");
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("In CENTRALIZED mode, all traffic flows through cluster heads:");
  NS_LOG_UNCOND ("  - Intra-cluster: Src -> ClusterHead -> Dst");
  NS_LOG_UNCOND ("  - Inter-cluster: Src -> LocalHead -> RemoteHead -> Dst");
  NS_LOG_UNCOND ("");

  // Print cluster head information
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Ipv4> ipv4 = nodes.Get (i)->GetObject<Ipv4> ();
      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing && routing->IsClusterHead ())
        {
          NS_LOG_UNCOND ("  Cluster " << routing->GetLocalClusterId ()
                        << " Head: Node " << i << " (" << interfaces.GetAddress (i) << ")");
        }
    }
}

int
main (int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 12;
  uint32_t numClusters = 3;
  double totalTime = 60.0;
  bool verbose = false;

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
      LogComponentEnable ("ClusterCentralizedDemo", LOG_LEVEL_ALL);
      LogComponentEnable ("SmartAodvV2Cluster", LOG_LEVEL_ALL);
      LogComponentEnable ("ClusterControlApp", LOG_LEVEL_ALL);
    }

  // Print header
  ClusterDemoHelper::PrintHeader ("Centralized Mode Demo",
                                   numNodes, numClusters, totalTime);
  NS_LOG_UNCOND ("Scenario:");
  NS_LOG_UNCOND ("  - All traffic goes through cluster heads");
  NS_LOG_UNCOND ("  - Compares intra-cluster vs inter-cluster routing");
  NS_LOG_UNCOND ("");

  // Create nodes
  NodeContainer nodes;
  nodes.Create (numNodes);

  // Mobility: Static positions (to avoid boundary issues with RandomWalk)
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

  // Set all nodes to CENTRALIZED mode
  ClusterDemoHelper::SetClusterMode (nodes, MODE_CENTRALIZED);

  // Install ClusterControlApp
  ApplicationContainer clusterApps = ClusterDemoHelper::InstallClusterApps (
      nodes, Seconds (1.0), Seconds (totalTime));

  // Print routing summary
  PrintCentralizedRoutingSummary (nodes, interfaces);

  // Test 1: Intra-cluster communication (Node 1 -> Node 2, same cluster)
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Test 1: Intra-cluster communication (Cluster 1)");
  NS_LOG_UNCOND ("  Source: Node 1, Destination: Node 2");
  NS_LOG_UNCOND ("  Expected path: Node 1 -> ClusterHead (Node 0) -> Node 2");

  uint16_t port1 = 9;
  UdpEchoServerHelper echoServer1 (port1);
  ApplicationContainer serverApps1 = echoServer1.Install (nodes.Get (2));
  serverApps1.Start (Seconds (2.0));
  serverApps1.Stop (Seconds (totalTime - 1.0));

  UdpEchoClientHelper echoClient1 (interfaces.GetAddress (2), port1);
  echoClient1.SetAttribute ("MaxPackets", UintegerValue (30));
  echoClient1.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  echoClient1.SetAttribute ("PacketSize", UintegerValue (512));

  ApplicationContainer clientApps1 = echoClient1.Install (nodes.Get (1));
  clientApps1.Start (Seconds (3.0));
  clientApps1.Stop (Seconds (25.0));

  // Test 2: Inter-cluster communication (Node 1 -> Node 5, different clusters)
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Test 2: Inter-cluster communication (Cluster 1 -> Cluster 2)");
  NS_LOG_UNCOND ("  Source: Node 1, Destination: Node 5");
  NS_LOG_UNCOND ("  Expected path: Node 1 -> Head1 (Node 0) -> Head2 (Node 4) -> Node 5");

  uint16_t port2 = 10;
  UdpEchoServerHelper echoServer2 (port2);
  ApplicationContainer serverApps2 = echoServer2.Install (nodes.Get (5));
  serverApps2.Start (Seconds (2.0));
  serverApps2.Stop (Seconds (totalTime - 1.0));

  UdpEchoClientHelper echoClient2 (interfaces.GetAddress (5), port2);
  echoClient2.SetAttribute ("MaxPackets", UintegerValue (30));
  echoClient2.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  echoClient2.SetAttribute ("PacketSize", UintegerValue (512));

  ApplicationContainer clientApps2 = echoClient2.Install (nodes.Get (1));
  clientApps2.Start (Seconds (30.0));
  clientApps2.Stop (Seconds (55.0));

  // Install FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  // Run simulation
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Starting simulation...");
  Simulator::Stop (Seconds (totalTime));
  Simulator::Run ();

  // Print statistics
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Flow Statistics ===");

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  for (const auto& stat : stats)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (stat.first);

      std::string flowType;
      if (t.destinationAddress == interfaces.GetAddress (2))
        {
          flowType = "Intra-cluster (Cluster 1)";
        }
      else if (t.destinationAddress == interfaces.GetAddress (5))
        {
          flowType = "Inter-cluster (Cluster 1 -> Cluster 2)";
        }
      else
        {
          flowType = "Unknown";
        }

      NS_LOG_UNCOND ("");
      NS_LOG_UNCOND ("Flow " << stat.first << " (" << flowType << ")");
      NS_LOG_UNCOND ("  Source: " << t.sourceAddress << " -> Destination: " << t.destinationAddress);
      NS_LOG_UNCOND ("  Tx Packets: " << stat.second.txPackets);
      NS_LOG_UNCOND ("  Rx Packets: " << stat.second.rxPackets);
      NS_LOG_UNCOND ("  Packet Loss: " << (stat.second.txPackets - stat.second.rxPackets));

      if (stat.second.rxPackets > 0)
        {
          double avgDelay = stat.second.delaySum.GetSeconds () / stat.second.rxPackets * 1000;
          double throughput = stat.second.rxBytes * 8.0 /
              (stat.second.timeLastRxPacket.GetSeconds () -
               stat.second.timeFirstTxPacket.GetSeconds ()) / 1024;

          NS_LOG_UNCOND ("  Avg Delay: " << std::fixed << std::setprecision (2) << avgDelay << " ms");
          NS_LOG_UNCOND ("  Throughput: " << std::setprecision (2) << throughput << " Kbps");
        }
    }

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Centralized Mode Analysis ===");
  NS_LOG_UNCOND ("1. All packets are routed through cluster heads");
  NS_LOG_UNCOND ("2. Inter-cluster traffic may have higher latency (more hops)");
  NS_LOG_UNCOND ("3. Cluster heads can monitor and audit all traffic");
  NS_LOG_UNCOND ("4. Provides better security and control at cost of efficiency");

  Simulator::Destroy ();

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Simulation completed successfully.");

  return 0;
}
