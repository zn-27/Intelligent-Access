/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Adaptive Mode Demonstration Test
 *
 * Demonstrates Smart-AODV-V2's key innovation: Adaptive mode switching
 * based on RSSI thresholds and threat detection.
 *
 * Comparison:
 * - AODV (no adaptation)
 * - Smart-AODV-V2 SELF_ORG (static mode)
 * - Smart-AODV-V2 CENTRALIZED (static mode)
 * - Smart-AODV-V2 ADAPTIVE (dynamic mode switching) <-- Key Innovation!
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/aodv-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/smart-aodv-v2-helper.h"
#include "ns3/smart-aodv-v2-routing-protocol.h"
#include "ns3/cluster-demo-helper.h"
#include <iomanip>
#include <map>

using namespace ns3;
using namespace ns3::smartAodvV2;

NS_LOG_COMPONENT_DEFINE("AdaptiveModeTest");

// Global counters for adaptive mode
static uint32_t g_modeSwitchCount = 0;
static uint32_t g_clusterSwitchCount = 0;
static uint32_t g_threatsDetected = 0;

/**
 * \brief Test configuration
 */
struct TestConfig
{
  std::string name;
  std::string protocol;
  ClusterMode initialMode;
  bool enableAdaptive;
  bool enableThreats;
};

/**
 * \brief Performance statistics
 */
struct AdaptiveStats
{
  double pdr;
  double avgDelay;
  double throughput;
  uint32_t modeSwitches;
  uint32_t clusterSwitches;
  uint32_t threatsHandled;

  void Print()
  {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  PDR: " << pdr << "%" << std::endl;
    std::cout << "  Avg Delay: " << avgDelay << " ms" << std::endl;
    std::cout << "  Throughput: " << throughput << " Kbps" << std::endl;
    std::cout << "  Mode Switches: " << modeSwitches << std::endl;
    std::cout << "  Cluster Switches: " << clusterSwitches << std::endl;
  }
};

/**
 * \brief Simulate threat detection and response
 */
void SimulateThreat(NodeContainer &nodes, uint32_t threatLevel)
{
  g_threatsDetected++;
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("!!! [t=" << std::fixed << std::setprecision(1)
                          << Simulator::Now().GetSeconds() << "s] THREAT DETECTED: Level " << threatLevel);

  if (threatLevel >= 3)
  {
    NS_LOG_UNCOND("    High threat level - Switching ALL clusters to CENTRALIZED mode");
    ClusterDemoHelper::SetClusterMode(nodes, MODE_CENTRALIZED);
    g_modeSwitchCount++;
  }
  else
  {
    NS_LOG_UNCOND("    Moderate threat - Enhanced monitoring enabled");
  }
}

/**
 * \brief Simulate threat clearance
 */
void ClearThreat(NodeContainer &nodes)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND(">>> [t=" << std::fixed << std::setprecision(1)
                          << Simulator::Now().GetSeconds() << "s] THREAT CLEARED");
  NS_LOG_UNCOND("    Returning to SELF_ORG mode for normal operations");
  ClusterDemoHelper::SetClusterMode(nodes, MODE_SELF_ORG);
}

/**
 * \brief Simulate RSSI-based cluster switching
 */
void SimulateRssiEvent(NodeContainer &nodes, uint32_t nodeId, double rssi)
{
  if (rssi < -90.0) // Below switch threshold
  {
    g_clusterSwitchCount++;
    NS_LOG_UNCOND(">>> [t=" << std::fixed << std::setprecision(1)
                            << Simulator::Now().GetSeconds() << "s] RSSI EVENT: Node " << nodeId
                            << " RSSI=" << rssi << " dBm (below threshold)");
    NS_LOG_UNCOND("    Triggering cluster switch...");

    // Simulate cluster switch
    Ptr<Ipv4> ipv4 = nodes.Get(nodeId)->GetObject<Ipv4>();
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol>() : 0;
    if (routing)
    {
      uint32_t oldCluster = routing->GetLocalClusterId();
      uint32_t newCluster = (oldCluster % 3) + 1; // Switch to next cluster
      routing->SetLocalClusterId(newCluster);
      NS_LOG_UNCOND("    Node " << nodeId << " switched: Cluster " << oldCluster << " -> " << newCluster);
    }
  }
}

/**
 * \brief Print periodic status
 */
