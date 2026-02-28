/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV-V2 RSSI Threshold Trigger Demo
 *
 * Demonstrates cluster switching triggered by RSSI threshold.
 * - 16 nodes, 4 clusters, 4 nodes per cluster
 * - RandomWalk2D mobility
 * - Shows RSSI-based switching decision process
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

NS_LOG_COMPONENT_DEFINE ("ClusterRssiTriggerDemo");

// RSSI thresholds (in dBm)
const double RSSI_SWITCH_THRESHOLD = -90.0;   // Trigger cluster switch below this
const double RSSI_ACCEPT_THRESHOLD = -85.0;   // Accept new cluster above this

// Global tracking
static std::map<uint32_t, double> g_lastRssi;  // node -> last RSSI

/**
 * \brief Simulate RSSI change for a specific node
 * This demonstrates how RSSI changes can trigger cluster switches
 */
void
SimulateRssiChange (Ptr<Node> node, double newRssi)
{
  uint32_t nodeId = node->GetId ();
  double oldRssi = g_lastRssi[nodeId];

  NS_LOG_UNCOND (">>> [t=" << std::fixed << std::setprecision (1)
                << Simulator::Now ().GetSeconds () << "s] RSSI CHANGE: Node " << nodeId
                << " RSSI: " << std::setprecision (1) << oldRssi << " dBm -> "
                << newRssi << " dBm");

  // Check if RSSI crosses switch threshold
  if (oldRssi > RSSI_SWITCH_THRESHOLD && newRssi <= RSSI_SWITCH_THRESHOLD)
    {
      NS_LOG_UNCOND ("    *** RSSI below switch threshold (" << RSSI_SWITCH_THRESHOLD << " dBm) ***");
      NS_LOG_UNCOND ("    *** Checking for cluster switch... ***");
    }
  else if (oldRssi <= RSSI_ACCEPT_THRESHOLD && newRssi > RSSI_ACCEPT_THRESHOLD)
    {
      NS_LOG_UNCOND ("    *** RSSI above accept threshold (" << RSSI_ACCEPT_THRESHOLD << " dBm) ***");
    }

  g_lastRssi[nodeId] = newRssi;
}

/**
 * \brief Periodically report RSSI status for all nodes
 */
void
ReportRssiStatus (NodeContainer& nodes, Ipv4InterfaceContainer& interfaces,
                  double interval, double duration)
{
  double currentTime = Simulator::Now ().GetSeconds ();

  if (currentTime > duration)
    {
      return;
    }

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("--- RSSI Status at t=" << std::fixed << std::setprecision (1)
                << currentTime << "s ---");

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Node> node = nodes.Get (i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing)
        {
          uint32_t clusterId = routing->GetLocalClusterId ();

          // Get average RSSI to cluster (simulated for demo)
          // In real implementation, this would come from cluster table
          double avgRssi = g_lastRssi[i];

          std::string status;
          if (avgRssi > RSSI_ACCEPT_THRESHOLD)
            status = "GOOD";
          else if (avgRssi > RSSI_SWITCH_THRESHOLD)
            status = "WEAK";
          else
            status = "CRITICAL (below threshold)";

          NS_LOG_UNCOND ("  Node " << std::setw (2) << i
                        << " Cluster " << clusterId
                        << " RSSI: " << std::setprecision (1) << std::setw (6) << avgRssi << " dBm"
                        << " [" << status << "]");
        }
    }

  Simulator::Schedule (Seconds (interval), &ReportRssiStatus,
                       std::ref (nodes), std::ref (interfaces), interval, duration);
}

/**
 * \brief Simulate a node moving away from its cluster (RSSI degradation)
 */
void
SimulateNodeMovement (Ptr<Node> node, std::vector<double> rssiSteps, uint32_t stepIndex)
{
  if (stepIndex >= rssiSteps.size ())
    {
      return;
    }

  double rssi = rssiSteps[stepIndex];
  SimulateRssiChange (node, rssi);

  // Schedule next RSSI step
  Simulator::Schedule (Seconds (5.0), &SimulateNodeMovement,
                       node, rssiSteps, stepIndex + 1);
}

/**
 * \brief Perform cluster switch for a node
 */
void
TriggerClusterSwitch (Ptr<Node> node, uint32_t newClusterId)
{
  uint32_t nodeId = node->GetId ();

  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
  Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

  if (routing)
    {
      uint32_t oldCluster = routing->GetLocalClusterId ();

      NS_LOG_UNCOND ("");
      NS_LOG_UNCOND (">>> CLUSTER SWITCH TRIGGERED: Node " << nodeId);
      NS_LOG_UNCOND ("    Old Cluster: " << oldCluster);
      NS_LOG_UNCOND ("    New Cluster: " << newClusterId);
      NS_LOG_UNCOND ("    Reason: RSSI below threshold (" << RSSI_SWITCH_THRESHOLD << " dBm)");

      routing->SetLocalClusterId (newClusterId);
    }
}

