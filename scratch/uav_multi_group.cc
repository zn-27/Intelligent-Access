/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 全无线无人机组场景
 * - 3个无人机群，每个群是不同的网段
 * - 组内通过AP通信
 * - 组间通过网关无人机(adhoc)进行通信
 */

#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/internet-module.h>
#include <ns3/wifi-module.h>
#include <ns3/mobility-module.h>
#include <ns3/applications-module.h>
#include <ns3/ipv4-static-routing-helper.h>
#include <ns3/olsr-helper.h>
#include <ns3/flow-monitor-module.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UavMultiGroup");

// 全局变量用于记录上次流量统计
std::map<FlowId, uint64_t> lastRxBytes;
std::map<FlowId, uint64_t> lastTxPackets;
FlowMonitorHelper *globalFlowHelper = nullptr; // 全局FlowMonitorHelper指针

// 流量监控函数
void MonitorFlow(Ptr<FlowMonitor> monitor, std::ostream *os)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(globalFlowHelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double now = Simulator::Now().GetSeconds();
    *os << now;

    for (auto const &it : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it.first);
        uint64_t rxBytes = it.second.rxBytes;
        uint32_t rxPackets = it.second.rxPackets;
        uint32_t txPackets = it.second.txPackets;

        // 计算吞吐量 (Kbps)
        double throughput = 0.0;
        if (lastRxBytes.count(it.first))
        {
            uint64_t rxBytesDelta = rxBytes - lastRxBytes[it.first];
            throughput = rxBytesDelta * 8.0 / (1.0 * 1024.0); // Kbps
        }
        lastRxBytes[it.first] = rxBytes;

        // 计算丢包率
        double lossRate = 0.0;
        if (txPackets > 0)
        {
            lossRate = 100.0 * (txPackets - rxPackets) / txPackets;
        }

        // 计算平均延迟 (ms)
        double avgDelay = 0.0;
        if (rxPackets > 0)
        {
            avgDelay = it.second.delaySum.GetSeconds() / rxPackets * 1000.0;
        }

        *os << "," << t.sourceAddress << "->" << t.destinationAddress
            << "," << throughput << "," << lossRate << "," << avgDelay;
    }
    *os << std::endl;

    Simulator::Schedule(Seconds(1.0), &MonitorFlow, monitor, os);
}

