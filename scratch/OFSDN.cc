#include <iostream>
#include <iomanip>
#include <vector>

#include "ns3/netanim-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ofswitch13-module.h"

/*
同一域中，均使用csma有线连接，SDN + csma有线通信的方式，实现SDN网络通信
*/

using namespace ns3;

static void PrintFlowTag(const Ipv4FlowClassifier::FiveTuple &t, uint16_t appPort)
{
  if (t.protocol == 6 && (t.sourcePort == 6653 || t.destinationPort == 6653))
  {
    std::cout << "[CTRL OpenFlow TCP] ";
  }
  else if (t.protocol == 17 && t.destinationPort == appPort)
  {
    std::cout << "[DATA UDP Tx->Rx]  ";
  }
  else
  {
    std::cout << "[OTHER]            ";
  }
}

int main(int argc, char *argv[])
{
  uint32_t nNodes = 4;     // 节点个数
  double stopTime = 30.0;  // 仿真结束时间
  bool writeXml = false;   // 是否输出全程 FlowMonitor XML
  uint16_t appPort = 9999; // UDP 业务端口

  std::string dataRate = "1Gbps";
  std::string delay = "2us";

  CommandLine cmd;
  cmd.AddValue("n", "Number of router-capable nodes in the domain", nNodes);
  cmd.AddValue("stop", "Simulation stop time (s)", stopTime);
  cmd.AddValue("writeXml", "Write FlowMonitor XML", writeXml);
  cmd.AddValue("rate", "CSMA DataRate", dataRate);
  cmd.AddValue("delay", "CSMA Delay", delay);
  cmd.Parse(argc, argv);

  NodeContainer nodes;
  nodes.Create(nNodes);
  Ptr<Node> sw = CreateObject<Node>();
  Ptr<Node> ctrl = CreateObject<Node>();

  InternetStackHelper internet;
  internet.Install(nodes);
  for (uint32_t i = 0; i < nNodes; ++i)
  {
    Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
    ipv4->SetAttribute("IpForward", BooleanValue(true)); 
  }

  CsmaHelper csma;
  csma.SetChannelAttribute("DataRate", StringValue(dataRate));
  csma.SetChannelAttribute("Delay", StringValue(delay));

  NetDeviceContainer switchPorts;        
  std::vector<NetDeviceContainer> links; 
  links.reserve(nNodes);

  for (uint32_t i = 0; i < nNodes; ++i)
  {
    NetDeviceContainer link = csma.Install(NodeContainer(nodes.Get(i), sw));
    links.push_back(link);
    switchPorts.Add(link.Get(1)); 
  }

  // 可选：导出 CSMA 抓包
  // csma.EnablePcapAll("pcap/csmaPCAP", false);

  Ipv4AddressHelper ip;
  ip.SetBase("10.200.0.0", "255.255.255.0");

  Ipv4InterfaceContainer ifaces; 
  for (uint32_t i = 0; i < nNodes; ++i)
  {
    Ipv4InterfaceContainer ifi = ip.Assign(NetDeviceContainer(links[i].Get(0)));
    ifaces.Add(ifi);
  }

  Ptr<OFSwitch13InternalHelper> of13 = CreateObject<OFSwitch13InternalHelper>();
  of13->InstallController(ctrl);
  of13->InstallSwitch(sw, switchPorts);
  of13->CreateOpenFlowChannels();
  // of13->EnableOpenFlowPcap("pcap/ofPCAP");

  if (nNodes >= 2)
  {
    Ipv4Address dst = ifaces.GetAddress(1); 
    UdpServerHelper sink(appPort);
    ApplicationContainer sinkApps = sink.Install(nodes.Get(1));
    sinkApps.Start(Seconds(0.2));
    sinkApps.Stop(Seconds(stopTime));

    UdpClientHelper client(dst, appPort);
    client.SetAttribute("MaxPackets", UintegerValue(1000000));
    client.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
    client.SetAttribute("PacketSize", UintegerValue(200));
    ApplicationContainer clientApps = client.Install(nodes.Get(0));
    clientApps.Start(Seconds(0.5));
    clientApps.Stop(Seconds(stopTime - 0.1));

    std::cout << "[INFO] UDP: node0(" << ifaces.GetAddress(0)
              << ") -> node1(" << dst << ":" << appPort << ")\n";
  }
  else
  {
    std::cout << "[WARN] nNodes < 2, 跳过\n";
  }

  AnimationInterface anim("SDN.xml");

  anim.EnablePacketMetadata(true);

  anim.SetMobilityPollInterval(Seconds(0.5));

  anim.SetMaxPktsPerTraceFile(200000);

  uint32_t gridCols = std::ceil(std::sqrt(double(nNodes)));
  double xMax = (gridCols - 1) * 50;

  for (uint32_t i = 0; i < nNodes; ++i)
  {
    uint32_t row = i / gridCols;
    uint32_t col = i % gridCols;

    std::ostringstream desc;
    desc << "UAV" << i << " [" << row << "," << col << "]";
    anim.UpdateNodeDescription(nodes.Get(i), desc.str());

    anim.UpdateNodeColor(nodes.Get(i), 0, 150, 255);

    anim.UpdateNodeSize((nodes.Get(i))->GetId(), 10.0, 10.0);
  }

  double midx = xMax * 0.5;

  anim.UpdateNodeDescription(sw, "SW");
  anim.UpdateNodeColor(sw, 255, 165, 0); 
  anim.UpdateNodeSize(sw->GetId(), 10.0, 10.0);
  anim.SetConstantPosition(sw, midx, 50);

  anim.UpdateNodeDescription(ctrl, "CTRL");
  anim.UpdateNodeColor(ctrl, 220, 20, 60); 
  anim.UpdateNodeSize(ctrl->GetId(), 10.0, 10.0);
  anim.SetConstantPosition(ctrl, midx, 50);

  FlowMonitorHelper fmh;
  Ptr<FlowMonitor> mon = fmh.InstallAll();

  Simulator::Stop(Seconds(stopTime));
  Simulator::Run();

  // --- 统计输出 ---
  mon->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> cls = DynamicCast<Ipv4FlowClassifier>(fmh.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = mon->GetFlowStats();

  std::cout << "==== FlowMonitor Results ====\n";
  for (const auto &kv : stats)
  {
    FlowId id = kv.first;
    const auto &st = kv.second;
    Ipv4FlowClassifier::FiveTuple t = cls->FindFlow(id);

    double rxTime = st.timeFirstRxPacket.IsZero() ? 0.0
                                                  : (st.timeLastRxPacket - st.timeFirstRxPacket).GetSeconds();
    double thrMbps = (rxTime > 0.0) ? (st.rxBytes * 8.0 / rxTime / 1e6) : 0.0;
    double avgDelayMs = (st.rxPackets > 0) ? (st.delaySum.GetSeconds() / st.rxPackets * 1000.0) : 0.0;
    double avgJitMs = (st.rxPackets > 1) ? (st.jitterSum.GetSeconds() / (st.rxPackets - 1) * 1000.0) : 0.0;
    double lossRatio = (st.txPackets > 0) ? double(st.txPackets - st.rxPackets) / st.txPackets : 0.0;

    PrintFlowTag(t, appPort);
    std::cout << "Flow " << id << " (" << t.sourceAddress << ":" << t.sourcePort
              << " -> " << t.destinationAddress << ":" << t.destinationPort << ")\n"
              << "  TxPkts=" << st.txPackets << ", RxPkts=" << st.rxPackets
              << ", Lost=" << (st.txPackets - st.rxPackets)
              << ", LossRatio=" << std::fixed << std::setprecision(4) << lossRatio << "\n"
              << "  RxBytes=" << st.rxBytes
              << ", Duration=" << std::setprecision(3) << rxTime << " s"
              << ", Throughput=" << std::setprecision(3) << thrMbps << " Mbps\n"
              << "  AvgDelay=" << std::setprecision(3) << avgDelayMs << " ms"
              << ", AvgJitter=" << std::setprecision(3) << avgJitMs << " ms\n";
  }

  if (writeXml)
  {
    mon->SerializeToXmlFile("flowmon.xml", true, true);
  }

  Simulator::Destroy();
  return 0;
}
