/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 单控制器，C域 SDN 示例（集成DHCP）
 *
 * - 域 C: 3个WiFi终端 + 1个AP -- sw3（ApC为DHCP服务器，StaC/Stain为客户端）
 * - 单 OpenFlow 控制器管理 sw3
 * - DHCP服务器：ApC（固定IP 10.3.1.7），分配池 10.3.1.10-10.3.1.20
 * - DHCP客户端：StaC（3个）、Stain（1个）自动获取IP
 * - 监控C域内流量
 */

#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/wifi-module.h>
#include <ns3/mobility-module.h>
#include <ns3/applications-module.h>
#include <ns3/flow-monitor-module.h>
#include <ns3/ipv4-flow-classifier.h>
#include <ns3/bridge-helper.h>
#include <ns3/aodv-module.h>
#include <ns3/olsr-module.h>

#include <map>
#include <fstream>
#include <cmath>
#include "ns3/internet-apps-module.h"
using namespace ns3;

// 逻辑上下线设备
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

// 流量监控相关变量
std::map<uint32_t, double> lastRxBytes;
std::map<uint32_t, double> lastPacketRtt;

// 流量监控函数（仅监控C域流量）
void MonitorFlow(Ptr<FlowMonitor> monitor, FlowMonitorHelper *flowHelper, double interval, std::ofstream *fout)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double now = Simulator::Now().GetSeconds();
    *fout << now;

    // 仅监控C域流量端口（port3: 12）
    std::vector<uint16_t> ports = {12};

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

            // 瞬时吞吐量（Kbps）
            double rxBytesDelta = flowStats.rxBytes;
            if (lastRxBytes.count(fid))
            {
                rxBytesDelta -= lastRxBytes[fid];
            }
            throughput = rxBytesDelta * 8.0 / (interval * 1024.0);
            lastRxBytes[fid] = flowStats.rxBytes;

            // 丢包率
            if (flowStats.txPackets > 0)
            {
                lossRate = 100.0 * (flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets;
            }

            // 平均RTT和抖动
            if (flowStats.rxPackets > 0)
            {
                avgRtt = flowStats.delaySum.GetSeconds() / flowStats.rxPackets * 1000.0;

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

//---------------------------------------------------------------
//---------------------------------------------------------------

int main(int argc, char *argv[])
{
    uint16_t simTime = 20;
    bool verbose = true;
    bool trace = false;

    CommandLine cmd;
    cmd.AddValue("simTime", "仿真时间", simTime);
    cmd.AddValue("verbose", "启用详细日志", verbose);
    cmd.AddValue("trace", "启用pcap追踪", trace);
    cmd.Parse(argc, argv);

    // 启用校验和（ofswitch13必需）
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    // ======================
    // C域节点创建
    // ======================
    NodeContainer StaC; // C域WiFi终端（3个，DHCP客户端）
    StaC.Create(3);
    NodeContainer ApC; // C域AP（1个，DHCP服务器 + 网关）
    ApC.Create(1);
    NodeContainer Stain; // 额外终端（DHCP客户端）
    Stain.Create(1);
    // 网络设备节点：sw3交换机 + 控制器
    Ptr<Node> sw3 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    // ======================
    // 有线链路配置（AP-C <-> sw3）
    // ======================
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer apCsmaDevsC;  // AP-C的有线接口
    NetDeviceContainer sw3Devsports; // sw3的端口设备

    // 连接AP-C到sw3
    {
        NodeContainer pair(ApC.Get(0), sw3);
        NetDeviceContainer link = csma.Install(pair);
        apCsmaDevsC.Add(link.Get(0));  // AP侧
        sw3Devsports.Add(link.Get(1)); // sw3侧
    }

    // ======================
    // C域WiFi配置
    // ======================
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::AarfWifiManager");
    wifi.SetStandard(WIFI_STANDARD_80211g);
    WifiMacHelper mac;

    // C域WiFi信道
    Ptr<YansWifiChannel> channelCin = channel.Create();
    YansWifiPhyHelper phyCin;
    phyCin.SetChannel(channelCin);
    phyCin.Set("ChannelNumber", UintegerValue(0));
    Ptr<YansWifiChannel> channelC = channel.Create();
    YansWifiPhyHelper phyC;
    phyC.SetChannel(channelC);
    phyC.Set("ChannelNumber", UintegerValue(0));

    Ssid ssidC = Ssid("C");

    // C域STA配置（WiFi客户端，DHCP客户端）
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssidC));
    NetDeviceContainer staWifiDevsC = wifi.Install(phyC, mac, StaC);

    // Stain节点WiFi配置（DHCP客户端）
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(Ssid("C")),
                "ActiveProbing", BooleanValue(true));
    NetDeviceContainer staWifiDevsCin = wifi.Install(phyC, mac, Stain);

    // C域AP配置（WiFi接入点，DHCP服务器）
    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssidC),
                "BeaconInterval", TimeValue(MicroSeconds(102400)));
    NetDeviceContainer apWifiDevsC = wifi.Install(phyC, mac, ApC);

    // C域AdHoc接口配置（保留，手动分配IP）
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer adhocDevsC = wifi.Install(phyC, mac, StaC);

    // ======================
    // 节点位置配置
    // ======================
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // 控制器位置
    Ptr<ListPositionAllocator> posController = CreateObject<ListPositionAllocator>();
    posController->Add(Vector(0, 0, 0));
    mobility.SetPositionAllocator(posController);
    mobility.Install(controllerNode);

    // sw3交换机位置
    Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
    swPos->Add(Vector(0, 150, 0)); // sw3
    mobility.SetPositionAllocator(swPos);
    mobility.Install(sw3);

    // C域节点位置
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(20.0, 230.0, 0.0));  // StaC[0]
    positionAlloc->Add(Vector(50.0, 210.0, 0.0));  // StaC[1]
    positionAlloc->Add(Vector(-20.0, 230.0, 0.0)); // StaC[2]
    positionAlloc->Add(Vector(0.0, 200.0, 0.0));   // ApC[0]
    positionAlloc->Add(Vector(30.0, 220.0, 0.0));  // Stain[0]

    MobilityHelper adhocMobility;
    adhocMobility.SetPositionAllocator(positionAlloc);
    adhocMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    adhocMobility.Install(StaC);
    adhocMobility.Install(ApC);
    adhocMobility.Install(Stain);

    // ======================
    // AP桥接（WiFi <-> 有线）
    // ======================
    BridgeHelper bridge;
    NetDeviceContainer bridgeDevC = bridge.Install(ApC.Get(0),
                                                   NetDeviceContainer(apWifiDevsC.Get(0), apCsmaDevsC.Get(0)));

    // ======================
    // OpenFlow控制器配置
    // ======================
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    of13Helper->InstallController(controllerNode); // 安装控制器
    of13Helper->InstallSwitch(sw3, sw3Devsports);  // 安装sw3交换机
    of13Helper->CreateOpenFlowChannels();          // 创建控制信道

    // 获取控制器实例
    auto controllerApps = of13Helper->GetController();
    Ptr<OFSwitch13LearningController> controllerApp =
        DynamicCast<OFSwitch13LearningController>(controllerApps.Get(0));

    // ======================
    // 网络协议栈配置（含DHCP依赖）
    // ======================
    InternetStackHelper stack;
    InternetStackHelper stack2;
    Ipv4ListRoutingHelper list;
    AodvHelper aodv;
    OlsrHelper olsr;
    Ipv4StaticRoutingHelper staticRoutingHelper;

    list.Add(aodv, 10);
    list.Add(olsr, 10);
    list.Add(staticRoutingHelper, 100);
    stack2.SetRoutingHelper(list);
    stack.Install(StaC);  // C域STA使用AODV/OLSR路由
    stack.Install(Stain); // Stain安装基础栈
    stack.Install(ApC);   // AP安装基础协议栈（DHCP服务器）

    // 启用IP转发（AP作为网关，DHCP服务器必需）
    Ptr<Ipv4> ipv4ApC = ApC.Get(0)->GetObject<Ipv4>();
    ipv4ApC->SetAttribute("IpForward", BooleanValue(true));

    // C域STA启用IP转发（保留原有逻辑）
    for (uint32_t i = 0; i < StaC.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }

    // ======================
    // DHCP配置（核心改造）
    // ======================
    DhcpHelper dhcpHelper;
    Time dhcpStopTime = Seconds(simTime);

    // 1. 给ApC（DHCP服务器）配置固定IP（避免DHCP池冲突）
    Ipv4InterfaceContainer apFixedIp = dhcpHelper.InstallFixedAddress(
        bridgeDevC.Get(0),        // ApC的有线接口
        Ipv4Address("10.3.1.7"),  // ApC固定IP（网关）
        Ipv4Mask("255.255.255.0") // 子网掩码
    );

    // 2. 安装DHCP服务器到ApC
    ApplicationContainer dhcpServerApp = dhcpHelper.InstallDhcpServer(
        bridgeDevC.Get(0),         // DHCP服务器绑定的WiFi接口
        Ipv4Address("10.3.1.7"),   // DHCP服务器自身IP（ApC固定IP）
        Ipv4Address("10.3.1.0"),   // DHCP分配网段
        Ipv4Mask("255.255.255.0"), // 子网掩码
        Ipv4Address("10.3.1.10"),  // DHCP池起始IP（动态分配）
        Ipv4Address("10.3.1.224"), // DHCP池结束IP
        Ipv4Address("10.3.1.7")    // 网关（ApC自身IP）
    );
    dhcpServerApp.Start(Seconds(0.0)); // 0秒启动服务器（先于客户端）
    dhcpServerApp.Stop(dhcpStopTime);

    // 3. 配置DHCP客户端（StaC + Stain）
    NetDeviceContainer dhcpClientDevs;
    // StaC的WiFi接口作为DHCP客户端
    // for (uint32_t i = 0; i < staWifiDevsC.GetN(); ++i) {
    //     dhcpClientDevs.Add(staWifiDevsC.Get(i));
    // }
    // Stain的WiFi接口作为DHCP客户端
    dhcpClientDevs.Add(staWifiDevsCin.Get(0));

    // 安装DHCP客户端（1秒启动，确保服务器已就绪）
    ApplicationContainer dhcpClientApps = dhcpHelper.InstallDhcpClient(dhcpClientDevs);
    dhcpClientApps.Start(Seconds(1.0));
    dhcpClientApps.Stop(dhcpStopTime);

    // 4. AdHoc接口手动分配IP（保留原有逻辑，非DHCP）
    //  Ipv4AddressHelper ipv4Adhoc;
    //  ipv4Adhoc.SetBase("10.3.1.40", "255.255.255.0");
    //  Ipv4InterfaceContainer adhocIf = ipv4Adhoc.Assign(adhocDevsC);

    // ======================
    // 调试信息输出（DHCP相关）
    // ======================
    std::cout << "=== C域DHCP配置信息 ===" << std::endl;
    // std::cout << "DHCP服务器（ApC）固定IP: " << apFixedIp.GetAddress(0) << std::endl;
    std::cout << "DHCP分配池: 10.3.1.10 ~ 10.3.1.20" << std::endl;
    std::cout << "网关（ApC）: 10.3.1.7" << std::endl;

    // ======================
    // 注册AP ARP信息到控制器（兼容原有逻辑）
    // ======================
    if (controllerApp != nullptr)
    {
        Ipv4Address apCIp = apFixedIp.GetAddress(0);
        Mac48Address apCMac = Mac48Address::ConvertFrom(apCsmaDevsC.Get(0)->GetAddress());
        controllerApp->AddArpEntry(apCIp, apCMac);
        std::cout << "注册ARP: ApC[0] " << apCIp << " -> " << apCMac << std::endl;
    }

    // ======================
    // 启用PCAP追踪（可选）
    // ======================
    if (trace)
    {
        phyC.EnablePcap("C_adhoc", adhocDevsC);              // C域AdHoc接口
        phyC.EnablePcap("C_sta", staWifiDevsC);              // C域STA WiFi接口
        csma.EnablePcap("C_domain_ap", apCsmaDevsC);         // AP-C有线接口
        of13Helper->EnableOpenFlowPcap("openflow-c-domain"); // OpenFlow控制信道
        csma.EnablePcap("sw3", sw3Devsports, true);          // sw3交换机端口
    }

    // ======================
    // C域应用层流量（StaC[0] -> StaC[2]，DHCP获取IP后通信）
    // ======================
    uint16_t port3 = 12;
    // 延迟启动流量（确保DHCP完成IP分配，原3.5秒→5秒）
    OnOffHelper onoff3("ns3::UdpSocketFactory", Address());
    onoff3.SetAttribute("DataRate", StringValue("600kbps"));
    onoff3.SetAttribute("PacketSize", UintegerValue(1024));
    onoff3.SetAttribute("StartTime", TimeValue(Seconds(5.0))); // 延迟到5秒启动
    onoff3.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // 注意：DHCP客户端IP是动态的，这里通过STA索引获取（或通过DHCP静态条目绑定）
    // 简化：假设StaC[2]获取的IP是10.3.1.12（也可通过DHCP静态条目固定）
    InetSocketAddress dst3(Ipv4Address("10.3.1.12"), port3);
    onoff3.SetAttribute("Remote", AddressValue(dst3));
    ApplicationContainer app3 = onoff3.Install(StaC.Get(0));

    // ======================
    // 流量监控（保留原有逻辑）
    // ======================
    FlowMonitorHelper flowmonHelper;
    NodeContainer monitorNodes;
    monitorNodes.Add(StaC);
    monitorNodes.Add(Stain);
    Ptr<FlowMonitor> monitor = flowmonHelper.Install(monitorNodes);

    // 输出CSV文件
    std::ofstream fout("c_domain_flow_stats.csv");
    fout << "Time,Throughput_C(12)(Kbps),LossRate_C(12)(%),AvgRTT_C(12)(ms),Jitter_C(12)(ms)" << std::endl;
    Simulator::Schedule(Seconds(0.1), &MonitorFlow, monitor, &flowmonHelper, 1, &fout);

    // 设置控制器路由优先级
    Simulator::Schedule(Seconds(2.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);

    // ======================
    // 运行仿真
    // ======================
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ======================
    // 输出统计结果（含DHCP分配信息）
    // ======================
    std::cout << "\n=== C域DHCP分配结果 ===" << std::endl;
    // 打印StaC各节点获取的IP
    // for (uint32_t i = 0; i < StaC.GetN(); ++i) {
    //     Ptr<Node> staNode = StaC.Get(i);
    //     Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
    //     // 获取WiFi接口（DHCP分配）的IP
    //     uint32_t wifiIfIdx = ipv4->GetInterfaceForDevice(staWifiDevsC.Get(i));
    //     Ipv4Address staIp = ipv4->GetAddress(wifiIfIdx, 0).GetLocal();
    //     std::cout << "StaC[" << i << "] WiFi接口 DHCP分配IP: " << staIp << std::endl;
    //     // AdHoc接口IP（手动分配）
    //     uint32_t adhocIfIdx = ipv4->GetInterfaceForDevice(adhocDevsC.Get(i));
    //     Ipv4Address adhocIp = ipv4->GetAddress(adhocIfIdx, 0).GetLocal();
    //     std::cout << "StaC[" << i << "] AdHoc接口 手动IP: " << adhocIp << std::endl;
    // }
    // 打印Stain节点IP
    Ptr<Ipv4> stainIpv4 = Stain.Get(0)->GetObject<Ipv4>();
    uint32_t stainIfIdx = stainIpv4->GetInterfaceForDevice(staWifiDevsCin.Get(0));
    Ipv4Address stainIp = stainIpv4->GetAddress(stainIfIdx, 0).GetLocal();
    std::cout << "Stain[0] WiFi接口 DHCP分配IP: " << stainIp << std::endl;

    std::cout << "\n=== C域流量统计结果 ===" << std::endl;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    for (auto &flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        std::cout << "Flow (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
        std::cout << "  发送字节: " << flow.second.txBytes << std::endl;
        std::cout << "  接收字节: " << flow.second.rxBytes << std::endl;
        std::cout << "  发送包数: " << flow.second.txPackets << std::endl;
        std::cout << "  接收包数: " << flow.second.rxPackets << std::endl;
        std::cout << "  平均延迟: " << (flow.second.rxPackets > 0 ? flow.second.delaySum.GetSeconds() / flow.second.rxPackets * 1000 : 0) << " ms" << std::endl;
        std::cout << "  丢包率: " << (flow.second.txPackets > 0 ? (double)(flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets * 100 : 0) << " %" << std::endl;
        std::cout << std::endl;
    }

    // 保存监控结果
    monitor->SerializeToXmlFile("c_domain_flowmon.xml", true, true);
    fout.close();

    Simulator::Destroy();
    return 0;
}