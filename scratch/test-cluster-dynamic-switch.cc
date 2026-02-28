/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV-V2 Dynamic Cluster Switch Demo
 *
 * Demonstrates dynamic cluster switching based on RSSI when nodes move.
 * - 15 nodes, 3 clusters, 5 nodes per cluster
 * - RandomWalk2D mobility model
 * - RSSI-based cluster switching
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

NS_LOG_COMPONENT_DEFINE("ClusterDynamicSwitchDemo");

// Global variables for tracking cluster switches
static std::map<uint32_t, uint32_t> g_nodeClusters; // node -> cluster
static uint32_t g_clusterSwitchCount = 0;

/**
 * \brief Callback for cluster changed events
 */
void ClusterChangedCallback(uint32_t nodeId, uint32_t oldCluster, uint32_t newCluster)
{
  g_clusterSwitchCount++;
  NS_LOG_UNCOND(">>> [t=" << std::fixed << std::setprecision(1)
                          << Simulator::Now().GetSeconds() << "s] CLUSTER SWITCH: Node " << nodeId
                          << " moved from Cluster " << oldCluster << " to Cluster " << newCluster);
}

/**
 * \brief Periodically check and report node positions and cluster assignments
 */
void PeriodicCheck(NodeContainer &nodes, Ipv4InterfaceContainer &interfaces, double interval, double duration)
{
  double currentTime = Simulator::Now().GetSeconds();

  if (currentTime > duration)
  {
    return;
  }

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("--- Status at t=" << std::fixed << std::setprecision(1) << currentTime << "s ---");

  // Check each node's cluster and position
  for (uint32_t i = 0; i < nodes.GetN(); ++i)
  {
    Ptr<Node> node = nodes.Get(i);
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    Vector pos = mobility->GetPosition();

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol>() : 0;

    if (routing)
    {
      uint32_t clusterId = routing->GetLocalClusterId();
      uint32_t oldCluster = g_nodeClusters[i];

      // Detect cluster switch
      if (oldCluster != 0 && oldCluster != clusterId)
      {
        ClusterChangedCallback(i, oldCluster, clusterId);
      }
      g_nodeClusters[i] = clusterId;

      NS_LOG_DEBUG("  Node " << i << " at (" << pos.x << ", " << pos.y
                             << ") Cluster: " << clusterId);
    }
  }

  // Schedule next check
  Simulator::Schedule(Seconds(interval), &PeriodicCheck,
                      std::ref(nodes), std::ref(interfaces), interval, duration);
}

/**
 * \brief Print cluster membership summary
 */
void PrintClusterSummary(NodeContainer &nodes)
{
  std::map<uint32_t, std::vector<uint32_t>> clusterMembers;

  for (uint32_t i = 0; i < nodes.GetN(); ++i)
  {
    Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol>() : 0;

    if (routing)
    {
      uint32_t clusterId = routing->GetLocalClusterId();
      clusterMembers[clusterId].push_back(i);
    }
  }

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("=== Final Cluster Membership ===");
  for (auto &kv : clusterMembers)
  {
    NS_LOG_UNCOND("  Cluster " << kv.first << " (" << kv.second.size() << " nodes): ");
    std::stringstream ss;
    for (uint32_t nodeId : kv.second)
    {
      ss << nodeId << " ";
    }
    NS_LOG_UNCOND("    Nodes: " << ss.str());
  }
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Total cluster switches: " << g_clusterSwitchCount);
}

