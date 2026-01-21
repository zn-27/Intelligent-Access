/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块
 *
 * - 域 A: hostsA (2 主机) -- sw1
 * - 域 B: hostsB (2 主机) -- sw2
 * - 域 C: 3个WiFi终端 + 1个AP -- sw3
 * - 交换机 sw1 与 sw2 直接连接，sw2 与 sw3 直接连接，sw3 与 sw1 直接连接（三角互连）
 * - 单 OpenFlow 控制器管理 sw1 和 sw2 和 sw3
 * - 为所有 AP 分配 IP 并注册到控制器
 * -
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

// 全局FlowMonitorHelper
FlowMonitorHelper flowmonHelper;

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


std::map<uint32_t, double> lastRxBytes;    // 上一次采样时接收字节数
std::map<uint32_t, double> lastPacketRtt;  // 上一次平均 RTT

void MonitorFlow(Ptr<FlowMonitor> monitor, FlowMonitorHelper* flowHelper, double interval, std::ofstream* fout)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double now = Simulator::Now().GetSeconds();
    *fout << now;

    // 按端口号 port0-3 对应顺序输出链路 1-4
    std::vector<uint16_t> ports = { 9, 10, 11, 12 }; // 对应 port0, port1, port2, port3

    for (auto port : ports)
    {
        // 找到对应端口的 FlowId
        FlowId fid = 0;
        for (auto const& flow : stats)
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


 // ---------------------------------------------------------
// 包装函数：周期性执行 SendPosition（每3秒）
// ---------------------------------------------------------
void PeriodicSendPosition(Ptr<OFSwitch13Device> sw3Device, double interval)
{
    // 执行 SendPosition 函数
    sw3Device->SendPosition();
    
    // 打印日志（可选，便于调试）
    std::cout << "[" << Simulator::Now().GetSeconds() << "s] 执行 SendPosition，下一次执行时间：" 
              << Simulator::Now().GetSeconds() + interval << "s" << std::endl;
    
    // 再次调度自身，实现周期性执行
    Simulator::Schedule(Seconds(interval), &PeriodicSendPosition, sw3Device, interval);
}
 int main(int argc, char* argv[])
{
    std::map<std::string, double> pingRttAvg;

    uint16_t simTime = 20;
    bool verbose = true;
    bool trace = true;

    CommandLine cmd;
    cmd.AddValue("simTime", "simulate time ", simTime); // 设置仿真时间
    cmd.AddValue("verbose", "enable verbose logs", verbose); //启用详细日志输出
    cmd.AddValue("trace", "enable trace /pcap", trace); //启用pcap文件
    cmd.Parse(argc, argv);


    if (verbose)
    {
        // LogComponentEnable("OFSwitch13Interface", LOG_LEVEL_ALL);
        // LogComponentEnable("OFSwitch13Device", LOG_LEVEL_ALL);
        // LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_ALL);
        // LogComponentEnable("V4Ping", LOG_LEVEL_ALL);
        // LogComponentEnable("BridgeNetDevice", LOG_LEVEL_ALL); 
    }

    // 启用校验和计算（ofswitch13 模块所需）
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    //创建域内节点
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

    // NodeContainer StaD;
    // StaD.Create(3);
    // NodeContainer ApD;
    // ApD.Create(1);

    NodeContainer wifiStaNodes;
    wifiStaNodes.Add(StaA);
    wifiStaNodes.Add(StaB);
    wifiStaNodes.Add(StaC);
    // wifiStaNodes.Add(StaD);


    //网络设备节点：3台交换机、1台控制器
    Ptr<Node> sw1 = CreateObject<Node>();
    Ptr<Node> sw2 = CreateObject<Node>();
    Ptr<Node> sw3 = CreateObject<Node>();
    // Ptr<Node> sw4 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    //有线链路配置
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));


    // NetDeviceContainer apCsmaDevsA, apCsmaDevsB, apCsmaDevsC, apCsmaDevsD; //AP设备有线接口
    // NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports, sw4Devsports; //交换机端口设备
    NetDeviceContainer apCsmaDevsA, apCsmaDevsB, apCsmaDevsC; //AP设备有线接口
    NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports; //交换机端口设备

    // 连接域 A 主机到 sw1
     // 遍历每个 AP 都连接交换机
    for (uint32_t i = 0; i < ApA.GetN(); ++i)
    {
        NodeContainer pair(ApA.Get(i), sw1); // sw1 是 A 域的交换机节点
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsA.Add(link.Get(0));        // AP 侧
        sw1Devsports.Add(link.Get(1));   // 交换机侧
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
    // //连接域 D 主机到 sw4
    // {
    //     NodeContainer pair(ApD.Get(0), sw4);
    //     NetDeviceContainer link = csma.Install(pair);
    //     apCsmaDevsD.Add(link.Get(0));
    //     sw4Devsports.Add(link.Get(1));
    // }


    // --------------------------
    // 交换机之间三角连接配置
    // --------------------------
    CsmaHelper csmaSwitch;// 交换机互连链路配置
    csmaSwitch.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csmaSwitch.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    // sw1 与 sw2 连接
    {
        NodeContainer pair(sw1, sw2);
        NetDeviceContainer link = csmaSwitch.Install(pair);
        sw1Devsports.Add(link.Get(0));
        sw2Devsports.Add(link.Get(1));
    }

    // sw2 与 sw3 连接（新增）
    {
        NodeContainer pair(sw2, sw3);
        NetDeviceContainer link = csmaSwitch.Install(pair);
        sw2Devsports.Add(link.Get(0));
        sw3Devsports.Add(link.Get(1));
    }
        // sw3 与 sw1 连接（新增）
    {
        NodeContainer pair(sw3, sw1);
        NetDeviceContainer link = csmaSwitch.Install(pair);
        sw3Devsports.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }

    // // sw3 与 sw4 连接（新增）
    // {
    //     NodeContainer pair(sw3, sw4);
    //     NetDeviceContainer link = csmaSwitch.Install(pair);
    //     sw3Devsports.Add(link.Get(0));
    //     sw4Devsports.Add(link.Get(1));
    // }

    // // sw4 与 sw1 连接（新增）
    // {
    //     NodeContainer pair(sw4, sw1);
    //     NetDeviceContainer link = csmaSwitch.Install(pair);
    //     sw4Devsports.Add(link.Get(0));
    //     sw1Devsports.Add(link.Get(1));
    // }


    // wifi配置部分
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");
    WifiMacHelper mac; //逻辑可复用

    //A域
    Ptr<YansWifiChannel> channelA = channel.Create();
    YansWifiPhyHelper phyA;
    phyA.SetChannel(channelA);

    Ssid ssidA = Ssid("A");

    mac.SetType("ns3::StaWifiMac",
        "Ssid", SsidValue(ssidA),
        "ActiveProbing", BooleanValue(false));  //关闭STA主动探测接口，STA无法直接通信
    NetDeviceContainer staWifiDevsA = wifi.Install(phyA, mac, StaA);

    mac.SetType("ns3::ApWifiMac",
        "Ssid", SsidValue(ssidA));
    NetDeviceContainer apWifiDevsA = wifi.Install(phyA, mac, ApA);

    //创建ap-ap backbone信道
    Ptr<YansWifiChannel> backboneChannel = channel.Create(); //独立信道
    YansWifiPhyHelper phyBackbone;
    phyBackbone.SetChannel(backboneChannel);

    WifiMacHelper backboneMac;
    backboneMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer apBackboneDevices = wifi.Install(phyBackbone, backboneMac, ApA);

    //B域
    Ptr<YansWifiChannel> channelB = channel.Create();
    YansWifiPhyHelper phyB;
    phyB.SetChannel(channelB);

    Ssid ssidB = Ssid("B");

    mac.SetType("ns3::StaWifiMac",
        "Ssid", SsidValue(ssidB),
        "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staWifiDevsB = wifi.Install(phyB, mac, StaB);

    mac.SetType("ns3::ApWifiMac",
        "Ssid", SsidValue(ssidB));
    NetDeviceContainer apWifiDevsB = wifi.Install(phyB, mac, ApB);

    //C域
    Ptr<YansWifiChannel> channelC = channel.Create();
    YansWifiPhyHelper phyC;
    phyC.SetChannel(channelC);

    Ssid ssidC = Ssid("C");

    mac.SetType("ns3::StaWifiMac",
        "Ssid", SsidValue(ssidC),
        "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staWifiDevsC = wifi.Install(phyC, mac, StaC);

    Ssid ssidC_sub = Ssid("C_sub");

    mac.SetType("ns3::ApWifiMac",
        "Ssid", SsidValue(ssidC_sub));
    NetDeviceContainer apOnStaDevsC = wifi.Install(phyC, mac, StaC);

    mac.SetType("ns3::ApWifiMac",
        "Ssid", SsidValue(ssidC));
    NetDeviceContainer apWifiDevsC = wifi.Install(phyC, mac, ApC);

    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer adhocDevsC = wifi.Install(phyC, mac, StaC);


// 在这里添加AP应用创建代码：
// 为域A的AP节点创建应用
for (uint32_t i = 0; i < ApA.GetN(); ++i) {
    Ptr<ApProtocolInfoApp> apAppA = CreateObject<ApProtocolInfoApp>();
    ApA.Get(i)->AddApplication(apAppA);
    apAppA->SetStartTime(Seconds(0.0));
    apAppA->SetStopTime(Seconds(simTime - 1.0));
}

// 为域B的AP节点创建应用
Ptr<ApProtocolInfoApp> apAppB = CreateObject<ApProtocolInfoApp>();
ApB.Get(0)->AddApplication(apAppB);
apAppB->SetStartTime(Seconds(0.0));
apAppB->SetStopTime(Seconds(simTime - 1.0));

// 为域C的AP节点创建应用
Ptr<ApProtocolInfoApp> apAppC = CreateObject<ApProtocolInfoApp>();
ApC.Get(0)->AddApplication(apAppC);
apAppC->SetStartTime(Seconds(0.0));
apAppC->SetStopTime(Seconds(simTime - 1.0));



    //合并所有sta设备
    NetDeviceContainer allStaDevices;
    allStaDevices.Add(staWifiDevsA);
    allStaDevices.Add(staWifiDevsB);
    allStaDevices.Add(staWifiDevsC);

    //合并所有ap设备
    NetDeviceContainer allApDevices;
    allApDevices.Add(apWifiDevsA);
    allApDevices.Add(apWifiDevsB);
    allApDevices.Add(apWifiDevsC);


   //节点位置配置
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // 控制器位置
    Ptr<ListPositionAllocator> posController = CreateObject<ListPositionAllocator>();
    posController->Add(Vector(0, 0, 0)); // 单个节点直接 Add
    mobility.SetPositionAllocator(posController);
    mobility.Install(controllerNode);

    // 交换机
    Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
    swPos->Add(Vector(-100, 150, 0)); // sw1
    swPos->Add(Vector(100, 150, 0)); // sw2
    swPos->Add(Vector(0, 150, 0)); // sw3
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


    // // 域C节点
    // MobilityHelper adhocMobility;
    // adhocMobility.SetMobilityModel("ns3::WaypointMobilityModel");
    // adhocMobility.Install(StaC);
    // adhocMobility.Install(ApC);

    // //AP位置固定
    // Ptr<WaypointMobilityModel> apMob =
    //     ApC.Get(0)->GetObject<WaypointMobilityModel>();

    // apMob->AddWaypoint(Waypoint(Seconds(0.0),
    //     Vector(0.0, 200.0, 0.0)));

    // //STA向AP逐渐收敛
    // for (uint32_t i = 0; i < StaC.GetN(); ++i)
    // {
    //     Ptr<Node> node = StaC.Get(i);
    //     Ptr<WaypointMobilityModel> mob =
    //         node->GetObject<WaypointMobilityModel>();

    //     // 初始位置（t=0）
    //     Vector initPos;
    //     if (i == 0) initPos = Vector(20.0, 230.0, 0.0);
    //     if (i == 1) initPos = Vector(30.0, 210.0, 0.0);
    //     if (i == 2) initPos = Vector(-20.0, 230.0, 0.0);

    //     mob->AddWaypoint(Waypoint(Seconds(0.0), initPos));

    //     //3秒时节点静止
    //     mob->AddWaypoint(Waypoint(Seconds(3.0), initPos));

    //     // 3 秒开始向 AP 移动
    //     Vector apPos = ApC.Get(0)->GetObject<MobilityModel>()->GetPosition();
    //     mob->AddWaypoint(Waypoint(Seconds(20.0), apPos));
    // }

    // //域D节点
    // Ptr<ListPositionAllocator> posD = CreateObject<ListPositionAllocator>();
    // posD->Add(Vector(131, 200, 0));
    // posD->Add(Vector(140, 230, 0));
    // posD->Add(Vector(145, 230, 0));
    // posD->Add(Vector(150, 220, 0));
    // mobility.SetPositionAllocator(posD);
    // mobility.Install(ApD);
    // mobility.Install(StaD);

    //将AP的WiFi接口与有线接口桥接（实现有线无线互通）
    BridgeHelper bridge;
    // NetDeviceContainer  bridgeDevA, bridgeDevB, bridgeDevC, bridgeDevD;
    NetDeviceContainer  bridgeDevA, bridgeDevB, bridgeDevC;

    for (uint32_t i = 0;i < ApA.GetN();++i)
    {

        NetDeviceContainer bridgeDev = bridge.Install(ApA.Get(i),
            NetDeviceContainer(apWifiDevsA.Get(i), apCsmaDevsA.Get(i)));
        bridgeDevA.Add(bridgeDev);
    }

    bridgeDevB = bridge.Install(ApB.Get(0),
        NetDeviceContainer(apWifiDevsB.Get(0), apCsmaDevsB.Get(0)));

    bridgeDevC = bridge.Install(ApC.Get(0),
        NetDeviceContainer(apWifiDevsC.Get(0), apCsmaDevsC.Get(0))); //桥接AP的WiFi和有线设备

    // bridgeDevD = bridge.Install(ApD.Get(0),
    //     NetDeviceContainer(adhocDevsD1.Get(0), apCsmaDevsD.Get(0)));

    // OpenFlow控制器与交换机配置
    Ptr<OFSwitch13InternalHelper> of13Helper =
        CreateObject<OFSwitch13InternalHelper>();

    //安装控制器应用到控制器节点
    of13Helper->InstallController(controllerNode);
    //安装交换机应用到交换机节点，并关联其端口设备
    of13Helper->InstallSwitch(sw1, sw1Devsports);
    of13Helper->InstallSwitch(sw2, sw2Devsports);
    of13Helper->InstallSwitch(sw3, sw3Devsports);
    // of13Helper->InstallSwitch(sw4, sw4Devsports);
    //创建控制器与交换机之间的OpenFlow信道
    of13Helper->CreateOpenFlowChannels();

    //获取控制器应用实例（用于后续配置路由优先级）
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

    InternetStackHelper stack; //基础IP协议栈

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


    // 需要确保所有AP节点也安装了协议栈
    stack2.Install(ApA);
    stack2.Install(ApB);
    stack2.Install(ApC);
    // stack2.Install(ApD); // 如果启用D域


    // 分配IPv4地址
    Ipv4AddressHelper ipv4;

    Ipv4InterfaceContainer ifA, ifApA; // 域A主机和路由器接口
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    // 给 A域 设备和 routerDevsA 分配地址
    {
        NetDeviceContainer netA = NetDeviceContainer();
        // 先为主机分配地址（10.1.1.1-10.1.1.4）
        for (uint32_t i = 0; i < staWifiDevsA.GetN(); ++i)
            netA.Add(staWifiDevsA.Get(i));
        ifA = ipv4.Assign(netA);

        // 为AP的网桥接口分配IP地址
        NetDeviceContainer apBridgeDevsA;
        for (uint32_t i = 0; i < bridgeDevA.GetN(); ++i)
            apBridgeDevsA.Add(bridgeDevA.Get(i));
        ifApA = ipv4.Assign(apBridgeDevsA);


    }

    Ipv4InterfaceContainer ifB, ifApB;
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    {
        NetDeviceContainer netB = NetDeviceContainer();
        // 先为主机分配地址（10.2.1.1-10.2.1.2）
        for (uint32_t i = 0; i < staWifiDevsB.GetN(); ++i)
            netB.Add(staWifiDevsB.Get(i));
        ifB = ipv4.Assign(netB);

         // 为AP的网桥接口分配IP地址
        NetDeviceContainer apBridgeDevsB;
        apBridgeDevsB.Add(bridgeDevB);
        ifApB = ipv4.Assign(apBridgeDevsB);

    }

    Ipv4InterfaceContainer ifC, ifApC;
    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    {
        NetDeviceContainer netC = NetDeviceContainer();
        // 先为主机分配地址（10.3.1.1-10.3.1.3）
        for (uint32_t i = 0; i < staWifiDevsC.GetN(); ++i)
            netC.Add(staWifiDevsC.Get(i));
        // 添加Adhoc接口（10.3.1.4-10.3.1.6）
        for (uint32_t i = 0; i < adhocDevsC.GetN(); ++i)
            netC.Add(adhocDevsC.Get(i));
        //添加ap接口（10.3.1.7-10.3.1.9）
        for (uint32_t i = 0; i < apOnStaDevsC.GetN(); ++i)
            netC.Add(apOnStaDevsC.Get(i));
        ifC = ipv4.Assign(netC);

         // 为AP的网桥接口分配IP地址
        NetDeviceContainer apBridgeDevsC;
        apBridgeDevsC.Add(bridgeDevC);
        ifApC = ipv4.Assign(apBridgeDevsC);

    } // wifi 网络

    // Ipv4InterfaceContainer ifD, ifApD;
    // ipv4.SetBase("10.4.1.0", "255.255.255.0");
    // {
    //     NetDeviceContainer netD = NetDeviceContainer();
    //     //先为主机分配地址（10.4.1.1-10.4.1.3）
    //     for (uint32_t i = 0; i < adhocDevsD2.GetN();++i)
    //         netD.Add(adhocDevsD2.Get(i));
    //     ifD = ipv4.Assign(netD);

    // }

// // 域A的STA节点默认路由指向sw1 (使用网桥A的实际IP地址作为网关)
// for (uint32_t i = 0; i < StaA.GetN(); ++i) {
//     Ptr<Ipv4> ipv4Sta = StaA.Get(i)->GetObject<Ipv4>();
//     Ptr<Ipv4StaticRouting> staticRoutingSta = staticRoutingHelper.GetStaticRouting(ipv4Sta);
//     // 设置默认路由指向网桥A的实际IP地址
//     staticRoutingSta->SetDefaultRoute(ifApA.GetAddress(0), 1); // 使用实际网桥IP
// }

// // 域B的STA节点默认路由指向sw2 (使用网桥B的实际IP地址作为网关)
// for (uint32_t i = 0; i < StaB.GetN(); ++i) {
//     Ptr<Ipv4> ipv4Sta = StaB.Get(i)->GetObject<Ipv4>();
//     Ptr<Ipv4StaticRouting> staticRoutingSta = staticRoutingHelper.GetStaticRouting(ipv4Sta);
//     // 设置默认路由指向网桥B的实际IP地址
//     staticRoutingSta->SetDefaultRoute(ifApB.GetAddress(0), 1);
// }

// // 域C的STA节点默认路由指向sw3 (使用网桥C的实际IP地址作为网关)
// for (uint32_t i = 0; i < StaC.GetN(); ++i) {
//     Ptr<Ipv4> ipv4Sta = StaC.Get(i)->GetObject<Ipv4>();
//     Ptr<Ipv4StaticRouting> staticRoutingSta = staticRoutingHelper.GetStaticRouting(ipv4Sta);
//     // 设置默认路由指向网桥C的实际IP地址
//     staticRoutingSta->SetDefaultRoute(ifApC.GetAddress(0), 1);
// }
//     配置主机默认路由给交换机
// 为所有STA节点添加默认路由指向虚拟网关IP (.254)

// 域A的STA节点默认路由指向sw1 (使用10.1.1.254作为虚拟网关)
for (uint32_t i = 0; i < StaA.GetN(); ++i) {
    Ptr<Ipv4> ipv4Sta = StaA.Get(i)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingSta = staticRoutingHelper.GetStaticRouting(ipv4Sta);
    // 设置默认路由指向虚拟网关IP 10.1.1.254
    staticRoutingSta->SetDefaultRoute(Ipv4Address("10.1.1.254"), 1);
}

// 域B的STA节点默认路由指向sw2 (使用10.2.1.254作为虚拟网关)
for (uint32_t i = 0; i < StaB.GetN(); ++i) {
    Ptr<Ipv4> ipv4Sta = StaB.Get(i)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingSta = staticRoutingHelper.GetStaticRouting(ipv4Sta);
    // 设置默认路由指向虚拟网关IP 10.2.1.254
    staticRoutingSta->SetDefaultRoute(Ipv4Address("10.2.1.254"), 1);
}

// 域C的STA节点默认路由指向sw3 (使用10.3.1.254作为虚拟网关)
for (uint32_t i = 0; i < StaC.GetN(); ++i) {
    Ptr<Ipv4> ipv4Sta = StaC.Get(i)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingSta = staticRoutingHelper.GetStaticRouting(ipv4Sta);
    // 设置默认路由指向虚拟网关IP 10.3.1.254
    staticRoutingSta->SetDefaultRoute(Ipv4Address("10.3.1.254"), 1);
}

// // 域D的STA节点默认路由指向sw4 (使用10.4.1.254作为虚拟网关)
// for (uint32_t i = 0; i < StaD.GetN(); ++i) {
//     Ptr<Ipv4> ipv4Sta = StaD.Get(i)->GetObject<Ipv4>();
//     Ptr<Ipv4StaticRouting> staticRoutingSta = staticRoutingHelper.GetStaticRouting(ipv4Sta);
//     // 设置默认路由指向虚拟网关IP 10.4.1.254
//     staticRoutingSta->SetDefaultRoute(Ipv4Address("10.4.1.254"), 1);
// }


    //打印输出

    for (uint32_t i = 0; i < StaA.GetN(); ++i) {
        Ptr<Ipv4> ipv4h = StaA.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> r = staticRoutingHelper.GetStaticRouting(ipv4h);
        std::cout << "STA A " << i << " default route: " << r->GetDefaultRoute() << std::endl;
    }

    for (uint32_t i = 0; i < StaC.GetN(); ++i) {
        Ptr<Ipv4> ipv4h = StaC.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> r = staticRoutingHelper.GetStaticRouting(ipv4h);
        std::cout << "STA " << i << " default route: "
            << r->GetDefaultRoute() << std::endl;
    }

    std::cout << "sw1 ports: " << sw1Devsports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw1Devsports.GetN(); ++i)
        std::cout << "Port " << i << ": " << sw1Devsports.Get(i)->GetAddress() << std::endl;

    std::cout << "sw2 ports: " << sw2Devsports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw2Devsports.GetN(); ++i)
        std::cout << "Port " << i << ": " << sw2Devsports.Get(i)->GetAddress() << std::endl;

    std::cout << "sw3 ports: " << sw3Devsports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw3Devsports.GetN(); ++i)
        std::cout << "Port " << i << ": " << sw3Devsports.Get(i)->GetAddress() << std::endl;

    // std::cout << "sw4 ports: " << sw4Devsports.GetN() << std::endl;
    // for (uint32_t i = 0; i < sw4Devsports.GetN(); ++i)
    //     std::cout << "Port " << i << ": " << sw4Devsports.Get(i)->GetAddress() << std::endl;




    // 启用pcap追踪
    if (true)
    {
        // ---- C 域抓包 ----
        phyC.EnablePcap("C_adhoc", adhocDevsC);       // C 域 AdHoc 接口
        phyC.EnablePcap("C_sta", staWifiDevsC);        // C 域 STA
        phyC.EnablePcap("C_ap", apWifiDevsC);          // C 域 AP  

        // ---- A/B 域抓包 ----
        phyA.EnablePcap("A_ap", apWifiDevsA);          // A 域 AP 
        phyA.EnablePcap("A_sta", staWifiDevsA);        // A 域 STA
        phyB.EnablePcap("B_ap", apWifiDevsB);          // B 域 AP 
        phyB.EnablePcap("B_sta", staWifiDevsB);        // B 域 STA


        // ---- OpenFlow / 交换机抓包 ----
        of13Helper->EnableOpenFlowPcap("openflow-interdomain");
        of13Helper->EnableDatapathStats("switch-stats");

        // 交换机端口抓包（包括互连端口）
        csmaSwitch.EnablePcap("sw1", sw1Devsports, true);
        csmaSwitch.EnablePcap("sw2", sw2Devsports, true);
        csmaSwitch.EnablePcap("sw3", sw3Devsports, true);



        // A/B/C 域 AP 的 CSMA 接口抓包
        csma.EnablePcap("A_domain_ap", apCsmaDevsA);
        csma.EnablePcap("B_domain_ap", apCsmaDevsB);
        csma.EnablePcap("C_domain_ap", apCsmaDevsC);


    }




    // 应用层udp发送
    // uint16_t port0 = 9;
    // uint16_t port1 = 10;
    // uint16_t port2 = 11;
    // uint16_t port3 = 12;

    // // --- Flow0: StaA[1] -> StaC[2] ---
    // OnOffHelper onoff0("ns3::UdpSocketFactory", Address());
    // onoff0.SetAttribute("DataRate", StringValue("600kbps")); // 原1Mbps
    // onoff0.SetAttribute("PacketSize", UintegerValue(1024));
    // onoff0.SetAttribute("StartTime", TimeValue(Seconds(3.5))); // 时间设置为接口启动后
    // onoff0.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // // 设置目标地址
    // InetSocketAddress dst0(ifC.GetAddress(2), port0);
    // onoff0.SetAttribute("Remote", AddressValue(dst0));

    // ApplicationContainer app0 = onoff0.Install(StaA.Get(1));

    // // --- Flow1: StaA[3] -> StaB[1] ---
    // OnOffHelper onoff1("ns3::UdpSocketFactory", Address());
    // onoff1.SetAttribute("DataRate", StringValue("600kbps"));
    // onoff1.SetAttribute("PacketSize", UintegerValue(1024));
    // onoff1.SetAttribute("StartTime", TimeValue(Seconds(3.5)));
    // onoff1.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // InetSocketAddress dst1(ifB.GetAddress(1), port1);
    // onoff1.SetAttribute("Remote", AddressValue(dst1));

    // ApplicationContainer app1 = onoff1.Install(StaA.Get(3));

    // // --- Flow2: StaB[0] -> StaC[1] ---
    // OnOffHelper onoff2("ns3::UdpSocketFactory", Address());
    // onoff2.SetAttribute("DataRate", StringValue("600kbps"));
    // onoff2.SetAttribute("PacketSize", UintegerValue(1024));
    // onoff2.SetAttribute("StartTime", TimeValue(Seconds(3.5)));
    // onoff2.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // InetSocketAddress dst2(ifC.GetAddress(1), port2);
    // onoff2.SetAttribute("Remote", AddressValue(dst2));

    // ApplicationContainer app2 = onoff2.Install(StaA.Get(0));

    // // --- Flow3: StaC[0] -> StaC[2] ---
    // OnOffHelper onoff3("ns3::UdpSocketFactory", Address());
    // onoff3.SetAttribute("DataRate", StringValue("600kbps"));
    // onoff3.SetAttribute("PacketSize", UintegerValue(1024));
    // onoff3.SetAttribute("StartTime", TimeValue(Seconds(3.5)));
    // onoff3.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // InetSocketAddress dst3(ifC.GetAddress(2), port3);
    // onoff3.SetAttribute("Remote", AddressValue(dst3));

    // ApplicationContainer app3 = onoff3.Install(StaC.Get(0));



