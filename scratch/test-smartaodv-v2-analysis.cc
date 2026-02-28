/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV-V2 Performance Impact Analysis with Protocol Comparison
 *
 * This test analyzes the performance impact of Q-Learning vs Clustering
 * in Smart-AODV-V2, compared with standard routing protocols:
 *
 * Standard Protocols:
 * - AODV: Standard reactive routing
 * - OLSR: Standard proactive routing
 * - Smart-AODV: Adaptive routing with energy awareness
 *
 * Smart-AODV-V2 Configurations:
 * - Full:      Q-Learning ON  + Clustering ON  (default)
 * - QLearn:    Q-Learning ON  + Clustering OFF (ClusterId=0)
 * - Cluster:   Q-Learning OFF + Clustering ON
 * - Basic:     Q-Learning OFF + Clustering OFF (baseline AODV-like)
 *
 * Test scenarios: Static, Low-speed (1-2 m/s), Medium-speed (3-5 m/s)
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
#include "ns3/smart-aodv-helper.h"
#include "ns3/smart-aodv-v2-helper.h"
#include "ns3/smart-aodv-v2-cluster.h"
#include <iomanip>
#include <map>
#include <vector>

using namespace ns3;
using namespace smartAodvV2;

NS_LOG_COMPONENT_DEFINE("SmartAodvV2Analysis");

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

  TestStats() : txPackets(0), rxPackets(0), lostPackets(0),
                pdr(0), avgDelay(0), throughput(0), jitter(0) {}
};

// ==================== Configuration Types ====================
enum ConfigType
{
  // Standard protocols
  CONFIG_AODV,
  CONFIG_OLSR,
  CONFIG_SMART_AODV,
  // Smart-AODV-V2 configurations
  CONFIG_V2_FULL,      // Q-Learning + Clustering
  CONFIG_V2_QLEARN,    // Q-Learning only
  CONFIG_V2_CLUSTER,   // Clustering only
  CONFIG_V2_BASIC      // Neither (baseline)
};

std::string ConfigToString(ConfigType config)
{
  switch (config)
  {
    case CONFIG_AODV:        return "AODV";
    case CONFIG_OLSR:        return "OLSR";
    case CONFIG_SMART_AODV:  return "Smart-AODV";
    case CONFIG_V2_FULL:     return "V2-Full";
    case CONFIG_V2_QLEARN:   return "V2-QLearn";
    case CONFIG_V2_CLUSTER:  return "V2-Cluster";
    case CONFIG_V2_BASIC:    return "V2-Basic";
    default:                 return "Unknown";
  }
}

bool IsV2Config(ConfigType config)
{
  return config >= CONFIG_V2_FULL;
}

