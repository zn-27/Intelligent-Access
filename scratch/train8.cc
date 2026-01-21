/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <cmath>
#include <map>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/smart-aodv-helper.h"
#include "ns3/aodv-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SmartAodvTest");

// 全局回调函数：用于捕获数据包在各个节点的转发情况，从而获取路径
void TracePacketForwarding(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    // 从 context 字符串中解析出当前节点的 ID
    // context 格式通常为: /NodeList/[nodeid]/$ns3::Ipv4L3Protocol/Udp...
    std::string sub = context.substr(10);
    uint32_t pos = sub.find("/");
    std::string nodeId = sub.substr(0, pos);

    Ipv4Header ipHeader;
    packet->PeekHeader(ipHeader);

    // 我们只关注从 10.1.0.2 (Node 1) 发往 10.1.0.50 (Node 49) 的业务包
    // 注意：IP地址分配取决于 Assign 顺序，通常 Node 1 为 10.1.0.2
    if (ipHeader.GetDestination() == "10.1.0.50")
    {
        std::cout << "Path Trace: Packet [ID " << packet->GetUid() 
                  << "] is currently at Node " << nodeId 
                  << " (TTL: " << (uint32_t)ipHeader.GetTtl() << ")" << std::endl;
    }
}

class SmartAodvTest
{
public:
  SmartAodvTest();
  bool Configure(int argc, char **argv);
  void Run();

private:
  uint32_t size;
  double step;
  double totalTime;
};

SmartAodvTest::SmartAodvTest()
    : size(50),
      step(30),
      totalTime(40)
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
  NS_LOG_UNCOND("SmartAODV Intelligent Sensing Test");
  
  NodeContainer nodes;
  nodes.Create(size);

  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.0),
                                "MinY", DoubleValue(0.0),
                                "DeltaX", DoubleValue(step),
                                "DeltaY", DoubleValue(step),
                                "GridWidth", UintegerValue(10),
                                "LayoutType", StringValue("RowFirst"));
  mobility.Install(nodes);

  WifiHelper wifi;
  wifi.SetRemoteStationManager("ns3::ArfWifiManager");
  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");
  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  wifiPhy.SetChannel(wifiChannel.Create());
  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

  // 路由配置
  InternetStackHelper internet;
  SmartAodvHelper smartAodv; // 确保 SmartAODV 模块已正确安装在 ns-3 中
  AodvHelper aodv;

  Ipv4ListRoutingHelper list;
  list.Add(smartAodv, 20); // 优先使用 SmartAODV
  list.Add(aodv, 10); 

  internet.SetRoutingHelper(list);
  internet.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // --- 关键修改：添加 Node 1 到 Node 49 的 UDP 测试链路 ---
  uint16_t port = 9999;
  
  // 接收端：Node 49 (第50个节点)
  UdpEchoServerHelper echoServer(port);
  ApplicationContainer serverApps = echoServer.Install(nodes.Get(49));
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(totalTime));

  // 发送端：Node 1
  UdpEchoClientHelper echoClient(interfaces.GetAddress(49), port);
  echoClient.SetAttribute("MaxPackets", UintegerValue(100));
  echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  echoClient.SetAttribute("PacketSize", UintegerValue(1024));

  ApplicationContainer clientApps = echoClient.Install(nodes.Get(1));
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(totalTime));

  // 挂载路径追踪回调
  Config::Connect("/NodeList/*/Ipv4L3Protocol/UnicastForward", MakeCallback(&TracePacketForwarding));

  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  NS_LOG_UNCOND("Starting simulation: Node 1 -> Node 49...");
  Simulator::Stop(Seconds(totalTime));
  Simulator::Run();

  // 统计结果
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "\n=========================================" << std::endl;
  std::cout << "Target Link Analysis: Node 1 -> Node 49" << std::endl;
  std::cout << "=========================================" << std::endl;

  bool found = false;
  for (auto const& x : stats)
  {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(x.first);
      // 过滤：源地址是 Node 1 (10.1.0.2)，目的地址是 Node 49 (10.1.0.50)
      if (t.sourceAddress == "10.1.0.2" && t.destinationAddress == "10.1.0.50")
      {
          found = true;
          double loss = ((double)x.second.txPackets - x.second.rxPackets) / x.second.txPackets * 100;
          std::cout << "Flow ID: " << x.first << std::endl;
          std::cout << "Packets Sent: " << x.second.txPackets << std::endl;
          std::cout << "Packets Recv: " << x.second.rxPackets << std::endl;
          std::cout << "Packet Loss : " << loss << "%" << std::endl;
          if (x.second.rxPackets > 0)
              std::cout << "Avg Delay   : " << (x.second.delaySum.GetSeconds() / x.second.rxPackets * 1000.0) << " ms" << std::endl;
      }
  }
  
  if(!found) std::cout << "No flow data found for Node 1 -> Node 49. Check connectivity/routing." << std::endl;

  Simulator::Destroy();
  return;
}

int main(int argc, char **argv)
{
  SmartAodvTest test;
  if (test.Configure(argc, argv))
  {
    test.Run();
  }
  return 0;
}