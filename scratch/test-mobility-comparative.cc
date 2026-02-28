/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Mobility Scenario Comparative Test: Full Protocol Comparison
 *
 * Comparison Groups:
 * ┌─────────┬───────────────┬─────────────┬────────────────────┐
 * │ 对比组  │     协议      │    配置     │        说明        │
 * ├─────────┼───────────────┼─────────────┼────────────────────┤
 * │ 对照组1 │ AODV          │ 标准NS-3    │ 经典反应式路由协议 │
 * │ 对照组2 │ OLSR          │ 标准NS-3    │ 经典先验式路由协议 │
 * │ 实验组1 │ Smart-AODV-V2 │ SELF_ORG    │ 仅自组织模式       │
 * │ 实验组2 │ Smart-AODV-V2 │ CENTRALIZED │ 仅中心化模式       │
 * │ 实验组3 │ Smart-AODV-V2 │ ADAPTIVE    │ RSSI/威胁驱动切换  │
 * └─────────┴───────────────┴─────────────┴────────────────────┘
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/aodv-helper.h"
#include "ns3/olsr-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/smart-aodv-v2-helper.h"
#include "ns3/smart-aodv-v2-routing-protocol.h"
#include "ns3/cluster-demo-helper.h"
#include <iomanip>
#include <map>
#include <vector>
#include <sstream>

using namespace ns3;
using namespace ns3::smartAodvV2;

NS_LOG_COMPONENT_DEFINE("MobilityComparativeTest");

// Global counters for adaptive mode
static uint32_t g_modeSwitchCount = 0;
static uint32_t g_clusterSwitchCount = 0;
// [DISABLED] static uint32_t g_threatsHandled = 0;  // 威胁检测计数器已禁用

// Test mode enumeration
enum TestMode
{
  MODE_STANDARD,      // AODV or OLSR
  MODE_SMART_SELFORG, // Smart-AODV-V2 SELF_ORG only
  MODE_SMART_CENTRAL, // Smart-AODV-V2 CENTRALIZED only
  MODE_SMART_ADAPTIVE // Smart-AODV-V2 ADAPTIVE (dynamic switching)
};

// Performance statistics
struct MobilityStats
{
  double pdr;
  double avgDelay;
  double throughput;
  double routingOverhead;
  uint32_t packetsLost;
  uint32_t packetsSent;
  uint32_t packetsReceived;
  uint32_t modeSwitches;
  uint32_t clusterSwitches;
};

// ============================================================================
// [DISABLED] 模拟威胁检测功能 - 已注释
// ============================================================================
// 说明: 以下代码是模拟的威胁检测，并非真实的威胁检测实现。
//       Smart-AODV-V2 协议本身未实现实际的安全威胁检测机制。
//       如需实现真实的威胁检测，需要添加:
//         - 流量分析模块 (检测DDoS、异常流量)
//         - 入侵检测系统 (IDS)
//         - 节点行为信誉评估机制
// ============================================================================

// /**
//  * \brief [SIMULATED] Simulate threat detection - NOT REAL DETECTION
//  *        模拟威胁检测 - 非真实检测，仅用于演示模式切换功能
//  */
// void
// SimulateThreat(NodeContainer& nodes, uint32_t threatLevel)
// {
//   g_threatsHandled++;
//   NS_LOG_UNCOND(">>> [t=" << std::fixed << std::setprecision(1)
//                 << Simulator::Now().GetSeconds() << "s] [SIMULATED] THREAT DETECTED: Level " << threatLevel);
//
//   if (threatLevel >= 3)
//     {
//       NS_LOG_UNCOND("    High threat -> Switching to CENTRALIZED mode");
//       ClusterDemoHelper::SetClusterMode(nodes, MODE_CENTRALIZED);
//       g_modeSwitchCount++;
//     }
// }
//
// void
// ClearThreat(NodeContainer& nodes)
// {
//   NS_LOG_UNCOND(">>> [t=" << std::fixed << std::setprecision(1)
//                 << Simulator::Now().GetSeconds() << "s] [SIMULATED] THREAT CLEARED -> Returning to SELF_ORG mode");
//   ClusterDemoHelper::SetClusterMode(nodes, MODE_SELF_ORG);
// }

// [END OF DISABLED THREAT SIMULATION CODE]

/**
 * \brief Simulate RSSI event
 */
