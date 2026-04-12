// train4_opt.cc - 完整 TDMA 版本（16节点跨域网络）
// -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*-
/*
 * 完整 TDMA 版本 - 16个节点，纯TDMA网络
 * 所有节点通过TDMA无线信道连接
 * Domain A: 4 STA + 2 AP (6 nodes)
 * Domain B: 2 STA + 1 AP (3 nodes)
 * Domain C: 3 STA + 1 AP (4 nodes)
 * Switches: 3个 (sw1, sw2, sw3)
 */

#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/internet-module.h>
#include <ns3/simple-wireless-tdma-module.h>
#include <ns3/mobility-module.h>
#include <ns3/applications-module.h>
#include <ns3/flow-monitor-module.h>
#include <ns3/ipv4-flow-classifier.h>
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TdmaCrossDomain");

// 流量监控回调函数
void MonitorFlow(Ptr<FlowMonitor> monitor, FlowMonitorHelper *flowmon, int flowNum, std::ofstream *fout)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double timeNow = Simulator::Now().GetSeconds();

    *fout << timeNow;

    for (int i = 1; i <= flowNum; ++i)
    {
        double throughput = 0;
        double lossRate = 0;
        double avgRtt = 0;
        double jitter = 0;

        if (stats.find(i) != stats.end())
        {
            FlowMonitor::FlowStats &s = stats[i];
            if (s.timeLastRxPacket.GetSeconds() > 0)
            {
                double duration = s.timeLastRxPacket.GetSeconds() - s.timeFirstTxPacket.GetSeconds();
                if (duration > 0)
                {
                    throughput = (s.rxBytes * 8.0 / 1000.0) / duration; // Kbps
                }
            }
            if (s.txPackets > 0)
            {
                lossRate = 100.0 * (s.txPackets - s.rxPackets) / s.txPackets;
            }
            if (s.rxPackets > 0)
            {
                avgRtt = s.delaySum.GetSeconds() / s.rxPackets * 1000; // ms
                if (s.rxPackets > 1)
                {
                    jitter = s.jitterSum.GetSeconds() / (s.rxPackets - 1) * 1000; // ms
                }
            }
        }

        *fout << "," << throughput << "," << lossRate << "," << avgRtt << "," << jitter;
    }
    *fout << std::endl;

    // 每1秒调度一次
    Simulator::Schedule(Seconds(1.0), &MonitorFlow, monitor, flowmon, flowNum, fout);
}