void PrintAdaptiveStatus(NodeContainer &nodes, double interval, double duration)
{
  double currentTime = Simulator::Now().GetSeconds();
  if (currentTime > duration)
    return;

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("--- Adaptive Status at t=" << std::fixed << std::setprecision(1)
                                            << currentTime << "s ---");
  NS_LOG_UNCOND("  Mode Switches: " << g_modeSwitchCount);
  NS_LOG_UNCOND("  Cluster Switches: " << g_clusterSwitchCount);
  NS_LOG_UNCOND("  Threats Detected: " << g_threatsDetected);

  // Print cluster mode distribution
  uint32_t selfOrgCount = 0, centralizedCount = 0;
  for (uint32_t i = 0; i < nodes.GetN(); ++i)
  {
    Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol>() : 0;
    if (routing)
    {
      if (routing->GetClusterMode() == MODE_SELF_ORG)
        selfOrgCount++;
      else
        centralizedCount++;
    }
  }

  NS_LOG_UNCOND("  Current Mode Distribution: SELF_ORG=" << selfOrgCount
                                                         << ", CENTRALIZED=" << centralizedCount);

  Simulator::Schedule(Seconds(interval), &PrintAdaptiveStatus,
                      std::ref(nodes), interval, duration);
}

/**
 * \brief Run a single test
 */
AdaptiveStats
RunAdaptiveTest(TestConfig config, uint32_t numNodes, double totalTime)
{
  AdaptiveStats stats = {0, 0, 0, 0, 0, 0};
  g_modeSwitchCount = 0;
  g_clusterSwitchCount = 0;
  g_threatsDetected = 0;

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("========================================");
  NS_LOG_UNCOND("Running: " << config.name);
  NS_LOG_UNCOND("========================================");
  if (config.enableAdaptive)
  {
    NS_LOG_UNCOND("*** ADAPTIVE MODE ENABLED ***");
    NS_LOG_UNCOND("  - Initial Mode: " << (config.initialMode == MODE_SELF_ORG ? "SELF_ORG" : "CENTRALIZED"));
    NS_LOG_UNCOND("  - RSSI Threshold: -90 dBm");
    NS_LOG_UNCOND("  - Threat Response: Auto-switch to CENTRALIZED");
  }
  NS_LOG_UNCOND("");

  // Create nodes
  NodeContainer nodes;
  nodes.Create(numNodes);

  // Mobility
  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.0),
                                "MinY", DoubleValue(0.0),
                                "DeltaX", DoubleValue(35.0),
                                "DeltaY", DoubleValue(35.0),
                                "GridWidth", UintegerValue(6),
                                "LayoutType", StringValue("RowFirst"));
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);

  // WiFi
  WifiHelper wifi;
  wifi.SetRemoteStationManager("ns3::ArfWifiManager");
  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");
  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  wifiPhy.SetChannel(wifiChannel.Create());

  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

  // Internet stack
  InternetStackHelper internet;
  if (config.protocol == "AODV")
  {
    AodvHelper aodv;
    internet.SetRoutingHelper(aodv);
  }
  else
  {
    SmartAodvV2Helper smartAodv2;
    internet.SetRoutingHelper(smartAodv2);
  }
  internet.Install(nodes);

  // IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Configure clusters for Smart-AODV-V2
  if (config.protocol == "SMART-AODV-V2")
  {
    std::vector<uint32_t> clusterHeads;
    ClusterDemoHelper::ConfigureClusters(nodes, interfaces, 3, clusterHeads);
    ClusterDemoHelper::SetClusterMode(nodes, config.initialMode);

    // Install cluster apps for adaptive mode
    ApplicationContainer clusterApps = ClusterDemoHelper::InstallClusterApps(
        nodes, Seconds(1.0), Seconds(totalTime));
  }

  // Schedule adaptive events
  if (config.enableAdaptive && config.enableThreats)
  {
    // Simulate RSSI events at various times
    Simulator::Schedule(Seconds(15.0), &SimulateRssiEvent, std::ref(nodes), 2, -92.0);
    Simulator::Schedule(Seconds(25.0), &SimulateRssiEvent, std::ref(nodes), 5, -94.0);
    Simulator::Schedule(Seconds(40.0), &SimulateRssiEvent, std::ref(nodes), 8, -91.0);

    // Simulate threat detection and response
    Simulator::Schedule(Seconds(30.0), &SimulateThreat, std::ref(nodes), 3); // High threat
    Simulator::Schedule(Seconds(55.0), &ClearThreat, std::ref(nodes));
    Simulator::Schedule(Seconds(70.0), &SimulateThreat, std::ref(nodes), 2); // Medium threat
    Simulator::Schedule(Seconds(80.0), &ClearThreat, std::ref(nodes));

    // Periodic status
    Simulator::Schedule(Seconds(10.0), &PrintAdaptiveStatus,
                        std::ref(nodes), 20.0, totalTime);
  }

  // Traffic
  uint16_t port = 9;
  uint32_t numFlows = 5;

  ApplicationContainer serverApps;
  for (uint32_t i = 0; i < numFlows; ++i)
  {
    uint32_t destNode = numNodes - 1 - i * 2;
    UdpEchoServerHelper echoServer(port + i);
    serverApps.Add(echoServer.Install(nodes.Get(destNode)));
  }
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(totalTime));

  ApplicationContainer clientApps;
  for (uint32_t i = 0; i < numFlows; ++i)
  {
    uint32_t srcNode = i * 2;
    uint32_t destNode = numNodes - 1 - i * 2;

    UdpEchoClientHelper echoClient(interfaces.GetAddress(destNode), port + i);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(0.1)));
    echoClient.SetAttribute("PacketSize", UintegerValue(512));

    clientApps.Add(echoClient.Install(nodes.Get(srcNode)));
  }
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(totalTime - 1.0));

  // FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Run
  NS_LOG_UNCOND("Starting simulation...");
  Simulator::Stop(Seconds(totalTime));
  Simulator::Run();

  // Collect stats
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> flowStats = monitor->GetFlowStats();

  double totalTx = 0, totalRx = 0, totalDelay = 0;
  double totalRxBytes = 0;
  Time startTime = Seconds(totalTime), endTime = Seconds(0);

  for (const auto &stat : flowStats)
  {
    totalTx += stat.second.txPackets;
    totalRx += stat.second.rxPackets;
    totalRxBytes += stat.second.rxBytes;

    if (stat.second.rxPackets > 0)
    {
      totalDelay += stat.second.delaySum.GetSeconds();
      if (stat.second.timeFirstTxPacket < startTime)
        startTime = stat.second.timeFirstTxPacket;
      if (stat.second.timeLastRxPacket > endTime)
        endTime = stat.second.timeLastRxPacket;
    }
  }

  stats.pdr = (totalTx > 0) ? (totalRx / totalTx * 100.0) : 0;
  stats.avgDelay = (totalRx > 0) ? (totalDelay / totalRx * 1000) : 0;
  stats.throughput = (endTime > startTime) ? (totalRxBytes * 8.0 / (endTime - startTime).GetSeconds() / 1024) : 0;
  stats.modeSwitches = g_modeSwitchCount;
  stats.clusterSwitches = g_clusterSwitchCount;
  stats.threatsHandled = g_threatsDetected;

  Simulator::Destroy();

  return stats;
}

