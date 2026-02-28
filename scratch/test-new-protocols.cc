/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Smart-AODV Variants Comparison Test
 *
 * This test compares:
 * - AODV: Standard reactive routing
 * - OLSR: Standard proactive routing
 * - Smart-AODV: Adaptive routing with energy awareness
 * - Smart-AODV-QLearning: Smart-AODV + Q-Learning
 * - Smart-AODV-Cluster: Smart-AODV + Clustering
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
#include "ns3/smart-aodv-qlearning-helper.h"
#include "ns3/smart-aodv-cluster-helper.h"
#include <iomanip>
#include <map>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NewProtocolsTest");

// ==================== Statistics Structure ====================
struct TestStats
{
  uint32_t txPackets;
  uint32_t rxPackets;
  uint32_t lostPackets;
  double pdr;
  double avgDelay;
  double throughput;
  double jitter;

  TestStats() : txPackets(0), rxPackets(0), lostPackets(0),
                pdr(0), avgDelay(0), throughput(0), jitter(0) {}
};

// ==================== Protocol Types ====================
enum ProtocolType
{
  PROTO_AODV,
  PROTO_OLSR,
  PROTO_SMART_AODV,
  PROTO_SMART_AODV_QLEARNING,
  PROTO_SMART_AODV_CLUSTER
};

std::string ProtocolToString(ProtocolType proto)
{
  switch (proto)
  {
    case PROTO_AODV:                return "AODV";
    case PROTO_OLSR:                return "OLSR";
    case PROTO_SMART_AODV:          return "Smart-AODV";
    case PROTO_SMART_AODV_QLEARNING: return "Smart-AODV-QL";
    case PROTO_SMART_AODV_CLUSTER:   return "Smart-AODV-CL";
    default:                         return "Unknown";
  }
}

