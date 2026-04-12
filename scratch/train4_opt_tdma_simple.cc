// TDMA 简化版本 - train4_opt_tdma_simple.cc
// 替换 WiFi CSMA/CA 为 TDMA 的简化实现
// -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*-
/*
 * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块 + TDMA
 */
#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/internet-apps-module.h>
#include "ns3/point-to-point-module.h"
#include "ns3/simple-wireless-tdma-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/bridge-helper.h"
#include "ns3/virtual-net-device.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include <cstdint>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <cmath>

using namespace ns3;

// 全局变量：存储设备指针对应的 switch 名称
std::map<Ptr<NetDevice>, std::string> g_switchNameMap;

// 全局追踪函数
void TraceIcmpPacket(std::string location, Ptr<const Packet> packet)
{
    std::cout << "[" << Simulator::Now().GetSeconds() << "s] " << location
              << " packet size: " << packet->GetSize() << std::endl;
}

// TDMA设备 -> VirtualNetDevice 转发回调
bool TdmaToVirtualDevForward(Ptr<VirtualNetDevice> vdev, Ptr<NetDevice> dev,
                              Ptr<const Packet> packet, uint16_t protocol,
                              const Address &source, const Address &dest,
                              NetDevice::PacketType packetType)
{
    std::string switchName = g_switchNameMap[dev];

    // 为IP包添加Ethernet头
    Ptr<Packet> packetCopy = packet->Copy();
    Mac48Address srcMac = Mac48Address::ConvertFrom(dev->GetAddress());
    Mac48Address dstMac = Mac48Address::ConvertFrom(vdev->GetAddress());
    EthernetHeader ethHeader(false);
    ethHeader.SetSource(srcMac);
    ethHeader.SetDestination(dstMac);
    ethHeader.SetLengthType(protocol);
    EthernetTrailer ethTrailer;
    packetCopy->AddHeader(ethHeader);
    packetCopy->AddTrailer(ethTrailer);

    return vdev->Receive(packetCopy, protocol, srcMac, dstMac, packetType);
}

// 流量监控
std::map<uint32_t, double> lastRxBytes;

void MonitorFlow(Ptr<FlowMonitor> monitor, FlowMonitorHelper *flowHelper, double interval, std::ofstream *fout)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double now = Simulator::Now().GetSeconds();
    *fout << now;

    std::vector<uint16_t> ports = {9, 10, 11, 12};

    for (auto port : ports)
    {
        FlowId fid = 0;
        for (auto const &flow : stats)
        {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
            if (t.destinationPort == port)
            {
                fid = flow.first;
                break;
            }
        }

        double throughput = 0.0;
        double lossRate = 0.0;
        double avgRtt = 0.0;
        double jitter = 0.0;

        if (fid != 0 && stats.count(fid))
        {
            FlowMonitor::FlowStats flowStats = stats[fid];
            double rxBytesDelta = flowStats.rxBytes;
            if (lastRxBytes.count(fid))
            {
                rxBytesDelta -= lastRxBytes[fid];
            }
            throughput = rxBytesDelta * 8.0 / (interval * 1024.0);
            lastRxBytes[fid] = flowStats.rxBytes;

            if (flowStats.txPackets > 0)
            {
                lossRate = 100.0 * (flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets;
            }
            if (flowStats.rxPackets > 0)
            {
                avgRtt = flowStats.delaySum.GetSeconds() / flowStats.rxPackets * 1000.0;
            }
        }

        *fout << "," << throughput << "," << lossRate << "," << avgRtt << "," << jitter;
    }

    *fout << std::endl;
    Simulator::Schedule(Seconds(interval), &MonitorFlow, monitor, flowHelper, interval, fout);
}