// ==================== Test Runner ====================
TestStats
RunSingleTest(ConfigType config, double minSpeed, double maxSpeed,
              uint32_t numNodes, double simTime, uint32_t numFlows)
{
  TestStats stats;

  NS_LOG_UNCOND("  Testing: " << ConfigToString(config) << " | Speed: "
                << minSpeed << "-" << maxSpeed << " m/s");

  // Create nodes
  NodeContainer nodes;
  nodes.Create(numNodes);

  // Mobility configuration
  MobilityHelper mobility;

  uint32_t gridWidth = static_cast<uint32_t>(std::sqrt(numNodes)) + 1;
  double spacing = 25.0;
  double areaSize = 150.0;

  double offset = (areaSize - (gridWidth - 1) * spacing) / 2.0;
  if (offset < 10.0)
    offset = 10.0;

  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(offset),
                                "MinY", DoubleValue(offset),
                                "DeltaX", DoubleValue(spacing),
                                "DeltaY", DoubleValue(spacing),
                                "GridWidth", UintegerValue(gridWidth),
                                "LayoutType", StringValue("RowFirst"));

  if (minSpeed == 0 && maxSpeed == 0)
  {
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
  }
  else
  {
    std::stringstream ssBounds;
    ssBounds << "0|" << areaSize << "|0|" << areaSize;

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Mode", StringValue("Time"),
                              "Time", StringValue("2s"),
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

  switch (config)
  {
    case CONFIG_AODV:
    {
      AodvHelper aodv;
      internet.SetRoutingHelper(aodv);
      break;
    }
    case CONFIG_OLSR:
    {
      OlsrHelper olsr;
      internet.SetRoutingHelper(olsr);
      break;
    }
    case CONFIG_SMART_AODV:
    {
      SmartAodvHelper smartAodv;
      internet.SetRoutingHelper(smartAodv);
      break;
    }
    case CONFIG_V2_FULL:
    {
      SmartAodvV2Helper smartAodvV2;
      smartAodvV2.Set("EnableQLearning", BooleanValue(true));
      smartAodvV2.Set("ClusterId", UintegerValue(1));
      smartAodvV2.Set("ClusterMode", EnumValue(MODE_SELF_ORG));
      internet.SetRoutingHelper(smartAodvV2);
      break;
    }
    case CONFIG_V2_QLEARN:
    {
      SmartAodvV2Helper smartAodvV2;
      smartAodvV2.Set("EnableQLearning", BooleanValue(true));
      smartAodvV2.Set("ClusterId", UintegerValue(0));
      internet.SetRoutingHelper(smartAodvV2);
      break;
    }
    case CONFIG_V2_CLUSTER:
    {
      SmartAodvV2Helper smartAodvV2;
      smartAodvV2.Set("EnableQLearning", BooleanValue(false));
      smartAodvV2.Set("ClusterId", UintegerValue(1));
      smartAodvV2.Set("ClusterMode", EnumValue(MODE_SELF_ORG));
      internet.SetRoutingHelper(smartAodvV2);
      break;
    }
    case CONFIG_V2_BASIC:
    {
      SmartAodvV2Helper smartAodvV2;
      smartAodvV2.Set("EnableQLearning", BooleanValue(false));
      smartAodvV2.Set("ClusterId", UintegerValue(0));
      internet.SetRoutingHelper(smartAodvV2);
      break;
    }
  }

  internet.Install(nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.0.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Traffic configuration
  uint16_t port = 9000;
  ApplicationContainer apps;

  Ptr<UniformRandomVariable> urng = CreateObject<UniformRandomVariable>();
  urng->SetStream(0);

  for (uint32_t i = 0; i < numFlows; ++i)
  {
    uint32_t src = static_cast<uint32_t>(urng->GetValue(0, numNodes));
    uint32_t dst = static_cast<uint32_t>(urng->GetValue(0, numNodes));

    while (dst == src)
    {
      dst = static_cast<uint32_t>(urng->GetValue(0, numNodes));
    }

    double sinkStart = 5.0;
    double srcStart = 10.0;

    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port + i));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(dst));
    sinkApp.Start(Seconds(sinkStart));
    sinkApp.Stop(Seconds(simTime));

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
  NS_LOG_UNCOND("║         Smart-AODV-V2 Performance Impact Analysis                              ║");
  NS_LOG_UNCOND("║              Q-Learning vs Clustering with Protocol Comparison                 ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║  Standard Protocols:                                                           ║");
  NS_LOG_UNCOND("║    AODV:        Standard reactive routing                                      ║");
  NS_LOG_UNCOND("║    OLSR:        Standard proactive routing                                     ║");
  NS_LOG_UNCOND("║    Smart-AODV:  Adaptive routing with energy awareness                         ║");
  NS_LOG_UNCOND("║  Smart-AODV-V2 Configurations:                                                 ║");
  NS_LOG_UNCOND("║    V2-Full:     Q-Learning ON  + Clustering ON  - Default                      ║");
  NS_LOG_UNCOND("║    V2-QLearn:   Q-Learning ON  + Clustering OFF - Q-Learning only             ║");
  NS_LOG_UNCOND("║    V2-Cluster:  Q-Learning OFF + Clustering ON  - Clustering only             ║");
  NS_LOG_UNCOND("║    V2-Basic:    Q-Learning OFF + Clustering OFF - Baseline                    ║");
  NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════════╝");
  NS_LOG_UNCOND("");
}