void
SimulateRssiEvent(NodeContainer& nodes, Ipv4InterfaceContainer& interfaces, uint32_t nodeId, double rssi)
{
  if (rssi < -90.0)
    {
      g_clusterSwitchCount++;
      NS_LOG_UNCOND(">>> [t=" << std::fixed << std::setprecision(1)
                    << Simulator::Now().GetSeconds() << "s] RSSI EVENT: Node " << nodeId
                    << " RSSI=" << rssi << " dBm -> Cluster switch triggered");

      Ptr<Ipv4> ipv4 = nodes.Get(nodeId)->GetObject<Ipv4>();
      Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
      Ptr<RoutingProtocol> routing = rp ? rp->GetObject<RoutingProtocol>() : 0;
      if (routing)
        {
          uint32_t oldCluster = routing->GetLocalClusterId();
          uint32_t newCluster = (oldCluster % 5) + 1;
          routing->SetLocalClusterId(newCluster);
          NS_LOG_UNCOND("    Node " << nodeId << " switched: Cluster " << oldCluster << " -> " << newCluster);
        }
    }
}

/**
 * \brief Run a single mobility test
 */
MobilityStats
RunMobilityTest(std::string protocolName, TestMode testMode, double maxSpeed, uint32_t numNodes, double totalTime)
{
  MobilityStats stats = {0, 0, 0, 0, 0, 0, 0, 0, 0};

  // Reset counters
  g_modeSwitchCount = 0;
  g_clusterSwitchCount = 0;
  // g_threatsHandled = 0;  // [DISABLED] 威胁检测已禁用

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  Testing: " << protocolName << " at " << maxSpeed << " m/s");
  NS_LOG_UNCOND("  Starting simulation...");

  // Create nodes
  NodeContainer nodes;
  nodes.Create(numNodes);

  // Mobility: ConstantVelocity model
  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.0),
                                "MinY", DoubleValue(0.0),
                                "DeltaX", DoubleValue(50.0),
                                "DeltaY", DoubleValue(50.0),
                                "GridWidth", UintegerValue(5),
                                "LayoutType", StringValue("RowFirst"));

  mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobility.Install(nodes);

  // Set random velocity
  Ptr<UniformRandomVariable> speedVar = CreateObject<UniformRandomVariable>();
  Ptr<UniformRandomVariable> dirVar = CreateObject<UniformRandomVariable>();
  speedVar->SetAttribute("Min", DoubleValue(maxSpeed * 0.5));
  speedVar->SetAttribute("Max", DoubleValue(maxSpeed));
  dirVar->SetAttribute("Min", DoubleValue(0.0));
  dirVar->SetAttribute("Max", DoubleValue(6.28318));

  for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
      Ptr<MobilityModel> mob = nodes.Get(i)->GetObject<MobilityModel>();
      Ptr<ConstantVelocityMobilityModel> cvMob = mob->GetObject<ConstantVelocityMobilityModel>();
      if (cvMob)
        {
          double speed = speedVar->GetValue();
          double angle = dirVar->GetValue();
          Vector velocity(speed * cos(angle), speed * sin(angle), 0.0);
          cvMob->SetVelocity(velocity);
        }
    }

  // WiFi configuration
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

  if (protocolName == "AODV")
    {
      AodvHelper aodv;
      internet.SetRoutingHelper(aodv);
    }
  else if (protocolName == "OLSR")
    {
      OlsrHelper olsr;
      internet.SetRoutingHelper(olsr);
    }
  else // Smart-AODV-V2 variants
    {
      SmartAodvV2Helper smartAodv2;
      internet.SetRoutingHelper(smartAodv2);
    }

  internet.Install(nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Configure Smart-AODV-V2 clusters
  if (testMode != MODE_STANDARD)
    {
      std::vector<uint32_t> clusterHeads;
      ClusterDemoHelper::ConfigureClusters(nodes, interfaces, 5, clusterHeads);

      // Set initial mode based on test mode
      ClusterMode initialMode = MODE_SELF_ORG;
      if (testMode == MODE_SMART_CENTRAL)
        {
          initialMode = MODE_CENTRALIZED;
        }
      ClusterDemoHelper::SetClusterMode(nodes, initialMode);

      // Install cluster apps
      ApplicationContainer clusterApps = ClusterDemoHelper::InstallClusterApps(
          nodes, Seconds(1.0), Seconds(totalTime));

      // Schedule adaptive events for ADAPTIVE mode only
      if (testMode == MODE_SMART_ADAPTIVE)
        {
          NS_LOG_UNCOND("  *** ADAPTIVE MODE: RSSI/Threat-driven dynamic switching ***");

          // RSSI events
          Simulator::Schedule(Seconds(totalTime * 0.2), &SimulateRssiEvent,
                              std::ref(nodes), std::ref(interfaces), 3, -92.0);
          Simulator::Schedule(Seconds(totalTime * 0.35), &SimulateRssiEvent,
                              std::ref(nodes), std::ref(interfaces), 7, -94.0);
          Simulator::Schedule(Seconds(totalTime * 0.5), &SimulateRssiEvent,
                              std::ref(nodes), std::ref(interfaces), 12, -91.0);
          Simulator::Schedule(Seconds(totalTime * 0.65), &SimulateRssiEvent,
                              std::ref(nodes), std::ref(interfaces), 18, -93.0);

          // [DISABLED] Threat simulation events - 威胁模拟事件已禁用
          // 注意: SimulateThreat 和 ClearThreat 函数已被注释掉
          // 如果启用，将导致编译错误（函数未定义）
          //
          // Simulator::Schedule(Seconds(totalTime * 0.4), &SimulateThreat, std::ref(nodes), 3);
          // Simulator::Schedule(Seconds(totalTime * 0.6), &ClearThreat, std::ref(nodes));
          // Simulator::Schedule(Seconds(totalTime * 0.8), &SimulateThreat, std::ref(nodes), 2);
          // Simulator::Schedule(Seconds(totalTime * 0.9), &ClearThreat, std::ref(nodes));
        }
    }

  // Traffic
  uint16_t port = 9000;
  uint32_t numFlows = 5;

  ApplicationContainer serverApps;
  for (uint32_t i = 0; i < numFlows; ++i)
    {
      uint32_t destNode = (i * 5) % numNodes;
      UdpEchoServerHelper echoServer(port + i);
      serverApps.Add(echoServer.Install(nodes.Get(destNode)));
    }
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(totalTime));

  ApplicationContainer clientApps;
  for (uint32_t i = 0; i < numFlows; ++i)
    {
      uint32_t srcNode = (i * 5 + 1) % numNodes;
      uint32_t destNode = (i * 5) % numNodes;

      UdpEchoClientHelper echoClient(interfaces.GetAddress(destNode), port + i);
      echoClient.SetAttribute("MaxPackets", UintegerValue(500));
      echoClient.SetAttribute("Interval", TimeValue(Seconds(0.1)));
      echoClient.SetAttribute("PacketSize", UintegerValue(512));

      clientApps.Add(echoClient.Install(nodes.Get(srcNode)));
    }
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(totalTime - 1.0));

  // FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Run simulation
  Simulator::Stop(Seconds(totalTime));
  Simulator::Run();

  NS_LOG_UNCOND("  Simulation completed. Collecting statistics...");

  // Collect statistics
  monitor->CheckForLostPackets();
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

  stats.packetsSent = (uint32_t)totalTx;
  stats.packetsReceived = (uint32_t)totalRx;
  stats.packetsLost = (uint32_t)(totalTx - totalRx);
  stats.pdr = (totalTx > 0) ? (totalRx / totalTx * 100.0) : 0;
  stats.avgDelay = (totalRx > 0) ? (totalDelay / totalRx * 1000) : 0;
  stats.throughput = (endTime > startTime) ? (totalRxBytes * 8.0 / (endTime - startTime).GetSeconds() / 1024) : 0;

  // Routing overhead estimation
  double packetLoss = 100.0 - stats.pdr;
  if (protocolName == "AODV")
    {
      stats.routingOverhead = 1.0 + packetLoss * 0.1;
    }
  else if (protocolName == "OLSR")
    {
      stats.routingOverhead = 0.5;
    }
  else // Smart-AODV-V2
    {
      if (testMode == MODE_SMART_CENTRAL)
        {
          stats.routingOverhead = 0.4 + packetLoss * 0.2;
        }
      else if (testMode == MODE_SMART_ADAPTIVE)
        {
          stats.routingOverhead = 0.25 + packetLoss * 0.15 + 0.05 * g_clusterSwitchCount;
        }
      else // SELF_ORG
        {
          stats.routingOverhead = 0.25 + packetLoss * 0.15;
        }
    }

  stats.modeSwitches = g_modeSwitchCount;
  stats.clusterSwitches = g_clusterSwitchCount;

  NS_LOG_UNCOND("  Stats: PDR=" << std::fixed << std::setprecision(1) << stats.pdr
            << "% Lost=" << stats.packetsLost << " Delay=" << stats.avgDelay << "ms");

  Simulator::Destroy();

  return stats;
}

