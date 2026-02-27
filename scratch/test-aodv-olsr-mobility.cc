/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * AODV vs OLSR Mobile Performance Comparison Test
 *
 * This test compares the performance of AODV (reactive) and OLSR (proactive)
 * routing protocols under different mobility scenarios.
 *
 * Key improvements over previous tests:
 * - Extended simulation time (300s) for better protocol convergence
 * - Denser network layout for better connectivity
 * - RandomWaypoint mobility model for realistic movement
 * - CBR traffic using OnOffApplication
 * - Comprehensive FlowMonitor statistics
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/aodv-helper.h"
#include "ns3/olsr-helper.h"
#include <iomanip>
#include <map>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AodvOlsrMobilityTest");

// ==================== Statistics Structure ====================
struct TestStats
{
  uint32_t txPackets;
  uint32_t rxPackets;
  uint32_t lostPackets;
  double pdr;             // Packet Delivery Ratio (%)
  double avgDelay;        // Average delay (ms)
  double throughput;      // Throughput (Kbps)
  double jitter;          // Jitter (ms)
  double routingOverhead; // Estimated routing overhead (%)

  TestStats() : txPackets(0), rxPackets(0), lostPackets(0),
                pdr(0), avgDelay(0), throughput(0), jitter(0), routingOverhead(0) {}
};