//ping测试
// 域C内通信：域C中的节点0 ping 域C中的节点1
Ipv4Address intraDomainDst = ifC.GetAddress(2);
V4PingHelper intraDomainPing(intraDomainDst);
intraDomainPing.SetAttribute("Verbose", BooleanValue(true));
ApplicationContainer intraDomainPingApp = intraDomainPing.Install(StaA.Get(1));  // 使用StaC而不是wifiStaNodesC
intraDomainPingApp.Start(Seconds(2.0));
intraDomainPingApp.Stop(Seconds(6.0));

//跨域ping测试示例
// 域A节点ping域C节点：域A中的节点0 ping 域C中的节点1
Ipv4Address interDomainDst = ifC.GetAddress(1);  // 域C的目标地址
V4PingHelper interDomainPing(interDomainDst);
interDomainPing.SetAttribute("Verbose", BooleanValue(true));
ApplicationContainer interDomainPingApp = interDomainPing.Install(StaA.Get(0));  // 从域A发送，使用StaA
interDomainPingApp.Start(Seconds(2.0));
interDomainPingApp.Stop(Seconds(6.0));

    //adhoc接口开关（C域）
    for (uint32_t i = 0; i < StaC.GetN(); i++)
    {
        Ptr<Node> node = StaC.Get(i);
        Ptr<NetDevice> dev = adhocDevsC.Get(i); // 假设每个 STA 的 AdHoc 接口索引相同

        // 0秒时关闭adhoc接口
        Simulator::Schedule(Seconds(0.0), &DisableDeviceLogical, node, dev);

        // 第 7 秒开启
        // Simulator::Schedule(Seconds(7.0), &EnableDeviceLogical, node, dev); //接口设置为3秒开启
    }


    //调试信息输出：MAC地址和IP地址
    {

         std::cout << "ap mac and ip" << std::endl;
    // 输出域A的AP设备
    for (uint32_t i = 0; i < ApA.GetN(); ++i) {
        // 显示网桥接口信息
        Ptr<NetDevice> dev = bridgeDevA.Get(i);  // 改为网桥接口
        Address addr = dev->GetAddress();
        std::cout << "  ApA[" << i << "] 网桥接口"
            << " -> MAC Address: "
            << Mac48Address::ConvertFrom(addr);
        // 输出IP地址（从网桥接口获取）
        Ptr<Ipv4> ipv4 = ApA.Get(i)->GetObject<Ipv4>();
        if (ipv4 && ipv4->GetNInterfaces() > 1) {
            std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
        }
        std::cout << std::endl;
    }

    // 输出域B的AP设备
    Ptr<NetDevice> devB = bridgeDevB.Get(0);  // 改为网桥接口
    Address addrB = devB->GetAddress();
    std::cout << "  ApB[0] 网桥接口"
        << " -> MAC Address: "
        << Mac48Address::ConvertFrom(addrB);
    // 输出IP地址（从网桥接口获取）
    Ptr<Ipv4> ipv4B = ApB.Get(0)->GetObject<Ipv4>();
    if (ipv4B && ipv4B->GetNInterfaces() > 1) {
        std::cout << " -> IP Address: " << ipv4B->GetAddress(1, 0).GetLocal();
    }
    std::cout << std::endl;

    // 输出域C的AP设备
    Ptr<NetDevice> devC = bridgeDevC.Get(0);  // 改为网桥接口
    Address addrC = devC->GetAddress();
    std::cout << "  ApC[0] 网桥接口"
        << " -> MAC Address: "
        << Mac48Address::ConvertFrom(addrC);
    // 输出IP地址（从网桥接口获取）
    Ptr<Ipv4> ipv4C = ApC.Get(0)->GetObject<Ipv4>();
    if (ipv4C && ipv4C->GetNInterfaces() > 1) {
        std::cout << " -> IP Address: " << ipv4C->GetAddress(1, 0).GetLocal();
    }
    std::cout << std::endl;

        std::cout << "sta mac and ip" << std::endl;
        // 输出域A的STA设备
        for (uint32_t i = 0; i < StaA.GetN(); ++i) {
            Ptr<NetDevice> dev = staWifiDevsA.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaA[" << i << "] WiFi接口"
                << " -> MAC Address: "
                << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaA.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1) {
                std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        // 输出域B的STA设备
        for (uint32_t i = 0; i < StaB.GetN(); ++i) {
            Ptr<NetDevice> dev = staWifiDevsB.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaB[" << i << "] WiFi接口"
                << " -> MAC Address: "
                << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaB.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1) {
                std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        // 输出域C的STA设备
        for (uint32_t i = 0; i < StaC.GetN(); ++i) {
            Ptr<NetDevice> dev = staWifiDevsC.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaC[" << i << "] WiFi接口"
                << " -> MAC Address: "
                << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1) {
                std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        std::cout << "adhoc mac and ip" << std::endl;
        // 输出域C的Adhoc设备
        for (uint32_t i = 0; i < StaC.GetN(); ++i) {
            Ptr<NetDevice> dev = adhocDevsC.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaC[" << i << "] Adhoc接口"
                << " -> MAC Address: "
                << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 2) {
                std::cout << " -> IP Address: " << ipv4->GetAddress(2, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        //输出域C的sta节点的ap设备
        for (uint32_t i = 0; i < StaC.GetN(); ++i) {
            Ptr<NetDevice> dev = apOnStaDevsC.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaC[" << i << "] Ap接口"
                << " -> MAC Address: "
                << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 3) {
                std::cout << " -> IP Address: " << ipv4->GetAddress(3, 0).GetLocal();
            }
            std::cout << std::endl;
        }
    }

// 在控制器应用程序中添加ARP条目，将虚拟网关IP映射到交换机端口MAC地址
if (controllerApp != nullptr) {
    // 为每个域添加ARP条目，需要将Address转换为Mac48Address，并添加日志输出
    Mac48Address macA = Mac48Address::ConvertFrom(sw1Devsports.Get(0)->GetAddress());
    // std::cout << "Adding ARP entry: 10.1.1.254 -> " << macA << std::endl;
    controllerApp->AddArpEntry(Ipv4Address("10.1.1.254"), macA); // 域A网关
    
    Mac48Address macB = Mac48Address::ConvertFrom(sw2Devsports.Get(0)->GetAddress());
    // std::cout << "Adding ARP entry: 10.2.1.254 -> " << macB << std::endl;
    controllerApp->AddArpEntry(Ipv4Address("10.2.1.254"), macB); // 域B网关
    
    Mac48Address macC = Mac48Address::ConvertFrom(sw3Devsports.Get(0)->GetAddress());
    // std::cout << "Adding ARP entry: 10.3.1.254 -> " << macC << std::endl;
    controllerApp->AddArpEntry(Ipv4Address("10.3.1.254"), macC); // 域C网关
    std::cout << "Switch mac and ip" << macC << std::endl;
}



    // LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_DEBUG);
    //两秒时设置控制器路由优先级
    Simulator::Schedule(Seconds(2.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.1), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.3), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    FlowMonitorHelper flowmonHelper;



// 添加获取AP和STA消息的调度函数
Ptr<OFSwitch13Device> sw1Device = sw1->GetObject<OFSwitch13Device>();
Ptr<OFSwitch13Device> sw2Device = sw2->GetObject<OFSwitch13Device>();
Ptr<OFSwitch13Device> sw3Device = sw3->GetObject<OFSwitch13Device>();


// if (sw1Device) {
//     Simulator::Schedule(Seconds(3.0), &OFSwitch13Device::GetApStaMessages, sw1Device);
// }
// if (sw2Device) {
//     Simulator::Schedule(Seconds(3.0), &OFSwitch13Device::GetApStaMessages, sw2Device);
// }
// if (sw3Device) {
//     Simulator::Schedule(Seconds(3.0), &OFSwitch13Device::GetApStaMessages, sw3Device);
// }


// Simulator::Schedule(Seconds(3.0),&OFSwitch13Device::GetApStaMessages,sw3Device);
    //  if (sw3Device)
    // {
    //     double interval = 3.0;  // 执行间隔（秒）
    //     // 首次调度：0秒后执行第一次，之后每3秒执行一次
    //     Simulator::Schedule(Seconds(0.0), &PeriodicSendPosition, sw3Device, interval);
    // }


    //  // 2. 直接调度周期性决策函数（替代原StartQLearningDecisionProcess）
    // // 3秒后再执行一次
    // Simulator::Schedule(Seconds(3.5), &OFSwitch13LearningController::PeriodicDecisionMaking, controllerApp); 
    

    // Simulator::Schedule(Seconds(10.0),&OFSwitch13Device::SendPosition,sw3Device);
    // Simulator::Schedule(Seconds(6.0),&OFSwitch13Device::SendPosition,sw3Device);
    // Simulator::Schedule(Seconds(9.0),&OFSwitch13Device::SendPosition,sw3Device);


    // 只监控 STA 节点
    NodeContainer monitorNodes;
    monitorNodes.Add(StaA);
    monitorNodes.Add(StaB);
    monitorNodes.Add(StaC);
    // monitorNodes.Add(StaD);

    Ptr<FlowMonitor> monitor = flowmonHelper.Install(monitorNodes);

    // 打开输出文件
    std::ofstream fout("flow_stats.csv");
    fout << "Time";
    for (int i = 1; i <= 4; ++i) {  // 有4条链路
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