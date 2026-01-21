/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
#include <iomanip>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/olsr-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DualInterfaceAdhoc");

// 定义辅助函数：创建一个Wifi配置，避免代码重复
// channelObject: 传入独立的信道对象，确保物理隔离
NetDeviceContainer ConfigureWifi(NodeContainer nodes, Ptr<YansWifiChannel> channel, uint32_t channelNumber)
{
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel);
    // 重要：手动锁定信道编号，防止自动配置导致重叠
    // 在实际仿真中，不同频率意味着无干扰
    // 这里虽然用了同一个Channel对象模型，但物理层参数建议区分，或者直接给不同Channel对象

    return wifi.Install(phy, mac, nodes);
}

int main(int argc, char *argv[])
{
    LogComponentEnable("DualInterfaceAdhoc", LOG_LEVEL_INFO);

    uint32_t nUavs = 4; // 域内无人机数量
    double simTime = 20.0;

    CommandLine cmd;
    cmd.Parse(argc, argv);

    // 1. 创建节点
    NodeContainer uavs;
    uavs.Create(nUavs);

    // 2. 移动性模型 - 正方形布局 + 待命随机移动
    // UAV0(0,0) ---- UAV1(100,0)
    //    |               |
    // UAV3(0,100) --- UAV2(100,100)
    // 每个节点在各自位置周围30米范围内缓慢随机移动，模拟待命悬停
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    // 独立设置每个节点的初始位置成正方形
    positionAlloc->Add(Vector(0, 0, 50));     // UAV0: 左下角
    positionAlloc->Add(Vector(100, 0, 50));   // UAV1: 右下角
    positionAlloc->Add(Vector(100, 100, 50)); // UAV2: 右上角
    positionAlloc->Add(Vector(0, 100, 50));   // UAV3: 左上角

    // 移除上面的 mobility.Install(uavs)，避免与随机游走模型冲突
    // 直接为每个节点单独设置 RandomWalk2dMobilityModel
    // 为每个节点单独设置移动边界，确保在初始位置周围±30米范围内
    // 使用独立的 MobilityHelper 为每个节点安装 RandomWalk2dMobilityModel
    std::vector<double> initX = {0, 100, 100, 0};
    std::vector<double> initY = {0, 0, 100, 100};

    for (uint32_t i = 0; i < nUavs; i++)
    {
        // 创建只包含单个节点的容器
        NodeContainer singleNode;
        singleNode.Add(uavs.Get(i));

        // 为该节点设置初始位置
        Ptr<ListPositionAllocator> alloc = CreateObject<ListPositionAllocator>();
        alloc->Add(Vector(initX[i], initY[i], 50));

        MobilityHelper nodeMobility;
        nodeMobility.SetPositionAllocator(alloc);
        // 降低移动速度和范围，模拟更稳定的待命悬停
        nodeMobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                      "Mode", StringValue("Time"),
                                      "Time", TimeValue(Seconds(2.0)),
                                      "Speed", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),                                // 降低速度
                                      "Bounds", RectangleValue(Rectangle(initX[i] - 20, initX[i] + 20, initY[i] - 20, initY[i] + 20))); // 缩小移动范围
        nodeMobility.Install(singleNode);
    }

    // ==========================================================
    // 3. 配置 Wi-Fi 物理层与链路层 (关键部分)
    // ==========================================================

    WifiHelper wifiCommon;
    wifiCommon.SetStandard(WIFI_STANDARD_80211n_5GHZ); // 使用 802.11n 以获得更高带宽

    WifiMacHelper macAdhoc;
    macAdhoc.SetType("ns3::AdhocWifiMac",
                     "QosSupported", BooleanValue(true)); // 启用QoS以获得更高吞吐量

    // 使用 MinstrelHtWifiManager 自适应速率管理器，根据链路质量动态调整数据率
    wifiCommon.SetRemoteStationManager("ns3::MinstrelHtWifiManager");

    // --- 接口 A: 域内私网 (Intra-Domain) ---
    // 使用信道 36 (5180 MHz)
    YansWifiChannelHelper channelHelperPrivate;
    channelHelperPrivate.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelHelperPrivate.AddPropagationLoss("ns3::TwoRayGroundPropagationLossModel"); // 更适合无人机网络
    Ptr<YansWifiChannel> privateChannel = channelHelperPrivate.Create();

    YansWifiPhyHelper phyPrivate;
    phyPrivate.SetChannel(privateChannel);
    // 增加发射功率以获得更好的链路质量
    phyPrivate.Set("TxPowerStart", DoubleValue(30.0));
    phyPrivate.Set("TxPowerEnd", DoubleValue(30.0));
    // 在 wifiCommon.Install 之前
    // Config::SetDefault("ns3::WifiMacQueue::MaxPackets", UintegerValue(1000));
    NetDeviceContainer privateDevs = wifiCommon.Install(phyPrivate, macAdhoc, uavs);

    // --- 接口 B: 域间公网 (Inter-Domain / Backbone) ---
    // 使用信道 44 (5220 MHz) - 完全隔离的频率
    YansWifiChannelHelper channelHelperPublic;
    channelHelperPublic.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelHelperPublic.AddPropagationLoss("ns3::TwoRayGroundPropagationLossModel");
    Ptr<YansWifiChannel> publicChannel = channelHelperPublic.Create();

    YansWifiPhyHelper phyPublic;
    phyPublic.SetChannel(publicChannel);
    // 公网接口使用高功率
    phyPublic.Set("TxPowerStart", DoubleValue(30.0));
    phyPublic.Set("TxPowerEnd", DoubleValue(30.0));

    NetDeviceContainer publicDevs = wifiCommon.Install(phyPublic, macAdhoc, uavs);

    // ==========================================================
    // 4. 协议栈与路由 (解决问题1和2的核心)
    // ==========================================================

    OlsrHelper olsr;
    // OLSR 会自动扫描所有接口，并建立拓扑表。
    // 如果要“记录连接稳定度”，OLSR 内部有 Link Set 和 Neighbor Set，
    // 并且会计算 ETX (Expected Transmission Count) 作为度量。
    // 你不需要手写协议，直接用 OLSR 就能找到最佳路径。

    Ipv4ListRoutingHelper list;
    list.Add(olsr, 10); // 优先级 10

    InternetStackHelper internet;
    internet.SetRoutingHelper(list);
    internet.Install(uavs);

    // ==========================================================
    // 5. IP 地址分配
    // ==========================================================
    Ipv4AddressHelper ipv4;

    // --- 分配私网地址 (对应 privateDevs) ---
    // 192.168.1.0/24
    ipv4.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer privateIfs = ipv4.Assign(privateDevs);

    // --- 分配公网地址 (对应 publicDevs) ---
    // 10.0.1.0/24 (假设这是域1，以后域2可以用 10.0.2.0)
    ipv4.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer publicIfs = ipv4.Assign(publicDevs);

    // ==========================================================
    // 6. 应用层测试
    // ==========================================================

    // 测试 1: 域内通信 (UAV0 -> UAV3，走私网 IP)
    // 理论上 OLSR 应该发现通过私网接口跳数可能更优，或者根据链路质量选择
    uint16_t port = 9;
    OnOffHelper onoff("ns3::UdpSocketFactory",
                      Address(InetSocketAddress(privateIfs.GetAddress(3), port)));
    onoff.SetConstantRate(DataRate("10Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer app = onoff.Install(uavs.Get(0));
    app.Start(Seconds(5.0));
    app.Stop(Seconds(simTime));

    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
    ApplicationContainer sinkApp = sink.Install(uavs.Get(3));
    sinkApp.Start(Seconds(0.0));

    // 测试 2: 模拟“跨域”通信 (UAV0 -> UAV3，强制走公网 IP)
    // 虽然都在一个域内，但如果我们Ping对方的公网IP，数据包逻辑上就是走公网平面
    uint16_t port2 = 10;
    OnOffHelper onoffPublic("ns3::UdpSocketFactory",
                            Address(InetSocketAddress(publicIfs.GetAddress(3), port2)));
    onoffPublic.SetConstantRate(DataRate("10Mbps"));

    ApplicationContainer app2 = onoffPublic.Install(uavs.Get(0));
    app2.Start(Seconds(5.0)); // 5秒后开始
    app2.Stop(Seconds(simTime));

    PacketSinkHelper sink2("ns3::UdpSocketFactory",
                           Address(InetSocketAddress(Ipv4Address::GetAny(), port2)));
    ApplicationContainer sinkApp2 = sink2.Install(uavs.Get(3));
    sinkApp2.Start(Seconds(0.0));

    // 测试 3: TCP流量 (用于获取RTT和抖动)
    // UAV1 -> UAV2 通过TCP传输
    uint16_t tcpPort = 8080;
    // TCP Sink (接收端)
    PacketSinkHelper tcpSink("ns3::TcpSocketFactory",
                             Address(InetSocketAddress(Ipv4Address::GetAny(), tcpPort)));
    ApplicationContainer tcpSinkApp = tcpSink.Install(uavs.Get(2));
    tcpSinkApp.Start(Seconds(0.0));
    tcpSinkApp.Stop(Seconds(simTime));

    // TCP BulkSend (发送端)
    BulkSendHelper tcpBulkSend("ns3::TcpSocketFactory",
                               Address(InetSocketAddress(privateIfs.GetAddress(2), tcpPort)));
    tcpBulkSend.SetAttribute("MaxBytes", UintegerValue(10 * 1024 * 1024)); // 发送100MB数据
    ApplicationContainer tcpApp = tcpBulkSend.Install(uavs.Get(1));
    tcpApp.Start(Seconds(10.0));
    tcpApp.Stop(Seconds(simTime));

    {
        // 测试 1: 域内通信 (UAV0 -> UAV3，走私网 IP)
        // 理论上 OLSR 应该发现通过私网接口跳数可能更优，或者根据链路质量选择
        uint16_t port = 11;
        OnOffHelper onoff("ns3::UdpSocketFactory",
                          Address(InetSocketAddress(privateIfs.GetAddress(2), port)));
        onoff.SetConstantRate(DataRate("10Mbps"));
        onoff.SetAttribute("PacketSize", UintegerValue(1024));

        ApplicationContainer app = onoff.Install(uavs.Get(1));
        app.Start(Seconds(5.0));
        app.Stop(Seconds(simTime));

        PacketSinkHelper sink("ns3::UdpSocketFactory",
                              Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
        ApplicationContainer sinkApp = sink.Install(uavs.Get(2));
        sinkApp.Start(Seconds(0.0));
    }

    // ==========================================================
    // 7. FlowMonitor 监控与运行
    // ==========================================================

    // 启用 PCAP，可以在 Wireshark 中看到两个接口分别抓包
    // pcap 文件名会包含节点ID和设备ID (dev0是私网, dev1是公网)
    phyPrivate.EnablePcap("uav-private", privateDevs.Get(0), true);
    phyPublic.EnablePcap("uav-public", publicDevs.Get(0), true);

    // 配置 FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    NS_LOG_INFO("Starting Simulation...");
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // 输出统计信息
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n=================================================================== Flow Monitor Statistics ===================================================================" << std::endl;
    // std::cout

    //     << std::setw(10) << "Flow ID"
    //     << std::setw(20) << "Source"
    //     << std::setw(20) << "Destination"
    //     << std::setw(12) << "Throughput"
    //     << std::setw(10) << "Loss Rate"
    //     << std::setw(12) << "Avg RTT"
    //     << std::setw(12) << "Jitter"
    //     << std::setw(10) << "Packets"
    //     << std::setw(10) << "Lost" << std::endl;
    std::cout << std::string(160, ' ') << std::endl;
    std::cout << std::string(160, '-') << std::endl;

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        std::cout << std::setw(5) << "Flow ID:" << i->first << "   "
                  << std::setw(5) << "Source:" << t.sourceAddress << "   "
                  << std::setw(5) << "Destination:" << t.destinationAddress << "   ";

        // 吞吐量 (Kbps)
        double throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1000.0;
        std::cout << std::setw(5) << "Throughput:" << std::fixed << std::setprecision(2) << throughput << " Kbps" << "   ";

        // 丢包率 (%)
        double lossRate = (double)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets * 100.0;
        std::cout << std::setw(5) << "Loss Rate:" << std::fixed << std::setprecision(2) << lossRate << " %" << "   ";

        // 平均 RTT (ms) - UDP流量为0
        double avgRtt = (i->second.rxPackets > 0) ? i->second.delaySum.GetSeconds() * 1000.0 / i->second.rxPackets : 0.0;
        std::cout << std::setw(5) << "Avg RTT:" << std::fixed << std::setprecision(2) << avgRtt << " ms" << "   ";

        // 抖动 (ms) - UDP流量为0
        double jitter = (i->second.rxPackets > 1) ? i->second.jitterSum.GetSeconds() * 1000.0 / (i->second.rxPackets - 1) : 0.0;
        std::cout << std::setw(5) << "Jitter:" << std::fixed << std::setprecision(2) << jitter << " ms" << "   ";

        std::cout
            << std::setw(5) << "Send Packets:" << i->second.txPackets << "   "
            << std::setw(5) << "Receive Packets:" << i->second.rxPackets << std::endl
            << std::endl;
    }
    std::cout << "========================================================================================================================================================================" << std::endl
              << std::endl;

    Simulator::Destroy();
    NS_LOG_INFO("Done.");

    return 0;
}