int main(int argc, char *argv[])
{
    bool verbose = true;
    uint32_t simTime = 30;     // 仿真时间(秒)
    uint32_t uavsPerGroup = 3; // 每个群的无人机数量(不含网关)

    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("verbose", "Enable verbose output", verbose);
    cmd.AddValue("uavsPerGroup", "Number of UAVs per group (excluding gateway)", uavsPerGroup);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("UavMultiGroup", LOG_LEVEL_INFO);
    }

    // ========== 创建节点 ==========
    // 3个无人机群，每个群包含普通无人机 + 1个网关无人机
    NodeContainer uavsA, gatewayA; // 组A
    NodeContainer uavsB, gatewayB; // 组B
    NodeContainer uavsC, gatewayC; // 组C

    uavsA.Create(uavsPerGroup);
    gatewayA.Create(1);

    uavsB.Create(uavsPerGroup);
    gatewayB.Create(1);

    uavsC.Create(uavsPerGroup);
    gatewayC.Create(1);

    // 所有网关无人机组成一个adhoc网络用于组间通信
    NodeContainer allGateways;
    allGateways.Add(gatewayA);
    allGateways.Add(gatewayB);
    allGateways.Add(gatewayC);

    // ========== WiFi配置 ==========
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps"));

    WifiMacHelper mac;

    // ============ 组A WiFi网络 (独立信道) ============
    YansWifiChannelHelper channelA;
    channelA.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelA.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phyA;
    phyA.SetChannel(channelA.Create());
    phyA.SetErrorRateModel("ns3::YansErrorRateModel");

    Ssid ssidA = Ssid("UAV-Group-A");
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssidA),
                "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevsA = wifi.Install(phyA, mac, uavsA);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssidA));
    NetDeviceContainer apDevsA = wifi.Install(phyA, mac, gatewayA);

    // ============ 组B WiFi网络 (独立信道) ============
    YansWifiChannelHelper channelB;
    channelB.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelB.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phyB;
    phyB.SetChannel(channelB.Create());
    phyB.SetErrorRateModel("ns3::YansErrorRateModel");

    Ssid ssidB = Ssid("UAV-Group-B");
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssidB),
                "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevsB = wifi.Install(phyB, mac, uavsB);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssidB));
    NetDeviceContainer apDevsB = wifi.Install(phyB, mac, gatewayB);

    // ============ 组C WiFi网络 (独立信道) ============
    YansWifiChannelHelper channelC;
    channelC.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelC.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phyC;
    phyC.SetChannel(channelC.Create());
    phyC.SetErrorRateModel("ns3::YansErrorRateModel");

    Ssid ssidC = Ssid("UAV-Group-C");
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssidC),
                "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevsC = wifi.Install(phyC, mac, uavsC);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssidC));
    NetDeviceContainer apDevsC = wifi.Install(phyC, mac, gatewayC);

    // ============ 网关间AdHoc网络配置 (独立信道) ============
    YansWifiChannelHelper channelAdhoc;
    channelAdhoc.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelAdhoc.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phyAdhoc;
    phyAdhoc.SetChannel(channelAdhoc.Create());
    phyAdhoc.SetErrorRateModel("ns3::YansErrorRateModel");

    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer gatewayAdhocDevs = wifi.Install(phyAdhoc, mac, allGateways);

    // ========== 移动性配置 ==========
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // 组A位置 (三角形顶点1)
    Ptr<ListPositionAllocator> posA = CreateObject<ListPositionAllocator>();
    posA->Add(Vector(150, 50, 100)); // 网关A - 三角形顶点1
    posA->Add(Vector(120, 0, 100));  // UAV A[0]
    posA->Add(Vector(150, 0, 100));  // UAV A[1]
    posA->Add(Vector(180, 0, 100));  // UAV A[2]
    mobility.SetPositionAllocator(posA);
    mobility.Install(gatewayA);
    mobility.Install(uavsA);

    // 组B位置 (三角形顶点2)
    Ptr<ListPositionAllocator> posB = CreateObject<ListPositionAllocator>();
    posB->Add(Vector(100, 100, 100)); // 网关B - 三角形顶点2
    posB->Add(Vector(70, 150, 100));  // UAV B[0]
    posB->Add(Vector(100, 150, 100)); // UAV B[1]
    posB->Add(Vector(130, 150, 100)); // UAV B[2]
    mobility.SetPositionAllocator(posB);
    mobility.Install(gatewayB);
    mobility.Install(uavsB);

    // 组C位置 (三角形顶点3)
    Ptr<ListPositionAllocator> posC = CreateObject<ListPositionAllocator>();
    posC->Add(Vector(200, 100, 100)); // 网关C - 三角形顶点3
    posC->Add(Vector(170, 150, 100)); // UAV C[0]
    posC->Add(Vector(200, 150, 100)); // UAV C[1]
    posC->Add(Vector(230, 150, 100)); // UAV C[2]
    mobility.SetPositionAllocator(posC);
    mobility.Install(gatewayC);
    mobility.Install(uavsC);

    // ========== 网络协议栈配置 ==========
    InternetStackHelper internet;
    OlsrHelper olsr;
    Ipv4StaticRoutingHelper staticRoutingHelper;
    Ipv4ListRoutingHelper list;
    list.Add(staticRoutingHelper, 100); // 优先尝试静态路由
    list.Add(olsr, 10);                 // 然后使用 OLSR
    internet.SetRoutingHelper(list);    // 在 Install 之前设置

    // 组A (使用静态路由)
    internet.Install(uavsA);
    internet.Install(gatewayA);

    // 组B (使用静态路由)
    internet.Install(uavsB);
    internet.Install(gatewayB);

    // 组C (使用静态路由)
    internet.Install(uavsC);
    internet.Install(gatewayC);

    // ========== IP地址分配 ==========
    Ipv4AddressHelper address;

    // 组A: 192.168.1.0/24
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesA = address.Assign(staDevsA);
    Ipv4InterfaceContainer apInterfaceA = address.Assign(apDevsA);

    // 组B: 192.168.2.0/24
    address.SetBase("192.168.2.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesB = address.Assign(staDevsB);
    Ipv4InterfaceContainer apInterfaceB = address.Assign(apDevsB);

    // 组C: 192.168.3.0/24
    address.SetBase("192.168.3.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesC = address.Assign(staDevsC);
    Ipv4InterfaceContainer apInterfaceC = address.Assign(apDevsC);

    // 网关间AdHoc网络: 10.0.0.0/24
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer gatewayAdhocInterfaces = address.Assign(gatewayAdhocDevs);

    // ========== 路由配置 ==========
    Ipv4Address gatewayAIP = apInterfaceA.GetAddress(0); // 192.168.1.254 (网关A的AP接口)
    Ipv4Address gatewayBIP = apInterfaceB.GetAddress(0); // 192.168.2.254 (网关B的AP接口)
    Ipv4Address gatewayCIP = apInterfaceC.GetAddress(0); // 192.168.3.254 (网关C的AP接口)

    Ipv4Address gatewayAdhocA = gatewayAdhocInterfaces.GetAddress(0); // 10.0.0.1
    Ipv4Address gatewayAdhocB = gatewayAdhocInterfaces.GetAddress(1); // 10.0.0.2
    Ipv4Address gatewayAdhocC = gatewayAdhocInterfaces.GetAddress(2); // 10.0.0.3

    // 获取各节点的AdHoc接口索引
    // uint32_t adhocIfIndexA = gatewayA.Get(0)->GetObject<Ipv4>()->GetInterfaceForDevice(gatewayAdhocDevs.Get(0));
    // uint32_t adhocIfIndexB = gatewayB.Get(0)->GetObject<Ipv4>()->GetInterfaceForDevice(gatewayAdhocDevs.Get(1));
    // uint32_t adhocIfIndexC = gatewayC.Get(0)->GetObject<Ipv4>()->GetInterfaceForDevice(gatewayAdhocDevs.Get(2));

    // 配置组A的路由
    for (uint32_t i = 0; i < uavsA.GetN(); i++)
    {
        Ptr<Ipv4> ipv4 = uavsA.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);
        // 默认路由指向网关A
        staticRouting->SetDefaultRoute(gatewayAIP, 1);
    }

    // 网关A的路由
    {
        Ptr<Ipv4> ipv4 = gatewayA.Get(0)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);

        // 到192.168.2.0/24 (组B) 的路由: 通过AdHoc网络到网关B
        // staticRouting->AddNetworkRouteTo(Ipv4Address("192.168.2.0"), Ipv4Mask("255.255.255.0"),gatewayAdhocB, adhocIfIndexA);

        // 到192.168.3.0/24 (组C) 的路由: 通过AdHoc网络到网关C
        // staticRouting->AddNetworkRouteTo(Ipv4Address("192.168.3.0"), Ipv4Mask("255.255.255.0"),gatewayAdhocC, adhocIfIndexA);
    }

    // 配置组B的路由
    for (uint32_t i = 0; i < uavsB.GetN(); i++)
    {
        Ptr<Ipv4> ipv4 = uavsB.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);
        staticRouting->SetDefaultRoute(gatewayBIP, 1);
    }

    // 网关B的路由
    {
        Ptr<Ipv4> ipv4 = gatewayB.Get(0)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);

        // 到192.168.1.0/24 (组A) 的路由
        // staticRouting->AddNetworkRouteTo(Ipv4Address("192.168.1.0"), Ipv4Mask("255.255.255.0"),gatewayAdhocA, adhocIfIndexB);

        // 到192.168.3.0/24 (组C) 的路由
        // staticRouting->AddNetworkRouteTo(Ipv4Address("192.168.3.0"), Ipv4Mask("255.255.255.0"),gatewayAdhocC, adhocIfIndexB);
    }

    // 配置组C的路由
    for (uint32_t i = 0; i < uavsC.GetN(); i++)
    {
        Ptr<Ipv4> ipv4 = uavsC.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);
        staticRouting->SetDefaultRoute(gatewayCIP, 1);
    }

    // 网关C的路由
    {
        Ptr<Ipv4> ipv4 = gatewayC.Get(0)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);

        // 到192.168.1.0/24 (组A) 的路由
        // staticRouting->AddNetworkRouteTo(Ipv4Address("192.168.1.0"), Ipv4Mask("255.255.255.0"),gatewayAdhocA, adhocIfIndexC);

        // 到192.168.2.0/24 (组B) 的路由
        // staticRouting->AddNetworkRouteTo(Ipv4Address("192.168.2.0"), Ipv4Mask("255.255.255.0"),gatewayAdhocB, adhocIfIndexC);
    }

    // ========== 启用网关的IP转发 ==========
    gatewayA.Get(0)->GetObject<Ipv4>()->SetAttribute("IpForward", BooleanValue(true));
    gatewayB.Get(0)->GetObject<Ipv4>()->SetAttribute("IpForward", BooleanValue(true));
    gatewayC.Get(0)->GetObject<Ipv4>()->SetAttribute("IpForward", BooleanValue(true));

    // ========== 输出网络配置信息 ==========
    std::cout << "\n========== 网络配置信息 ==========" << std::endl;
    std::cout << "组A (192.168.1.0/24):" << std::endl;
    std::cout << "  网关A AP IP: " << gatewayAIP << std::endl;
    std::cout << "  网关A AdHoc IP: " << gatewayAdhocA << std::endl;
    for (uint32_t i = 0; i < uavsA.GetN(); i++)
    {
        std::cout << "  UAV A[" << i << "] IP: " << interfacesA.GetAddress(i) << std::endl;
    }

    std::cout << "\n组B (192.168.2.0/24):" << std::endl;
    std::cout << "  网关B AP IP: " << gatewayBIP << std::endl;
    std::cout << "  网关B AdHoc IP: " << gatewayAdhocB << std::endl;
    for (uint32_t i = 0; i < uavsB.GetN(); i++)
    {
        std::cout << "  UAV B[" << i << "] IP: " << interfacesB.GetAddress(i) << std::endl;
    }

    std::cout << "\n组C (192.168.3.0/24):" << std::endl;
    std::cout << "  网关C AP IP: " << gatewayCIP << std::endl;
    std::cout << "  网关C AdHoc IP: " << gatewayAdhocC << std::endl;
    for (uint32_t i = 0; i < uavsC.GetN(); i++)
    {
        std::cout << "  UAV C[" << i << "] IP: " << interfacesC.GetAddress(i) << std::endl;
    }

    // ========== 应用层配置 (测试跨组通信) ==========
    uint16_t port = 9;

    // 流量1: 组A UAV0 -> 组B UAV0
    OnOffHelper onoff1("ns3::UdpSocketFactory", Address());
    onoff1.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff1.SetAttribute("PacketSize", UintegerValue(1024));
    onoff1.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff1.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff1.SetAttribute("StartTime", TimeValue(Seconds(2.0)));
    onoff1.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    InetSocketAddress dest1(interfacesB.GetAddress(0), port);
    onoff1.SetAttribute("Remote", AddressValue(dest1));
    ApplicationContainer app1 = onoff1.Install(uavsA.Get(0));

    // 接收端1: 组B UAV0
    PacketSinkHelper sink1("ns3::UdpSocketFactory", Address());
    sink1.SetAttribute("Local", AddressValue(dest1));
    ApplicationContainer sinkApp1 = sink1.Install(uavsB.Get(0));
    sinkApp1.Start(Seconds(0.0));
    sinkApp1.Stop(Seconds(simTime));

    // 流量2: 组B UAV0 -> 组C UAV0
    OnOffHelper onoff2("ns3::UdpSocketFactory", Address());
    onoff2.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff2.SetAttribute("PacketSize", UintegerValue(1024));
    onoff2.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff2.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff2.SetAttribute("StartTime", TimeValue(Seconds(2.0)));
    onoff2.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    InetSocketAddress dest2(interfacesC.GetAddress(0), port + 1);
    onoff2.SetAttribute("Remote", AddressValue(dest2));
    ApplicationContainer app2 = onoff2.Install(uavsB.Get(0));

    // 接收端2: 组C UAV0
    PacketSinkHelper sink2("ns3::UdpSocketFactory", Address());
    sink2.SetAttribute("Local", AddressValue(dest2));
    ApplicationContainer sinkApp2 = sink2.Install(uavsC.Get(0));
    sinkApp2.Start(Seconds(0.0));
    sinkApp2.Stop(Seconds(simTime));

    // 流量3: 组A UAV0 -> 组C UAV0
    OnOffHelper onoff3("ns3::UdpSocketFactory", Address());
    onoff3.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff3.SetAttribute("PacketSize", UintegerValue(1024));
    onoff3.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff3.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff3.SetAttribute("StartTime", TimeValue(Seconds(5.0)));
    onoff3.SetAttribute("StopTime", TimeValue(Seconds(simTime - 1)));

    InetSocketAddress dest3(interfacesC.GetAddress(0), port + 2);
    onoff3.SetAttribute("Remote", AddressValue(dest3));
    ApplicationContainer app3 = onoff3.Install(uavsA.Get(1));

    // 接收端3: 组C UAV0
    PacketSinkHelper sink3("ns3::UdpSocketFactory", Address());
    sink3.SetAttribute("Local", AddressValue(dest3));
    ApplicationContainer sinkApp3 = sink3.Install(uavsC.Get(0));
    sinkApp3.Start(Seconds(0.0));
    sinkApp3.Stop(Seconds(simTime));

    // ========== 流量监控 ==========
    NodeContainer allUavs;
    allUavs.Add(uavsA);
    allUavs.Add(uavsB);
    allUavs.Add(uavsC);

    FlowMonitorHelper flowmon;
    globalFlowHelper = &flowmon; // 设置全局指针
    Ptr<FlowMonitor> monitor = flowmon.Install(allUavs);

    std::ofstream flowStatsFile("uav_flow_stats.csv");
    flowStatsFile << "Time,Flow,Throughput(Kbps),LossRate(%),AvgDelay(ms)" << std::endl;

    Simulator::Schedule(Seconds(1.0), &MonitorFlow, monitor, &flowStatsFile);

    // ========== PCAP追踪 ==========
    phyC.EnablePcapAll("uav_multi_group");

    // ========== 开始仿真 ==========
    std::cout << "\n========== 开始仿真 ==========" << std::endl;
    std::cout << "仿真时间: " << simTime << " 秒" << std::endl;
    std::cout << "流量1: " << interfacesA.GetAddress(0) << " -> " << interfacesB.GetAddress(0) << std::endl;
    std::cout << "流量2: " << interfacesB.GetAddress(0) << " -> " << interfacesC.GetAddress(0) << std::endl;
    std::cout << "流量3: " << interfacesA.GetAddress(1) << " -> " << interfacesC.GetAddress(0) << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ========== 输出流量统计 ==========
    std::cout << "\n========== 流量统计结果 ==========" << std::endl;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    for (auto const &it : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it.first);
        std::cout << "\nFlow " << t.sourceAddress << " -> " << t.destinationAddress << std::endl;
        std::cout << "  Tx Bytes: " << it.second.txBytes << std::endl;
        std::cout << "  Rx Bytes: " << it.second.rxBytes << std::endl;
        std::cout << "  Tx Packets: " << it.second.txPackets << std::endl;
        std::cout << "  Rx Packets: " << it.second.rxPackets << std::endl;

        if (it.second.txPackets > 0)
        {
            double lossRate = 100.0 * (it.second.txPackets - it.second.rxPackets) / it.second.txPackets;
            std::cout << "  丢包率: " << lossRate << " %" << std::endl;
        }

        if (it.second.rxPackets > 0)
        {
            double avgDelay = it.second.delaySum.GetSeconds() / it.second.rxPackets * 1000.0;
            std::cout << "  平均延迟: " << avgDelay << " ms" << std::endl;
        }
    }

    monitor->SerializeToXmlFile("uav_flowmon_results.xml", true, true);
    flowStatsFile.close();

    Simulator::Destroy();

    std::cout << "\n仿真完成!" << std::endl;
    return 0;
}