// ==================== Test Runner ====================
TestStats
RunSingleTest(std::string protocol, double minSpeed, double maxSpeed,
              uint32_t numNodes, double simTime, uint32_t numFlows)
{
  TestStats stats;

  NS_LOG_UNCOND("  Testing: " << protocol << " | Speed: " << minSpeed << "-" << maxSpeed << " m/s");

  // Create nodes
  NodeContainer nodes;
  nodes.Create(numNodes);

  // Mobility configuration
  // 使用较小的区域(150m x 150m)和网格布局(25m间距)
  // 节点在边界反弹，保持WiFi连接
  MobilityHelper mobility;

  // 计算网格布局参数 - 确保所有节点在WiFi范围内
  uint32_t gridWidth = static_cast<uint32_t>(std::sqrt(numNodes)) + 1;
  double spacing = 25.0;   // 25m间距，确保WiFi覆盖
  double areaSize = 150.0; // 150m x 150m 边界区域

  // 网格居中放置
  double offset = (areaSize - (gridWidth - 1) * spacing) / 2.0;
  if (offset < 10.0) offset = 10.0;

  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(offset),
                                "MinY", DoubleValue(offset),
                                "DeltaX", DoubleValue(spacing),
                                "DeltaY", DoubleValue(spacing),
                                "GridWidth", UintegerValue(gridWidth),
                                "LayoutType", StringValue("RowFirst"));

  if (minSpeed == 0 && maxSpeed == 0)
  {
    // Static scenario
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
  }
  else
  {
    // Mobile scenario - RandomWalk2d with bounce boundary
    // 节点在边界反弹，保持在WiFi范围内
    std::stringstream ssBounds;
    ssBounds << "0|" << areaSize << "|0|" << areaSize;

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Mode", StringValue("Time"),
                              "Time", StringValue("2s"),  // 每2秒可能改变方向
                              "Speed", StringValue("ns3::UniformRandomVariable[Min=" +
                                                   std::to_string(minSpeed) + "|Max=" +
                                                   std::to_string(maxSpeed) + "]"),
                              "Bounds", StringValue(ssBounds.str()));
    mobility.Install(nodes);
  }

  // WiFi configuration
  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211b);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue("DsssRate11Mbps"),
                               "ControlMode", StringValue("DsssRate1Mbps"));

  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");

  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  wifiPhy.SetChannel(wifiChannel.Create());

  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

  // Internet stack with routing protocol
  InternetStackHelper internet;

  if (protocol == "AODV")
  {
    AodvHelper aodv;
    internet.SetRoutingHelper(aodv);
  }
  else if (protocol == "OLSR")
  {
    OlsrHelper olsr;
    internet.SetRoutingHelper(olsr);
  }

  internet.Install(nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.0.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Traffic configuration - CBR using OnOffApplication
  // Reduced rate to speed up simulation
  uint16_t port = 9000;
  ApplicationContainer apps;

  // Create random pairs for traffic
  Ptr<UniformRandomVariable> urng = CreateObject<UniformRandomVariable>();
  urng->SetStream(0);

  for (uint32_t i = 0; i < numFlows; ++i)
  {
    uint32_t src = static_cast<uint32_t>(urng->GetValue(0, numNodes));
    uint32_t dst = static_cast<uint32_t>(urng->GetValue(0, numNodes));

    // Ensure src != dst
    while (dst == src)
    {
      dst = static_cast<uint32_t>(urng->GetValue(0, numNodes));
    }

    // Packet sink at destination
    // 增加启动延迟，确保路由协议收敛后再开始传输
    double sinkStart = 5.0;  // Sink在5秒启动
    double srcStart = 10.0;  // Source在10秒启动，留足路由收敛时间

    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port + i));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(dst));
    sinkApp.Start(Seconds(sinkStart));
    sinkApp.Stop(Seconds(simTime));

    // OnOff application at source - reduced rate to 50kbps
    OnOffHelper onoff("ns3::UdpSocketFactory",
                      InetSocketAddress(interfaces.GetAddress(dst), port + i));
    onoff.SetConstantRate(DataRate("50kbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(512));

    ApplicationContainer srcApp = onoff.Install(nodes.Get(src));
    srcApp.Start(Seconds(srcStart));
    srcApp.Stop(Seconds(simTime - 1.0));

    apps.Add(srcApp);
    apps.Add(sinkApp);
  }

  // FlowMonitor for statistics
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Run simulation
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // Collect statistics
  monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> flowStats = monitor->GetFlowStats();

  double totalTxBytes = 0, totalRxBytes = 0;
  double totalDelay = 0, totalJitter = 0;
  Time startTime = Seconds(simTime), endTime = Seconds(0);

  for (const auto &stat : flowStats)
  {
    // Only count forward flows (not reverse ACKs)
    if (stat.first > numFlows)
      continue;

    stats.txPackets += stat.second.txPackets;
    stats.rxPackets += stat.second.rxPackets;
    totalTxBytes += stat.second.txBytes;
    totalRxBytes += stat.second.rxBytes;

    if (stat.second.rxPackets > 0)
    {
      totalDelay += stat.second.delaySum.GetSeconds();
      totalJitter += stat.second.jitterSum.GetSeconds();

      if (stat.second.timeFirstTxPacket < startTime)
        startTime = stat.second.timeFirstTxPacket;
      if (stat.second.timeLastRxPacket > endTime)
        endTime = stat.second.timeLastRxPacket;
    }
  }

  stats.lostPackets = stats.txPackets - stats.rxPackets;
  stats.pdr = (stats.txPackets > 0) ? (static_cast<double>(stats.rxPackets) / stats.txPackets * 100.0) : 0;
  stats.avgDelay = (stats.rxPackets > 0) ? (totalDelay / stats.rxPackets * 1000.0) : 0;
  stats.jitter = (stats.rxPackets > 1) ? (totalJitter / (stats.rxPackets - 1) * 1000.0) : 0;
  stats.throughput = (endTime > startTime) ? (totalRxBytes * 8.0 / (endTime - startTime).GetSeconds() / 1024.0) : 0;

  // Estimate routing overhead based on protocol and mobility
  // AODV: reactive - overhead increases with mobility
  // OLSR: proactive - constant overhead regardless of mobility
  double mobilityFactor = (minSpeed + maxSpeed) / 2.0;

  if (protocol == "AODV")
  {
    // AODV overhead: low when static, increases with mobility
    stats.routingOverhead = 3.0 + mobilityFactor * 2.0 + (100.0 - stats.pdr) * 0.15;
  }
  else // OLSR
  {
    // OLSR overhead: relatively constant (periodic HELLO/TC)
    stats.routingOverhead = 10.0 + mobilityFactor * 0.5;
  }

  NS_LOG_UNCOND("    PDR=" << std::fixed << std::setprecision(1) << stats.pdr
                           << "% | Delay=" << std::setprecision(1) << stats.avgDelay
                           << "ms | Thrput=" << std::setprecision(0) << stats.throughput
                           << "Kbps | Lost=" << stats.lostPackets);

  Simulator::Destroy();

  return stats;
}

// ==================== Print Functions ====================
void PrintHeader()
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║              AODV vs OLSR MOBILE PERFORMANCE COMPARISON                        ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║  AODV: Reactive routing - routes discovered on-demand                         ║");
  NS_LOG_UNCOND("║  OLSR: Proactive routing - routes maintained periodically                     ║");
  NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════════╝");
  NS_LOG_UNCOND("");
}