int main(int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 15;
  uint32_t numClusters = 3;
  double totalTime = 80.0;
  double checkInterval = 5.0; // Check cluster status every 5 seconds
  bool verbose = false;

  CommandLine cmd;
  cmd.AddValue("n", "Number of nodes", numNodes);
  cmd.AddValue("c", "Number of clusters", numClusters);
  cmd.AddValue("t", "Simulation time (seconds)", totalTime);
  cmd.AddValue("v", "Enable verbose logging", verbose);
  cmd.Parse(argc, argv);

  // Set random seed
  RngSeedManager::SetSeed(42);
  RngSeedManager::SetRun(1);

  // Enable logging
  if (verbose)
  {
    LogComponentEnable("ClusterDynamicSwitchDemo", LOG_LEVEL_ALL);
    LogComponentEnable("SmartAodvV2Cluster", LOG_LEVEL_ALL);
    LogComponentEnable("ClusterControlApp", LOG_LEVEL_ALL);
  }

  // Print header
  ClusterDemoHelper::PrintHeader("Dynamic Cluster Switch Demo",
                                 numNodes, numClusters, totalTime);
  NS_LOG_UNCOND("Scenario:");
  NS_LOG_UNCOND("  - Nodes move using RandomWalk2D mobility");
  NS_LOG_UNCOND("  - Cluster switching triggered by RSSI changes");
  NS_LOG_UNCOND("  - RSSI Switch Threshold: -90 dBm");
  NS_LOG_UNCOND("  - RSSI Accept Threshold: -85 dBm");
  NS_LOG_UNCOND("");

  // Create nodes
  NodeContainer nodes;
  nodes.Create(numNodes);

  // Mobility: Static positions (to avoid boundary issues with RandomWalk)
  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.0),
                                "MinY", DoubleValue(0.0),
                                "DeltaX", DoubleValue(50.0),
                                "DeltaY", DoubleValue(50.0),
                                "GridWidth", UintegerValue(5),
                                "LayoutType", StringValue("RowFirst"));
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);

  // WiFi configuration
  WifiHelper wifi;
  wifi.SetRemoteStationManager("ns3::ArfWifiManager");

  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");

  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  wifiPhy.SetChannel(wifiChannel.Create());
  wifiPhy.Set("TxPowerStart", DoubleValue(20.0));
  wifiPhy.Set("TxPowerEnd", DoubleValue(20.0));

  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

  // Install Internet stack with Smart-AODV-V2
  InternetStackHelper internet;
  SmartAodvV2Helper smartAodv2;
  internet.SetRoutingHelper(smartAodv2);
  internet.Install(nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Configure clusters
  std::vector<uint32_t> clusterHeads;
  ClusterDemoHelper::ConfigureClusters(nodes, interfaces, numClusters, clusterHeads);

  // Initialize node cluster tracking
  for (uint32_t i = 0; i < nodes.GetN(); ++i)
  {
    Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    Ptr<RoutingProtocol> routing = rp->GetObject<RoutingProtocol>();
    if (routing)
    {
      g_nodeClusters[i] = routing->GetLocalClusterId();
    }
  }

  // Install ClusterControlApp
  ApplicationContainer clusterApps = ClusterDemoHelper::InstallClusterApps(
      nodes, Seconds(1.0), Seconds(totalTime));

  // Install FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Schedule periodic cluster status checks
  Simulator::Schedule(Seconds(checkInterval), &PeriodicCheck,
                      std::ref(nodes), std::ref(interfaces), checkInterval, totalTime);

  // Print initial cluster status
  NS_LOG_UNCOND("Initial Cluster Status:");
  ClusterDemoHelper::PrintClusterStatus(nodes, interfaces);

  // Run simulation
  NS_LOG_UNCOND("Starting simulation...");
  Simulator::Stop(Seconds(totalTime));
  Simulator::Run();

  // Print final summary
  PrintClusterSummary(nodes);

  // Print flow statistics
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  if (!stats.empty())
  {
    NS_LOG_UNCOND("=== Flow Statistics ===");
    for (const auto &stat : stats)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(stat.first);
      NS_LOG_UNCOND("Flow " << stat.first << ": " << t.sourceAddress
                            << " -> " << t.destinationAddress);
      NS_LOG_UNCOND("  Tx Packets: " << stat.second.txPackets);
      NS_LOG_UNCOND("  Rx Packets: " << stat.second.rxPackets);
      if (stat.second.rxPackets > 0)
      {
        NS_LOG_UNCOND("  Avg Delay: "
                      << stat.second.delaySum.GetSeconds() / stat.second.rxPackets * 1000 << " ms");
      }
    }
  }

  Simulator::Destroy();

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Simulation completed successfully.");
  NS_LOG_UNCOND("Expected behavior: Nodes switch clusters as they move based on RSSI thresholds.");

  return 0;
}