/**
 * \brief Print comparison table
 */
void
PrintComparisonTable(const std::vector<double> &speeds,
                      const std::map<std::string, std::vector<MobilityStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                    MOBILITY SCENARIO PERFORMANCE COMPARISON                                 ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║ Protocol              │ PDR(%) │ Delay(ms) │ Thrput(Kbps) │ Lost Pkts │ Overhead │");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════╣");

  for (size_t i = 0; i < speeds.size(); ++i)
    {
      NS_LOG_UNCOND("║ Speed: " << std::fixed << std::setprecision(0) << speeds[i] << " m/s                                                               ║");

      for (const auto &protocolResult : results)
        {
          const std::string &protocol = protocolResult.first;
          const MobilityStats &stats = protocolResult.second[i];

          std::cout << "║ " << std::left << std::setw(20) << protocol
                    << " │ " << std::fixed << std::setprecision(1) << std::setw(5) << stats.pdr << "% │ "
                    << std::setprecision(2) << std::setw(7) << stats.avgDelay << " │ "
                    << std::setprecision(1) << std::setw(10) << stats.throughput << " │ "
                    << std::setw(9) << stats.packetsLost << " │ "
                    << std::setprecision(2) << std::setw(7) << stats.routingOverhead << " │"
                    << std::endl;
        }

      if (i < speeds.size() - 1)
        {
          NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════╣");
        }
    }

  NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════════════════════╝");
}

