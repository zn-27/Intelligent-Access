// 3-2 开始 针对train4的无线优化与智能自组织的接入融合
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
#include "ns3/virtual-net-device.h"
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
// 全局 map：存储 WiFi 设备指针对应的 switch 名称
std::map<Ptr<WifiNetDevice>, std::string> g_switchNameMap;

// 全局追踪函数：追踪 ICMP 包，输出 MAC 和 IP 详细信息
void TraceIcmpPacketWithMac(std::string location, Ptr<const Packet> packet)
{
    Ptr<Packet> pkt = packet->Copy();

    // 方法1: 先尝试直接解析 IP 头（WiFi AdHoc 模式下包没有 Ethernet 头）
    Ipv4Header ipv4Header;
    if (pkt->PeekHeader(ipv4Header))
    {
        // 验证是否是有效的 IPv4 头（通过检查源/目的 IP 是否合理）
        Ipv4Address srcIp = ipv4Header.GetSource();
        Ipv4Address dstIp = ipv4Header.GetDestination();

        // 检查 IP 地址是否在合理范围内 (10.x.x.x 或其他配置的网段)
        if (srcIp.Get() != 0 || dstIp.Get() != 0)
        {
            // 这是一个有效的 IP 包
            std::cout << "[" << Simulator::Now().GetSeconds() << "s] "
                      << "[TRACE-IP] " << location
                      << " | IP: " << srcIp << " -> " << dstIp
                      << " | Proto: " << (int)ipv4Header.GetProtocol()
                      << " | Size: " << packet->GetSize() << " bytes" << std::endl;
            return;
        }
    }

    // 方法2: 尝试解析 Ethernet 头 + IP 头（CSMA/Bridge 模式）
    pkt = packet->Copy();
    EthernetHeader ethHeader;
    if (pkt->RemoveHeader(ethHeader))
    {
        Mac48Address srcMac = ethHeader.GetSource();
        Mac48Address dstMac = ethHeader.GetDestination();
        uint16_t ethType = ethHeader.GetLengthType();

        // 如果是 IPv4 (0x0800)，继续解析 IP 头
        if (ethType == 0x0800 && pkt->PeekHeader(ipv4Header))
        {
            std::cout << "[" << Simulator::Now().GetSeconds() << "s] "
                      << "[TRACE-ETH] " << location
                      << " | MAC: " << srcMac << " -> " << dstMac
                      << " | IP: " << ipv4Header.GetSource()
                      << " -> " << ipv4Header.GetDestination()
                      << " | Proto: " << (int)ipv4Header.GetProtocol()
                      << " | Size: " << packet->GetSize() << " bytes" << std::endl;
        }
        else
        {
            std::cout << "[" << Simulator::Now().GetSeconds() << "s] "
                      << "[TRACE-ETH] " << location
                      << " | MAC: " << srcMac << " -> " << dstMac
                      << " | EthType: 0x" << std::hex << ethType << std::dec
                      << " | Size: " << packet->GetSize() << " bytes" << std::endl;
        }
        return;
    }

    // 方法3: 无法解析
    std::cout << "[" << Simulator::Now().GetSeconds() << "s] "
              << "[TRACE-RAW] " << location
              << " | Unknown packet format"
              << " | Size: " << packet->GetSize() << " bytes" << std::endl;
}

