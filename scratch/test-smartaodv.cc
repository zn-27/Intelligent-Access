/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * SmartAODV Intelligent Sensing Test
 *
 * This script tests SmartAODV routing protocol with link quality monitoring
 */
// go go go
#include <iostream>
#include <cmath>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-module.h"

// Include SmartAODV and AODV helpers
#include "ns3/smart-aodv-helper.h"
#include "ns3/smart-aodv-v2-helper.h"
#include "ns3/aodv-helper.h"
#include "ns3/harp-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SmartAodvTest");

class SmartAodvTest
{
public:
  SmartAodvTest();
  /// Configure script parameters
  bool Configure(int argc, char **argv);
  /// Run simulation
  void Run();

private:
  // parameters
  uint32_t size;
  double step;
  double totalTime;

  // trace helpers
  void CheckThroughput();
  uint32_t m_bytesTotal;
};

SmartAodvTest::SmartAodvTest()
    : size(50),
      step(30),
      totalTime(40),
      m_bytesTotal(0)
{
}

bool SmartAodvTest::Configure(int argc, char **argv)
{
  CommandLine cmd;
  cmd.AddValue("size", "Number of nodes", size);
  cmd.AddValue("step", "Grid step distance (m)", step);
  cmd.AddValue("time", "Simulation time (s)", totalTime);

  cmd.Parse(argc, argv);
  return true;
}