void PrintComparisonTable(const std::vector<std::pair<double, double>> &speeds,
                          const std::map<ConfigType, std::vector<TestStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                              PERFORMANCE COMPARISON TABLE                                            ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║ Protocol       │ Speed  │ PDR(%) │ Delay(ms) │ Thrput(Kbps) │ Lost Pkts │ Type            ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");

  std::vector<ConfigType> configs = {
    CONFIG_AODV, CONFIG_OLSR, CONFIG_SMART_AODV,
    CONFIG_V2_FULL, CONFIG_V2_QLEARN, CONFIG_V2_CLUSTER, CONFIG_V2_BASIC
  };

  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double minSpeed = speeds[i].first;
    double maxSpeed = speeds[i].second;
    std::string speedStr = (minSpeed == 0 && maxSpeed == 0) ? "Static" :
                           (minSpeed == maxSpeed ? std::to_string(static_cast<int>(minSpeed)) :
                            std::to_string(static_cast<int>(minSpeed)) + "-" + std::to_string(static_cast<int>(maxSpeed)));

    NS_LOG_UNCOND("║ Speed: " << std::setw(10) << std::left << speedStr << "                                                                           ║");

    for (ConfigType config : configs)
    {
      const TestStats &stats = results.at(config).at(i);
      std::string type = IsV2Config(config) ? "V2 Config" : "Standard";

      NS_LOG_UNCOND("║ " << std::setw(14) << std::left << ConfigToString(config)
                         << " │ " << std::setw(6) << speedStr
                         << " │ " << std::setw(6) << std::fixed << std::setprecision(1) << stats.pdr
                         << " │ " << std::setw(9) << std::setprecision(1) << stats.avgDelay
                         << " │ " << std::setw(12) << std::setprecision(0) << stats.throughput
                         << " │ " << std::setw(9) << stats.lostPackets
                         << " │ " << std::setw(16) << type
                         << " ║");
    }
    NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");
  }

  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════════════════════════════╝");
}

void PrintAnalysis(const std::vector<std::pair<double, double>> &speeds,
                   const std::map<ConfigType, std::vector<TestStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                              COMPARATIVE ANALYSIS                                                    ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");

  const std::vector<TestStats> &aodvStats = results.at(CONFIG_AODV);
  const std::vector<TestStats> &olsrStats = results.at(CONFIG_OLSR);
  const std::vector<TestStats> &smartAodvStats = results.at(CONFIG_SMART_AODV);
  const std::vector<TestStats> &v2FullStats = results.at(CONFIG_V2_FULL);
  const std::vector<TestStats> &v2QlearnStats = results.at(CONFIG_V2_QLEARN);
  const std::vector<TestStats> &v2ClusterStats = results.at(CONFIG_V2_CLUSTER);
  const std::vector<TestStats> &v2BasicStats = results.at(CONFIG_V2_BASIC);

  NS_LOG_UNCOND("║                                                                                                      ║");
  NS_LOG_UNCOND("║ PDR Comparison (vs AODV baseline):                                                                   ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    std::string speedStr = (speeds[i].first == 0) ? "Static" :
                           std::to_string(static_cast<int>(speeds[i].first)) + "-" +
                           std::to_string(static_cast<int>(speeds[i].second)) + " m/s";

    NS_LOG_UNCOND("║   " << std::setw(10) << std::left << speedStr << ": "
                         << "OLSR=" << std::showpos << std::setw(6) << std::fixed << std::setprecision(1) << (olsrStats[i].pdr - aodvStats[i].pdr)
                         << "%  SmartAODV=" << std::setw(6) << (smartAodvStats[i].pdr - aodvStats[i].pdr)
                         << "%  V2-Full=" << std::setw(6) << (v2FullStats[i].pdr - aodvStats[i].pdr)
                         << "%  V2-Cluster=" << std::setw(6) << (v2ClusterStats[i].pdr - aodvStats[i].pdr) << std::noshowpos
                         << "%        ║");
  }

  NS_LOG_UNCOND("║                                                                                                      ║");
  NS_LOG_UNCOND("║ Smart-AODV-V2 Feature Impact (vs V2-Basic):                                                          ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    std::string speedStr = (speeds[i].first == 0) ? "Static" :
                           std::to_string(static_cast<int>(speeds[i].first)) + "-" +
                           std::to_string(static_cast<int>(speeds[i].second)) + " m/s";

    double qlearnImpact = v2QlearnStats[i].pdr - v2BasicStats[i].pdr;
    double clusterImpact = v2ClusterStats[i].pdr - v2BasicStats[i].pdr;
    double fullImpact = v2FullStats[i].pdr - v2BasicStats[i].pdr;

    NS_LOG_UNCOND("║   " << std::setw(10) << std::left << speedStr << ": "
                         << "Q-Learning=" << std::showpos << std::setw(6) << std::fixed << std::setprecision(1) << qlearnImpact
                         << "%  Clustering=" << std::setw(6) << clusterImpact
                         << "%  Combined=" << std::setw(6) << fullImpact << std::noshowpos
                         << "%                      ║");
  }

  NS_LOG_UNCOND("║                                                                                                      ║");
  NS_LOG_UNCOND("║ Synergy Analysis (Full vs QLearn+Cluster-Basic):                                                     ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    std::string speedStr = (speeds[i].first == 0) ? "Static" :
                           std::to_string(static_cast<int>(speeds[i].first)) + "-" +
                           std::to_string(static_cast<int>(speeds[i].second)) + " m/s";

    double expected = v2QlearnStats[i].pdr + v2ClusterStats[i].pdr - v2BasicStats[i].pdr;
    double actual = v2FullStats[i].pdr;
    double synergy = actual - expected;

    NS_LOG_UNCOND("║   " << std::setw(10) << std::left << speedStr << ": "
                         << "Expected=" << std::setw(6) << std::fixed << std::setprecision(1) << expected
                         << "%  Actual=" << std::setw(6) << actual
                         << "%  Synergy=" << std::showpos << std::setw(6) << synergy << std::noshowpos
                         << "%                          ║");
  }

  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════════════════════════════╝");
}