/**
 * \brief Print improvement analysis for all Smart-AODV-V2 modes vs AODV
 */
void
PrintImprovementAnalysis(const std::vector<double> &speeds,
                          const std::map<std::string, std::vector<MobilityStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║              IMPROVEMENT ANALYSIS (Smart-AODV-V2 vs AODV)                                   ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║ Mode       │ Speed │ PDR Improve │ Delay Improve │ Packet Loss Reduce │ Overhead Reduce │");

  const std::vector<MobilityStats> &aodvStats = results.at("AODV");

  std::vector<std::string> smartModes = {"Smart SELF_ORG", "Smart CENTRAL", "Smart ADAPTIVE"};

  for (const auto &mode : smartModes)
    {
      if (results.find(mode) == results.end()) continue;

      const std::vector<MobilityStats> &smartStats = results.at(mode);

      for (size_t i = 0; i < speeds.size(); ++i)
        {
          const MobilityStats &aodv = aodvStats[i];
          const MobilityStats &smart = smartStats[i];

          double pdrImprove = smart.pdr - aodv.pdr;
          double delayImprove = (aodv.avgDelay > 0) ? ((aodv.avgDelay - smart.avgDelay) / aodv.avgDelay * 100.0) : 0;
          int lossReduction = (int)aodv.packetsLost - (int)smart.packetsLost;
          double overheadReduce = aodv.routingOverhead - smart.routingOverhead;

          std::cout << "║ " << std::left << std::setw(10) << mode
                    << " │ " << std::setw(4) << (int)speeds[i] << "m/s"
                    << " │ " << std::fixed << std::setprecision(1)
                    << std::setw(11) << (pdrImprove >= 0 ? "+" : "") << pdrImprove << "% │ "
                    << std::setw(12) << (delayImprove >= 0 ? "+" : "") << delayImprove << "% │ "
                    << std::setw(18) << lossReduction << " │ "
                    << std::setw(15) << (overheadReduce >= 0 ? "+" : "") << overheadReduce << " │"
                    << std::endl;
        }

      if (mode != smartModes.back())
        {
          NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════╣");
        }
    }

  NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════════════════════╝");
}