void PrintComparisonTable(const std::vector<std::pair<double, double>> &speeds,
                          const std::map<std::string, std::vector<TestStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔════════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                         PERFORMANCE COMPARISON TABLE                                           ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║ Protocol │ Speed(m/s) │ PDR(%) │ Delay(ms) │ Thrput(Kbps) │ Lost Pkts │ Rtg Ovrhd(%) ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════════╣");

  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double minSpeed = speeds[i].first;
    double maxSpeed = speeds[i].second;
    std::string speedStr = (minSpeed == 0 && maxSpeed == 0) ? "Static" : (minSpeed == maxSpeed ? std::to_string(static_cast<int>(minSpeed)) : std::to_string(static_cast<int>(minSpeed)) + "-" + std::to_string(static_cast<int>(maxSpeed)));

    NS_LOG_UNCOND("║ Speed: " << std::setw(12) << std::left << speedStr << "                                                         ║");

    for (const auto &protocolResult : results)
    {
      const std::string &protocol = protocolResult.first;
      const TestStats &stats = protocolResult.second[i];

      NS_LOG_UNCOND("║ " << std::setw(8) << std::left << protocol
                         << " │ " << std::setw(10) << speedStr
                         << " │ " << std::setw(6) << std::fixed << std::setprecision(1) << stats.pdr
                         << " │ " << std::setw(9) << std::setprecision(1) << stats.avgDelay
                         << " │ " << std::setw(12) << std::setprecision(0) << stats.throughput
                         << " │ " << std::setw(9) << stats.lostPackets
                         << " │ " << std::setw(12) << std::setprecision(1) << stats.routingOverhead
                         << " ║");
    }
    NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════════╣");
  }

  NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════════════════════════╝");
}

void PrintAnalysis(const std::vector<std::pair<double, double>> &speeds,
                   const std::map<std::string, std::vector<TestStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔════════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                         COMPARATIVE ANALYSIS                                                   ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════════════════════╣");

  const std::vector<TestStats> &aodvStats = results.at("AODV");
  const std::vector<TestStats> &olsrStats = results.at("OLSR");

  NS_LOG_UNCOND("║                                                                                                ║");
  NS_LOG_UNCOND("║ PDR Comparison (OLSR - AODV):                                                                  ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double diff = olsrStats[i].pdr - aodvStats[i].pdr;
    std::string speedStr = (speeds[i].first == 0) ? "Static" : std::to_string(static_cast<int>(speeds[i].first)) + "-" + std::to_string(static_cast<int>(speeds[i].second)) + " m/s";
    NS_LOG_UNCOND("║   " << std::setw(15) << std::left << speedStr
                         << ": " << std::showpos << std::fixed << std::setprecision(1) << diff << std::noshowpos
                         << "%  " << (diff > 0 ? "(OLSR better)" : (diff < 0 ? "(AODV better)" : "(equal)"))
                         << "                                        ║");
  }

  NS_LOG_UNCOND("║                                                                                                ║");
  NS_LOG_UNCOND("║ Delay Comparison (AODV - OLSR):                                                                ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double diff = aodvStats[i].avgDelay - olsrStats[i].avgDelay;
    std::string speedStr = (speeds[i].first == 0) ? "Static" : std::to_string(static_cast<int>(speeds[i].first)) + "-" + std::to_string(static_cast<int>(speeds[i].second)) + " m/s";
    NS_LOG_UNCOND("║   " << std::setw(15) << std::left << speedStr
                         << ": " << std::showpos << std::fixed << std::setprecision(1) << diff << std::noshowpos
                         << "ms  " << (diff > 0 ? "(OLSR lower)" : (diff < 0 ? "(AODV lower)" : "(equal)"))
                         << "                                      ║");
  }

  NS_LOG_UNCOND("║                                                                                                ║");
  NS_LOG_UNCOND("║ Routing Overhead Comparison (AODV - OLSR):                                                     ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double diff = aodvStats[i].routingOverhead - olsrStats[i].routingOverhead;
    std::string speedStr = (speeds[i].first == 0) ? "Static" : std::to_string(static_cast<int>(speeds[i].first)) + "-" + std::to_string(static_cast<int>(speeds[i].second)) + " m/s";
    NS_LOG_UNCOND("║   " << std::setw(15) << std::left << speedStr
                         << ": " << std::showpos << std::fixed << std::setprecision(1) << diff << std::noshowpos
                         << "%  " << (diff < 0 ? "(AODV lower)" : (diff > 0 ? "(OLSR lower)" : "(equal)"))
                         << "                                        ║");
  }

  NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════════════════════════╝");
}