int main(int argc, char *argv[])
{
    uint16_t simTime = 30;
    CommandLine cmd;
    cmd.AddValue("simTime", "simulation time", simTime);
    cmd.Parse(argc, argv);

    std::cout << "========================================" << std::endl;
    std::cout << "  TDMA 跨域网络仿真 - 纯TDMA 16节点版本" << std::endl;
    std::cout << "========================================" << std::endl;

    // ==========================
    // 1. 创建节点
    // ==========================

    // 域 A: 4 STA + 2 AP
    NodeContainer StaA;
    StaA.Create(4);
    NodeContainer ApA;
    ApA.Create(2);

    // 域 B: 2 STA + 1 AP
    NodeContainer StaB;
    StaB.Create(2);
    NodeContainer ApB;
    ApB.Create(1);

    // 域 C: 3 STA + 1 AP
    NodeContainer StaC;
    StaC.Create(3);
    NodeContainer ApC;
    ApC.Create(1);

    // 交换机节点
    NodeContainer switches;
    switches.Create(3);
    Ptr<Node> sw1 = switches.Get(0);
    Ptr<Node> sw2 = switches.Get(1);
    Ptr<Node> sw3 = switches.Get(2);

    std::cout << "节点创建完成: 域A(6) + 域B(3) + 域C(4) + 交换机(3) = 16节点" << std::endl;

    // ==========================
    // 2. TDMA 配置 - 所有节点使用同一个TDMA信道
    // ==========================
    Config::SetDefault("ns3::SimpleWirelessChannel::MaxRange", DoubleValue(500.0));

    // 收集所有无线节点到一个容器
    NodeContainer allNodes;
    // 按照顺序添加：交换机 -> 域A -> 域B -> 域C
    allNodes.Add(switches); // 0, 1, 2 (sw1, sw2, sw3)
    allNodes.Add(StaA);     // 3, 4, 5, 6
    allNodes.Add(ApA);      // 7, 8
    allNodes.Add(StaB);     // 9, 10
    allNodes.Add(ApB);      // 11
    allNodes.Add(StaC);     // 12, 13, 14
    allNodes.Add(ApC);      // 15

    uint32_t numNodes = allNodes.GetN();
    std::cout << "总节点数: " << numNodes << std::endl;

    // 创建 TDMA Helper (16个节点, 16个时隙)
    // 保持16时隙以维持帧周期，通过非对称分配给转发路径更多带宽
    uint32_t totalSlots = 16;
    TdmaHelper tdma = TdmaHelper(numNodes, totalSlots);

    // 配置 TDMA 控制器
    TdmaControllerHelper controller;
    controller.Set("SlotTime", TimeValue(MicroSeconds(500)));
    controller.Set("GuardTime", TimeValue(MicroSeconds(50)));
    controller.Set("InterFrameTime", TimeValue(MicroSeconds(0)));
    controller.Set("DataRate", DataRateValue(DataRate("54Mbps")));
    tdma.SetTdmaControllerHelper(controller);

    // 16时隙优化分配: 给转发路径更多连续时隙
    // 节点ID: StaA[0-3]=0-3, ApA[0-1]=4-5, StaB[0-1]=6-7, ApB[0]=8, StaC[0-2]=9-11, ApC[0]=12, sw[0-2]=13-15
    // 路径: Node1 (source, StaA[1]) → Node13 (sw1) → Node11 (dest, StaC[2])
    // 列: nodeId, slot0, slot1, ..., slot15
    tdma.SetSlots(16,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // StaA[0]: 0 slots
                  1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,  // StaA[1] source: slots 5-8
                  2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // StaA[2]: 0 slots
                  3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // StaA[3]: 0 slots
                  4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // ApA[0]: 0 slots
                  5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // ApA[1]: 0 slots
                  6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // StaB[0]: 0 slots
                  7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // StaB[1]: 0 slots
                  8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // ApB[0]: 0 slots
                  9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // StaC[0]: 0 slots
                  10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // StaC[1]: 0 slots
                  11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, // StaC[2] dest: slot 9
                  12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // ApC[0]: 0 slots
                  13, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // sw1: slots 0-3
                  14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // sw2: 0 slots
                  15, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // sw3: slot 4
    );
    std::cout << "SetSlots: source=1(slots5-8), sw1=13(slots0-3), sw3=15(slot4), dest=11(slot9)" << std::endl;

    // 安装 TDMA 设备到所有节点
    NetDeviceContainer tdmaDevices = tdma.Install(allNodes);
    std::cout << "TDMA 设备安装完成" << std::endl;

    // ==========================
    // 3. 节点位置配置
    // ==========================
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // 交换机位置 - 位于中心
    Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
    swPos->Add(Vector(-50, 150, 0)); // sw1
    swPos->Add(Vector(50, 150, 0));  // sw2
    swPos->Add(Vector(0, 150, 0));   // sw3
    mobility.SetPositionAllocator(swPos);
    mobility.Install(switches);

    // 域 A 节点位置 - 左侧
    Ptr<ListPositionAllocator> posA = CreateObject<ListPositionAllocator>();
    posA->Add(Vector(-150, 200, 0)); // STA A[0]
    posA->Add(Vector(-120, 200, 0)); // STA A[1]
    posA->Add(Vector(-155, 230, 0)); // STA A[2]
    posA->Add(Vector(-155, 250, 0)); // STA A[3]
    posA->Add(Vector(-140, 180, 0)); // AP A[0] - 靠近 sw1
    posA->Add(Vector(-130, 190, 0)); // AP A[1]
    mobility.SetPositionAllocator(posA);
    mobility.Install(ApA);
    mobility.Install(StaA);

    // 域 B 节点位置 - 右侧
    Ptr<ListPositionAllocator> posB = CreateObject<ListPositionAllocator>();
    posB->Add(Vector(120, 200, 0)); // STA B[0]
    posB->Add(Vector(150, 230, 0)); // STA B[1]
    posB->Add(Vector(140, 180, 0)); // AP B[0] - 靠近 sw2
    mobility.SetPositionAllocator(posB);
    mobility.Install(StaB);
    mobility.Install(ApB);

    // 域 C 节点位置 - 中间
    Ptr<ListPositionAllocator> posC = CreateObject<ListPositionAllocator>();
    posC->Add(Vector(20.0, 230.0, 0.0));  // STA C[0]
    posC->Add(Vector(50.0, 210.0, 0.0));  // STA C[1]
    posC->Add(Vector(-20.0, 230.0, 0.0)); // STA C[2]
    posC->Add(Vector(0.0, 180.0, 0.0));   // AP C[0] - 靠近 sw3
    mobility.SetPositionAllocator(posC);
    mobility.Install(StaC);
    mobility.Install(ApC);

    std::cout << "节点位置配置完成" << std::endl;

    // ==========================
    // 4. 网络协议栈
    // ==========================
    InternetStackHelper stack;
    stack.Install(allNodes);

    // 仅转发节点开启 IP 转发（防止广播风暴）
    // 节点ID: sw1=13, sw3=15
    // 其他节点关闭 IP 转发，避免所有节点都尝试转发导致拥塞
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = allNodes.Get(i)->GetObject<Ipv4>();
        if (ipv4)
        {
            // 只有 sw1(node 13) 需要转发 (source→sw1→dest 直连路径)
            bool shouldForward = (i == 13);
            ipv4->SetAttribute("IpForward", BooleanValue(shouldForward));
        }
    }

    std::cout << "协议栈安装完成" << std::endl;

    // ==========================
    // 5. IP 地址分配 - 所有节点在同一子网
    // ==========================
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(tdmaDevices);

    std::cout << "IP 地址分配完成 (10.1.1.0/24)" << std::endl;

    // 打印节点IP地址
    std::cout << "\n节点IP地址:" << std::endl;
    std::cout << "  sw1: " << interfaces.GetAddress(0) << std::endl;
    std::cout << "  sw2: " << interfaces.GetAddress(1) << std::endl;
    std::cout << "  sw3: " << interfaces.GetAddress(2) << std::endl;
    std::cout << "  StaA[0]: " << interfaces.GetAddress(3) << std::endl;
    std::cout << "  StaA[1]: " << interfaces.GetAddress(4) << std::endl;
    std::cout << "  StaA[2]: " << interfaces.GetAddress(5) << std::endl;
    std::cout << "  StaA[3]: " << interfaces.GetAddress(6) << std::endl;
    std::cout << "  ApA[0]: " << interfaces.GetAddress(7) << std::endl;
    std::cout << "  ApA[1]: " << interfaces.GetAddress(8) << std::endl;
    std::cout << "  StaB[0]: " << interfaces.GetAddress(9) << std::endl;
    std::cout << "  StaB[1]: " << interfaces.GetAddress(10) << std::endl;
    std::cout << "  ApB[0]: " << interfaces.GetAddress(11) << std::endl;
    std::cout << "  StaC[0]: " << interfaces.GetAddress(12) << std::endl;
    std::cout << "  StaC[1]: " << interfaces.GetAddress(13) << std::endl;
    std::cout << "  StaC[2]: " << interfaces.GetAddress(14) << std::endl;
    std::cout << "  ApC[0]: " << interfaces.GetAddress(15) << std::endl;

    // ==========================
    // 6. 配置静态路由
    // ==========================
    Ipv4StaticRoutingHelper staticRouting;

    // 由于所有节点在同一子网，使用全局路由即可
    // 但为了确保路径经过交换机，配置静态路由

    // 域 A STA 默认路由 -> sw1 (交换机作为网关)
    Ipv4Address gatewayA = interfaces.GetAddress(0); // sw1
    for (uint32_t i = 0; i < StaA.GetN(); ++i)
    {
        Ptr<Node> node = StaA.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> routing = staticRouting.GetStaticRouting(ipv4);
        routing->SetDefaultRoute(gatewayA, 1);
    }
    std::cout << "域 A 静态路由配置完成 (网关: sw1)" << std::endl;

    // 域 B STA 默认路由 -> sw2
    Ipv4Address gatewayB = interfaces.GetAddress(1); // sw2
    for (uint32_t i = 0; i < StaB.GetN(); ++i)
    {
        Ptr<Node> node = StaB.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> routing = staticRouting.GetStaticRouting(ipv4);
        routing->SetDefaultRoute(gatewayB, 1);
    }
    std::cout << "域 B 静态路由配置完成 (网关: sw2)" << std::endl;

    // 域 C STA 默认路由 -> sw3
    Ipv4Address gatewayC = interfaces.GetAddress(2); // sw3
    for (uint32_t i = 0; i < StaC.GetN(); ++i)
    {
        Ptr<Node> node = StaC.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> routing = staticRouting.GetStaticRouting(ipv4);
        routing->SetDefaultRoute(gatewayC, 1);
    }
    std::cout << "域 C 静态路由配置完成 (网关: sw3)" << std::endl;

    // AP 节点默认路由 -> 对应的交换机
    {
        Ptr<Ipv4> ipv4 = ApA.Get(0)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> routing = staticRouting.GetStaticRouting(ipv4);
        routing->SetDefaultRoute(gatewayA, 1);
    }
    {
        Ptr<Ipv4> ipv4 = ApA.Get(1)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> routing = staticRouting.GetStaticRouting(ipv4);
        routing->SetDefaultRoute(gatewayA, 1);
    }
    {
        Ptr<Ipv4> ipv4 = ApB.Get(0)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> routing = staticRouting.GetStaticRouting(ipv4);
        routing->SetDefaultRoute(gatewayB, 1);
    }
    {
        Ptr<Ipv4> ipv4 = ApC.Get(0)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> routing = staticRouting.GetStaticRouting(ipv4);
        routing->SetDefaultRoute(gatewayC, 1);
    }
    std::cout << "AP 路由配置完成" << std::endl;

    // 交换机路由 - 直接路由，因为所有节点在同一子网
    // 不需要额外配置，IP转发已启用
    std::cout << "交换机已启用 IP 转发" << std::endl;

    // ==========================
    // 7. 应用配置
    // ==========================

    // Flow: StaA[1] (10.1.1.5) -> StaC[2] (10.1.1.15)
    uint16_t port = 9;
    OnOffHelper onoff("ns3::UdpSocketFactory", Address());
    onoff.SetAttribute("DataRate", StringValue("15Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1024));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff.SetAttribute("StartTime", TimeValue(Seconds(0))); // Start immediately
    onoff.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    // 关闭 TDMA 调试日志（仅显示错误）
    LogComponentEnable("TdmaController", LOG_LEVEL_ERROR);
    LogComponentEnable("TdmaCentralMac", LOG_LEVEL_ERROR);
    LogComponentEnable("TdmaMacLow", LOG_LEVEL_ERROR);

    InetSocketAddress dst(interfaces.GetAddress(14), port); // StaC[2] 是 interface 14
    onoff.SetAttribute("Remote", AddressValue(dst));
    ApplicationContainer app = onoff.Install(StaA.Get(1)); // StaA[1] 是 interface 4

    // 接收端
    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(StaC.Get(2));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simTime));

    std::cout << "应用配置完成" << std::endl;
    std::cout << "  Flow: StaA[1] (" << interfaces.GetAddress(4) << ") -> StaC[2] (" << interfaces.GetAddress(14) << ") @ 10Mbps" << std::endl;

    // ==========================
    // 8. 流量监控
    // ==========================
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    std::ofstream fout("flow_stats.csv");
    fout << "Time";
    for (int i = 1; i <= 4; ++i)
    {
        fout << ",Throughput_" << i << "(Kbps),LossRate_" << i << "(%),AvgRTT_" << i << "(ms),Jitter_" << i << "(ms)";
    }
    fout << std::endl;

    Simulator::Schedule(Seconds(0.1), &MonitorFlow, monitor, &flowmon, 4, &fout);

    // ==========================
    // 9. 运行仿真
    // ==========================
    std::cout << "\n开始仿真 (" << simTime << " 秒)..." << std::endl;
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ==========================
    // 10. 统计结果
    // ==========================
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n========== TDMA 流量统计结果 =========" << std::endl;
    for (auto const &flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        std::cout << "Flow " << flow.first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
        std::cout << "  Tx Packets: " << flow.second.txPackets << std::endl;
        std::cout << "  Rx Packets: " << flow.second.rxPackets << std::endl;
        std::cout << "  Tx Bytes: " << flow.second.txBytes << std::endl;
        std::cout << "  Rx Bytes: " << flow.second.rxBytes << std::endl;
        if (flow.second.txPackets > 0)
        {
            double lossRate = 100.0 * (flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets;
            std::cout << "  丢包率: " << lossRate << " %" << std::endl;
        }
        if (flow.second.rxPackets > 0)
        {
            double avgDelay = flow.second.delaySum.GetSeconds() / flow.second.rxPackets * 1000;
            std::cout << "  平均延迟: " << avgDelay << " ms" << std::endl;
            double throughput = (flow.second.rxBytes * 8.0 / 1000.0) / (flow.second.timeLastRxPacket.GetSeconds() - flow.second.timeFirstTxPacket.GetSeconds());
            std::cout << "  平均吞吐量: " << throughput << " Kbps" << std::endl;
        }
        std::cout << std::endl;
    }

    monitor->SerializeToXmlFile("flowmon-results.xml", true, true);
    fout.close();

    Simulator::Destroy();

    std::cout << "========================================" << std::endl;
    std::cout << "  TDMA 仿真完成!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