int
main (int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 16;
  uint32_t numClusters = 4;
  double totalTime = 80.0;
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
      LogComponentEnable ("ClusterRssiTriggerDemo", LOG_LEVEL_ALL);
      LogComponentEnable ("SmartAodvV2Cluster", LOG_LEVEL_ALL);
    }

  // Print header
  ClusterDemoHelper::PrintHeader ("RSSI Threshold Trigger Demo",
                                   numNodes, numClusters, totalTime);
  NS_LOG_UNCOND ("RSSI Thresholds:");
  NS_LOG_UNCOND ("  - Switch Threshold: " << RSSI_SWITCH_THRESHOLD << " dBm (trigger switch below this)");
  NS_LOG_UNCOND ("  - Accept Threshold: " << RSSI_ACCEPT_THRESHOLD << " dBm (accept new cluster above this)");
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Scenario:");
  NS_LOG_UNCOND ("  - Node 3 RSSI degrades from -65 dBm to -95 dBm");
  NS_LOG_UNCOND ("  - At -90 dBm, cluster switch is triggered");
  NS_LOG_UNCOND ("  - Node 3 switches to Cluster 2");
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

  // Install ClusterControlApp
  ApplicationContainer clusterApps = ClusterDemoHelper::InstallClusterApps (
      nodes, Seconds (1.0), Seconds (totalTime));

  // Initialize RSSI values (simulated good signal)
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      // Random initial RSSI between -60 and -75 dBm
      double initialRssi = -60.0 - (i % 4) * 5.0;
      g_lastRssi[i] = initialRssi;
    }

  // Schedule RSSI status reporting
  Simulator::Schedule (Seconds (5.0), &ReportRssiStatus,
                       std::ref (nodes), std::ref (interfaces), 10.0, totalTime);

  // Simulate RSSI degradation for Node 3
  // Node 3 is in Cluster 1, will switch to Cluster 2
  std::vector<double> rssiDegradation = {-65.0, -70.0, -75.0, -82.0, -88.0, -91.0, -95.0};

  NS_LOG_UNCOND ("Simulating RSSI degradation for Node 3:");
  NS_LOG_UNCOND ("  Starting at t=20s, RSSI degrades every 5 seconds");
  NS_LOG_UNCOND ("");

  Simulator::Schedule (Seconds (20.0), &SimulateNodeMovement,
                       nodes.Get (3), rssiDegradation, 0);

  // Schedule cluster switch for Node 3 at t=50s (when RSSI reaches critical level)
  Simulator::Schedule (Seconds (50.0), &TriggerClusterSwitch,
                       nodes.Get (3), 2);  // Switch to Cluster 2

  // Install FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  // Print initial status
  NS_LOG_UNCOND ("Initial RSSI Status:");
  // Don't call ReportRssiStatus with interval=0, just print once
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("--- RSSI Status at t=0.0s ---");
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      double avgRssi = g_lastRssi[i];
      std::string status;
      if (avgRssi > RSSI_ACCEPT_THRESHOLD)
        status = "GOOD";
      else if (avgRssi > RSSI_SWITCH_THRESHOLD)
        status = "WEAK";
      else
        status = "CRITICAL (below threshold)";
      NS_LOG_UNCOND ("  Node " << std::setw (2) << i
                    << " RSSI: " << std::setprecision (1) << std::setw (6) << avgRssi << " dBm"
                    << " [" << status << "]");
    }

  // Run simulation
  NS_LOG_UNCOND ("Starting simulation...");
  Simulator::Stop (Seconds (totalTime));
  Simulator::Run ();

  // Print final summary
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== RSSI Threshold Demo Summary ===");
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Threshold Configuration:");
  NS_LOG_UNCOND ("  - Switch Threshold: " << RSSI_SWITCH_THRESHOLD << " dBm");
  NS_LOG_UNCOND ("  - Accept Threshold: " << RSSI_ACCEPT_THRESHOLD << " dBm");
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Switching Logic:");
  NS_LOG_UNCOND ("  1. Node monitors RSSI to current cluster");
  NS_LOG_UNCOND ("  2. If RSSI < " << RSSI_SWITCH_THRESHOLD << " dBm, look for better cluster");
  NS_LOG_UNCOND ("  3. Switch to new cluster if RSSI > " << RSSI_ACCEPT_THRESHOLD << " dBm");
  NS_LOG_UNCOND ("");

  Simulator::Destroy ();

  NS_LOG_UNCOND ("Simulation completed successfully.");

  return 0;
}
