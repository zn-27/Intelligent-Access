// 无线融合 SDN 架构：AP 合并到 Switch，VND 聚合所有无线接口
/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块
 *
 * - 域 A: 4 STA -- sw1 (融合: ApWifiMac + AdhocBackhaul + AdhocEmergency)
 * - 域 B: 2 STA -- sw2 (融合: ApWifiMac + AdhocBackhaul + AdhocEmergency)
 * - 域 C: 3 STA -- sw3 (融合: ApWifiMac + AdhocBackhaul + AdhocEmergency)
 * - Switch 间通过 Adhoc 骨干网三角互连
 * - 所有 STA 双模: StaWifiMac + AdhocWifiMac
 * - 每个 Switch 有 3 个 OpenFlow 数据端口 (VND 包装)
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
#include "ns3/ap-app-module.h"
#include "ns3/blind-connect-app.h"
#include "ns3/blind-connect-helper.h"
#include <cstdint>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <cmath>
using namespace ns3;

// AP 关联 trace 回调（诊断用）
static void
AssocTraceCb(std::string name, uint16_t aid, Mac48Address addr)
{
    std::cout << "[AssocTrace] " << name << " AID=" << aid
              << " STA=" << addr << " at t="
              << Simulator::Now().GetSeconds() << "s" << std::endl;
}

// ---------------------------------------------------------
// 辅助函数：逻辑上下线设备
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