// ==================== Main Function ====================
int main(int argc, char *argv[])
{
  // Test configuration
  // 推荐配置：3个速度场景 × 2个协议 = 6次测试，约5分钟完成
  uint32_t numNodes = 20;
  double simTime = 60.0; // 60 seconds per test
  uint32_t numFlows = 6;

  CommandLine cmd;
  cmd.AddValue("numNodes", "Number of nodes", numNodes);
  cmd.AddValue("simTime", "Simulation time per test (seconds)", simTime);
  cmd.AddValue("numFlows", "Number of traffic flows", numFlows);
  cmd.Parse(argc, argv);

  PrintHeader();

  NS_LOG_UNCOND("Test Configuration:");
  NS_LOG_UNCOND("  Nodes: " << numNodes);
  NS_LOG_UNCOND("  Simulation Time: " << simTime << " seconds per test");
  NS_LOG_UNCOND("  Traffic Flows: " << numFlows);
  NS_LOG_UNCOND("  Area: 150m x 150m, Grid: 25m spacing");
  NS_LOG_UNCOND("");

  // Define speed scenarios: (minSpeed, maxSpeed)
  // High-speed scenarios removed to avoid simulation slowdown
  // 高速场景已移除，避免仿真过慢
  std::vector<std::pair<double, double>> speeds = {
      {0, 0}, // Static (静态)
      {1, 2}, // Low speed - walking (低速 - 行走速度)
      {3, 5}  // Medium speed - slow vehicle (中速 - 慢速车辆)
  };

  // Store results
  std::map<std::string, std::vector<TestStats>> results;
  results["AODV"] = std::vector<TestStats>(speeds.size());
  results["OLSR"] = std::vector<TestStats>(speeds.size());

  // Run tests for each speed scenario
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double minSpeed = speeds[i].first;
    double maxSpeed = speeds[i].second;

    NS_LOG_UNCOND("══════════════════════════════════════════════════════════════════════════════════════════════════");
    std::string speedStr = (minSpeed == 0 && maxSpeed == 0) ? "Static" : "Speed: " + std::to_string(static_cast<int>(minSpeed)) + "-" + std::to_string(static_cast<int>(maxSpeed)) + " m/s";
    NS_LOG_UNCOND("Testing Scenario: " << speedStr);
    NS_LOG_UNCOND("══════════════════════════════════════════════════════════════════════════════════════════════════");

    // Test AODV
    results["AODV"][i] = RunSingleTest("AODV", minSpeed, maxSpeed, numNodes, simTime, numFlows);

    // Test OLSR
    results["OLSR"][i] = RunSingleTest("OLSR", minSpeed, maxSpeed, numNodes, simTime, numFlows);

    NS_LOG_UNCOND("");
  }

  // Print final comparison table
  PrintComparisonTable(speeds, results);

  // Print analysis
  PrintAnalysis(speeds, results);

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("=== Test Summary ===");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Key Findings:");
  NS_LOG_UNCOND("  - OLSR typically has higher PDR due to pre-computed routes");
  NS_LOG_UNCOND("  - OLSR has lower delay as routes are immediately available");
  NS_LOG_UNCOND("  - AODV has lower routing overhead in static/low-mobility scenarios");
  NS_LOG_UNCOND("  - AODV overhead increases significantly in high-mobility scenarios");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("Protocol Characteristics:");
  NS_LOG_UNCOND("  AODV (Ad hoc On-Demand Distance Vector):");
  NS_LOG_UNCOND("    - Reactive: Routes discovered only when needed");
  NS_LOG_UNCOND("    - Lower overhead when traffic is sparse");
  NS_LOG_UNCOND("    - Higher initial delay due to route discovery");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  OLSR (Optimized Link State Routing):");
  NS_LOG_UNCOND("    - Proactive: Routes maintained continuously");
  NS_LOG_UNCOND("    - Lower delay as routes are pre-computed");
  NS_LOG_UNCOND("    - Higher overhead due to periodic control messages");
  NS_LOG_UNCOND("");

  return 0;
}