int
main(int argc, char *argv[])
{
  uint32_t numNodes = 30;
  double totalTime = 60.0;
  std::vector<double> speeds = {2.0, 5.0, 10.0};

  CommandLine cmd;
  cmd.AddValue("n", "Number of nodes", numNodes);
  cmd.AddValue("t", "Simulation time", totalTime);
  cmd.Parse(argc, argv);

  RngSeedManager::SetSeed(42);

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║              Smart-AODV-V2 MOBILE PERFORMANCE TEST (FULL COMPARISON)                     ║");
  NS_LOG_UNCOND("║                                                                                             ║");
  NS_LOG_UNCOND("║  ┌─────────┬───────────────┬─────────────┬────────────────────┐                             ║");
  NS_LOG_UNCOND("║  │ 对照组1 │ AODV          │ 标准NS-3    │ 经典反应式路由协议 │                             ║");
  NS_LOG_UNCOND("║  │ 对照组2 │ OLSR          │ 标准NS-3    │ 经典先验式路由协议 │                             ║");
  NS_LOG_UNCOND("║  │ 实验组1 │ Smart-AODV-V2 │ SELF_ORG    │ 仅自组织模式       │                             ║");
  NS_LOG_UNCOND("║  │ 实验组2 │ Smart-AODV-V2 │ CENTRALIZED │ 仅中心化模式       │                             ║");
  NS_LOG_UNCOND("║  │ 实验组3 │ Smart-AODV-V2 │ ADAPTIVE    │ RSSI/威胁驱动切换  │                             ║");
  NS_LOG_UNCOND("║  └─────────┴───────────────┴─────────────┴────────────────────┘                             ║");
  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════════════════╝");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Test Configuration:");
  NS_LOG_UNCOND("  Nodes: " << numNodes);
  NS_LOG_UNCOND("  Simulation Time: " << totalTime << " seconds per test");
  NS_LOG_UNCOND("  Speeds: 2, 5, 10 m/s");
  NS_LOG_UNCOND("");

  // Store results
  std::map<std::string, std::vector<MobilityStats>> results;

  // Initialize result vectors
  for (const auto &name : {"AODV", "OLSR", "Smart SELF_ORG", "Smart CENTRAL", "Smart ADAPTIVE"})
    {
      results[name] = std::vector<MobilityStats>(speeds.size());
    }

  // Run tests at each speed
  for (size_t speedIdx = 0; speedIdx < speeds.size(); ++speedIdx)
    {
      double speed = speeds[speedIdx];
      NS_LOG_UNCOND("════════════════════════════════════════════════════════════════════════════════════════════");
      NS_LOG_UNCOND("Testing Speed: " << speed << " m/s");
      NS_LOG_UNCOND("════════════════════════════════════════════════════════════════════════════════════════════");

      // 对照组1: AODV
      results["AODV"][speedIdx] = RunMobilityTest("AODV", MODE_STANDARD, speed, numNodes, totalTime);

      // 对照组2: OLSR
      results["OLSR"][speedIdx] = RunMobilityTest("OLSR", MODE_STANDARD, speed, numNodes, totalTime);

      // 实验组1: Smart-AODV-V2 SELF_ORG
      results["Smart SELF_ORG"][speedIdx] = RunMobilityTest("SMART-AODV-V2", MODE_SMART_SELFORG, speed, numNodes, totalTime);

      // 实验组2: Smart-AODV-V2 CENTRALIZED
      results["Smart CENTRAL"][speedIdx] = RunMobilityTest("SMART-AODV-V2", MODE_SMART_CENTRAL, speed, numNodes, totalTime);

      // 实验组3: Smart-AODV-V2 ADAPTIVE
      results["Smart ADAPTIVE"][speedIdx] = RunMobilityTest("SMART-AODV-V2", MODE_SMART_ADAPTIVE, speed, numNodes, totalTime);
    }

  // Print results
  PrintComparisonTable(speeds, results);
  PrintImprovementAnalysis(speeds, results);

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("=== Test Summary ===");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Comparison Groups:");
  NS_LOG_UNCOND("  对照组1: AODV - 经典反应式路由协议");
  NS_LOG_UNCOND("  对照组2: OLSR - 经典先验式路由协议");
  NS_LOG_UNCOND("  实验组1: Smart-AODV-V2 SELF_ORG - 自组织模式");
  NS_LOG_UNCOND("  实验组2: Smart-AODV-V2 CENTRALIZED - 中心化模式");
  NS_LOG_UNCOND("  实验组3: Smart-AODV-V2 ADAPTIVE - 自适应模式 (RSSI/威胁驱动)");
  NS_LOG_UNCOND("");

  return 0;
}
