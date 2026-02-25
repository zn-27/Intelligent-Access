/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV-V2 Security Threat Response Demo
 *
 * Demonstrates automatic mode switching in response to detected threats.
 * - 15 nodes, 3 clusters, 5 nodes per cluster
 * - RandomDirection mobility model
 * - Threat detection triggers switch to CENTRALIZED mode
 * - Threat cleared returns to SELF_ORG mode
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

NS_LOG_COMPONENT_DEFINE ("ClusterSecurityDemo");

// Global state
static bool g_threatActive = false;
static uint32_t g_packetsAudited = 0;
static uint32_t g_packetsBlocked = 0;

/**
 * \brief Simulate threat detection
 */
void
OnThreatDetected (NodeContainer& nodes, const std::string& threatType)
{
  g_threatActive = true;

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  NS_LOG_UNCOND ("!!! THREAT DETECTED: " << threatType);
  NS_LOG_UNCOND ("!!! Switching to SECURE MODE (CENTRALIZED)");
  NS_LOG_UNCOND ("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  NS_LOG_UNCOND ("");

  // Switch all nodes to CENTRALIZED mode for security audit
  ClusterDemoHelper::SetClusterMode (nodes, MODE_CENTRALIZED);

  NS_LOG_UNCOND ("All cluster heads now monitoring traffic for audit.");
}

/**
 * \brief Simulate threat cleared
 */
void
OnThreatCleared (NodeContainer& nodes)
{
  g_threatActive = false;

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND (">>> THREAT CLEARED: Returning to NORMAL MODE (SELF_ORG)");
  NS_LOG_UNCOND ("");

  // Switch back to SELF_ORG mode
  ClusterDemoHelper::SetClusterMode (nodes, MODE_SELF_ORG);

  NS_LOG_UNCOND ("Normal operations resumed.");
}

/**
 * \brief Audit packet callback (simulated)
 */
void
AuditPacket (Ipv4Address src, Ipv4Address dst, bool allowed)
{
  g_packetsAudited++;

  if (!allowed)
    {
      g_packetsBlocked++;
      NS_LOG_UNCOND ("[AUDIT] BLOCKED: " << src << " -> " << dst);
    }
  else
    {
      NS_LOG_DEBUG ("[AUDIT] ALLOWED: " << src << " -> " << dst);
    }
}

/**
 * \brief Print security status
 */
void
PrintSecurityStatus (NodeContainer& nodes, double interval, double duration)
{
  double currentTime = Simulator::Now ().GetSeconds ();

  if (currentTime > duration)
    {
      return;
    }

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("--- Security Status at t=" << std::fixed << std::setprecision (1)
                << currentTime << "s ---");
  NS_LOG_UNCOND ("  Threat Status: " << (g_threatActive ? "ACTIVE" : "CLEAR"));
  NS_LOG_UNCOND ("  Current Mode: " << (g_threatActive ? "CENTRALIZED (Secure)" : "SELF_ORG (Normal)"));
  NS_LOG_UNCOND ("  Packets Audited: " << g_packetsAudited);
  NS_LOG_UNCOND ("  Packets Blocked: " << g_packetsBlocked);

  // Print mode for each cluster
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<Ipv4> ipv4 = nodes.Get (i)->GetObject<Ipv4> ();
      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol ();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol> () : 0;

      if (routing && routing->IsClusterHead ())
        {
          NS_LOG_UNCOND ("  Cluster " << routing->GetLocalClusterId ()
                        << " Head (Node " << i << "): "
                        << (routing->GetClusterMode () == MODE_CENTRALIZED ? "AUDITING" : "FORWARDING"));
        }
    }

  Simulator::Schedule (Seconds (interval), &PrintSecurityStatus,
                       std::ref (nodes), interval, duration);
}

/**
 * \brief Simulate suspicious activity detection
 */
void
SimulateSuspiciousActivity (Ipv4Address src)
{
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("[ALERT] Suspicious activity detected from " << src);
  NS_LOG_UNCOND ("[ALERT] Pattern: Unusual traffic volume");
}

