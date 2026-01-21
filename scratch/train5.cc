/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块
 *
 * - 域 A: hostsA (2 主机) -- sw1
 * - 域 B: hostsB (2 主机) -- sw2
 * - 域 C: 3个WiFi终端 + 1个AP -- sw3
 * - 路由器节点(routerNode1)连接 sw1、sw2和sw3（配置三个子网IP）
 * - 单 OpenFlow 控制器管理 sw1 和 sw2 和 sw3
 *
 * 构建：确保 ns-3 已构建并启用 ofswitch13 模块。
 */

#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/internet-apps-module.h>
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include <ns3/internet-apps-module.h>
#include "ns3/bridge-helper.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/ipv4-address.h"
#include <cstdint>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <cmath>
using namespace ns3;

// ---------------------------------------------------------
// 函数：禁用 AdHoc 接口
// ---------------------------------------------------------
// 逻辑 down
// ---------------------------------------------------------
// 函数：逻辑上下线设备
// enable = true 表示开启，false 表示关闭
// ---------------------------------------------------------

void DisableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    uint32_t idx = ipv4->GetInterfaceForDevice(dev);
    if (idx != uint32_t(-1))
        ipv4->SetDown(idx);
}
void EnableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    uint32_t idx = ipv4->GetInterfaceForDevice(dev);
    if (idx != uint32_t(-1))
        ipv4->SetUp(idx);
}

std::map<uint32_t, double> lastRxBytes;   // 上一次采样时接收字节数
std::map<uint32_t, double> lastPacketRtt; // 上一次平均 RTT

void MonitorFlow(Ptr<FlowMonitor> monitor, FlowMonitorHelper *flowHelper, double interval, std::ofstream *fout)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double now = Simulator::Now().GetSeconds();
    *fout << now;

    // 按端口号 port0-3 对应顺序输出链路 1-4
    std::vector<uint16_t> ports = {9, 10, 11, 12}; // 对应 port0, port1, port2, port3

    for (auto port : ports)
    {
        // 找到对应端口的 FlowId
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

            // 瞬时吞吐量（Kbps）
            double rxBytesDelta = flowStats.rxBytes;
            if (lastRxBytes.count(fid))
            {
                rxBytesDelta -= lastRxBytes[fid];
            }
            throughput = rxBytesDelta * 8.0 / (interval * 1024.0); // Kbps
            lastRxBytes[fid] = flowStats.rxBytes;

            // 丢包率
            if (flowStats.txPackets > 0)
            {
                lossRate = 100.0 * (flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets;
            }

            // 平均 RTT
            if (flowStats.rxPackets > 0)
            {
                avgRtt = flowStats.delaySum.GetSeconds() / flowStats.rxPackets * 1000.0; // ms

                double packetRtt = avgRtt;
                if (lastPacketRtt.count(fid))
                {
                    jitter = std::abs(packetRtt - lastPacketRtt[fid]);
                }
                lastPacketRtt[fid] = packetRtt;
            }
        }

        *fout << "," << throughput
              << "," << lossRate
              << "," << avgRtt
              << "," << jitter;
    }

    *fout << std::endl;

    Simulator::Schedule(Seconds(interval), &MonitorFlow, monitor, flowHelper, interval, fout);
}