void SmartAodvTest::Run()
{
  // Set fixed random seed for reproducibility
  RngSeedManager::SetSeed(2); // 1,2,3,4,5,6,7,8,9,10
  // 42
  RngSeedManager::SetRun(1);

  NS_LOG_UNCOND("SmartAODV Intelligent Sensing Test");
  NS_LOG_UNCOND("Number of nodes: " << size);
  NS_LOG_UNCOND("Grid step: " << step);
  NS_LOG_UNCOND("Simulation time: " << totalTime);

  // Create nodes
  NodeContainer nodes;
  nodes.Create(size);

  // Create grid positions
  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.0),
                                "MinY", DoubleValue(0.0),
                                "DeltaX", DoubleValue(step),
                                "DeltaY", DoubleValue(step),
                                "GridWidth", UintegerValue(5),
                                "LayoutType", StringValue("RowFirst"));
  mobility.Install(nodes);

  // Create WiFi devices
  WifiHelper wifi;
  wifi.SetRemoteStationManager("ns3::ArfWifiManager");

  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");

  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  wifiPhy.SetChannel(wifiChannel.Create());

  wifiPhy.Set("TxPowerStart", DoubleValue(20.0));
  wifiPhy.Set("TxPowerEnd", DoubleValue(20.0)); // dBm
  wifiPhy.Set("RxGain", DoubleValue(0));
  wifiPhy.Set("RxNoiseFigure", DoubleValue(7)); // dBm

  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

  // Internet stack with SmartAODV
  InternetStackHelper internet;

  // Try SmartAODV first, fall back to regular AODV
  SmartAodvHelper smartAodv;
  SmartAodvV2Helper smartAodv2;
  AodvHelper aodv;
  HarpHelper harp;
  Ipv4ListRoutingHelper list;
  // list.Add(harp, 20); // Higher priority for SmartAODV
  list.Add(aodv, 10); // Using Q-learning Smart-AODV-V2

  internet.SetRoutingHelper(list);
  internet.Install(nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.255.0");

  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // // Setup UDP echo client/server applications
  // uint16_t port = 9;
  // UdpEchoServerHelper echoServer(port);
  // ApplicationContainer serverApps = echoServer.Install(nodes.Get(size - 1));
  // serverApps.Start(Seconds(2.0));
  // serverApps.Stop(Seconds(totalTime - 1.0));

  // UdpEchoClientHelper echoClient(interfaces.GetAddress(size - 1), port);
  // echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
  // echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  // echoClient.SetAttribute("PacketSize", UintegerValue(1024));

  // ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));
  // clientApps.Start(Seconds(3.0));
  // clientApps.Stop(Seconds(totalTime - 2.0));

  // ... 在原有的 echoClient 设置之后添加 ...

  uint16_t port2 = 10;
  UdpEchoServerHelper echoServer2(port2);
  // Node 50 的索引是 49
  ApplicationContainer serverApps2 = echoServer2.Install(nodes.Get(49));
  serverApps2.Start(Seconds(1.0));
  serverApps2.Stop(Seconds(totalTime - 1.0));

  // 从 Node 1 发送到 Node 49
  UdpEchoClientHelper echoClient2(interfaces.GetAddress(49), port2);
  echoClient2.SetAttribute("MaxPackets", UintegerValue(1000));
  echoClient2.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  echoClient2.SetAttribute("PacketSize", UintegerValue(1024));

  ApplicationContainer clientApps2 = echoClient2.Install(nodes.Get(1));
  clientApps2.Start(Seconds(20));
  clientApps2.Stop(Seconds(totalTime - 2.0));

  // Flow monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Enable PCAP tracing
  wifiPhy.EnablePcapAll("smart-aodv-pcap");
  Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>(&std::cout);
  smartAodv.PrintRoutingTableAllAt(Seconds(totalTime / 2), routingStream);
  // Run simulation
  NS_LOG_UNCOND("Starting simulation...");
  Simulator::Stop(Seconds(totalTime));
  Simulator::Run();

  // Print flow monitor statistics
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  NS_LOG_UNCOND("\n=== Flow Monitor Statistics ===");
  float avg_l = 0;
  float avg_delay = 0;
  // for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
  //      i != stats.end(); ++i)
  // {
  //   Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
  //   float lost_rate = ((i->second.txPackets - i->second.rxPackets) / i->second.txPackets) * 100;
  //   avg_l += lost_rate;
  //   float delay = (i->second.delaySum.GetSeconds() / i->second.rxPackets * 1000.0);
  //   if (lost_rate != 100)
  //     avg_delay += delay / stats.size();
  //   NS_LOG_UNCOND("Flow " << i->first
  //                         << "  SrcAddr: " << t.sourceAddress
  //                         << "  DstAddr: " << t.destinationAddress
  //                         << "  TxPackets: " << i->second.txPackets
  //                         << "  RxPackets: " << i->second.rxPackets
  //                         << "  Lost: " << (i->second.txPackets - i->second.rxPackets)
  //                         << "  Lost Rate:" << lost_rate
  //                         << "  AvgDelay: " << delay << " ms");
  // }
  NS_LOG_UNCOND("\n=== Target Link Statistics (Node 1 -> Node 49) ===");

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
       i != stats.end(); ++i)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

    // 过滤条件：源地址是 Node 1 的 IP，目的地址是 Node 49 的 IP
    if (t.sourceAddress == interfaces.GetAddress(1) && t.destinationAddress == interfaces.GetAddress(49))
    {
      float lost_rate = ((float)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets) * 100;
      float delay = (i->second.rxPackets > 0) ? (i->second.delaySum.GetSeconds() / i->second.rxPackets * 1000.0) : 0;

      NS_LOG_UNCOND("Flow ID:    " << i->first);
      NS_LOG_UNCOND("Source:     " << t.sourceAddress << " (Node 1)");
      NS_LOG_UNCOND("Dest:       " << t.destinationAddress << " (Node 49)");
      NS_LOG_UNCOND("Tx Packets: " << i->second.txPackets);
      NS_LOG_UNCOND("Rx Packets: " << i->second.rxPackets);
      NS_LOG_UNCOND("Lost Rate:  " << lost_rate << "%");
      NS_LOG_UNCOND("Avg Delay:  " << delay << " ms");

      // 注意：ns-3 FlowMonitor 本身不直接记录每一跳的中间节点 ID
      // 路径信息建议通过开启 PCAP 后使用 Wireshark 查看 TTL 变化，
      // 或观察 NetAnim 的 XML 输出。
    }
  }

  avg_l /= stats.size();

  Simulator::Destroy();

  NS_LOG_UNCOND("Simulation completed!");

  std::cout << "\n=========================================" << std::endl;
  std::cout << "SmartAODV Intelligent Sensing Test Summary" << std::endl;
  std::cout << "=========================================" << std::endl;
  std::cout << "Nodes: " << size << std::endl;
  std::cout << "Grid step: " << step << " m" << std::endl;
  std::cout << "Sim time: " << totalTime << " s" << std::endl;
  std::cout << "Total flows: " << stats.size() << std::endl;
  std::cout << "Total lost rate : " << avg_l << std::endl;
  std::cout << "Total rtt : " << avg_delay << std::endl;
  std::cout << "=========================================" << std::endl;
}

int main(int argc, char **argv)
{
  SmartAodvTest test;
  if (!test.Configure(argc, argv))
  {
    std::cerr << "Configuration failed!" << std::endl;
    return 1;
  }
  test.Run();
  return 0;
}
