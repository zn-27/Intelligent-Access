// TDMA 最小化测试版本 - 仅验证 TDMA 模块功能
// -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*-
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

NS_LOG_COMPONENT_DEFINE("TdmaMinimalTest");

int main(int argc, char *argv[])
{
    uint16_t simTime = 30;
    bool verbose = false;

    CommandLine cmd;
    cmd.AddValue("simTime", "simulation time", simTime);
    cmd.AddValue("verbose", "enable verbose", verbose);
    cmd.Parse(argc, argv);

    std::cout << "========================================" << std::endl;
    std::cout << "  TDMA 最小化测试" << std::endl;
    std::cout << "========================================" << std::endl;

    // 配置 TDMA 信道
    Config::SetDefault("ns3::SimpleWirelessChannel::MaxRange", DoubleValue(400.0));

    // 创建节点 (4 个节点作为简单测试)
    NodeContainer nodes;
    nodes.Create(4);

    std::cout << "创建 " << nodes.GetN() << " 个节点" << std::endl;

    // 创建 TDMA Helper (4 个节点, 4 个时隙)
    TdmaHelper tdma = TdmaHelper(4, 8);

    // 配置 TDMA 控制器
    TdmaControllerHelper controller;
    controller.Set("SlotTime", TimeValue(MicroSeconds(500)));
    controller.Set("GuardTime", TimeValue(MicroSeconds(50)));
    controller.Set("InterFrameTime", TimeValue(MicroSeconds(0)));
    controller.Set("DataRate", DataRateValue(DataRate("54Mbps")));
    tdma.SetTdmaControllerHelper(controller);
    tdma.SetSlots(8,
                  0, 1, 1, 1, 1, 0, 0, 0, 0, // Node 0 (源): slots 0-3
                  1, 0, 0, 0, 0, 0, 0, 0, 0, // Node 1 (中继): slot 0
                  2, 0, 0, 0, 0, 0, 0, 0, 0, // Node 2 (中继): slot 0
                  3, 0, 0, 0, 0, 1, 0, 0, 0  // Node 3 (目的): slot 4
    );
    // 安装 TDMA 设备
    NetDeviceContainer devices = tdma.Install(nodes);
    std::cout << "安装 TDMA 设备完成" << std::endl;

    // 配置节点位置
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    positionAlloc->Add(Vector(100.0, 1.0, 0.0));
    positionAlloc->Add(Vector(200.0, 1.0, 0.0));
    positionAlloc->Add(Vector(300.0, 1.0, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.Install(nodes);

    // 安装协议栈
    InternetStackHelper stack;
    stack.Install(nodes);
    // 开启中间节点 IP 转发（Node 1, Node 2 作为中继）
    for (uint32_t i = 1; i <= 2; ++i)
    {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }
    // 分配 IP 地址
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    std::cout << "IP 地址分配完成:" << std::endl;
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        std::cout << "  Node " << i << ": " << interfaces.GetAddress(i) << std::endl;
    }

    // 创建 UDP 应用
    uint16_t port = 9;

    // 发送端: Node 0 -> Node 3
    OnOffHelper onoff("ns3::UdpSocketFactory", Address());
    onoff.SetAttribute("DataRate", StringValue("35Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1024));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    InetSocketAddress remote(interfaces.GetAddress(3), port);
    onoff.SetAttribute("Remote", AddressValue(remote));

    ApplicationContainer app = onoff.Install(nodes.Get(0));
    app.Start(Seconds(1.0));
    app.Stop(Seconds(simTime - 1));

    // 接收端: Node 3
    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simTime));

    // 流量监控
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // 运行仿真
    std::cout << "开始仿真 (" << simTime << " 秒)..." << std::endl;
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // 统计结果
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n========== TDMA 流量统计结果 ==========" << std::endl;
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
            std::cout << "  丢包率: "
                      << 100.0 * (flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets
                      << " %" << std::endl;
        }
        if (flow.second.rxPackets > 0)
        {
            std::cout << "  平均延迟: "
                      << flow.second.delaySum.GetSeconds() / flow.second.rxPackets * 1000
                      << " ms" << std::endl;
        }
    }

    monitor->SerializeToXmlFile("tdma-minimal-results.xml", true, true);

    Simulator::Destroy();

    std::cout << "\nTDMA 最小化测试完成!" << std::endl;
    std::cout << "结果已保存到 tdma-minimal-results.xml" << std::endl;

    return 0;
}