// ==================== Main Function ====================
int main(int argc, char *argv[])
{
  // Test configuration
  uint32_t numNodes = 20;
  double simTime = 60.0;
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
  NS_LOG_UNCOND("  Total Tests: 7 protocols x 3 speeds = 21 tests");
  NS_LOG_UNCOND("");

  // Define speed scenarios
  std::vector<std::pair<double, double>> speeds = {
      {0, 0},    // Static
      {1, 2},    // Low speed
      {3, 5}     // Medium speed
  };

  // Define protocols to test
  std::vector<ConfigType> configs = {
      CONFIG_AODV,
      CONFIG_OLSR,
      CONFIG_SMART_AODV,
      CONFIG_V2_FULL,
      CONFIG_V2_QLEARN,
      CONFIG_V2_CLUSTER,
      CONFIG_V2_BASIC
  };

  // Store results
  std::map<ConfigType, std::vector<TestStats>> results;
  for (ConfigType config : configs)
  {
    results[config] = std::vector<TestStats>(speeds.size());
  }

  // Run tests for each speed scenario
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double minSpeed = speeds[i].first;
    double maxSpeed = speeds[i].second;

    NS_LOG_UNCOND("══════════════════════════════════════════════════════════════════════════════════════════════════════");
    std::string speedStr = (minSpeed == 0 && maxSpeed == 0) ? "Static" : "Speed: " + std::to_string(static_cast<int>(minSpeed)) + "-" + std::to_string(static_cast<int>(maxSpeed)) + " m/s";
    NS_LOG_UNCOND("Testing Scenario: " << speedStr);
    NS_LOG_UNCOND("══════════════════════════════════════════════════════════════════════════════════════════════════════");

    // Test each protocol
    for (ConfigType config : configs)
    {
      results[config][i] = RunSingleTest(config, minSpeed, maxSpeed, numNodes, simTime, numFlows);
    }

    NS_LOG_UNCOND("");
  }

  // Print final comparison table
  PrintComparisonTable(speeds, results);

  // Print analysis
  PrintAnalysis(speeds, results);

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("=== Summary ===");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  Best Protocol per Scenario (by PDR):");
  NS_LOG_UNCOND("");

  for (size_t i = 0; i < speeds.size(); ++i)
  {
    std::string speedStr = (speeds[i].first == 0) ? "Static" :
                           std::to_string(static_cast<int>(speeds[i].first)) + "-" +
                           std::to_string(static_cast<int>(speeds[i].second)) + " m/s";

    ConfigType best = configs[0];
    double bestPdr = results[configs[0]][i].pdr;

    for (ConfigType config : configs)
    {
      if (results[config][i].pdr > bestPdr)
      {
        bestPdr = results[config][i].pdr;
        best = config;
      }
    }

    NS_LOG_UNCOND("    " << speedStr << ": " << ConfigToString(best) << " (" << std::fixed << std::setprecision(1) << bestPdr << "%)");
  }

  NS_LOG_UNCOND("");

  return 0;
}