// ---------------------------------------------------------
// WiFi -> VirtualNetDevice 转发函数 (Adhoc 模式)
// WiFi AdHoc 传输 IP 包（无 Ethernet 头），OpenFlow 需要 Ethernet 帧
// ---------------------------------------------------------
bool WifiToVirtualDevForward(Ptr<VirtualNetDevice> vdev, Ptr<NetDevice> dev,
                             Ptr<const Packet> packet, uint16_t protocol,
                             const Address &source, const Address &dest,
                             NetDevice::PacketType packetType)
{
    static int bhFwdCount = 0;
    if (Simulator::Now().GetSeconds() >= 10.0 && bhFwdCount < 20)
    {
        bhFwdCount++;
        std::cout << "[BhFwd] vdev=" << Mac48Address::ConvertFrom(vdev->GetAddress())
                  << " proto=0x" << std::hex << protocol << std::dec
                  << " at t=" << Simulator::Now().GetSeconds() << "s" << std::endl;
    }
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

// ---------------------------------------------------------
// AP WiFi -> VirtualNetDevice 转发函数
// AP 收到 STA 的包，source 是 STA 的 MAC（保留真实源 MAC）
// ---------------------------------------------------------
bool ApWifiToVirtualDevForward(Ptr<VirtualNetDevice> vdev, Ptr<NetDevice> dev,
                                Ptr<const Packet> packet, uint16_t protocol,
                                const Address &source, const Address &dest,
                                NetDevice::PacketType packetType)
{
    Ptr<Packet> packetCopy = packet->Copy();
    Mac48Address srcMac = Mac48Address::ConvertFrom(source);   // STA 的 MAC
    Mac48Address dstMac = Mac48Address::ConvertFrom(vdev->GetAddress());  // AP/switch MAC
    EthernetHeader ethHeader(false);
    ethHeader.SetSource(srcMac);
    ethHeader.SetDestination(dstMac);
    ethHeader.SetLengthType(protocol);
    EthernetTrailer ethTrailer;
    packetCopy->AddHeader(ethHeader);
    packetCopy->AddTrailer(ethTrailer);
    return vdev->Receive(packetCopy, protocol, srcMac, dstMac, packetType);
}

// ---------------------------------------------------------
// FlowMonitor 相关
// ---------------------------------------------------------
std::map<uint32_t, double> lastRxBytes;
std::map<uint32_t, double> lastPacketRtt;

void MonitorFlow(Ptr<FlowMonitor> monitor, FlowMonitorHelper *flowHelper, double interval, std::ofstream *fout)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double now = Simulator::Now().GetSeconds();
    *fout << now;

    std::vector<uint16_t> ports = {9, 10, 11, 12, 13, 14, 15};
    for (auto port : ports)
    {
        FlowId fid = 0;
        for (auto const &flow : stats)
        {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
            if (t.destinationPort == port) { fid = flow.first; break; }
        }

        double throughput = 0.0, lossRate = 0.0, avgRtt = 0.0, jitter = 0.0;
        if (fid != 0 && stats.count(fid))
        {
            FlowMonitor::FlowStats flowStats = stats[fid];
            double rxBytesDelta = flowStats.rxBytes;
            if (lastRxBytes.count(fid)) rxBytesDelta -= lastRxBytes[fid];
            throughput = rxBytesDelta * 8.0 / (interval * 1024.0);
            lastRxBytes[fid] = flowStats.rxBytes;
            if (flowStats.txPackets > 0)
                lossRate = 100.0 * (flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets;
            if (flowStats.rxPackets > 0)
            {
                avgRtt = flowStats.delaySum.GetSeconds() / flowStats.rxPackets * 1000.0;
                double packetRtt = avgRtt;
                if (lastPacketRtt.count(fid)) jitter = std::abs(packetRtt - lastPacketRtt[fid]);
                lastPacketRtt[fid] = packetRtt;
            }
        }
        *fout << "," << throughput << "," << lossRate << "," << avgRtt << "," << jitter;
    }
    *fout << std::endl;
    Simulator::Schedule(Seconds(interval), &MonitorFlow, monitor, flowHelper, interval, fout);
}

// ---------------------------------------------------------
// 域 C Ad-Hoc 邻居发现 + ARP 注入回调
// ---------------------------------------------------------
void InjectAdhocNeighbors(Ptr<SwitchProtocolInfoApp> sw3App,
                           Ptr<OFSwitch13LearningController> controllerApp)
{
    sw3App->CollectAdhocNeighbors();
    auto& msgs = sw3App->GetStaMessages();
    for (const auto& sm : msgs)
    {
        uint8_t buf[6];
        for (int k = 0; k < 6; k++)
            buf[k] = (sm.mac_address >> (40 - 8*k)) & 0xFF;
        Mac48Address mac;
        mac.CopyFrom(buf);
        Ipv4Address ip(sm.ip_address);
        controllerApp->AddArpEntry(ip, mac);
        std::cout << "[C-Adhoc] ARP注入: " << ip << " -> " << mac << std::endl;
    }
}

// ---------------------------------------------------------
// 移动节点状态日志回调
// ---------------------------------------------------------
static void
PrintMobileNodeStatus(Ptr<Node> node, Ptr<BlindConnectApp> app)
{
    Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
    Vector pos = mob->GetPosition();
    const char* netTypeStr = (app->GetCurrentNetType() == (int)BlindConnectApp::ScannedNodeInfo::TYPE_AP)
                             ? "AP" : "Adhoc";
    std::cout << "[Mobile@" << Simulator::Now().GetSeconds() << "s]"
              << " Pos=(" << pos.x << "," << pos.y << ")"
              << " Net=" << netTypeStr
              << " SNR=" << app->GetCurrentSnr() << "dBm"
              << " IP=" << app->GetAssignedIp()
              << std::endl;
    Simulator::Schedule(Seconds(2.0), &PrintMobileNodeStatus, node, app);
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

    // 启用校验和计算（ofswitch13 模块所需）
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    // =========================================================
    // 1. 创建节点
    // =========================================================
    NodeContainer StaA;  StaA.Create(4);
    NodeContainer StaB;  StaB.Create(2);
    NodeContainer StaC;  StaC.Create(3);

    // 移动节点：从最左侧穿越域A→域C→域B
    NodeContainer mobileNode; mobileNode.Create(1);

    NodeContainer wifiStaNodes;
    wifiStaNodes.Add(StaA);
    wifiStaNodes.Add(StaB);
    wifiStaNodes.Add(StaC);
    wifiStaNodes.Add(mobileNode);

    Ptr<Node> sw1 = CreateObject<Node>();
    Ptr<Node> sw2 = CreateObject<Node>();
    Ptr<Node> sw3 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    // =========================================================
    // 2. WiFi 基础配置
    // =========================================================
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n_2_4GHZ);
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
    WifiMacHelper mac;

    Config::SetDefault("ns3::WifiMacQueue::MaxSize", StringValue("2000p"));
    Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(Seconds(10.0)));

    // =========================================================
    // 3. Switch 间骨干 Adhoc WiFi (Port 2 基础)
    // =========================================================
    YansWifiChannelHelper switchChannelHelper = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> switchChannel = switchChannelHelper.Create();

    YansWifiPhyHelper switchPhy;
    switchPhy.SetChannel(switchChannel);
    switchPhy.Set("ChannelWidth", UintegerValue(40));
    switchPhy.Set("TxPowerStart", DoubleValue(33.0));
    switchPhy.Set("TxPowerEnd", DoubleValue(33.0));
    switchPhy.Set("Antennas", UintegerValue(4));
    switchPhy.Set("MaxSupportedTxSpatialStreams", UintegerValue(4));
    switchPhy.Set("MaxSupportedRxSpatialStreams", UintegerValue(4));
    switchPhy.Set("TxGain", DoubleValue(10.0));
    switchPhy.Set("RxGain", DoubleValue(10.0));

    WifiMacHelper switchMac;
    switchMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer sw1BackhaulDev = wifi.Install(switchPhy, switchMac, sw1);
    NetDeviceContainer sw2BackhaulDev = wifi.Install(switchPhy, switchMac, sw2);
    NetDeviceContainer sw3BackhaulDev = wifi.Install(switchPhy, switchMac, sw3);

    // =========================================================
    // 4. 域内专属 Adhoc WiFi (Port 3 基础) -- 每域独立信道
    // =========================================================
    WifiMacHelper domainAdhocMac;
    domainAdhocMac.SetType("ns3::AdhocWifiMac", "Ssid", SsidValue(Ssid("Adhoc-Net")));

    // 域 A 专属 Adhoc 信道
    YansWifiChannelHelper domainChHelperA = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> domainChA = domainChHelperA.Create();
    YansWifiPhyHelper domainPhyA;
    domainPhyA.SetChannel(domainChA);
    domainPhyA.Set("TxPowerStart", DoubleValue(33.0));
    domainPhyA.Set("TxPowerEnd", DoubleValue(33.0));
    domainPhyA.Set("TxGain", DoubleValue(10.0));
    domainPhyA.Set("RxGain", DoubleValue(10.0));

    NetDeviceContainer sw1EmDev = wifi.Install(domainPhyA, domainAdhocMac, sw1);
    NetDeviceContainer staEmDevsA = wifi.Install(domainPhyA, domainAdhocMac, StaA);

    // 域 B 专属 Adhoc 信道
    YansWifiChannelHelper domainChHelperB = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> domainChB = domainChHelperB.Create();
    YansWifiPhyHelper domainPhyB;
    domainPhyB.SetChannel(domainChB);
    domainPhyB.Set("TxPowerStart", DoubleValue(33.0));
    domainPhyB.Set("TxPowerEnd", DoubleValue(33.0));
    domainPhyB.Set("TxGain", DoubleValue(10.0));
    domainPhyB.Set("RxGain", DoubleValue(10.0));

    NetDeviceContainer sw2EmDev = wifi.Install(domainPhyB, domainAdhocMac, sw2);
    NetDeviceContainer staEmDevsB = wifi.Install(domainPhyB, domainAdhocMac, StaB);

    // 域 C 专属 Adhoc 信道
    YansWifiChannelHelper domainChHelperC = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> domainChC = domainChHelperC.Create();
    YansWifiPhyHelper domainPhyC;
    domainPhyC.SetChannel(domainChC);
    domainPhyC.Set("TxPowerStart", DoubleValue(33.0));
    domainPhyC.Set("TxPowerEnd", DoubleValue(33.0));
    domainPhyC.Set("TxGain", DoubleValue(10.0));
    domainPhyC.Set("RxGain", DoubleValue(10.0));

    NetDeviceContainer sw3EmDev = wifi.Install(domainPhyC, domainAdhocMac, sw3);
    NetDeviceContainer staEmDevsC = wifi.Install(domainPhyC, domainAdhocMac, StaC);
    // 域C Adhoc 锁定 ch11，与 mobileEmDev 同频（SSID="Adhoc-Net" 已在 MAC Helper 创建时设好）
    DynamicCast<WifiNetDevice>(sw3EmDev.Get(0))->GetPhy()->SetOperatingChannel(11, 2462, 20);

    // =========================================================
    // 5. 共享 AP WiFi 信道 (三域共用同一物理介质，通过频道号 ch1/6/11 隔离)
    // =========================================================
    Ptr<YansWifiChannel> apChannel = channel.Create();
    YansWifiPhyHelper phyAp;
    phyAp.SetChannel(apChannel);
    phyAp.Set("TxPowerStart", DoubleValue(33.0));
    phyAp.Set("TxPowerEnd", DoubleValue(33.0));
    phyAp.Set("TxGain", DoubleValue(10.0));
    phyAp.Set("RxGain", DoubleValue(10.0));

    // 域 A: SSID "A", AP 频道 1
    Ssid ssidA = Ssid("A");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidA));
    NetDeviceContainer staWifiDevsA = wifi.Install(phyAp, mac, StaA);
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidA));
    NetDeviceContainer apWifiDevsA = wifi.Install(phyAp, mac, sw1);

    // 域 B: SSID "B", AP 频道 6
    Ssid ssidB = Ssid("B");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidB));
    NetDeviceContainer staWifiDevsB = wifi.Install(phyAp, mac, StaB);
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidB));
    NetDeviceContainer apWifiDevsB = wifi.Install(phyAp, mac, sw2);

    // 域 C: SSID "C", AP 频道 11 (GATEWAY伪信标也在此频道发送)
    Ssid ssidC = Ssid("C");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidC));
    NetDeviceContainer staWifiDevsC = wifi.Install(phyAp, mac, StaC);
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidC));
    NetDeviceContainer apWifiDevsC = wifi.Install(phyAp, mac, sw3);

    // 设置各域 AP 的工作频道: A→ch1, B→ch6, C→ch11
    DynamicCast<WifiNetDevice>(apWifiDevsA.Get(0))->GetPhy()->SetOperatingChannel(1, 2412, 20);
    DynamicCast<WifiNetDevice>(apWifiDevsB.Get(0))->GetPhy()->SetOperatingChannel(6, 2437, 20);
    DynamicCast<WifiNetDevice>(apWifiDevsC.Get(0))->GetPhy()->SetOperatingChannel(11, 2462, 20);
    // 域内STA也锁定对应频道
    for (uint32_t i = 0; i < staWifiDevsA.GetN(); ++i)
        DynamicCast<WifiNetDevice>(staWifiDevsA.Get(i))->GetPhy()->SetOperatingChannel(1, 2412, 20);
    for (uint32_t i = 0; i < staWifiDevsB.GetN(); ++i)
        DynamicCast<WifiNetDevice>(staWifiDevsB.Get(i))->GetPhy()->SetOperatingChannel(6, 2437, 20);
    for (uint32_t i = 0; i < staWifiDevsC.GetN(); ++i)
        DynamicCast<WifiNetDevice>(staWifiDevsC.Get(i))->GetPhy()->SetOperatingChannel(11, 2462, 20);

    // =========================================================
    // 7b. 移动节点 WiFi 设备 (StaWifiMac + AdhocWifiMac)
    //     StaWifiMac 共享AP信道跳频扫描 ch1/6/11 全部AP
    //     AdhocWifiMac 在域C专属Adhoc信道 (domainChC，与GATEWAY Adhoc设备同信道)
    //     GATEWAY 的 AdhocWifiMac 已设置SSID并发送真实IBSS信标，移动节点可完成同步
    // =========================================================
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(Ssid("Dummy")));
    NetDeviceContainer mobileStaDev = wifi.Install(phyAp, mac, mobileNode);
    NetDeviceContainer mobileEmDev  = wifi.Install(domainPhyC, domainAdhocMac, mobileNode);
    DynamicCast<WifiNetDevice>(mobileEmDev.Get(0))->GetPhy()->SetOperatingChannel(11, 2462, 20);

    // =========================================================
    // 8. 为每个 Switch 创建 3 个 VirtualNetDevice (Port 1/2/3)
    // =========================================================
    struct SwitchVnds {
        Ptr<VirtualNetDevice> apVnd;      // Port 1: ApWifiMac
        Ptr<VirtualNetDevice> backhaulVnd; // Port 2: Adhoc 骨干
        Ptr<VirtualNetDevice> emVnd;       // Port 3: Adhoc 紧急
    };

    auto createVndForSwitch = [](Ptr<Node> switchNode,
                                  Ptr<WifiNetDevice> apDev,
                                  Ptr<WifiNetDevice> bhDev,
                                  Ptr<WifiNetDevice> emDev) -> SwitchVnds {
        SwitchVnds vnds;

        // Port 1: AP VirtualNetDevice
        vnds.apVnd = CreateObject<VirtualNetDevice>();
        vnds.apVnd->SetAddress(apDev->GetAddress());
        vnds.apVnd->SetSupportsSendFrom(true);
        vnds.apVnd->SetSendCallback(MakeCallback(&WifiNetDevice::SendFrom, apDev));
        switchNode->AddDevice(vnds.apVnd);
        apDev->SetPromiscReceiveCallback(
            MakeBoundCallback(&ApWifiToVirtualDevForward, vnds.apVnd));

        // Port 2: Backhaul VirtualNetDevice
        vnds.backhaulVnd = CreateObject<VirtualNetDevice>();
        vnds.backhaulVnd->SetAddress(bhDev->GetAddress());
        vnds.backhaulVnd->SetSupportsSendFrom(true);
        vnds.backhaulVnd->SetSendCallback(MakeCallback(&WifiNetDevice::SendFrom, bhDev));
        switchNode->AddDevice(vnds.backhaulVnd);
        bhDev->SetPromiscReceiveCallback(
            MakeBoundCallback(&WifiToVirtualDevForward, vnds.backhaulVnd));

        // Port 3: Emergency VirtualNetDevice
        vnds.emVnd = CreateObject<VirtualNetDevice>();
        vnds.emVnd->SetAddress(emDev->GetAddress());
        vnds.emVnd->SetSupportsSendFrom(true);
        vnds.emVnd->SetSendCallback(MakeCallback(&WifiNetDevice::SendFrom, emDev));
        switchNode->AddDevice(vnds.emVnd);
        emDev->SetPromiscReceiveCallback(
            MakeBoundCallback(&WifiToVirtualDevForward, vnds.emVnd));

        return vnds;
    };

    SwitchVnds sw1Vnds = createVndForSwitch(sw1,
        DynamicCast<WifiNetDevice>(apWifiDevsA.Get(0)),
        DynamicCast<WifiNetDevice>(sw1BackhaulDev.Get(0)),
        DynamicCast<WifiNetDevice>(sw1EmDev.Get(0)));

    SwitchVnds sw2Vnds = createVndForSwitch(sw2,
        DynamicCast<WifiNetDevice>(apWifiDevsB.Get(0)),
        DynamicCast<WifiNetDevice>(sw2BackhaulDev.Get(0)),
        DynamicCast<WifiNetDevice>(sw2EmDev.Get(0)));

    SwitchVnds sw3Vnds = createVndForSwitch(sw3,
        DynamicCast<WifiNetDevice>(apWifiDevsC.Get(0)),
        DynamicCast<WifiNetDevice>(sw3BackhaulDev.Get(0)),
        DynamicCast<WifiNetDevice>(sw3EmDev.Get(0)));

    // =========================================================
    // 9. 构建 OpenFlow 端口容器 (Port 1/2/3 per switch)
    // =========================================================
    NetDeviceContainer sw1Ports, sw2Ports, sw3Ports;
    sw1Ports.Add(sw1Vnds.apVnd);      sw1Ports.Add(sw1Vnds.backhaulVnd);  sw1Ports.Add(sw1Vnds.emVnd);
    sw2Ports.Add(sw2Vnds.apVnd);      sw2Ports.Add(sw2Vnds.backhaulVnd);  sw2Ports.Add(sw2Vnds.emVnd);
    sw3Ports.Add(sw3Vnds.apVnd);      sw3Ports.Add(sw3Vnds.backhaulVnd);  sw3Ports.Add(sw3Vnds.emVnd);

    // =========================================================
    // 10. OpenFlow 控制器与交换机配置
    // =========================================================
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    of13Helper->InstallController(controllerNode);
    of13Helper->InstallSwitch(sw1, sw1Ports);
    of13Helper->InstallSwitch(sw2, sw2Ports);
    of13Helper->InstallSwitch(sw3, sw3Ports);
    of13Helper->CreateOpenFlowChannels();

    auto get = of13Helper->GetController();
    Ptr<OFSwitch13LearningController> controllerApp =
        DynamicCast<OFSwitch13LearningController>(get.Get(0));

    // 配置交换机互联端口映射
    // train4-new: 所有交换机骨干 VND 在 Port 2
    controllerApp->SetSwitchPortMapping(1, 2, 2);
    controllerApp->SetSwitchPortMapping(1, 3, 2);
    controllerApp->SetSwitchPortMapping(2, 1, 2);
    controllerApp->SetSwitchPortMapping(2, 3, 2);
    controllerApp->SetSwitchPortMapping(3, 1, 2);
    controllerApp->SetSwitchPortMapping(3, 2, 2);

    // =========================================================
    // 11. Internet 协议栈
    // =========================================================
    Ipv4StaticRoutingHelper staticRoutingHelper;

    // 静态 STA：AODV+静态路由（OLSR 去掉，避免 0.0.0.0 断言崩溃）
    InternetStackHelper stack2;
    Ipv4ListRoutingHelper list;
    AodvHelper aodv;
    list.Add(aodv, 10);
    list.Add(staticRoutingHelper, 100);
    stack2.SetRoutingHelper(list);
    NodeContainer staticStaNodes;
    staticStaNodes.Add(StaA);
    staticStaNodes.Add(StaB);
    staticStaNodes.Add(StaC);
    stack2.Install(staticStaNodes);

    // 移动节点：纯静态路由
    InternetStackHelper stackMobile;
    stackMobile.SetRoutingHelper(staticRoutingHelper);
    stackMobile.Install(mobileNode);

    // Switch 节点不需要额外协议栈 (InstallSwitch 已安装基础栈)
    // 启用 IP 转发
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }

    // =========================================================
    // 12. IP 地址分配
    // =========================================================
    Ipv4AddressHelper ipv4;

    // 域 A: STA + AP VND (Port 1) + 原生AP设备(BlindConnectApp socket绑定)
    Ipv4InterfaceContainer ifStaA, ifApA;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    {
        NetDeviceContainer netA;
        for (uint32_t i = 0; i < staWifiDevsA.GetN(); ++i)
            netA.Add(staWifiDevsA.Get(i));
        ifStaA = ipv4.Assign(netA);
        ifApA = ipv4.Assign(NetDeviceContainer(sw1Vnds.apVnd));  // 网关 IP 在 Port 1 VND
        ipv4.Assign(NetDeviceContainer(apWifiDevsA.Get(0)));     // 原生AP设备也需要IP
    }

    // 域 B: STA + AP VND (Port 1) + 原生AP设备
    Ipv4InterfaceContainer ifStaB, ifApB;
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    {
        NetDeviceContainer netB;
        for (uint32_t i = 0; i < staWifiDevsB.GetN(); ++i)
            netB.Add(staWifiDevsB.Get(i));
        ifStaB = ipv4.Assign(netB);
        ifApB = ipv4.Assign(NetDeviceContainer(sw2Vnds.apVnd));
        ipv4.Assign(NetDeviceContainer(apWifiDevsB.Get(0)));
    }

    // 域 C: STA + AP VND (Port 1) + 原生AP设备
    Ipv4InterfaceContainer ifStaC, ifApC;
    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    {
        NetDeviceContainer netC;
        for (uint32_t i = 0; i < staWifiDevsC.GetN(); ++i)
            netC.Add(staWifiDevsC.Get(i));
        ifStaC = ipv4.Assign(netC);
        ifApC = ipv4.Assign(NetDeviceContainer(sw3Vnds.apVnd));
        ipv4.Assign(NetDeviceContainer(apWifiDevsC.Get(0)));
    }

    // 移动节点: StaWifiMac=0.0.0.0(动态获取), AdhocWifiMac=预分配(临时)
    {
        Ptr<Ipv4> ipv4h = mobileNode.Get(0)->GetObject<Ipv4>();
        int32_t ifIdx = ipv4h->GetInterfaceForDevice(mobileStaDev.Get(0));
        if (ifIdx >= 0) ipv4h->AddAddress(ifIdx, Ipv4InterfaceAddress(Ipv4Address("0.0.0.0"), Ipv4Mask("/0")));
        ifIdx = ipv4h->GetInterfaceForDevice(mobileEmDev.Get(0));
        if (ifIdx >= 0) ipv4h->AddAddress(ifIdx, Ipv4InterfaceAddress(Ipv4Address("10.100.3.100"), Ipv4Mask("255.255.255.0")));
    }

    // 骨干回传: Port 2 VND
    Ipv4InterfaceContainer switchBackhaulIfaces;
    {
        Ipv4AddressHelper switchIpHelper;
        switchIpHelper.SetBase("10.10.1.0", "255.255.255.0");
        NetDeviceContainer bhVnds;
        bhVnds.Add(sw1Vnds.backhaulVnd);
        bhVnds.Add(sw2Vnds.backhaulVnd);
        bhVnds.Add(sw3Vnds.backhaulVnd);
        switchBackhaulIfaces = switchIpHelper.Assign(bhVnds);
    }

    // 域 A 专属 Adhoc 子网: 10.100.1.0/24
    Ipv4InterfaceContainer emIfacesA;
    {
        Ipv4AddressHelper emIpA;
        emIpA.SetBase("10.100.1.0", "255.255.255.0");
        NetDeviceContainer emDevsA;
        emDevsA.Add(sw1Vnds.emVnd);
        for (uint32_t i = 0; i < staEmDevsA.GetN(); ++i)
            emDevsA.Add(staEmDevsA.Get(i));
        emIfacesA = emIpA.Assign(emDevsA);
    }

    // 域 B 专属 Adhoc 子网: 10.100.2.0/24
    Ipv4InterfaceContainer emIfacesB;
    {
        Ipv4AddressHelper emIpB;
        emIpB.SetBase("10.100.2.0", "255.255.255.0");
        NetDeviceContainer emDevsB;
        emDevsB.Add(sw2Vnds.emVnd);
        for (uint32_t i = 0; i < staEmDevsB.GetN(); ++i)
            emDevsB.Add(staEmDevsB.Get(i));
        emIfacesB = emIpB.Assign(emDevsB);
    }

    // 域 C 专属 Adhoc 子网: 10.100.3.0/24
    Ipv4InterfaceContainer emIfacesC;
    {
        Ipv4AddressHelper emIpC;
        emIpC.SetBase("10.100.3.0", "255.255.255.0");
        NetDeviceContainer emDevsC;
        emDevsC.Add(sw3Vnds.emVnd);
        for (uint32_t i = 0; i < staEmDevsC.GetN(); ++i)
            emDevsC.Add(staEmDevsC.Get(i));
        emIfacesC = emIpC.Assign(emDevsC);
    }

    // STA 默认路由指向 AP VND 网关
    Ipv4Address apAGateway = ifApA.GetAddress(0);
    for (uint32_t i = 0; i < StaA.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4h = StaA.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> sr = staticRoutingHelper.GetStaticRouting(ipv4h);
        sr->SetDefaultRoute(apAGateway, 1);
    }

    Ipv4Address apBGateway = ifApB.GetAddress(0);
    for (uint32_t i = 0; i < StaB.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4h = StaB.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> sr = staticRoutingHelper.GetStaticRouting(ipv4h);
        sr->SetDefaultRoute(apBGateway, 1);
    }

    // 域 C STA 默认路由 → sw3 的 Ad-Hoc VND 网关（初始无中心模式）
    Ipv4Address emCGateway = emIfacesC.GetAddress(0);  // sw3 emVnd 的 IP
    for (uint32_t i = 0; i < StaC.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4h = StaC.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> sr = staticRoutingHelper.GetStaticRouting(ipv4h);
        // 动态找到 Ad-Hoc 设备的接口索引
        for (uint32_t d = 0; d < StaC.Get(i)->GetNDevices(); d++)
        {
            Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(StaC.Get(i)->GetDevice(d));
            if (wdev && DynamicCast<AdhocWifiMac>(wdev->GetMac()))
            {
                uint32_t ifIdx = ipv4h->GetInterfaceForDevice(wdev);
                sr->SetDefaultRoute(emCGateway, ifIdx);
                std::cout << "[Route] StaC[" << i << "] default -> " << emCGateway
                          << " via ifIdx=" << ifIdx << std::endl;
                break;
            }
        }
    }

    // 域 A/B：禁用 STA 的 Ad-Hoc 接口（有中心模式）
    // 域 C：启用 Ad-Hoc 接口，禁用 StaWifiMac（初始无中心模式）
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Node> node = wifiStaNodes.Get(i);

        // 跳过移动节点（由 BlindConnectApp 管理）
        if (node == mobileNode.Get(0)) continue;

        // 判断是否属于域 C
        bool isDomainC = false;
        for (uint32_t j = 0; j < StaC.GetN(); ++j)
        {
            if (node == StaC.Get(j)) { isDomainC = true; break; }
        }

        for (uint32_t d = 0; d < node->GetNDevices(); d++)
        {
            Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(node->GetDevice(d));
            if (!wdev) continue;

            Ptr<Ipv4> ipv4Dev = node->GetObject<Ipv4>();
            uint32_t ifIdx = ipv4Dev->GetInterfaceForDevice(wdev);
            if (ifIdx == uint32_t(-1)) continue;

            if (isDomainC)
            {
                // 域 C：Ad-Hoc UP，StaWifiMac DOWN
                if (DynamicCast<AdhocWifiMac>(wdev->GetMac()))
                {
                    ipv4Dev->SetUp(ifIdx);
                    std::cout << "[Init] StaC node Ad-Hoc UP (ifIdx=" << ifIdx << ")" << std::endl;
                }
                else if (DynamicCast<StaWifiMac>(wdev->GetMac()))
                {
                    ipv4Dev->SetDown(ifIdx);
                    std::cout << "[Init] StaC node StaWifiMac DOWN (ifIdx=" << ifIdx << ")" << std::endl;
                }
            }
            else
            {
                // 域 A/B：Ad-Hoc DOWN（原逻辑）
                if (DynamicCast<AdhocWifiMac>(wdev->GetMac()))
                {
                    ipv4Dev->SetDown(ifIdx);
                }
            }
        }
    }

    // 移动节点：两张网卡初始 UP（BlindConnectApp 需要 UP 才能收发）
    {
        Ptr<Node> mNode = mobileNode.Get(0);
        Ptr<Ipv4> ipv4Mob = mNode->GetObject<Ipv4>();
        for (uint32_t d = 0; d < mNode->GetNDevices(); d++) {
            Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(mNode->GetDevice(d));
            if (!wdev) continue;
            uint32_t ifIdx = ipv4Mob->GetInterfaceForDevice(wdev);
            if (ifIdx != uint32_t(-1)) ipv4Mob->SetUp(ifIdx);
        }
    }

    // =========================================================
    // 13. 节点位置配置
    // =========================================================
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // 控制器
    Ptr<ListPositionAllocator> posController = CreateObject<ListPositionAllocator>();
    posController->Add(Vector(0, 0, 0));
    mobility.SetPositionAllocator(posController);
    mobility.Install(controllerNode);

    // Switch 节点（同时是 AP）
    Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
    swPos->Add(Vector(-50, 150, 0));  // sw1 (域 A AP)
    swPos->Add(Vector(50, 150, 0));   // sw2 (域 B AP)
    swPos->Add(Vector(0, 150, 0));    // sw3 (域 C AP)
    mobility.SetPositionAllocator(swPos);
    mobility.Install(sw1);
    mobility.Install(sw2);
    mobility.Install(sw3);

    // 域 A STA
    Ptr<ListPositionAllocator> posA = CreateObject<ListPositionAllocator>();
    posA->Add(Vector(-150, 200, 0));
    posA->Add(Vector(-120, 200, 0));
    posA->Add(Vector(-155, 230, 0));
    posA->Add(Vector(-150, 230, 0));
    mobility.SetPositionAllocator(posA);
    mobility.Install(StaA);

    // 域 B STA
    Ptr<ListPositionAllocator> posB = CreateObject<ListPositionAllocator>();
    posB->Add(Vector(120, 200, 0));
    posB->Add(Vector(150, 230, 0));
    mobility.SetPositionAllocator(posB);
    mobility.Install(StaB);

    // 域 C STA
    Ptr<ListPositionAllocator> posC = CreateObject<ListPositionAllocator>();
    posC->Add(Vector(20.0, 230.0, 0.0));
    posC->Add(Vector(50.0, 210.0, 0.0));
    posC->Add(Vector(-20.0, 230.0, 0.0));
    mobility.SetPositionAllocator(posC);
    mobility.Install(StaC);

    // --- 移动节点：ConstantVelocityMobilityModel，从最左侧水平向右穿越 ---
    {
        MobilityHelper mobMobile;
        mobMobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
        Ptr<ListPositionAllocator> posMobile = CreateObject<ListPositionAllocator>();
        posMobile->Add(Vector(-250.0, 200.0, 0.0));
        mobMobile.SetPositionAllocator(posMobile);
        mobMobile.Install(mobileNode);
        Ptr<ConstantVelocityMobilityModel> cvmm =
            mobileNode.Get(0)->GetObject<ConstantVelocityMobilityModel>();
        cvmm->SetVelocity(Vector(15.0, 0.0, 0.0));  // 向右 15 m/s
        std::cout << "[Mobile] 初始位置: (-250, 200), 速度: (15, 0, 0)" << std::endl;
    }

    // =========================================================
    // 14. ARP 注册到控制器
    // =========================================================
    if (controllerApp != nullptr)
    {
        // 域 A 网关 (sw1 Port 1 VND)
        Ipv4Address apAIp = ifApA.GetAddress(0);
        Mac48Address apAMac = Mac48Address::ConvertFrom(sw1Vnds.apVnd->GetAddress());
        controllerApp->AddArpEntry(apAIp, apAMac);
        std::cout << "注册ARP: sw1 AP " << apAIp << " -> " << apAMac << std::endl;

        // 域 B 网关 (sw2 Port 1 VND)
        Ipv4Address apBIp = ifApB.GetAddress(0);
        Mac48Address apBMac = Mac48Address::ConvertFrom(sw2Vnds.apVnd->GetAddress());
        controllerApp->AddArpEntry(apBIp, apBMac);
        std::cout << "注册ARP: sw2 AP " << apBIp << " -> " << apBMac << std::endl;

        // 域 C 网关 (sw3 Port 1 VND)
        Ipv4Address apCIp = ifApC.GetAddress(0);
        Mac48Address apCMac = Mac48Address::ConvertFrom(sw3Vnds.apVnd->GetAddress());
        controllerApp->AddArpEntry(apCIp, apCMac);
        std::cout << "注册ARP: sw3 AP " << apCIp << " -> " << apCMac << std::endl;

        // Switch 骨干 VND ARP
        for (uint32_t i = 0; i < 3; ++i)
        {
            Ipv4Address switchIp = switchBackhaulIfaces.GetAddress(i);
            Ptr<VirtualNetDevice> bhVnds[] = {sw1Vnds.backhaulVnd, sw2Vnds.backhaulVnd, sw3Vnds.backhaulVnd};
            Mac48Address switchMac = Mac48Address::ConvertFrom(bhVnds[i]->GetAddress());
            controllerApp->AddArpEntry(switchIp, switchMac);
            std::cout << "注册ARP: Switch[" << i << "] 骨干 " << switchIp << " -> " << switchMac << std::endl;
        }

        // 静态 STA ARP 注册
        for (uint32_t i = 0; i < StaA.GetN(); ++i) {
            controllerApp->AddArpEntry(ifStaA.GetAddress(i),
                Mac48Address::ConvertFrom(staWifiDevsA.Get(i)->GetAddress()));
        }
        for (uint32_t i = 0; i < StaB.GetN(); ++i) {
            controllerApp->AddArpEntry(ifStaB.GetAddress(i),
                Mac48Address::ConvertFrom(staWifiDevsB.Get(i)->GetAddress()));
        }
        for (uint32_t i = 0; i < StaC.GetN(); ++i) {
            controllerApp->AddArpEntry(ifStaC.GetAddress(i),
                Mac48Address::ConvertFrom(staWifiDevsC.Get(i)->GetAddress()));
        }

    // =========================================================
    // 15. SwitchProtocolInfoApp 安装（融合架构）
    // =========================================================
    auto installSwitchApp = [](Ptr<Node> switchNode, Ptr<WifiNetDevice> apWifiDev, uint32_t portNo) {
        Ptr<SwitchProtocolInfoApp> app = CreateObject<SwitchProtocolInfoApp>();
        Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apWifiDev->GetMac());
        app->SetApMac(apMac);
        app->SetApPortNo(portNo);
        switchNode->AddApplication(app);
        app->SetStartTime(Seconds(0.0));
        app->SetStopTime(Seconds(30.0));
        return app;
    };

    Ptr<SwitchProtocolInfoApp> sw1App = installSwitchApp(sw1,
        DynamicCast<WifiNetDevice>(apWifiDevsA.Get(0)), 1);
    Ptr<SwitchProtocolInfoApp> sw2App = installSwitchApp(sw2,
        DynamicCast<WifiNetDevice>(apWifiDevsB.Get(0)), 1);
    Ptr<SwitchProtocolInfoApp> sw3App = installSwitchApp(sw3,
        DynamicCast<WifiNetDevice>(apWifiDevsC.Get(0)), 1);

    // sw3 额外配置：Ad-Hoc 邻居发现（域 C 初始无中心模式）
    sw3App->SetAdhocMac(DynamicCast<AdhocWifiMac>(
        DynamicCast<WifiNetDevice>(sw3EmDev.Get(0))->GetMac()));
    sw3App->SetAdhocPortNo(3);  // Port 3 = 域内 Ad-Hoc VND
    sw3App->SetAdhocChannelDevices(staEmDevsC);
    sw3App->SetInitialAdhocMode(true);

    // t=2s：域 C 邻居发现 + ARP 直接注入控制器
    Simulator::Schedule(Seconds(2.0), &InjectAdhocNeighbors, sw3App, controllerApp);

    // =========================================================
    // 15c. BlindConnectApp 安装 — 动态入网与 IP 分配
    // =========================================================

    // --- 辅助：安装 ROLE_AP_SERVER (sw1/sw2, socket绑定原生AP设备) ---
    auto installApServerApp = [&controllerApp](Ptr<Node> switchNode,
                                  Ptr<WifiNetDevice> rawApDev,
                                  Ipv4Address poolBase, Ipv4Address poolStart,
                                  Ipv4Address poolEnd, Ipv4Mask poolMask) {
        Ptr<BlindConnectApp> app = CreateObject<BlindConnectApp>();
        app->SetRole(BlindConnectApp::ROLE_AP_SERVER);
        app->SetStaDevice(rawApDev);
        app->SetAttribute("PoolBase", Ipv4AddressValue(poolBase));
        app->SetAttribute("PoolStart", Ipv4AddressValue(poolStart));
        app->SetAttribute("PoolEnd", Ipv4AddressValue(poolEnd));
        app->SetAttribute("PoolMask", Ipv4MaskValue(poolMask));
        app->SetIpAllocatedCallback([controllerApp](Mac48Address mac, Ipv4Address ip) {
            controllerApp->AddArpEntry(ip, mac);
        });
        switchNode->AddApplication(app);
        app->SetStartTime(Seconds(1.0));
        app->SetStopTime(Seconds(30.0));
        return app;
    };

    // --- 辅助：安装 ROLE_GATEWAY (伪信标走AP网卡, IP_REQUEST监听走Adhoc网卡) ---
    auto installGatewayApp = [&controllerApp](Ptr<Node> switchNode,
                                Ptr<WifiNetDevice> staDev,
                                Ptr<WifiNetDevice> rawEmDev,
                                Ptr<VirtualNetDevice> emVnd,
                                Ipv4Address poolBase, Ipv4Address poolStart,
                                Ipv4Address poolEnd, Ipv4Mask poolMask) {
        Ptr<BlindConnectApp> app = CreateObject<BlindConnectApp>();
        app->SetRole(BlindConnectApp::ROLE_GATEWAY);
        app->SetStaDevice(staDev);               // 伪信标通过AP网卡发送
        app->SetAdhocDevice(rawEmDev);           // IP_REQUEST 通过Adhoc PHY监听
        app->SetSocketAdhocDevice(emVnd);        // IP_OFFER 从VND发送(IP在此)
        app->SetAdhocSsid(Ssid("Adhoc-Net"));
        app->SetAttribute("PoolBase", Ipv4AddressValue(poolBase));
        app->SetAttribute("PoolStart", Ipv4AddressValue(poolStart));
        app->SetAttribute("PoolEnd", Ipv4AddressValue(poolEnd));
        app->SetAttribute("PoolMask", Ipv4MaskValue(poolMask));
        app->SetIpAllocatedCallback([controllerApp](Mac48Address mac, Ipv4Address ip) {
            controllerApp->AddArpEntry(ip, mac);
        });
        switchNode->AddApplication(app);
        app->SetStartTime(Seconds(1.0));
        app->SetStopTime(Seconds(30.0));
        return app;
    };

    // --- 安装到 sw1 (域 A): ROLE_AP_SERVER ---
    installApServerApp(sw1,
        DynamicCast<WifiNetDevice>(apWifiDevsA.Get(0)),
        Ipv4Address("10.1.1.0"), Ipv4Address("10.1.1.100"),
        Ipv4Address("10.1.1.200"), Ipv4Mask("255.255.255.0"));

    // --- 安装到 sw2 (域 B): ROLE_AP_SERVER ---
    installApServerApp(sw2,
        DynamicCast<WifiNetDevice>(apWifiDevsB.Get(0)),
        Ipv4Address("10.2.1.0"), Ipv4Address("10.2.1.100"),
        Ipv4Address("10.2.1.200"), Ipv4Mask("255.255.255.0"));

    // --- 安装到 sw3 (域 C): ROLE_GATEWAY ---
    installGatewayApp(sw3,
        DynamicCast<WifiNetDevice>(apWifiDevsC.Get(0)),
        DynamicCast<WifiNetDevice>(sw3EmDev.Get(0)), sw3Vnds.emVnd,
        Ipv4Address("10.100.3.0"), Ipv4Address("10.100.3.10"),
        Ipv4Address("10.100.3.50"), Ipv4Mask("255.255.255.0"));

    // --- 安装到移动节点: ROLE_TERMINAL ---
    Ptr<BlindConnectApp> mobileApp = CreateObject<BlindConnectApp>();
    mobileApp->SetRole(BlindConnectApp::ROLE_TERMINAL);
    mobileApp->SetStaDevice(DynamicCast<WifiNetDevice>(mobileStaDev.Get(0)));
    mobileApp->SetSocketStaDevice(mobileStaDev.Get(0));
    mobileApp->SetAdhocDevice(DynamicCast<WifiNetDevice>(mobileEmDev.Get(0)));
    mobileApp->SetSocketAdhocDevice(mobileEmDev.Get(0));
    mobileNode.Get(0)->AddApplication(mobileApp);
    mobileApp->SetStartTime(Seconds(1.5));
    mobileApp->SetStopTime(Seconds(30.0));

    // 注册 Adhoc 域信道映射（SSID → YansWifiChannel），用于 ExecuteSwitch 动态切换
    mobileApp->RegisterAdhocChannel("Adhoc-Net", domainChC);

    std::cout << "[Mobile] BlindConnectApp 已安装 (ROLE_TERMINAL, start=1.5s)" << std::endl;

    // 启动移动节点状态日志（t=3s 起，每 2s 一次）
    Simulator::Schedule(Seconds(3.0), &PrintMobileNodeStatus, mobileNode.Get(0), mobileApp);

    } // end if (controllerApp != nullptr)

    // =========================================================
    // 15b. AP 关联 trace（诊断 STA 是否成功关联）
    // =========================================================
    {
        Ptr<WifiNetDevice> apDevs[] = {
            DynamicCast<WifiNetDevice>(apWifiDevsA.Get(0)),
            DynamicCast<WifiNetDevice>(apWifiDevsB.Get(0)),
            DynamicCast<WifiNetDevice>(apWifiDevsC.Get(0))
        };
        std::string names[] = {"sw1", "sw2", "sw3"};
        for (int i = 0; i < 3; i++)
        {
            Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(apDevs[i]->GetMac());
            if (apMac)
            {
                apMac->TraceConnectWithoutContext("AssociatedSta",
                    MakeBoundCallback(&AssocTraceCb, names[i]));
            }
        }
    }

    // =========================================================
    // 16. 获取 OFSwitch13Device 并调度 STA 信息上报
    // =========================================================
    Ptr<OFSwitch13Device> sw1Device = sw1->GetObject<OFSwitch13Device>();
    Ptr<OFSwitch13Device> sw2Device = sw2->GetObject<OFSwitch13Device>();
    Ptr<OFSwitch13Device> sw3Device = sw3->GetObject<OFSwitch13Device>();

    if (sw1Device)
        Simulator::Schedule(Seconds(5.0), &OFSwitch13Device::GetApStaMessages, sw1Device);
    if (sw2Device)
        Simulator::Schedule(Seconds(5.0), &OFSwitch13Device::GetApStaMessages, sw2Device);
    if (sw3Device)
        Simulator::Schedule(Seconds(5.0), &OFSwitch13Device::GetApStaMessages, sw3Device);

    if (controllerApp)
        Simulator::Schedule(Seconds(7.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);

    // =========================================================
    // 16b. 组网模式切换与路由优先级切换调度
    // =========================================================

    // --- 组网模式切换 ---
    // 域 C 初始已是 Ad-Hoc 模式，无需 CDL 切换
    // 保留切回基础设施模式的调度（需要时取消注释）：
    // if (sw3Device)
    //     Simulator::Schedule(Seconds(20.0), &OFSwitch13Device::Changelogical, sw3Device, 0);

    // --- 路由协议优先级切换演示 ---
    // 13s: 通过控制器向 sw3 发送优先级设置消息
    //      SetPriorityToAll -> SetRPtoAll -> 发送 ADHOC_EXT_TEST 到 sw3
    //Simulator::Schedule(Seconds(11.0), &OFSwitch13LearningController::SetPriorityToAll, controllerApp);

    // --- 二次 STA 信息采集（此时 STA 已关联 AP）---
   // if (sw1Device)
        //Simulator::Schedule(Seconds(10.0), &OFSwitch13Device::GetApStaMessages, sw1Device);
   // if (sw2Device)
        //Simulator::Schedule(Seconds(10.0), &OFSwitch13Device::GetApStaMessages, sw2Device);
   // if (sw3Device)
        //Simulator::Schedule(Seconds(10.0), &OFSwitch13Device::GetApStaMessages, sw3Device);

    // =========================================================
    // 17. 打印信息
    // =========================================================
    std::cout << "\n===== 融合架构网络信息 =====" << std::endl;

    // Switch 端口信息
    std::cout << "\nsw1 ports: " << sw1Ports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw1Ports.GetN(); ++i)
        std::cout << "  Port " << (i+1) << ": " << sw1Ports.Get(i)->GetAddress() << std::endl;

    std::cout << "sw2 ports: " << sw2Ports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw2Ports.GetN(); ++i)
        std::cout << "  Port " << (i+1) << ": " << sw2Ports.Get(i)->GetAddress() << std::endl;

    std::cout << "sw3 ports: " << sw3Ports.GetN() << std::endl;
    for (uint32_t i = 0; i < sw3Ports.GetN(); ++i)
        std::cout << "  Port " << (i+1) << ": " << sw3Ports.Get(i)->GetAddress() << std::endl;

    // STA MAC/IP 信息
    std::cout << "\nSTA MAC and IP:" << std::endl;
    auto printStaInfo = [](NodeContainer stas, NetDeviceContainer staDevs, std::string name) {
        for (uint32_t i = 0; i < stas.GetN(); ++i) {
            Ptr<NetDevice> dev = staDevs.Get(i);
            std::cout << "  " << name << "[" << i << "] MAC: " << Mac48Address::ConvertFrom(dev->GetAddress());
            Ptr<Ipv4> ipv4 = stas.Get(i)->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1)
                std::cout << " IP: " << ipv4->GetAddress(1, 0).GetLocal();
            std::cout << std::endl;
        }
    };
    printStaInfo(StaA, staWifiDevsA, "StaA");
    printStaInfo(StaB, staWifiDevsB, "StaB");
    printStaInfo(StaC, staWifiDevsC, "StaC");

    // =========================================================
    // 18. pcap 追踪
    // =========================================================
    if (true)
    {
        phyAp.EnablePcap("A_sta", staWifiDevsA);
        phyAp.EnablePcap("A_ap", apWifiDevsA);
        phyAp.EnablePcap("B_sta", staWifiDevsB);
        phyAp.EnablePcap("B_ap", apWifiDevsB);
        phyAp.EnablePcap("C_sta", staWifiDevsC);
        phyAp.EnablePcap("C_ap", apWifiDevsC);
        switchPhy.EnablePcap("switch-backhaul", NodeContainer(sw1, sw2, sw3));
        domainPhyA.EnablePcap("domainA-adhoc-sw", NodeContainer(sw1));
        domainPhyA.EnablePcap("domainA-adhoc-sta", StaA);
        domainPhyB.EnablePcap("domainB-adhoc-sw", NodeContainer(sw2));
        domainPhyB.EnablePcap("domainB-adhoc-sta", StaB);
        domainPhyC.EnablePcap("domainC-adhoc-sw", NodeContainer(sw3));
        domainPhyC.EnablePcap("domainC-adhoc-sta", StaC);
        phyAp.EnablePcap("mobile-sta", mobileStaDev);
        phyAp.EnablePcap("mobile-em", mobileEmDev);
        of13Helper->EnableOpenFlowPcap("openflow");
        of13Helper->EnableDatapathStats("switch-stats");
    }

    // =========================================================
    // 19. UDP 流量应用（4条跨域流 )
    // =========================================================
    uint16_t basePort = 9;

    OnOffHelper onoff("ns3::UdpSocketFactory", Address());
    onoff.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1024));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff.SetAttribute("StartTime", TimeValue(Seconds(10)));
    onoff.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

   // Flow 1: A→C  StaA[1] (10.1.1.2) → StaC[2] (10.3.1.3)  port 9
    //onoff.SetAttribute("StartTime", TimeValue(Seconds(12)));
    //onoff.SetAttribute("Remote", AddressValue(InetSocketAddress(ifStaC.GetAddress(2), basePort)));
    //onoff.Install(StaA.Get(0));

    // Flow 2: A→C  StaA[0] (10.1.1.1) → StaC[2] adhoc (10.100.3.4)  port 13
    // 域 C 初始即为 Ad-Hoc 模式，无需等待模式切换，提前启动
    onoff.SetAttribute("StartTime", TimeValue(Seconds(10.0)));
    onoff.SetAttribute("Remote", AddressValue(InetSocketAddress(emIfacesC.GetAddress(3), basePort + 4)));
    onoff.Install(StaA.Get(0));

    // Flow 3: A→B  StaA[0] (10.1.1.1) → StaB[0] (10.2.1.1)  port 10
    //onoff.SetAttribute("Remote", AddressValue(InetSocketAddress(ifStaB.GetAddress(0), basePort + 1)));
    //onoff.Install(StaA.Get(0));
    /* // Flow 4: B→A  StaB[1] (10.2.1.2) → StaA[2] (10.1.1.3)  port 11
    onoff.SetAttribute("Remote", AddressValue(InetSocketAddress(ifStaA.GetAddress(2), basePort + 2)));
    onoff.Install(StaB.Get(1));

    // Flow 5: B→C  StaB[0] (10.2.1.1) → StaC[0] (10.3.1.1)  port 12
    onoff.SetAttribute("Remote", AddressValue(InetSocketAddress(ifStaC.GetAddress(0), basePort + 3)));
    onoff.Install(StaB.Get(0));
    */
    

    // =========================================================
    // 20. FlowMonitor
    // =========================================================
    FlowMonitorHelper flowmonHelper;
    NodeContainer monitorNodes;
    monitorNodes.Add(StaA);
    monitorNodes.Add(StaB);
    monitorNodes.Add(StaC);
    Ptr<FlowMonitor> monitor = flowmonHelper.Install(monitorNodes);

    std::ofstream fout("flow_stats.csv");
    fout << "Time";
    for (int i = 1; i <= 7; ++i)
        fout << ",Throughput_" << i << "(Kbps),LossRate_" << i << "(%),AvgRTT_" << i << "(ms),Jitter_" << i << "(ms)";
    fout << std::endl;
    Simulator::Schedule(Seconds(0.1), &MonitorFlow, monitor, &flowmonHelper, 1, &fout);

    // =========================================================
    // 21. 运行仿真
    // =========================================================
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n========== 流量统计结果 ==========" << std::endl;
    for (auto const &i : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i.first);
        std::cout << "Flow (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
        std::cout << "  Tx Bytes: " << i.second.txBytes << "  Rx Bytes: " << i.second.rxBytes << std::endl;
        std::cout << "  Tx Packets: " << i.second.txPackets << "  Rx Packets: " << i.second.rxPackets << std::endl;
        if (i.second.rxPackets > 0)
            std::cout << "  平均延迟: " << i.second.delaySum.GetSeconds() / i.second.rxPackets * 1000 << " ms" << std::endl;
        if (i.second.txPackets > 0)
            std::cout << "  丢包率: " << (double)(i.second.txPackets - i.second.rxPackets) / i.second.txPackets * 100 << " %" << std::endl;
        std::cout << std::endl;
    }

    monitor->SerializeToXmlFile("flowmon-results.xml", true, true);
    fout.close();

    Simulator::Destroy();
    return 0;
}