int
main (int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 15;
  uint32_t numClusters = 3;
  double totalTime = 80.0;
  bool verbose = false;

  // Security event times
  double threatDetectTime = 25.0;
  double threatClearTime = 55.0;

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
      LogComponentEnable ("ClusterSecurityDemo", LOG_LEVEL_ALL);
      LogComponentEnable ("SmartAodvV2Cluster", LOG_LEVEL_ALL);
      LogComponentEnable ("ClusterControlApp", LOG_LEVEL_ALL);
    }

  // Print header
  ClusterDemoHelper::PrintHeader ("Security Threat Response Demo",
                                   numNodes, numClusters, totalTime);
  NS_LOG_UNCOND ("Security Scenario:");
  NS_LOG_UNCOND ("  - t=0-25s:  Normal operation (SELF_ORG mode)");
  NS_LOG_UNCOND ("  - t=25s:    Threat detected -> Switch to CENTRALIZED");
  NS_LOG_UNCOND ("  - t=25-55s: Secure mode with traffic audit");
  NS_LOG_UNCOND ("  - t=55s:    Threat cleared -> Return to SELF_ORG");
  NS_LOG_UNCOND ("  - t=55-80s: Normal operation resumed");
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Security Features:");
  NS_LOG_UNCOND ("  - Cluster heads audit all traffic in CENTRALIZED mode");
  NS_LOG_UNCOND ("  - Suspicious packets can be blocked");
  NS_LOG_UNCOND ("  - Automatic mode switching based on threat level");
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

  // Set initial mode to SELF_ORG (normal operation)
  ClusterDemoHelper::SetClusterMode (nodes, MODE_SELF_ORG);

  // Install ClusterControlApp
  ApplicationContainer clusterApps = ClusterDemoHelper::InstallClusterApps (
      nodes, Seconds (1.0), Seconds (totalTime));

  // Create normal traffic: Node 1 -> Node 7 (cross-cluster)
  uint16_t port = 9;
  UdpEchoServerHelper echoServer (port);
  ApplicationContainer serverApps = echoServer.Install (nodes.Get (7));
  serverApps.Start (Seconds (2.0));
  serverApps.Stop (Seconds (totalTime - 1.0));

  UdpEchoClientHelper echoClient (interfaces.GetAddress (7), port);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (200));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (0.3)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (512));

  ApplicationContainer clientApps = echoClient.Install (nodes.Get (1));
  clientApps.Start (Seconds (3.0));
  clientApps.Stop (Seconds (totalTime - 2.0));

  NS_LOG_UNCOND ("Traffic Flow: Node 1 (Cluster 1) -> Node 7 (Cluster 2)");
  NS_LOG_UNCOND ("");

  // Schedule security events
  Simulator::Schedule (Seconds (threatDetectTime), &OnThreatDetected,
                       std::ref (nodes), "Anomalous traffic pattern detected");

  // Simulate suspicious activity before threat detection
  Simulator::Schedule (Seconds (20.0), &SimulateSuspiciousActivity,
                       interfaces.GetAddress (1));

  Simulator::Schedule (Seconds (threatClearTime), &OnThreatCleared,
                       std::ref (nodes));

  // Schedule periodic security status
  Simulator::Schedule (Seconds (10.0), &PrintSecurityStatus,
                       std::ref (nodes), 10.0, totalTime);

  // Install FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  // Run simulation
  NS_LOG_UNCOND ("Starting simulation...");
  Simulator::Stop (Seconds (totalTime));
  Simulator::Run ();

  // Print final statistics
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Security Demo Summary ===");
  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Total Packets Audited: " << g_packetsAudited);
  NS_LOG_UNCOND ("Total Packets Blocked: " << g_packetsBlocked);
  NS_LOG_UNCOND ("");

  // Flow statistics
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  NS_LOG_UNCOND ("=== Flow Statistics ===");
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
          NS_LOG_UNCOND ("  Avg Delay: " << std::fixed << std::setprecision (2) << avgDelay << " ms");
        }
    }

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("=== Security Response Analysis ===");
  NS_LOG_UNCOND ("1. SELF_ORG mode provides low-latency routing");
  NS_LOG_UNCOND ("2. CENTRALIZED mode enables traffic audit at cluster heads");
  NS_LOG_UNCOND ("3. Mode switching allows adaptive security response");
  NS_LOG_UNCOND ("4. Trade-off: Security vs. Performance");

  Simulator::Destroy ();

  NS_LOG_UNCOND ("");
  NS_LOG_UNCOND ("Simulation completed successfully.");

  return 0;
}