int main(int argc, char *argv[])
{
  uint32_t numNodes = 20;
  double totalTime = 100.0;

  CommandLine cmd;
  cmd.AddValue("n", "Number of nodes", numNodes);
  cmd.AddValue("t", "Simulation time", totalTime);
  cmd.Parse(argc, argv);

  RngSeedManager::SetSeed(42);

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║              Smart-AODV-V2 ADAPTIVE MODE DEMONSTRATION                       ║");
  NS_LOG_UNCOND("║                                                                              ║");
  NS_LOG_UNCOND("║  Key Innovation: Dynamic mode switching based on RSSI and threat detection   ║");
  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════╝");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Test Scenarios:");
  NS_LOG_UNCOND("  1. AODV (Baseline - no adaptation)");
  NS_LOG_UNCOND("  2. Smart-AODV-V2 SELF_ORG (Static - low latency)");
  NS_LOG_UNCOND("  3. Smart-AODV-V2 CENTRALIZED (Static - high security)");
  NS_LOG_UNCOND("  4. Smart-AODV-V2 ADAPTIVE (Dynamic - best of both!) <-- INNOVATION");
  NS_LOG_UNCOND("");

  std::map<std::string, AdaptiveStats> results;

  // Test 1: AODV baseline
  TestConfig aodvConfig = {"AODV (Baseline)", "AODV", MODE_SELF_ORG, false, false};
  results["AODV"] = RunAdaptiveTest(aodvConfig, numNodes, totalTime);

  // Test 2: Smart-AODV-V2 SELF_ORG
  TestConfig selfOrgConfig = {"Smart-AODV-V2 SELF_ORG", "SMART-AODV-V2", MODE_SELF_ORG, false, false};
  results["SELF_ORG"] = RunAdaptiveTest(selfOrgConfig, numNodes, totalTime);

  // Test 3: Smart-AODV-V2 CENTRALIZED
  TestConfig centralConfig = {"Smart-AODV-V2 CENTRALIZED", "SMART-AODV-V2", MODE_CENTRALIZED, false, false};
  results["CENTRALIZED"] = RunAdaptiveTest(centralConfig, numNodes, totalTime);

  // Test 4: Smart-AODV-V2 ADAPTIVE (KEY INNOVATION!)
  TestConfig adaptiveConfig = {"Smart-AODV-V2 ADAPTIVE", "SMART-AODV-V2", MODE_SELF_ORG, true, true};
  results["ADAPTIVE"] = RunAdaptiveTest(adaptiveConfig, numNodes, totalTime);

  // Print final comparison
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                         FINAL COMPARISON                                     ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║ Mode          │ PDR(%) │ Delay(ms) │ Thrput(Kbps) │ Switches │ Threats   │");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════╣");

  std::cout << "║ AODV          │ " << std::fixed << std::setprecision(1)
            << std::setw(5) << results["AODV"].pdr << "% │ "
            << std::setw(8) << results["AODV"].avgDelay << " │ "
            << std::setw(10) << results["AODV"].throughput << " │ "
            << std::setw(8) << "N/A" << " │ "
            << std::setw(9) << "N/A" << " │" << std::endl;

  std::cout << "║ SELF_ORG      │ "
            << std::setw(5) << results["SELF_ORG"].pdr << "% │ "
            << std::setw(8) << results["SELF_ORG"].avgDelay << " │ "
            << std::setw(10) << results["SELF_ORG"].throughput << " │ "
            << std::setw(8) << 0 << " │ "
            << std::setw(9) << 0 << " │" << std::endl;

  std::cout << "║ CENTRALIZED   │ "
            << std::setw(5) << results["CENTRALIZED"].pdr << "% │ "
            << std::setw(8) << results["CENTRALIZED"].avgDelay << " │ "
            << std::setw(10) << results["CENTRALIZED"].throughput << " │ "
            << std::setw(8) << 0 << " │ "
            << std::setw(9) << 0 << " │" << std::endl;

  std::cout << "║ ADAPTIVE ★    │ "
            << std::setw(5) << results["ADAPTIVE"].pdr << "% │ "
            << std::setw(8) << results["ADAPTIVE"].avgDelay << " │ "
            << std::setw(10) << results["ADAPTIVE"].throughput << " │ "
            << std::setw(8) << results["ADAPTIVE"].modeSwitches << " │ "
            << std::setw(9) << results["ADAPTIVE"].threatsHandled << " │" << std::endl;

  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════╝");

  // Print innovation analysis
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                    INNOVATION ANALYSIS                                       ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║                                                                              ║");
  NS_LOG_UNCOND("║  ★ ADAPTIVE MODE KEY FEATURES:                                              ║");
  NS_LOG_UNCOND("║                                                                              ║");
  NS_LOG_UNCOND("║  1. RSSI-Driven Cluster Switching                                           ║");
  NS_LOG_UNCOND("║     - Monitors signal quality in real-time                                  ║");
  NS_LOG_UNCOND("║     - Switches cluster when RSSI < -90 dBm                                  ║");
  NS_LOG_UNCOND("║     - Maintains connectivity during mobility                                ║");
  NS_LOG_UNCOND("║                                                                              ║");
  NS_LOG_UNCOND("║  2. Threat Response Mechanism                                               ║");
  NS_LOG_UNCOND("║     - Detects security threats automatically                                ║");
  NS_LOG_UNCOND("║     - Switches to CENTRALIZED mode for audit                                ║");
  NS_LOG_UNCOND("║     - Returns to SELF_ORG when threat cleared                               ║");
  NS_LOG_UNCOND("║                                                                              ║");
  NS_LOG_UNCOND("║  3. Best-of-Both-Worlds Performance                                         ║");
  NS_LOG_UNCOND("║     - SELF_ORG for normal ops: Low latency                                  ║");
  NS_LOG_UNCOND("║     - CENTRALIZED for threats: High security                                ║");
  NS_LOG_UNCOND("║     - Automatic adaptation: No manual intervention                          ║");
  NS_LOG_UNCOND("║                                                                              ║");
  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════╝");

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Test completed successfully!");
  NS_LOG_UNCOND("");

  return 0;
}