int main(int argc, char *argv[])
{
    std::map<std::string, double> pingRttAvg;

    uint16_t simTime = 20;
    bool verbose = true;
    bool trace = false;

    CommandLine cmd;
    cmd.AddValue("simTime", "simulate time ", simTime);      // 设置仿真时间
    cmd.AddValue("verbose", "enable verbose logs", verbose); // 启用详细日志输出
    cmd.AddValue("trace", "enable trace /pcap", trace);      // 启用pcap文件
    cmd.Parse(argc, argv);

    if (verbose)
    {
        /* code */
    }

    // 启用校验和计算（ofswitch13 模块所需）
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    // 创建域内节点
    NodeContainer StaA;
    StaA.Create(4);
    NodeContainer ApA;
    ApA.Create(2);

    NodeContainer StaB;
    StaB.Create(2);
    NodeContainer ApB;
    ApB.Create(1);

    NodeContainer StaC;
    StaC.Create(3);
    NodeContainer ApC;
    ApC.Create(1);

    NodeContainer wifiStaNodes;
    wifiStaNodes.Add(StaA);
    wifiStaNodes.Add(StaB);
    wifiStaNodes.Add(StaC);

    // 网络设备节点：3台交换机、1台路由器、1台控制器
    Ptr<Node> sw1 = CreateObject<Node>();
    Ptr<Node> sw2 = CreateObject<Node>();
    Ptr<Node> sw3 = CreateObject<Node>();
    Ptr<Node> sw4 = CreateObject<Node>();
    Ptr<Node> routerNode1 = CreateObject<Node>();
    // Ptr<Node> routerNode2 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    // 有线链路配置
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer apCsmaDevsA, apCsmaDevsB, apCsmaDevsC;    // AP设备有线接口
    NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports; // 交换机端口设备
    NetDeviceContainer routerDevsA, routerDevsB, routerDevsC;    // 路由器接口设备

    // 连接域 A 主机到 sw1
    // 遍历每个 AP 都连接交换机
    for (uint32_t i = 0; i < ApA.GetN(); ++i)
    {
        NodeContainer pair(ApA.Get(i), sw1); // sw1 是 A 域的交换机节点
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsA.Add(link.Get(0));  // AP 侧
        sw1Devsports.Add(link.Get(1)); // 交换机侧
    }
    // 连接域 B 主机到 sw2
    {
        NodeContainer pair(ApB.Get(0), sw2);
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsB.Add(link.Get(0));
        sw2Devsports.Add(link.Get(1));
    }

    // 连接域 C 主机到 sw3
    {
        NodeContainer pair(ApC.Get(0), sw3);
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsC.Add(link.Get(0));
        sw3Devsports.Add(link.Get(1));
    }

    // 连接路由器到 sw1（域 A 网络）
    {
        NodeContainer pair(routerNode1, sw1);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsA.Add(link.Get(0));  // 路由器侧设备
        sw1Devsports.Add(link.Get(1)); // sw1新增端口
    }
    // 连接路由器到 sw2（域 B 网络）
    {
        NodeContainer pair(routerNode1, sw2);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsB.Add(link.Get(0));  // 路由器在 B 网络的接口
        sw2Devsports.Add(link.Get(1)); // sw2新增端口
    }
    // 连接路由器到 sw3（域 C 网络）
    {
        NodeContainer pair(routerNode1, sw3);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsC.Add(link.Get(0));  // 路由器在 C 网络的接口
        sw3Devsports.Add(link.Get(1)); // sw3新增端口
    }
    csma.EnablePcapAll("csma-trace", true);

    // wifi配置部分
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");
    WifiMacHelper mac; // 逻辑可复用

    // A域
    Ptr<YansWifiChannel> channelA = channel.Create();
    YansWifiPhyHelper phyA;
    phyA.SetChannel(channelA);

    Ssid ssidA = Ssid("A");

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssidA));
    NetDeviceContainer staWifiDevsA = wifi.Install(phyA, mac, StaA);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssidA));
    NetDeviceContainer apWifiDevsA = wifi.Install(phyA, mac, ApA);

    // 创建ap-ap backbone信道
    Ptr<YansWifiChannel> backboneChannel = channel.Create(); // 独立信道
    YansWifiPhyHelper phyBackbone;
    phyBackbone.SetChannel(backboneChannel);

    WifiMacHelper backboneMac;
    backboneMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer apBackboneDevices = wifi.Install(phyBackbone, backboneMac, ApA);

    // B域
    Ptr<YansWifiChannel> channelB = channel.Create();
    YansWifiPhyHelper phyB;
    phyB.SetChannel(channelB);

    Ssid ssidB = Ssid("B");

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssidB));
    NetDeviceContainer staWifiDevsB = wifi.Install(phyB, mac, StaB);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssidB));
    NetDeviceContainer apWifiDevsB = wifi.Install(phyB, mac, ApB);

    // C域
    Ptr<YansWifiChannel> channelC = channel.Create();
    YansWifiPhyHelper phyC;
    phyC.SetChannel(channelC);

    Ssid ssidC = Ssid("C");

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssidC));
    NetDeviceContainer staWifiDevsC = wifi.Install(phyC, mac, StaC);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssidC));
    NetDeviceContainer apWifiDevsC = wifi.Install(phyC, mac, ApC);

    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer adhocDevsC = wifi.Install(phyC, mac, StaC);

    // 合并所有sta设备
    NetDeviceContainer allStaDevices;
    allStaDevices.Add(staWifiDevsA);
    allStaDevices.Add(staWifiDevsB);
    allStaDevices.Add(staWifiDevsC);

    // 合并所有ap设备
    NetDeviceContainer allApDevices;
    allApDevices.Add(apWifiDevsA);
    allApDevices.Add(apWifiDevsB);
    allApDevices.Add(apWifiDevsC);

    // 节点位置配置
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // 控制器位置
    Ptr<ListPositionAllocator> posController = CreateObject<ListPositionAllocator>();
    posController->Add(Vector(0, 0, 0)); // 单个节点直接 Add
    mobility.SetPositionAllocator(posController);
    mobility.Install(controllerNode);

    // 核心路由器位置
    Ptr<ListPositionAllocator> posRouter = CreateObject<ListPositionAllocator>();
    posRouter->Add(Vector(0, 100, 0));
    mobility.SetPositionAllocator(posRouter);
    mobility.Install(routerNode1);

    // 交换机
    Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
    swPos->Add(Vector(-100, 150, 0)); // sw1
    swPos->Add(Vector(100, 150, 0));  // sw2
    swPos->Add(Vector(0, 150, 0));    // sw3
    mobility.SetPositionAllocator(swPos);
    mobility.Install(sw1);
    mobility.Install(sw2);
    mobility.Install(sw3);

    // 域A节点
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

    // 域B节点
    Ptr<ListPositionAllocator> posB = CreateObject<ListPositionAllocator>();
    posB->Add(Vector(120, 200, 0));
    posB->Add(Vector(150, 230, 0));
    posB->Add(Vector(130, 230, 0));
    mobility.SetPositionAllocator(posB);
    mobility.Install(ApB);
    mobility.Install(StaB);

    // 域C节点
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(20.0, 230.0, 0.0));
    positionAlloc->Add(Vector(50.0, 210.0, 0.0));
    positionAlloc->Add(Vector(-20.0, 230.0, 0.0));
    positionAlloc->Add(Vector(0.0, 200.0, 0.0));

    MobilityHelper adhocMobility;
    adhocMobility.SetPositionAllocator(positionAlloc);
    adhocMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    adhocMobility.Install(StaC);
    adhocMobility.Install(ApC);

    // 将AP的WiFi接口与有线接口桥接（实现有线无线互通）
    BridgeHelper bridge;
    NetDeviceContainer bridgeDevA, bridgeDevB, bridgeDevC;

    for (uint32_t i = 0; i < ApA.GetN(); ++i)
    {

        NetDeviceContainer bridgeDev = bridge.Install(ApA.Get(i),
                                                      NetDeviceContainer(apWifiDevsA.Get(i), apCsmaDevsA.Get(i)));
        bridgeDevA.Add(bridgeDev);
    }

    bridgeDevB = bridge.Install(ApB.Get(0),
                                NetDeviceContainer(apWifiDevsB.Get(0), apCsmaDevsB.Get(0)));

    bridgeDevC = bridge.Install(ApC.Get(0),
                                NetDeviceContainer(apWifiDevsC.Get(0), apCsmaDevsC.Get(0))); // 桥接AP的WiFi和有线设备

    // OpenFlow控制器与交换机配置
    Ptr<OFSwitch13InternalHelper> of13Helper =
        CreateObject<OFSwitch13InternalHelper>();

    // 安装控制器应用到控制器节点
    of13Helper->InstallController(controllerNode);
    // 安装交换机应用到交换机节点，并关联其端口设备
    of13Helper->InstallSwitch(sw1, sw1Devsports);
    of13Helper->InstallSwitch(sw2, sw2Devsports);
    of13Helper->InstallSwitch(sw3, sw3Devsports);
    // 创建控制器与交换机之间的OpenFlow信道
    of13Helper->CreateOpenFlowChannels();

    // 获取控制器应用实例（用于后续配置路由优先级）
    auto get = of13Helper->GetController();
    Ptr<OFSwitch13LearningController>
        controllerApp =
            DynamicCast<OFSwitch13LearningController>(get.Get(0));

    // --------------------------
    // 4. 网络栈配置部分
    // --------------------------
    // Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));
    // Config::SetDefault("ns3::Ipv4::IpForward", BooleanValue(true));

    // 为主机配置静态默认路由指向路由器
    Ipv4StaticRoutingHelper staticRoutingHelper;

    InternetStackHelper stack; // 基础IP协议栈

    InternetStackHelper stack2;
    Ipv4ListRoutingHelper list;
    // Ipv4StaticRoutingHelper staticC;

    AodvHelper aodv;
    OlsrHelper olsr;
    list.Add(aodv, 10);
    list.Add(olsr, 10);
    list.Add(staticRoutingHelper, 100);
    stack2.SetRoutingHelper(list); // 对于 AdHoc 节点使用 AODV
    stack2.Install(wifiStaNodes);
    stack.Install(routerNode1); // 路由器安装基础协议栈

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true)); // 启用转发
    }

    // 分配IPv4地址
    Ipv4AddressHelper ipv4;

    Ipv4InterfaceContainer ifA; // 域A主机和路由器接口
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    // 给 A域 设备和 routerDevsA 分配地址
    {
        NetDeviceContainer netA = NetDeviceContainer();
        // 主机先分配
        for (uint32_t i = 0; i < staWifiDevsA.GetN(); ++i)
            netA.Add(staWifiDevsA.Get(i));
        // 为A域配置路由器接口
        for (uint32_t i = 0; i < routerDevsA.GetN(); ++i)
            netA.Add(routerDevsA.Get(i));
        ifA = ipv4.Assign(netA);
    }

    Ipv4InterfaceContainer ifB;
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    {
        NetDeviceContainer netB = NetDeviceContainer();
        for (uint32_t i = 0; i < staWifiDevsB.GetN(); ++i)
            netB.Add(staWifiDevsB.Get(i));
        for (uint32_t i = 0; i < routerDevsB.GetN(); ++i)
            netB.Add(routerDevsB.Get(i));
        ifB = ipv4.Assign(netB);
    }

    Ipv4InterfaceContainer ifC;
    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    {
        NetDeviceContainer netC = NetDeviceContainer();
        // 先配置主机
        for (uint32_t i = 0; i < staWifiDevsC.GetN(); ++i)
            netC.Add(staWifiDevsC.Get(i));

        // 为C域配置路由器接口
        for (uint32_t i = 0; i < routerDevsC.GetN(); ++i)
            netC.Add(routerDevsC.Get(i));
        for (uint32_t i = 0; i < adhocDevsC.GetN(); ++i)
            netC.Add(adhocDevsC.Get(i)); // AdHoc 接口
        ifC = ipv4.Assign(netC);
    } // wifi 网络

    ///----------------------------------------///
    // 路由器的 IP 地址配置
    Ipv4Address routerA = ifA.GetAddress(staWifiDevsA.GetN()); // 路由器A的地址
    Ipv4Address routerB = ifB.GetAddress(staWifiDevsB.GetN());
    Ipv4Address routerC = ifC.GetAddress(staWifiDevsC.GetN());

    // 为 A 网络的主机设置默认路由到路由器
    for (uint32_t i = 0; i < StaA.GetN(); ++i)
    {
        Ptr<Node> h = StaA.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerA, 1);
    }

    // 为 B 网络的主机设置默认路由到路由器 B
    for (uint32_t i = 0; i < StaB.GetN(); ++i)
    {
        Ptr<Node> h = StaB.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerB, 1);
    }

    // 为 C 网络的主机设置默认路由到路由器 C
    for (uint32_t i = 0; i < StaC.GetN(); ++i)
    {
        Ptr<Node> h = StaC.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerC, 1);
    }

    // 打印输出
    for (uint32_t i = 0; i < StaA.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4h = StaA.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> r = staticRoutingHelper.GetStaticRouting(ipv4h);
        std::cout << "STA A " << i << " default route: " << r->GetDefaultRoute() << std::endl;
    }

    for (uint32_t i = 0; i < StaC.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4h = StaC.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> r = staticRoutingHelper.GetStaticRouting(ipv4h);
        std::cout << "STA " << i << " default route: "
                  << r->GetDefaultRoute() << std::endl;
    }

    std::cout << "sw1 ports: " << sw1Devsports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw1Devsports.GetN(); ++i)
        std::cout << "Port " << i << ": " << sw1Devsports.Get(i)->GetAddress() << std::endl;

    // 启用pcap追踪
    if (true)
    {
        // ---- C 域抓包 ----
        phyC.EnablePcap("C_adhoc", adhocDevsC); // C 域 AdHoc 接口
        phyC.EnablePcap("C_sta", staWifiDevsC); // C 域 STA

        // ---- A/B 域抓包 ----
        phyA.EnablePcap("A_ap", apWifiDevsA); // A 域 AP
        phyB.EnablePcap("B_ap", apWifiDevsB); // B 域 AP

        // ---- OpenFlow / 交换机抓包 ----
        of13Helper->EnableOpenFlowPcap("openflow-interdomain");
        of13Helper->EnableDatapathStats("switch-stats");

        csma.EnablePcap("sw1", sw1Devsports, true);
        csma.EnablePcap("sw2", sw2Devsports, true);

        // A/B 域 AP 的 CSMA 接口
        csma.EnablePcap("A_domain", apCsmaDevsA);
        csma.EnablePcap("B_domain", apCsmaDevsB);
    }

    // 无线终端静态主机路由配置
    for (uint32_t i = 0; i < StaA.GetN(); i++)
    {
        Ipv4StaticRoutingHelper staticRoutingHelper2;
        Ptr<Ipv4> ipv42 = StaA.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);
        // 添加到域A其他节点的主机路由
        s->AddHostRouteTo(ifA.GetAddress(0), 1);
        s->AddHostRouteTo(ifA.GetAddress(1), 1);
        s->AddHostRouteTo(ifA.GetAddress(2), 1);
        s->AddHostRouteTo(ifA.GetAddress(3), 1);
        // s->AddHostRouteTo(ifA.GetAddress(i + 4), 0);
    }

    for (uint32_t i = 0; i < StaB.GetN(); i++)
    {
        Ipv4StaticRoutingHelper staticRoutingHelper2;
        Ptr<Ipv4> ipv42 = StaB.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);
        // 添加到域B其他节点的主机路由
        s->AddHostRouteTo(ifB.GetAddress(0), 1);
        s->AddHostRouteTo(ifB.GetAddress(1), 1);
        // s->AddHostRouteTo(ifB.GetAddress(i + 2), 0);
    }

    for (uint32_t i = 0; i < StaC.GetN(); i++)
    {
        Ipv4StaticRoutingHelper staticRoutingHelper2;
        Ptr<Ipv4> ipv42 = StaC.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);
        // 添加到域C其他节点的主机路由（通过接口1）
        s->AddHostRouteTo(ifC.GetAddress(0), 1);
        s->AddHostRouteTo(ifC.GetAddress(1), 1);
        s->AddHostRouteTo(ifC.GetAddress(2), 1);
        s->AddHostRouteTo(ifC.GetAddress(3), 1);
        // 添加到自身地址的路由（通过接口0）
        // s->AddHostRouteTo(ifC.GetAddress(i + 4), 0);
    }

    // 应用层udp发送
    uint16_t port0 = 9;
    uint16_t port1 = 10;
    uint16_t port2 = 11;
    uint16_t port3 = 12;

    // --- Flow0: StaA[1] -> StaC[2] ---
    OnOffHelper onoff0("ns3::UdpSocketFactory", Address());
    onoff0.SetAttribute("DataRate", StringValue("1Mbps")); // 原1Mbps
    onoff0.SetAttribute("PacketSize", UintegerValue(1024));
    onoff0.SetAttribute("StartTime", TimeValue(Seconds(3.5))); // 时间设置为接口启动后
    onoff0.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // 设置目标地址
    InetSocketAddress dst0(ifC.GetAddress(2), port0);
    onoff0.SetAttribute("Remote", AddressValue(dst0));

    ApplicationContainer app0 = onoff0.Install(StaA.Get(1));

    // --- Flow1: StaA[3] -> StaB[1] ---
    OnOffHelper onoff1("ns3::UdpSocketFactory", Address());
    onoff1.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff1.SetAttribute("PacketSize", UintegerValue(1024));
    onoff1.SetAttribute("StartTime", TimeValue(Seconds(3.5)));
    onoff1.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    InetSocketAddress dst1(ifB.GetAddress(1), port1);
    onoff1.SetAttribute("Remote", AddressValue(dst1));

    ApplicationContainer app1 = onoff1.Install(StaA.Get(3));

    // --- Flow2: StaB[0] -> StaC[1] ---
    OnOffHelper onoff2("ns3::UdpSocketFactory", Address());
    onoff2.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff2.SetAttribute("PacketSize", UintegerValue(1024));
    onoff2.SetAttribute("StartTime", TimeValue(Seconds(3.5)));
    onoff2.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    InetSocketAddress dst2(ifC.GetAddress(1), port2);
    onoff2.SetAttribute("Remote", AddressValue(dst2));

    ApplicationContainer app2 = onoff2.Install(StaB.Get(0));

    // --- Flow3: StaC[0] -> StaC[2] ---
    OnOffHelper onoff3("ns3::UdpSocketFactory", Address());
    onoff3.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff3.SetAttribute("PacketSize", UintegerValue(1024));
    onoff3.SetAttribute("StartTime", TimeValue(Seconds(3.5)));
    onoff3.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    InetSocketAddress dst3(ifC.GetAddress(5), port3);
    onoff3.SetAttribute("Remote", AddressValue(dst3));

    Ipv4Address srcIp = ifC.GetAddress(3); // device 1 的 IP

    InetSocketAddress local(srcIp);
    onoff3.SetAttribute("Local", AddressValue(local));

    ApplicationContainer app3 = onoff3.Install(StaC.Get(0));

    // adhoc接口开关（C域）
    // for (uint32_t i = 0; i < StaC.GetN(); i++)
    // {
    //     Ptr<Node> node = StaC.Get(i);
    //     Ptr<NetDevice> dev = adhocDevsC.Get(i); // 假设每个 STA 的 AdHoc 接口索引相同

    //     // 0秒时关闭adhoc接口
    //     Simulator::Schedule(Seconds(0.0), &DisableDeviceLogical, node, dev);

    //     // 第 7 秒开启
    //     Simulator::Schedule(Seconds(7.0), &EnableDeviceLogical, node, dev); // 接口设置为3秒开启
    // }

    // 调试信息输出：MAC地址
    {
        std::cout << "ap mac" << std::endl;
        for (uint32_t j = 0; j < allApDevices.GetN(); ++j)
        {
            Ptr<NetDevice> dev = allApDevices.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        std::cout << "sta mac" << std::endl;
        for (uint32_t j = 0; j < allStaDevices.GetN(); ++j)
        {
            Ptr<NetDevice> dev = allStaDevices.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        //
        std::cout << "adhoc mac" << std::endl;
        for (uint32_t j = 0; j < adhocDevsC.GetN(); ++j)
        {
            Ptr<NetDevice> dev = adhocDevsC.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        for (uint32_t j = 0; j < routerDevsC.GetN(); ++j)
        {
            Ptr<NetDevice> dev = routerDevsC.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
    }

    // LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_DEBUG);
    // 两秒时设置控制器路由优先级
    // Simulator::Schedule(Seconds(2.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.1), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.3), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);

    FlowMonitorHelper flowmonHelper;

    // 只监控 STA 节点和路由器接口
    NodeContainer monitorNodes;
    monitorNodes.Add(StaA);
    monitorNodes.Add(StaB);
    monitorNodes.Add(StaC);
    monitorNodes.Add(routerNode1);

    Ptr<FlowMonitor> monitor = flowmonHelper.Install(monitorNodes);

    // 打开输出文件
    std::ofstream fout("flow_stats.csv");
    fout << "Time";
    for (int i = 1; i <= 4; ++i)
    { // 有4条链路
        fout << ",Throughput_" << i << "(Kbps),LossRate_" << i << "(%),AvgRTT_" << i << "(ms),Jitter_" << i << "(ms)";
    }
    fout << std::endl;

    // 每1秒采样一次
    Simulator::Schedule(Seconds(0.1), &MonitorFlow, monitor, &flowmonHelper, 1, &fout);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // 在Simulator::Run()之后添加结果分析
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n========== 流量统计结果 ==========" << std::endl;
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::cout << "Flow (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
        std::cout << "  Tx Bytes: " << i->second.txBytes << std::endl;
        std::cout << "  Rx Bytes: " << i->second.rxBytes << std::endl;
        std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
        std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
        std::cout << "  平均延迟: " << i->second.delaySum.GetSeconds() / i->second.rxPackets * 1000 << " ms" << std::endl;
        std::cout << "  丢包率: " << (double)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets * 100 << " %" << std::endl;
        std::cout << std::endl;
    }

    monitor->SerializeToXmlFile("flowmon-results.xml", true, true);
    fout.close();

    /*  PrintMyFlowStats(monitor, &flowmonHelper);*/

    Simulator::Destroy();

    return 0;
}