// ==================== Test Runner ====================
TestStats
RunSingleTest(ProtocolType protocol, double minSpeed, double maxSpeed,
              uint32_t numNodes, double simTime, uint32_t numFlows)
{
  TestStats stats;

  NS_LOG_UNCOND("  Testing: " << ProtocolToString(protocol) << " | Speed: "
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

  switch (protocol)
  {
    case PROTO_AODV:
    {
      AodvHelper aodv;
      internet.SetRoutingHelper(aodv);
      break;
    }
    case PROTO_OLSR:
    {
      OlsrHelper olsr;
      internet.SetRoutingHelper(olsr);
      break;
    }
    case PROTO_SMART_AODV:
    {
      SmartAodvHelper smartAodv;
      internet.SetRoutingHelper(smartAodv);
      break;
    }
    case PROTO_SMART_AODV_QLEARNING:
    {
      SmartAodvQlearningHelper smartAodvQl;
      internet.SetRoutingHelper(smartAodvQl);
      break;
    }
    case PROTO_SMART_AODV_CLUSTER:
    {
      SmartAodvClusterHelper smartAodvCl;
      internet.SetRoutingHelper(smartAodvCl);
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
  NS_LOG_UNCOND("║           Smart-AODV Variants Comparison Test                                  ║");
  NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║  AODV:              Standard reactive routing                                   ║");
  NS_LOG_UNCOND("║  OLSR:              Standard proactive routing                                  ║");
  NS_LOG_UNCOND("║  Smart-AODV:        Adaptive routing with energy awareness + link prediction   ║");
  NS_LOG_UNCOND("║  Smart-AODV-QL:     Smart-AODV + Q-Learning for intelligent routing            ║");
  NS_LOG_UNCOND("║  Smart-AODV-CL:     Smart-AODV + Clustering for hierarchical routing           ║");
  NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════════════════════╝");
  NS_LOG_UNCOND("");
}

void PrintComparisonTable(const std::vector<std::pair<double, double>> &speeds,
                          const std::map<ProtocolType, std::vector<TestStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                              PERFORMANCE COMPARISON TABLE                                            ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");
  NS_LOG_UNCOND("║ Protocol           │ Speed  │ PDR(%) │ Delay(ms) │ Thrput(Kbps) │ Lost Pkts │ Feature        ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");

  std::vector<ProtocolType> protocols = {
    PROTO_AODV, PROTO_OLSR, PROTO_SMART_AODV,
    PROTO_SMART_AODV_QLEARNING, PROTO_SMART_AODV_CLUSTER
  };

  std::map<ProtocolType, std::string> features = {
    {PROTO_AODV, "Baseline"},
    {PROTO_OLSR, "Proactive"},
    {PROTO_SMART_AODV, "LinkPred"},
    {PROTO_SMART_AODV_QLEARNING, "Q-Learning"},
    {PROTO_SMART_AODV_CLUSTER, "Clustering"}
  };

  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double minSpeed = speeds[i].first;
    double maxSpeed = speeds[i].second;
    std::string speedStr = (minSpeed == 0 && maxSpeed == 0) ? "Static" :
                           std::to_string(static_cast<int>(minSpeed)) + "-" + std::to_string(static_cast<int>(maxSpeed));

    NS_LOG_UNCOND("║ Speed: " << std::setw(10) << std::left << speedStr << "                                                                         ║");

    for (ProtocolType proto : protocols)
    {
      const TestStats &stats = results.at(proto).at(i);

      NS_LOG_UNCOND("║ " << std::setw(18) << std::left << ProtocolToString(proto)
                         << " │ " << std::setw(6) << speedStr
                         << " │ " << std::setw(6) << std::fixed << std::setprecision(1) << stats.pdr
                         << " │ " << std::setw(9) << std::setprecision(1) << stats.avgDelay
                         << " │ " << std::setw(12) << std::setprecision(0) << stats.throughput
                         << " │ " << std::setw(9) << stats.lostPackets
                         << " │ " << std::setw(14) << features[proto]
                         << " ║");
    }
    NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");
  }

  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════════════════════════════╝");
}

void PrintAnalysis(const std::vector<std::pair<double, double>> &speeds,
                   const std::map<ProtocolType, std::vector<TestStats>> &results)
{
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║                              COMPARATIVE ANALYSIS                                                    ║");
  NS_LOG_UNCOND("╠══════════════════════════════════════════════════════════════════════════════════════════════════════╣");

  const std::vector<TestStats> &smartAodvStats = results.at(PROTO_SMART_AODV);
  const std::vector<TestStats> &qlearnStats = results.at(PROTO_SMART_AODV_QLEARNING);
  const std::vector<TestStats> &clusterStats = results.at(PROTO_SMART_AODV_CLUSTER);

  NS_LOG_UNCOND("║                                                                                                      ║");
  NS_LOG_UNCOND("║ PDR Comparison (vs Smart-AODV baseline):                                                             ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    std::string speedStr = (speeds[i].first == 0) ? "Static" :
                           std::to_string(static_cast<int>(speeds[i].first)) + "-" +
                           std::to_string(static_cast<int>(speeds[i].second)) + " m/s";

    double qlearnDiff = qlearnStats[i].pdr - smartAodvStats[i].pdr;
    double clusterDiff = clusterStats[i].pdr - smartAodvStats[i].pdr;

    NS_LOG_UNCOND("║   " << std::setw(10) << std::left << speedStr << ": "
                         << "Q-Learning=" << std::showpos << std::setw(6) << std::fixed << std::setprecision(1) << qlearnDiff
                         << "%  Clustering=" << std::setw(6) << clusterDiff << std::noshowpos
                         << "%                                              ║");
  }

  NS_LOG_UNCOND("║                                                                                                      ║");
  NS_LOG_UNCOND("║ Best Protocol per Scenario:                                                                          ║");
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    std::string speedStr = (speeds[i].first == 0) ? "Static" :
                           std::to_string(static_cast<int>(speeds[i].first)) + "-" +
                           std::to_string(static_cast<int>(speeds[i].second)) + " m/s";

    ProtocolType best = PROTO_AODV;
    double bestPdr = results.at(PROTO_AODV)[i].pdr;

    for (const auto &pair : results)
    {
      if (pair.second[i].pdr > bestPdr)
      {
        bestPdr = pair.second[i].pdr;
        best = pair.first;
      }
    }

    NS_LOG_UNCOND("║   " << std::setw(10) << std::left << speedStr << ": "
                         << std::setw(18) << ProtocolToString(best)
                         << " (" << std::fixed << std::setprecision(1) << bestPdr << "%)"
                         << "                                                       ║");
  }

  NS_LOG_UNCOND("╚══════════════════════════════════════════════════════════════════════════════════════════════════════╝");
}

// ==================== Main Function ====================
int main(int argc, char *argv[])
{
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
  NS_LOG_UNCOND("  Total Tests: 5 protocols x 3 speeds = 15 tests");
  NS_LOG_UNCOND("");

  std::vector<std::pair<double, double>> speeds = {
      {0, 0},    // Static
      {1, 2},    // Low speed
      {3, 5}     // Medium speed
  };

  std::vector<ProtocolType> protocols = {
      PROTO_AODV,
      PROTO_OLSR,
      PROTO_SMART_AODV,
      PROTO_SMART_AODV_QLEARNING,
      PROTO_SMART_AODV_CLUSTER
  };

  std::map<ProtocolType, std::vector<TestStats>> results;
  for (ProtocolType proto : protocols)
  {
    results[proto] = std::vector<TestStats>(speeds.size());
  }

  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double minSpeed = speeds[i].first;
    double maxSpeed = speeds[i].second;

    NS_LOG_UNCOND("══════════════════════════════════════════════════════════════════════════════════════════════════════");
    std::string speedStr = (minSpeed == 0 && maxSpeed == 0) ? "Static" : "Speed: " + std::to_string(static_cast<int>(minSpeed)) + "-" + std::to_string(static_cast<int>(maxSpeed)) + " m/s";
    NS_LOG_UNCOND("Testing Scenario: " << speedStr);
    NS_LOG_UNCOND("══════════════════════════════════════════════════════════════════════════════════════════════════════");

    for (ProtocolType proto : protocols)
    {
      results[proto][i] = RunSingleTest(proto, minSpeed, maxSpeed, numNodes, simTime, numFlows);
    }

    NS_LOG_UNCOND("");
  }

  PrintComparisonTable(speeds, results);
  PrintAnalysis(speeds, results);

  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("=== Protocol Descriptions ===");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  AODV (Ad hoc On-Demand Distance Vector):");
  NS_LOG_UNCOND("    - Reactive: Routes discovered only when needed");
  NS_LOG_UNCOND("    - Lower overhead in static scenarios");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  OLSR (Optimized Link State Routing):");
  NS_LOG_UNCOND("    - Proactive: Routes maintained continuously");
  NS_LOG_UNCOND("    - Lower delay but higher overhead");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  Smart-AODV:");
  NS_LOG_UNCOND("    - Link quality monitoring (RSSI/SNR)");
  NS_LOG_UNCOND("    - Preemptive route discovery");
  NS_LOG_UNCOND("    - Energy-aware routing decisions");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  Smart-AODV-QLearning:");
  NS_LOG_UNCOND("    - Smart-AODV + Q-Learning for intelligent route selection");
  NS_LOG_UNCOND("    - Learns from network conditions");
  NS_LOG_UNCOND("");
  NS_LOG_UNCOND("  Smart-AODV-Cluster:");
  NS_LOG_UNCOND("    - Smart-AODV + Hierarchical clustering");
  NS_LOG_UNCOND("    - Reduces routing overhead through cluster heads");
  NS_LOG_UNCOND("");

  return 0;
}