int main(int argc, char *argv[])
{
    uint16_t simTime = 30;
    bool verbose = true;
    bool trace = false;

    CommandLine cmd;
    cmd.AddValue("simTime", "simulate time", simTime);
    cmd.AddValue("verbose", "enable verbose logs", verbose);
    cmd.AddValue("trace", "enable trace /pcap", trace);
    cmd.Parse(argc, argv);

    // 启用校验和计算
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    // ==========================
    // 1. 创建节点
    // ==========================

    // 域内节点
    NodeContainer StaA; StaA.Create(4);
    NodeContainer ApA;  ApA.Create(2);
    NodeContainer StaB; StaB.Create(2);
    NodeContainer ApB;  ApB.Create(1);
    NodeContainer StaC; StaC.Create(3);
    NodeContainer ApC;  ApC.Create(1);

    NodeContainer wifiStaNodes;
    wifiStaNodes.Add(StaA);
    wifiStaNodes.Add(StaB);
    wifiStaNodes.Add(StaC);

    // 交换机和控制器节点
    Ptr<Node> sw1 = CreateObject<Node>();
    Ptr<Node> sw2 = CreateObject<Node>();
    Ptr<Node> sw3 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    // ==========================
    // 2. 有线链路配置
    // ==========================
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(500)));

    NetDeviceContainer apCsmaDevsA, apCsmaDevsB, apCsmaDevsC;
    NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports;

    // 连接 AP 到交换机
    for (uint32_t i = 0; i < ApA.GetN(); ++i)
    {
        NodeContainer pair(ApA.Get(i), sw1);
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsA.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }

    {
        NodeContainer pair(ApB.Get(0), sw2);
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsB.Add(link.Get(0));
        sw2Devsports.Add(link.Get(1));
    }

    {
        NodeContainer pair(ApC.Get(0), sw3);
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsC.Add(link.Get(0));
        sw3Devsports.Add(link.Get(1));
    }

    // ==========================
    // 3. TDMA 配置 (替换 WiFi)
    // ==========================
    std::cout << "配置 TDMA 信道..." << std::endl;

    // 配置 TDMA 信道最大传输范围
    Config::SetDefault("ns3::SimpleWirelessChannel::MaxRange", DoubleValue(400.0));

    // 创建统一 TDMA 控制器
    // 节点总数: sw1, sw2, sw3 (3) + Domain A (4 STA + 2 AP = 6) + Domain B (2 STA + 1 AP = 3) + Domain C (3 STA + 1 AP = 4) = 16
    TdmaHelper tdma = TdmaHelper(16, 16);

    TdmaControllerHelper controller;
    controller.Set("SlotTime", TimeValue(MicroSeconds(1100)));
    controller.Set("GuardTime", TimeValue(MicroSeconds(100)));
    controller.Set("InterFrameTime", TimeValue(MicroSeconds(0)));
    controller.Set("DataRate", DataRateValue(DataRate("54Mbps")));
    tdma.SetTdmaControllerHelper(controller);

    // 收集所有无线节点
    NodeContainer allWirelessNodes;
    allWirelessNodes.Add(sw1);
    allWirelessNodes.Add(sw2);
    allWirelessNodes.Add(sw3);
    allWirelessNodes.Add(StaA);
    allWirelessNodes.Add(ApA);
    allWirelessNodes.Add(StaB);
    allWirelessNodes.Add(ApB);
    allWirelessNodes.Add(StaC);
    allWirelessNodes.Add(ApC);

    std::cout << "安装 TDMA 设备到 " << allWirelessNodes.GetN() << " 个节点..." << std::endl;
    NetDeviceContainer tdmaDevices = tdma.Install(allWirelessNodes);

    // 为交换机创建 VirtualNetDevice 包装
    NetDeviceContainer switchVirtualDevs;
    for (uint32_t i = 0; i < 3; ++i)
    {
        Ptr<NetDevice> tdmaDev = tdmaDevices.Get(i);
        Ptr<Node> switchNode = allWirelessNodes.Get(i);
        std::string switchName = (i == 0) ? "sw1" : ((i == 1) ? "sw2" : "sw3");

        g_switchNameMap[tdmaDev] = switchName;

        Ptr<VirtualNetDevice> virtualDev = CreateObject<VirtualNetDevice>();
        virtualDev->SetAddress(tdmaDev->GetAddress());
        virtualDev->SetSupportsSendFrom(true);

        // 使用 TdmaNetDevice::Send 方法
        // VirtualNetDevice::SendCallback 签名: Callback<bool, Ptr<Packet>, const Address&, const Address&, uint16_t>
        // TdmaNetDevice::Send 签名: bool Send(Ptr<Packet>, const Address&, uint16_t)
        // 由于参数不完全匹配，使用 IsBridge() 方式可能更合适
        // 或者直接不使用 VirtualNetDevice 包装，而是让 TDMA 设备直接参与 OpenFlow
        // 简化方案：不使用 VirtualNetDevice 包装
        // virtualDev->SetSendCallback(MakeCallback(&TdmaNetDevice::Send, tdmaNetDev));

        switchNode->AddDevice(virtualDev);
        switchVirtualDevs.Add(virtualDev);

        // 设置接收回调
        tdmaDev->SetPromiscReceiveCallback(MakeBoundCallback(&TdmaToVirtualDevForward, virtualDev));
    }

    sw1Devsports.Add(switchVirtualDevs.Get(0));
    sw2Devsports.Add(switchVirtualDevs.Get(1));
    sw3Devsports.Add(switchVirtualDevs.Get(2));

    std::cout << "TDMA 配置完成" << std::endl;

    // ==========================
    // 4. 节点位置配置
    // ==========================
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    Ptr<ListPositionAllocator> posController = CreateObject<ListPositionAllocator>();
    posController->Add(Vector(0, 0, 0));
    mobility.SetPositionAllocator(posController);
    mobility.Install(controllerNode);

    Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
    swPos->Add(Vector(-50, 150, 0)); // sw1
    swPos->Add(Vector(50, 150, 1));  // sw2
    swPos->Add(Vector(0, 150, 1));   // sw3
    mobility.SetPositionAllocator(swPos);
    mobility.Install(sw1);
    mobility.Install(sw2);
    mobility.Install(sw3);

    // 域A节点位置
    Ptr<ListPositionAllocator> posA = CreateObject<ListPositionAllocator>();
    posA->Add(Vector(-150, 200, 0));
    posA->Add(Vector(-120, 200, 0));
    posA->Add(Vector(-155, 230, 0));
    posA->Add(Vector(-150, 230, 0));
    posA->Add(Vector(-165, 210, 0));
    posA->Add(Vector(-123, 220, 0));
    mobility.SetPositionAllocator(posA);
    mobility.Install(ApA);
    mobility.Install(StaA);

    // 域B节点位置
    Ptr<ListPositionAllocator> posB = CreateObject<ListPositionAllocator>();
    posB->Add(Vector(120, 200, 0));
    posB->Add(Vector(150, 230, 0));
    posB->Add(Vector(130, 230, 0));
    mobility.SetPositionAllocator(posB);
    mobility.Install(ApB);
    mobility.Install(StaB);

    // 域C节点位置
    Ptr<ListPositionAllocator> posC = CreateObject<ListPositionAllocator>();
    posC->Add(Vector(20.0, 230.0, 0.0));
    posC->Add(Vector(50.0, 210.0, 0.0));
    posC->Add(Vector(-20.0, 230.0, 0.0));
    posC->Add(Vector(0.0, 200.0, 0.0));
    mobility.SetPositionAllocator(posC);
    mobility.Install(StaC);
    mobility.Install(ApC);

    // ==========================
    // 5. OpenFlow 控制器配置
    // ==========================
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    of13Helper->InstallController(controllerNode);
    of13Helper->InstallSwitch(sw1, sw1Devsports);
    of13Helper->InstallSwitch(sw2, sw2Devsports);
    of13Helper->InstallSwitch(sw3, sw3Devsports);
    of13Helper->CreateOpenFlowChannels();

    auto get = of13Helper->GetController();
    Ptr<OFSwitch13LearningController> controllerApp = DynamicCast<OFSwitch13LearningController>(get.Get(0));

    // ==========================
    // 6. 网络栈配置
    // ==========================
    Ipv4StaticRoutingHelper staticRoutingHelper;
    InternetStackHelper stack;
    InternetStackHelper stack2;
    Ipv4ListRoutingHelper list;

    AodvHelper aodv;
    OlsrHelper olsr;
    list.Add(aodv, 10);
    list.Add(olsr, 10);
    list.Add(staticRoutingHelper, 100);
    stack2.SetRoutingHelper(list);
    stack2.Install(wifiStaNodes);

    stack.Install(ApA);
    stack.Install(ApB);
    stack.Install(ApC);

    // 启用IP转发
    for (uint32_t i = 0; i < ApA.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = ApA.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }
    ApB.Get(0)->GetObject<Ipv4>()->SetAttribute("IpForward", BooleanValue(true));
    ApC.Get(0)->GetObject<Ipv4>()->SetAttribute("IpForward", BooleanValue(true));

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }

    // ==========================
    // 7. IP 地址分配
    // ==========================
    Ipv4AddressHelper ipv4;

    // 域A
    Ipv4InterfaceContainer ifA, ifApA;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    NetDeviceContainer netA;
    for (uint32_t i = 0; i < StaA.GetN(); ++i)
        netA.Add(tdmaDevices.Get(3 + i)); // STA A 使用 TDMA 设备 (索引从3开始)
    for (uint32_t i = 0; i < ApA.GetN(); ++i)
        netA.Add(tdmaDevices.Get(3 + 4 + i)); // AP A 使用 TDMA 设备
    ifA = ipv4.Assign(netA);
    ifApA = ipv4.Assign(apCsmaDevsA);

    // 域B
    Ipv4InterfaceContainer ifB, ifApB;
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    NetDeviceContainer netB;
    for (uint32_t i = 0; i < StaB.GetN(); ++i)
        netB.Add(tdmaDevices.Get(3 + 4 + 2 + i)); // STA B
    netB.Add(tdmaDevices.Get(3 + 4 + 2 + 2)); // AP B
    ifB = ipv4.Assign(netB);
    ifApB = ipv4.Assign(apCsmaDevsB);

    // 域C
    Ipv4InterfaceContainer ifC, ifApC;
    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    NetDeviceContainer netC;
    for (uint32_t i = 0; i < StaC.GetN(); ++i)
        netC.Add(tdmaDevices.Get(3 + 4 + 2 + 3 + i)); // STA C
    netC.Add(tdmaDevices.Get(3 + 4 + 2 + 3 + 3)); // AP C
    ifC = ipv4.Assign(netC);
    ifApC = ipv4.Assign(apCsmaDevsC);

    // 交换机虚拟接口
    Ipv4InterfaceContainer switchWifiIfaces;
    {
        Ipv4AddressHelper switchIpHelper;
        switchIpHelper.SetBase("10.10.1.0", "255.255.255.0");
        switchWifiIfaces = switchIpHelper.Assign(switchVirtualDevs);
    }

    std::cout << "Switch Virtual Interfaces (TDMA-backed):" << std::endl;
    std::cout << "  sw1: " << switchWifiIfaces.GetAddress(0) << std::endl;
    std::cout << "  sw2: " << switchWifiIfaces.GetAddress(1) << std::endl;
    std::cout << "  sw3: " << switchWifiIfaces.GetAddress(2) << std::endl;

    // 配置默认路由
    Ipv4Address apAGateway = ifApA.GetAddress(0);
    for (uint32_t i = 0; i < StaA.GetN(); ++i)
    {
        Ptr<Node> h = StaA.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(apAGateway, 1);
    }

    Ipv4Address apBGateway = ifApB.GetAddress(0);
    for (uint32_t i = 0; i < StaB.GetN(); ++i)
    {
        Ptr<Node> h = StaB.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(apBGateway, 1);
    }

    Ipv4Address apCGateway = ifApC.GetAddress(0);
    for (uint32_t i = 0; i < StaC.GetN(); ++i)
    {
        Ptr<Node> h = StaC.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(apCGateway, 1);
    }

    // 注册ARP条目
    if (controllerApp != nullptr)
    {
        for (uint32_t i = 0; i < ApA.GetN(); ++i)
        {
            Ipv4Address apIp = ifApA.GetAddress(i);
            Mac48Address apMac = Mac48Address::ConvertFrom(apCsmaDevsA.Get(i)->GetAddress());
            controllerApp->AddArpEntry(apIp, apMac);
        }
        controllerApp->AddArpEntry(ifApB.GetAddress(0), Mac48Address::ConvertFrom(apCsmaDevsB.Get(0)->GetAddress()));
        controllerApp->AddArpEntry(ifApC.GetAddress(0), Mac48Address::ConvertFrom(apCsmaDevsC.Get(0)->GetAddress()));

        for (uint32_t i = 0; i < switchVirtualDevs.GetN(); ++i)
        {
            Ipv4Address switchIp = switchWifiIfaces.GetAddress(i);
            Mac48Address switchMac = Mac48Address::ConvertFrom(switchVirtualDevs.Get(i)->GetAddress());
            controllerApp->AddArpEntry(switchIp, switchMac);
        }
    }

    // ==========================
    // 8. 应用层配置
    // ==========================
    uint16_t port0 = 9;

    // Flow0: StaA[1] -> StaC[2]
    OnOffHelper onoff0("ns3::UdpSocketFactory", Address());
    onoff0.SetAttribute("DataRate", StringValue("2Mbps"));
    onoff0.SetAttribute("PacketSize", UintegerValue(1024));
    onoff0.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff0.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff0.SetAttribute("StartTime", TimeValue(Seconds(10)));
    onoff0.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    InetSocketAddress dst0(ifC.GetAddress(2), port0);
    onoff0.SetAttribute("Remote", AddressValue(dst0));
    ApplicationContainer app0 = onoff0.Install(StaA.Get(1));

    // ==========================
    // 9. 流量监控
    // ==========================
    FlowMonitorHelper flowmonHelper;
    NodeContainer monitorNodes;
    monitorNodes.Add(StaA);
    monitorNodes.Add(StaB);
    monitorNodes.Add(StaC);
    Ptr<FlowMonitor> monitor = flowmonHelper.Install(monitorNodes);

    std::ofstream fout("flow_stats_tdma.csv");
    fout << "Time";
    for (int i = 1; i <= 4; ++i)
    {
        fout << ",Throughput_" << i << "(Kbps),LossRate_" << i << "(%),AvgRTT_" << i << "(ms),Jitter_" << i << "(ms)";
    }
    fout << std::endl;

    Simulator::Schedule(Seconds(0.1), &MonitorFlow, monitor, &flowmonHelper, 1, &fout);

    // 设置路由优先级
    Simulator::Schedule(Seconds(4.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);

    // 运行仿真
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // 输出统计结果
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n========== TDMA 流量统计结果 ==========" << std::endl;
    for (auto const &flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        std::cout << "Flow (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
        std::cout << "  Tx Bytes: " << flow.second.txBytes << std::endl;
        std::cout << "  Rx Bytes: " << flow.second.rxBytes << std::endl;
        std::cout << "  Tx Packets: " << flow.second.txPackets << std::endl;
        std::cout << "  Rx Packets: " << flow.second.rxPackets << std::endl;
        if (flow.second.rxPackets > 0)
        {
            std::cout << "  平均延迟: " << flow.second.delaySum.GetSeconds() / flow.second.rxPackets * 1000 << " ms" << std::endl;
        }
        if (flow.second.txPackets > 0)
        {
            std::cout << "  丢包率: " << (double)(flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets * 100 << " %" << std::endl;
        }
        std::cout << std::endl;
    }

    monitor->SerializeToXmlFile("flowmon-results-tdma.xml", true, true);
    fout.close();

    Simulator::Destroy();

    std::cout << "TDMA 仿真完成！" << std::endl;
    return 0;
}