// Helper function for WiFi -> VirtualNetDevice packet forwarding
// Used to bridge WiFi received packets to VirtualNetDevice for OpenFlow processing
// 【关键修复】WiFi AdHoc传输的是IP包（无Ethernet头），但OpenFlow Pipeline需要Ethernet帧
// 所以在转发给VirtualNetDevice之前需要添加Ethernet头
bool WifiToVirtualDevForward(Ptr<VirtualNetDevice> vdev, Ptr<NetDevice> dev,
                             Ptr<const Packet> packet, uint16_t protocol,
                             const Address &source, const Address &dest,
                             NetDevice::PacketType packetType)
{
    std::string switchName = g_switchNameMap[DynamicCast<WifiNetDevice>(dev)];

    // 检测 ICMP (ping)
    if (protocol == 0x0800)
    {
        Ptr<Packet> pkt = packet->Copy();
        Ipv4Header ipv4Header;
        pkt->PeekHeader(ipv4Header);

        if (ipv4Header.GetProtocol() == 1)
        {
            // 追踪：进入 vdev 之前
            // TraceIcmpPacketWithMac(switchName + "-1-BeforeVdev", packet);

            // 【关键修复】为IP包添加Ethernet头
            Ptr<Packet> packetCopy = packet->Copy();
            Mac48Address srcMac = Mac48Address::ConvertFrom(dev->GetAddress());
            Mac48Address dstMac = Mac48Address::ConvertFrom(vdev->GetAddress());
            EthernetHeader ethHeader(false); // 不使用preamble
            ethHeader.SetSource(srcMac);
            ethHeader.SetDestination(dstMac);
            ethHeader.SetLengthType(0x0800); // IPv4
            EthernetTrailer ethTrailer;
            packetCopy->AddHeader(ethHeader);
            packetCopy->AddTrailer(ethTrailer);

            // 调用 vdev->Receive()，使用Ethernet协议类型
            bool ret = vdev->Receive(packetCopy, 0x0800, srcMac, dstMac, packetType);

            // 追踪: vdev 返回后
            // TraceIcmpPacketWithMac(switchName + "-2-AfterVdev-" + std::string(ret ? "SUCCESS" : "FAIL"), packet);
            return ret;
        }
    }

    // 非 ping 包 - 同样需要添加Ethernet头
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

    uint16_t simTime = 30;
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
    // LogComponentEnable("OFSwitch13Port", LOG_LEVEL_INFO);
    // LogComponentEnable("OFSwitch13Device", LOG_LEVEL_INFO);
    // LogComponentEnable("OFSwitch13LearningController", LOG_LEVEL_INFO);

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

    // 网络设备节点：3台交换机、1台控制器
    Ptr<Node> sw1 = CreateObject<Node>();
    Ptr<Node> sw2 = CreateObject<Node>();
    Ptr<Node> sw3 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    // 有线链路配置
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer apCsmaDevsA, apCsmaDevsB, apCsmaDevsC;    // AP设备有线接口
    NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports; // 交换机端口设备

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

    // wifi配置部分
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n_2_4GHZ);
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
    WifiMacHelper mac; // 逻辑可复用

    // --------------------------
    // 交换机之间AdHoc WiFi无线连接配置（替换原CSMA有线连接）
    // 使用VirtualNetDevice包装WiFi设备以兼容OpenFlow
    // --------------------------
    // 创建switch间无线互连专用WiFi信道
    // 使用默认信道配置（更宽松的传播模型）以确保switch间通信
    YansWifiChannelHelper switchChannelHelper = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> switchChannel = switchChannelHelper.Create();

    // 配置PHY (使用更高功率确保可靠连接)
    YansWifiPhyHelper switchPhy;

    switchPhy.SetChannel(switchChannel);
    switchPhy.Set("ChannelWidth", UintegerValue(40));
    switchPhy.Set("TxPowerStart", DoubleValue(33.0)); // 提高功率到30dBm (1W)
    switchPhy.Set("TxPowerEnd", DoubleValue(33.0));
    // 启用 2x2 MIMO（需要 802.11n 或 802.11ac 标准）
    switchPhy.Set("Antennas", UintegerValue(4));
    switchPhy.Set("MaxSupportedTxSpatialStreams", UintegerValue(4));
    switchPhy.Set("MaxSupportedRxSpatialStreams", UintegerValue(4));
    switchPhy.Set("TxGain", DoubleValue(10.0));
    switchPhy.Set("RxGain", DoubleValue(10.0));
    // 配置AdHoc MAC
    Config::SetDefault("ns3::WifiMacQueue::MaxSize", StringValue("2000p"));
    Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(Seconds(10.0)));
    WifiMacHelper switchMac;
    switchMac.SetType("ns3::AdhocWifiMac");

    // 为三个交换机创建WiFi设备

    NetDeviceContainer sw1WifiDev = wifi.Install(switchPhy, switchMac, sw1);
    NetDeviceContainer sw2WifiDev = wifi.Install(switchPhy, switchMac, sw2);
    NetDeviceContainer sw3WifiDev = wifi.Install(switchPhy, switchMac, sw3);

    NetDeviceContainer switchWifiArray[3] = {sw1WifiDev, sw2WifiDev, sw3WifiDev};
    Ptr<Node> switchNodeArray[3] = {sw1, sw2, sw3};

    // 创建VirtualNetDevice作为OpenFlow端口（包装WiFi设备）
    // VirtualNetDevice发送时 -> 通过WiFi设备发送
    // WiFi设备接收时 -> 注入到VirtualNetDevice
    NetDeviceContainer switchVirtualDevs;
    for (uint32_t i = 0; i < 3; ++i)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(switchWifiArray[i].Get(0));
        Ptr<Node> switchNode = switchNodeArray[i];
        std::string switchName = (i == 0) ? "sw1" : ((i == 1) ? "sw2" : "sw3");
        // 注册到 map
        g_switchNameMap[wifiDev] = switchName;

        // 创建VirtualNetDevice
        Ptr<VirtualNetDevice> virtualDev = CreateObject<VirtualNetDevice>();
        virtualDev->SetAddress(wifiDev->GetAddress());
        virtualDev->SetSupportsSendFrom(true);
        virtualDev->SetSendCallback(MakeCallback(&WifiNetDevice::SendFrom, wifiDev));

        switchNode->AddDevice(virtualDev);
        switchVirtualDevs.Add(virtualDev);

        wifiDev->SetPromiscReceiveCallback(
            MakeBoundCallback(&WifiToVirtualDevForward, virtualDev));
    }

    // 将VirtualNetDevice添加到交换机端口容器（而不是WiFi设备）
    sw1Devsports.Add(switchVirtualDevs.Get(0)); // sw1的虚拟设备
    sw2Devsports.Add(switchVirtualDevs.Get(1)); // sw2的虚拟设备
    sw3Devsports.Add(switchVirtualDevs.Get(2)); // sw3的虚拟设备

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

    // 在这里添加AP应用创建代码：
    // 为域A的AP节点创建应用
    for (uint32_t i = 0; i < ApA.GetN(); ++i)
    {
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

    // 交换机 (位置调整以确保WiFi覆盖范围内互连)
    Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
    swPos->Add(Vector(-50, 150, 0)); // sw1
    swPos->Add(Vector(50, 150, 0));  // sw2
    swPos->Add(Vector(0, 150, 0));   // sw3
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

    // 配置交换机互联端口映射
    // train4: sw1 有 2 个 CSMA 端口，骨干 WiFi VND 在 Port 3
    controllerApp->SetSwitchPortMapping(1, 2, 3);  // sw1→sw2 走 port 3
    controllerApp->SetSwitchPortMapping(1, 3, 3);  // sw1→sw3 走 port 3
    controllerApp->SetSwitchPortMapping(2, 1, 2);  // sw2→sw1 走 port 2
    controllerApp->SetSwitchPortMapping(2, 3, 2);  // sw2→sw3 走 port 2
    controllerApp->SetSwitchPortMapping(3, 1, 2);  // sw3→sw1 走 port 2
    controllerApp->SetSwitchPortMapping(3, 2, 2);  // sw3→sw2 走 port 2

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

    // 为AP节点安装协议栈（AP作为网关需要IP协议栈）
    stack.Install(ApA);
    stack.Install(ApB);
    stack.Install(ApC);

    // 为AP启用IP转发（AP作为网关需要转发功能）
    for (uint32_t i = 0; i < ApA.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = ApA.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }
    Ptr<Ipv4> ipv4ApB = ApB.Get(0)->GetObject<Ipv4>();
    ipv4ApB->SetAttribute("IpForward", BooleanValue(true));
    Ptr<Ipv4> ipv4ApC = ApC.Get(0)->GetObject<Ipv4>();
    ipv4ApC->SetAttribute("IpForward", BooleanValue(true));

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true)); // 启用转发
    }

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
        // 为AP分配地址（10.1.1.5-10.1.1.6）
        ifApA = ipv4.Assign(apCsmaDevsA);
    }

    Ipv4InterfaceContainer ifB, ifApB;
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    {
        NetDeviceContainer netB = NetDeviceContainer();
        // 先为主机分配地址（10.2.1.1-10.2.1.2）
        for (uint32_t i = 0; i < staWifiDevsB.GetN(); ++i)
            netB.Add(staWifiDevsB.Get(i));
        ifB = ipv4.Assign(netB);
        // 为AP分配地址（10.2.1.3）
        ifApB = ipv4.Assign(apCsmaDevsB);
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
        ifC = ipv4.Assign(netC);
        // 为AP分配地址（10.3.1.7）
        ifApC = ipv4.Assign(apCsmaDevsC);
    } // wifi 网络

    // 为switch虚拟接口分配IP地址（switch间无线互连网段）
    // 注意：使用VirtualNetDevice而不是WiFi设备，因为VirtualNetDevice是OpenFlow端口
    Ipv4InterfaceContainer switchWifiIfaces;
    {
        Ipv4AddressHelper switchIpHelper;
        switchIpHelper.SetBase("10.10.1.0", "255.255.255.0");
        switchWifiIfaces = switchIpHelper.Assign(switchVirtualDevs);
    }

    // 打印switch虚拟接口信息
    std::cout << "Switch Virtual Interfaces (WiFi-backed):" << std::endl;
    std::cout << "  sw1: " << switchWifiIfaces.GetAddress(0) << std::endl;
    std::cout << "  sw2: " << switchWifiIfaces.GetAddress(1) << std::endl;
    std::cout << "  sw3: " << switchWifiIfaces.GetAddress(2) << std::endl;
    ;

    // 配置主机默认路由指向本域AP
    // 域A主机默认路由（指向AP的IP，这里用第一个AP作为主网关）
    Ipv4Address apAGateway = ifApA.GetAddress(0); // 10.1.1.5
    for (uint32_t i = 0; i < StaA.GetN(); ++i)
    {
        Ptr<Node> h = StaA.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(apAGateway, 1);
    }

    // 域B主机默认路由（指向域B AP的IP：10.2.1.3）
    Ipv4Address apBGateway = ifApB.GetAddress(0);
    for (uint32_t i = 0; i < StaB.GetN(); ++i)
    {
        Ptr<Node> h = StaB.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(apBGateway, 1);
    }

    // 域C主机默认路由（指向域C AP的IP：10.3.1.4）
    Ipv4Address apCGateway = ifApC.GetAddress(0);
    for (uint32_t i = 0; i < StaC.GetN(); ++i)
    {
        Ptr<Node> h = StaC.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(apCGateway, 1);
    }

    std::cout << "交换机和AP路由配置完成" << std::endl;

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

    std::cout << "sw2 ports: " << sw2Devsports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw2Devsports.GetN(); ++i)
        std::cout << "Port " << i << ": " << sw2Devsports.Get(i)->GetAddress() << std::endl;

    std::cout << "sw3 ports: " << sw3Devsports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw3Devsports.GetN(); ++i)
        std::cout << "Port " << i << ": " << sw3Devsports.Get(i)->GetAddress() << std::endl;

    // 启用pcap追踪
    if (true)
    {
        // ---- C 域抓包 ----
        phyC.EnablePcap("C_adhoc", adhocDevsC); // C 域 AdHoc 接口
        phyC.EnablePcap("C_sta", staWifiDevsC); // C 域 STA

        // ---- A/B 域抓包 ----

        phyA.EnablePcap("A_ap", apWifiDevsA);   // A 域 AP
        phyA.EnablePcap("A_sta", staWifiDevsA); // A 域 STA
        phyB.EnablePcap("B_ap", apWifiDevsB);   // B 域 AP
        phyC.EnablePcap("C_ap", apWifiDevsC);
        phyB.EnablePcap("B_sta", staWifiDevsB); // B 域 STA

        // ---- OpenFlow / 交换机抓包 ----
        of13Helper->EnableOpenFlowPcap("openflow-interdomain");
        of13Helper->EnableDatapathStats("switch-stats");

        csma.EnablePcap("cmsac-switch", sw3Devsports);
        // Switch WiFi接口抓包（switch间无线通信）
        switchPhy.EnablePcap("switch111-wifi", sw1WifiDev);
        switchPhy.EnablePcap("switch222-wifi", sw2WifiDev);
        switchPhy.EnablePcap("switch333-wifi", sw3WifiDev);

        // A/B/C 域 AP 的 CSMA 接口抓包
        csma.EnablePcap("A_domain_ap", apCsmaDevsA);
        csma.EnablePcap("B_domain_ap", apCsmaDevsB);
        csma.EnablePcap("C_domain_ap", apCsmaDevsC);
    }

    // // 创建ping应用，测试StaB[1] -> StaC[1]的连通性
    // V4PingHelper pingHelper(ifC.GetAddress(1)); // StaC[2]的IP地址
    // pingHelper.SetAttribute("Verbose", BooleanValue(true));

    // // 在StaB[1]上安装ping应用
    // ApplicationContainer pingApps = pingHelper.Install(StaB.Get(1));
    // pingApps.Start(Seconds(6.0));          // 【修复】延迟启动，等待流表规则建立完成
    // pingApps.Stop(Seconds(simTime - 1.0)); // 在仿真结束前2秒停止

    // 应用层udp发送
    uint16_t port0 = 9;
    // uint16_t port1 = 10;
    // uint16_t port2 = 11;
    // uint16_t port3 = 12;

    // --- Flow0: StaA[1] -> StaC[2] ---
    OnOffHelper onoff0("ns3::UdpSocketFactory", Address());
    onoff0.SetAttribute("DataRate", StringValue("2Mbps")); // 原1Mbps
    onoff0.SetAttribute("PacketSize", UintegerValue(1024));
    // 【新增这两行，干掉默认的间歇发包机制】
    onoff0.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff0.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff0.SetAttribute("StartTime", TimeValue(Seconds(10))); // 时间设置为接口启动后
    onoff0.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // 设置目标地址
    InetSocketAddress dst0(ifC.GetAddress(2), port0);
    onoff0.SetAttribute("Remote", AddressValue(dst0));

    ApplicationContainer app0 = onoff0.Install(StaA.Get(1));

    // // --- Flow1: StaA[3] -> StaB[1] ---
    // OnOffHelper onoff1("ns3::UdpSocketFactory", Address());
    // onoff1.SetAttribute("DataRate", StringValue("0.4Mbps"));
    // onoff1.SetAttribute("PacketSize", UintegerValue(1024));
    // // 【新增这两行，干掉默认的间歇发包机制】
    // onoff1.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    // onoff1.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    // onoff1.SetAttribute("StartTime", TimeValue(Seconds(10)));
    // onoff1.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // InetSocketAddress dst1(ifB.GetAddress(1), port1);
    // onoff1.SetAttribute("Remote", AddressValue(dst1));

    // ApplicationContainer app1 = onoff1.Install(StaA.Get(3));

    // // --- Flow2: StaB[0] -> StaC[1] ---
    // OnOffHelper onoff2("ns3::UdpSocketFactory", Address());
    // onoff2.SetAttribute("DataRate", StringValue("0.4Mbps"));
    // onoff2.SetAttribute("PacketSize", UintegerValue(1024));
    // // 【新增这两行，干掉默认的间歇发包机制】
    // onoff2.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    // onoff2.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    // onoff2.SetAttribute("StartTime", TimeValue(Seconds(10)));
    // onoff2.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // InetSocketAddress dst2(ifC.GetAddress(1), port2);
    // onoff2.SetAttribute("Remote", AddressValue(dst2));

    // ApplicationContainer app2 = onoff2.Install(StaB.Get(0));

    // // --- Flow3: StaC[0] -> StaC[2] ---
    // OnOffHelper onoff3("ns3::UdpSocketFactory", Address());
    // onoff3.SetAttribute("DataRate", StringValue("0.4Mbps"));
    // onoff3.SetAttribute("PacketSize", UintegerValue(1024));
    // // 【新增这两行，干掉默认的间歇发包机制】
    // onoff3.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    // onoff3.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    // onoff3.SetAttribute("StartTime", TimeValue(Seconds(10)));
    // onoff3.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // InetSocketAddress dst3(ifC.GetAddress(5), port3);
    // onoff3.SetAttribute("Remote", AddressValue(dst3));

    // Ipv4Address srcIp = ifC.GetAddress(3); // device 1 的 IP

    // InetSocketAddress local(srcIp);
    // onoff3.SetAttribute("Local", AddressValue(local));

    // ApplicationContainer app3 = onoff3.Install(StaC.Get(0));

    //----------------------------
    // adhoc接口开关（C域）
    // for (uint32_t i = 0; i < StaC.GetN(); i++)
    // {
    //     Ptr<Node> node = StaC.Get(i);
    //     Ptr<NetDevice> dev = adhocDevsC.Get(i); // 假设每个 STA 的 AdHoc 接口索引相同

    //     // 0秒时关闭adhoc接口
    //     Simulator::Schedule(Seconds(7.0), &DisableDeviceLogical, node, dev);

    //     // 第 7 秒开启
    //     Simulator::Schedule(Seconds(10.0), &EnableDeviceLogical, node, dev); // 接口设置为3秒开启
    // }

    // 调试信息输出：MAC地址和IP地址
    {
        std::cout << "ap mac and ip" << std::endl;
        // 输出域A的AP设备
        for (uint32_t i = 0; i < ApA.GetN(); ++i)
        {
            Ptr<NetDevice> dev = apWifiDevsA.Get(i); // AP的WiFi接口
            Address addr = dev->GetAddress();
            std::cout << "  ApA[" << i << "] WiFi接口"
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = ApA.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1)
            {
                std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        // 输出域B的AP设备
        Ptr<NetDevice> devB = apWifiDevsB.Get(0);
        Address addrB = devB->GetAddress();
        std::cout << "  ApB[0] WiFi接口"
                  << " -> MAC Address: "
                  << Mac48Address::ConvertFrom(addrB);
        // 输出IP地址
        Ptr<Ipv4> ipv4B = ApB.Get(0)->GetObject<Ipv4>();
        if (ipv4B && ipv4B->GetNInterfaces() > 1)
        {
            std::cout << " -> IP Address: " << ipv4B->GetAddress(1, 0).GetLocal();
        }
        std::cout << std::endl;

        // 输出域C的AP设备
        Ptr<NetDevice> devC = apWifiDevsC.Get(0);
        Address addrC = devC->GetAddress();
        std::cout << "  ApC[0] WiFi接口"
                  << " -> MAC Address: "
                  << Mac48Address::ConvertFrom(addrC);
        // 输出IP地址
        Ptr<Ipv4> ipv4C = ApC.Get(0)->GetObject<Ipv4>();
        if (ipv4C && ipv4C->GetNInterfaces() > 1)
        {
            std::cout << " -> IP Address: " << ipv4C->GetAddress(1, 0).GetLocal();
        }
        std::cout << std::endl;

        std::cout << "sta mac and ip" << std::endl;
        // 输出域A的STA设备
        for (uint32_t i = 0; i < StaA.GetN(); ++i)
        {
            Ptr<NetDevice> dev = staWifiDevsA.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaA[" << i << "] WiFi接口"
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaA.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1)
            {
                std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        // 输出域B的STA设备
        for (uint32_t i = 0; i < StaB.GetN(); ++i)
        {
            Ptr<NetDevice> dev = staWifiDevsB.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaB[" << i << "] WiFi接口"
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaB.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1)
            {
                std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        // 输出域C的STA设备
        for (uint32_t i = 0; i < StaC.GetN(); ++i)
        {
            Ptr<NetDevice> dev = staWifiDevsC.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaC[" << i << "] WiFi接口"
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1)
            {
                std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
            }
            std::cout << std::endl;
        }

        std::cout << "adhoc mac and ip" << std::endl;
        // 输出域C的Adhoc设备
        for (uint32_t i = 0; i < StaC.GetN(); ++i)
        {
            Ptr<NetDevice> dev = adhocDevsC.Get(i);
            Address addr = dev->GetAddress();
            std::cout << "  StaC[" << i << "] Adhoc接口"
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr);
            // 输出IP地址
            Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 2)
            {
                std::cout << " -> IP Address: " << ipv4->GetAddress(2, 0).GetLocal();
            }
            std::cout << std::endl;
        }
    }

    // 向控制器注册网关ARP信息（核心新增功能）
    if (controllerApp != nullptr)
    {
        // 域A网关（AP）ARP条目：IP -> MAC
        for (uint32_t i = 0; i < ApA.GetN(); ++i)
        {
            Ipv4Address apIp = ifApA.GetAddress(i);
            Mac48Address apMac = Mac48Address::ConvertFrom(apCsmaDevsA.Get(i)->GetAddress());
            controllerApp->AddArpEntry(apIp, apMac);
            std::cout << "注册ARP条目：ApA[" << i << "] 网关 " << apIp << " -> " << apMac << std::endl;
        }
        // 域B网关（AP）ARP条目
        Ipv4Address apBIp = ifApB.GetAddress(0);
        Mac48Address apBMac = Mac48Address::ConvertFrom(apCsmaDevsB.Get(0)->GetAddress());
        controllerApp->AddArpEntry(apBIp, apBMac);
        std::cout << "注册ARP条目：ApB[0] 网关 " << apBIp << " -> " << apBMac << std::endl;
        // 域C网关（AP）ARP条目
        Ipv4Address apCIp = ifApC.GetAddress(0);
        Mac48Address apCMac = Mac48Address::ConvertFrom(apCsmaDevsC.Get(0)->GetAddress());
        controllerApp->AddArpEntry(apCIp, apCMac);
        std::cout << "注册ARP条目：ApC[0] 网关 " << apCIp << " -> " << apCMac << std::endl;

        // 注册switch虚拟接口ARP条目（用于switch间无线通信）
        // 注意：VirtualNetDevice使用WiFi设备的MAC地址
        for (uint32_t i = 0; i < switchVirtualDevs.GetN(); ++i)
        {
            Ipv4Address switchIp = switchWifiIfaces.GetAddress(i);
            Mac48Address switchMac = Mac48Address::ConvertFrom(switchVirtualDevs.Get(i)->GetAddress());
            controllerApp->AddArpEntry(switchIp, switchMac);
            std::cout << "注册ARP条目：Switch[" << i << "] 虚拟接口 "
                      << switchIp << " -> " << switchMac << std::endl;
        }
    }

    // 添加获取AP和STA消息的调度函数
    Ptr<OFSwitch13Device> sw1Device = sw1->GetObject<OFSwitch13Device>();
    Ptr<OFSwitch13Device> sw2Device = sw2->GetObject<OFSwitch13Device>();
    Ptr<OFSwitch13Device> sw3Device = sw3->GetObject<OFSwitch13Device>();

    // 【修复】延迟调用，确保OpenFlow连接已完全建立
    if (sw1Device)
    {
        Simulator::Schedule(Seconds(5.0), &OFSwitch13Device::GetApStaMessages, sw1Device);
    }
    if (sw2Device)
    {
        Simulator::Schedule(Seconds(5.0), &OFSwitch13Device::GetApStaMessages, sw2Device);
    }
    if (sw3Device)
    {
        Simulator::Schedule(Seconds(5.0), &OFSwitch13Device::GetApStaMessages, sw3Device);
    }

    // LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_DEBUG);
    // 【修复】延迟设置路由优先级，确保OpenFlow连接已建立
    Simulator::Schedule(Seconds(4.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);

    // 组网模式切换
    // Simulator::Schedule(Seconds(11.5), &OFSwitch13LearningController::CDL, controllerApp);
    // 路由协议
    // Simulator::Schedule(Seconds(11.5), &OFSwitch13LearningController::SetPriorityToAll, controllerApp);

    FlowMonitorHelper flowmonHelper;

    // 只监控 STA 节点
    NodeContainer monitorNodes;
    monitorNodes.Add(StaA);
    monitorNodes.Add(StaB);
    monitorNodes.Add(StaC);

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
