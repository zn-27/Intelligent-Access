/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
#include <iomanip>
#include <iostream>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/aodv-helper.h"
#include "ns3/olsr-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TriangleAdhoc");

// 全局变量：节点容器，用于路由查询
NodeContainer *globalNodes = nullptr;

// 查询从源节点到目的地的路由路径
std::vector<uint32_t> GetRoutePath(uint32_t srcNodeId, Ipv4Address destAddr)
{
    std::vector<uint32_t> path;
    path.push_back(srcNodeId);

    Ptr<Node> srcNode = globalNodes->Get(srcNodeId);
    Ptr<Ipv4> ipv4 = srcNode->GetObject<Ipv4>();
    if (!ipv4)
        return path;

    // 使用OLSR路由协议查询路由
    Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
    if (!routing)
        return path;

    // 构造路由头部
    Socket::SocketErrno sockerr;
    Ipv4Header header;
    header.SetDestination(destAddr);

    Ptr<Ipv4Route> route = routing->RouteOutput(Ptr<Packet>(),
                                                header,
                                                Ptr<NetDevice>(),
                                                sockerr);

    if (!route)
        return path;

    // 获取下一跳
    Ipv4Address nextHop = route->GetGateway();
    if (nextHop == Ipv4Address())
        return path; // 直连，没有中间节点

    // 找到下一跳对应的节点
    for (uint32_t i = 0; i < globalNodes->GetN(); ++i)
    {
        Ptr<Ipv4> nodeIpv4 = globalNodes->Get(i)->GetObject<Ipv4>();
        if (nodeIpv4)
        {
            for (uint32_t j = 0; j < nodeIpv4->GetNInterfaces(); ++j)
            {
                Ipv4InterfaceAddress iface = nodeIpv4->GetAddress(j, 0);
                if (iface.GetLocal() == nextHop || iface.GetBroadcast() == nextHop)
                {
                    uint32_t nextNodeId = i;
                    if (nextNodeId != srcNodeId)
                    {
                        path.push_back(nextNodeId);
                    }
                    return path;
                }
            }
        }
    }

    return path;
}

// 根据源IP和目的IP查找源节点ID
uint32_t GetNodeIdByIp(Ipv4Address ip)
{
    for (uint32_t i = 0; i < globalNodes->GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = globalNodes->Get(i)->GetObject<Ipv4>();
        if (ipv4)
        {
            for (uint32_t j = 0; j < ipv4->GetNInterfaces(); ++j)
            {
                Ipv4InterfaceAddress iface = ipv4->GetAddress(j, 0);
                if (iface.GetLocal() == ip)
                {
                    return i;
                }
            }
        }
    }
    return 0;
}

// 打印路由路径
void PrintRoutePath(Ipv4Address src, Ipv4Address dest)
{
    std::cout << "      Path: ";

    uint32_t srcNodeId = GetNodeIdByIp(src);
    std::vector<uint32_t> path = GetRoutePath(srcNodeId, dest);

    if (path.size() == 1)
    {
        std::cout << "Node" << path[0] << " -> (direct)" << std::endl;
        return;
    }

    for (size_t i = 0; i < path.size(); ++i)
    {
        std::cout << "Node" << path[i];
        if (i < path.size() - 1)
            std::cout << " -> ";
    }
    std::cout << std::endl;
}
// 查找节点ID的辅助函数
int32_t FindNodeIdByIp(Ipv4Address ip)
{
    if (globalNodes == nullptr)
        return -1;
    for (uint32_t i = 0; i < globalNodes->GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = globalNodes->Get(i)->GetObject<Ipv4>();
        if (ipv4)
        {
            // 检查该节点的所有接口
            for (uint32_t j = 0; j < ipv4->GetNInterfaces(); ++j)
            {
                Ipv4InterfaceAddress iface = ipv4->GetAddress(j, 0);
                if (iface.GetLocal() == ip)
                {
                    return i;
                }
            }
        }
    }
    return -1;
}

// 递归查询路由路径
void TraceCurrentRoute(uint32_t currentNodeId, Ipv4Address destAddr, std::vector<uint32_t> &path)
{
    // 防止死循环或路径过长
    if (path.size() > 20)
        return;

    path.push_back(currentNodeId);

    // 获取当前节点 IPv4 对象
    Ptr<Node> node = globalNodes->Get(currentNodeId);
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();

    // 检查是否到达目的地（当前节点拥有目的IP）
    for (uint32_t k = 0; k < ipv4->GetNInterfaces(); ++k)
    {
        if (ipv4->GetAddress(k, 0).GetLocal() == destAddr)
        {
            return; // 到达终点
        }
    }

    // 查询路由表
    Socket::SocketErrno sockerr;
    Ipv4Header header;
    header.SetDestination(destAddr);
    // 模拟一个TTL，防止某些协议检查
    header.SetTtl(64);

    Ptr<Ipv4Route> route = routing->RouteOutput(Ptr<Packet>(), header, Ptr<NetDevice>(), sockerr);

    if (route)
    {
        Ipv4Address nextHop = route->GetGateway();

        // 如果 Gateway 是 0.0.0.0，说明是直连（Directly Connected）
        if (nextHop == Ipv4Address("0.0.0.0"))
        {
            // 下一跳就是目的地IP对应的节点
            int32_t nextNodeId = FindNodeIdByIp(destAddr);
            if (nextNodeId != -1 && (uint32_t)nextNodeId != currentNodeId)
            {
                path.push_back(nextNodeId);
            }
            return;
        }

        // 查找下一跳 IP 对应的节点 ID
        int32_t nextNodeId = FindNodeIdByIp(nextHop);

        // 如果找到了下一跳节点，且不是自己（防止自环），则递归
        if (nextNodeId != -1 && (uint32_t)nextNodeId != currentNodeId)
        {
            TraceCurrentRoute(nextNodeId, destAddr, path);
        }
    }
    // 如果没有路由 (route == null)，路径到此中断
}
// 实时监控特定流的路由
void MonitorFlow(Ipv4Address srcIp, Ipv4Address destIp, std::string description)
{
    std::vector<uint32_t> path;
    int32_t srcId = FindNodeIdByIp(srcIp);

    if (srcId != -1)
    {
        TraceCurrentRoute(srcId, destIp, path);
    }

    // 格式化输出
    std::cout << "[Time " << std::fixed << std::setprecision(1) << Simulator::Now().GetSeconds() << "s] "
              << description << " (" << srcIp << " -> " << destIp << "): ";

    if (path.empty())
    {
        std::cout << "No Route Found (Scanning...)" << std::endl;
    }
    else
    {
        for (size_t i = 0; i < path.size(); ++i)
        {
            std::cout << "Node" << path[i];
            if (i < path.size() - 1)
                std::cout << " -> ";
        }

        // 如果路径最后一个节点不是目的节点，说明路由中断
        int32_t destId = FindNodeIdByIp(destIp);
        if (!path.empty() && (int32_t)path.back() != destId)
        {
            std::cout << " -> (Broken/Searching)";
        }
        std::cout << std::endl;
    }

    // 重新调度自己，每隔 1.0 秒执行一次
    Simulator::Schedule(Seconds(1.0), &MonitorFlow, srcIp, destIp, description);
}
// 定义辅助函数：创建一个Wifi配置，避免代码重复
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

    return wifi.Install(phy, mac, nodes);
}

int main(int argc, char *argv[])
{

    uint32_t nUavs = 12; // 总共12架无人机（3组，每组4架）
    double simTime = 20.0;

    CommandLine cmd;
    cmd.Parse(argc, argv);

    // 1. 创建节点
    NodeContainer uavs;
    uavs.Create(nUavs);
    // 设置全局节点容器，用于路由查询
    NodeContainer uavsCopy(uavs);
    globalNodes = &uavsCopy;

    // 2. 移动性模型 - 三角形布局 + 每组正方形布局 + 随机移动

    // 方法3: 增大节点间距，使跨组通信需要多跳路由
    // 组1中心: (0, 0)
    // 组2中心: (1000, 0)  - 距离组1 1000米
    // 组3中心: (500, 800)  - 距离组1/2 约943米

    std::vector<double> initX;
    std::vector<double> initY;

    // 组1: UAV0-3, 中心(0,0)
    initX.push_back(-200);
    initY.push_back(-200); // UAV0: 左下
    initX.push_back(200);
    initY.push_back(-200); // UAV1: 右下
    initX.push_back(200);
    initY.push_back(200); // UAV2: 右上
    initX.push_back(-200);
    initY.push_back(200); // UAV3: 左上

    // 组2: UAV4-7, 中心(1000,0)
    initX.push_back(950);
    initY.push_back(-50); // UAV4: 左下
    initX.push_back(1050);
    initY.push_back(-50); // UAV5: 右下
    initX.push_back(1050);
    initY.push_back(50); // UAV6: 右上
    initX.push_back(950);
    initY.push_back(50); // UAV7: 左上

    // 组3: UAV8-11, 中心(500,800)
    initX.push_back(1000);
    initY.push_back(1000); // UAV8: 左下
    initX.push_back(1000);
    initY.push_back(1500); // UAV9: 右下
    initX.push_back(1500);
    initY.push_back(1000); // UAV10: 右上
    initX.push_back(1500);
    initY.push_back(1500); // UAV11: 左上

    for (uint32_t i = 0; i < nUavs; i++)
    {
        NodeContainer singleNode;
        singleNode.Add(uavs.Get(i));

        Ptr<ListPositionAllocator> alloc = CreateObject<ListPositionAllocator>();
        alloc->Add(Vector(initX[i], initY[i], 50));

        MobilityHelper nodeMobility;
        nodeMobility.SetPositionAllocator(alloc);
        nodeMobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                      "Mode", StringValue("Time"),
                                      "Time", TimeValue(Seconds(2.0)),
                                      "Speed", StringValue("ns3::ConstantRandomVariable[Constant=10.0]"),
                                      "Bounds", RectangleValue(Rectangle(initX[i] - 20, initX[i] + 20, initY[i] - 20, initY[i] + 20)));
        nodeMobility.Install(singleNode);
    }

    // ==========================================================
    // 3. 配置 Wi-Fi 物理层与链路层 (关键部分)
    // ==========================================================

    WifiHelper wifiCommon;
    wifiCommon.SetStandard(WIFI_STANDARD_80211n_5GHZ);

    WifiMacHelper macAdhoc;
    macAdhoc.SetType("ns3::AdhocWifiMac", "QosSupported", BooleanValue(true));
    wifiCommon.SetRemoteStationManager("ns3::MinstrelHtWifiManager");

    // --- 接口 A: 组内私网 (三个组分别使用不同信道) ---
    // 组1: 信道36 (5180 MHz)
    // 组2: 信道40 (5200 MHz)
    // 组3: 信道44 (5220 MHz)
    // 使用Friis传播模型，增加传播损耗以模拟多跳路由
    YansWifiChannelHelper channelHelperPrivate1;
    channelHelperPrivate1.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelHelperPrivate1.AddPropagationLoss("ns3::FriisPropagationLossModel");
    Ptr<YansWifiChannel> privateChannel1 = channelHelperPrivate1.Create();

    YansWifiChannelHelper channelHelperPrivate2;
    channelHelperPrivate2.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelHelperPrivate2.AddPropagationLoss("ns3::FriisPropagationLossModel");
    Ptr<YansWifiChannel> privateChannel2 = channelHelperPrivate2.Create();

    YansWifiChannelHelper channelHelperPrivate3;
    channelHelperPrivate3.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelHelperPrivate3.AddPropagationLoss("ns3::FriisPropagationLossModel");
    Ptr<YansWifiChannel> privateChannel3 = channelHelperPrivate3.Create();

    YansWifiPhyHelper phyPrivate1;

    phyPrivate1.SetChannel(privateChannel1);
    phyPrivate1.Set("TxPowerStart", DoubleValue(30.0));
    phyPrivate1.Set("TxPowerEnd", DoubleValue(30.0));

    YansWifiPhyHelper phyPrivate2;
    phyPrivate2.SetChannel(privateChannel2);
    phyPrivate2.Set("TxPowerStart", DoubleValue(30.0));
    phyPrivate2.Set("TxPowerEnd", DoubleValue(30.0));

    YansWifiPhyHelper phyPrivate3;
    phyPrivate3.SetChannel(privateChannel3);
    phyPrivate3.Set("TxPowerStart", DoubleValue(30.0));
    phyPrivate3.Set("TxPowerEnd", DoubleValue(30.0));

    // 组1 UAV0-3
    NodeContainer group1;
    for (uint32_t i = 0; i < 4; i++)
        group1.Add(uavs.Get(i));
    NetDeviceContainer privateDevs1 = wifiCommon.Install(phyPrivate1, macAdhoc, group1);

    // 组2 UAV4-7
    NodeContainer group2;
    for (uint32_t i = 4; i < 8; i++)
        group2.Add(uavs.Get(i));
    NetDeviceContainer privateDevs2 = wifiCommon.Install(phyPrivate2, macAdhoc, group2);

    // 组3 UAV8-11
    NodeContainer group3;
    for (uint32_t i = 8; i < 12; i++)
        group3.Add(uavs.Get(i));
    NetDeviceContainer privateDevs3 = wifiCommon.Install(phyPrivate3, macAdhoc, group3);

    // --- 接口 B: 组间公网 (所有组共享信道48) ---
    // 方案1+3: 提高发射功率 + 增加天线增益，实现2km以上远距离通信
    YansWifiChannelHelper channelHelperPublic;
    channelHelperPublic.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelHelperPublic.AddPropagationLoss("ns3::FriisPropagationLossModel");
    Ptr<YansWifiChannel> publicChannel = channelHelperPublic.Create();

    YansWifiPhyHelper phyPublic;
    phyPublic.SetChannel(publicChannel);
    // 方案1: 提高发射功率从30dBm到45dBm (1W → 31.6W)
    phyPublic.Set("TxPowerStart", DoubleValue(20.0)); // 降低功率，迫使多跳路由
    phyPublic.Set("TxPowerEnd", DoubleValue(20.0));
    // 方案3: 增加天线增益10dB (10倍功率等效)
    phyPublic.Set("TxGain", DoubleValue(10.0)); // 发射增益10dB
    phyPublic.Set("RxGain", DoubleValue(10.0)); // 接收增益10dB

    NetDeviceContainer publicDevs1 = wifiCommon.Install(phyPublic, macAdhoc, group1);
    NetDeviceContainer publicDevs2 = wifiCommon.Install(phyPublic, macAdhoc, group2);
    NetDeviceContainer publicDevs3 = wifiCommon.Install(phyPublic, macAdhoc, group3);

    // 合并所有设备
    NetDeviceContainer allPrivateDevs;
    allPrivateDevs.Add(privateDevs1);
    allPrivateDevs.Add(privateDevs2);
    allPrivateDevs.Add(privateDevs3);

    NetDeviceContainer allPublicDevs;
    allPublicDevs.Add(publicDevs1);
    allPublicDevs.Add(publicDevs2);
    allPublicDevs.Add(publicDevs3);

    // ==========================================================
    // 4. 协议栈与路由
    // ==========================================================

    AodvHelper aodv; // 使用OLSR路由协议（MPR机制自动选择多跳路径）
    Ipv4ListRoutingHelper list;
    // 给AodvHelper直接设置属性，这是3.34的正确写法！一一对应之前的优化项
    // aodv.Set("HelloInterval", TimeValue(Seconds(1.0)));      // 心跳包1秒1次
    // aodv.Set("ActiveRouteTimeout", TimeValue(Seconds(5.0))); // 路由超时5秒
    // aodv.Set("RreqRetries", UintegerValue(2));               // 路由请求重试2次
    // aodv.Set("AllowedHelloLoss", UintegerValue(2));          // 2次没收到心跳=邻居失效
    list.Add(aodv, 10);

    InternetStackHelper internet;
    internet.SetRoutingHelper(list);
    internet.Install(uavs);

    // ==========================================================
    // 5. IP 地址分配
    // ==========================================================
    Ipv4AddressHelper ipv4;

    // --- 分配私网地址 ---
    // 组1: 192.168.1.0/24
    ipv4.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer privateIfs1 = ipv4.Assign(privateDevs1);

    // 组2: 192.168.2.0/24
    ipv4.SetBase("192.168.2.0", "255.255.255.0");
    Ipv4InterfaceContainer privateIfs2 = ipv4.Assign(privateDevs2);

    // 组3: 192.168.3.0/24
    ipv4.SetBase("192.168.3.0", "255.255.255.0");
    Ipv4InterfaceContainer privateIfs3 = ipv4.Assign(privateDevs3);

    // 合并私网接口
    Ipv4InterfaceContainer privateIfs;
    privateIfs.Add(privateIfs1);
    privateIfs.Add(privateIfs2);
    privateIfs.Add(privateIfs3);

    // --- 分配公网地址 ---
    // 10.0.1.0/24 (所有组共享)
    ipv4.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer publicIfs1 = ipv4.Assign(publicDevs1);
    Ipv4InterfaceContainer publicIfs2 = ipv4.Assign(publicDevs2);
    Ipv4InterfaceContainer publicIfs3 = ipv4.Assign(publicDevs3);

    // 合并公网接口
    Ipv4InterfaceContainer publicIfs;
    publicIfs.Add(publicIfs1);
    publicIfs.Add(publicIfs2);
    publicIfs.Add(publicIfs3);

    // ==========================================================
    // 6. 应用层测试
    // ==========================================================

    // 测试 1: 组1内通信 (UAV0 -> UAV3，走私网 IP)
    uint16_t port = 9;
    OnOffHelper onoff("ns3::UdpSocketFactory",
                      Address(InetSocketAddress(privateIfs.GetAddress(3), port)));
    onoff.SetConstantRate(DataRate("1Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1024));
    ApplicationContainer app = onoff.Install(uavs.Get(0));
    app.Start(Seconds(5.0));
    app.Stop(Seconds(simTime));

    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
    ApplicationContainer sinkApp = sink.Install(uavs.Get(3));
    sinkApp.Start(Seconds(0.0));

    // 测试 2: 跨组通信 (组1 -> 组2，走公网 IP)
    uint16_t port2 = 10;
    OnOffHelper onoffPublic("ns3::UdpSocketFactory",
                            Address(InetSocketAddress(publicIfs.GetAddress(4), port2)));
    onoffPublic.SetConstantRate(DataRate("1Mbps"));
    ApplicationContainer app2 = onoffPublic.Install(uavs.Get(0));
    app2.Start(Seconds(5.0));
    app2.Stop(Seconds(simTime));

    PacketSinkHelper sink2("ns3::UdpSocketFactory",
                           Address(InetSocketAddress(Ipv4Address::GetAny(), port2)));
    ApplicationContainer sinkApp2 = sink2.Install(uavs.Get(4));
    sinkApp2.Start(Seconds(0.0));

    // 测试 3: 组2内通信 (UAV4 -> UAV7)
    uint16_t port3 = 11;
    OnOffHelper onoffG2("ns3::UdpSocketFactory",
                        Address(InetSocketAddress(privateIfs.GetAddress(7), port3)));
    onoffG2.SetConstantRate(DataRate("1Mbps"));
    ApplicationContainer app3 = onoffG2.Install(uavs.Get(4));
    app3.Start(Seconds(5.0));
    app3.Stop(Seconds(simTime));

    PacketSinkHelper sink3("ns3::UdpSocketFactory",
                           Address(InetSocketAddress(Ipv4Address::GetAny(), port3)));
    ApplicationContainer sinkApp3 = sink3.Install(uavs.Get(7));
    sinkApp3.Start(Seconds(0.0));

    // 测试 4: 跨组通信 (组2 -> 组3，走公网 IP)
    uint16_t port4 = 12;
    OnOffHelper onoffCross("ns3::UdpSocketFactory",
                           Address(InetSocketAddress(publicIfs.GetAddress(11), port4)));
    onoffCross.SetConstantRate(DataRate("1Mbps"));
    ApplicationContainer app4 = onoffCross.Install(uavs.Get(4));
    app4.Start(Seconds(5.0));
    app4.Stop(Seconds(simTime));

    PacketSinkHelper sink4("ns3::UdpSocketFactory",
                           Address(InetSocketAddress(Ipv4Address::GetAny(), port4)));
    ApplicationContainer sinkApp4 = sink4.Install(uavs.Get(11));
    sinkApp4.Start(Seconds(0.0));

    // 测试 5: TCP流量 (组1 -> 组3)
    uint16_t tcpPort = 8080;
    PacketSinkHelper tcpSink("ns3::TcpSocketFactory",
                             Address(InetSocketAddress(Ipv4Address::GetAny(), tcpPort)));
    ApplicationContainer tcpSinkApp = tcpSink.Install(uavs.Get(11));
    tcpSinkApp.Start(Seconds(0.0));
    tcpSinkApp.Stop(Seconds(simTime));

    BulkSendHelper tcpBulkSend("ns3::TcpSocketFactory",
                               Address(InetSocketAddress(publicIfs.GetAddress(11), tcpPort)));
    tcpBulkSend.SetAttribute("MaxBytes", UintegerValue(1 * 1024 * 1024));
    ApplicationContainer tcpApp = tcpBulkSend.Install(uavs.Get(0));
    tcpApp.Start(Seconds(5.0));
    tcpApp.Stop(Seconds(simTime));

    // ==========================================================
    // 7. FlowMonitor 监控与运行
    // ==========================================================

    phyPrivate1.EnablePcap("uav-private-group0", privateDevs1.Get(0), true);
    phyPublic.EnablePcap("uav-public-group0", publicDevs1.Get(0), true);

    phyPrivate2.EnablePcap("uav-private-group1", privateDevs2.Get(0), true);
    phyPublic.EnablePcap("uav-public-group1", publicDevs2.Get(0), true);

    phyPrivate3.EnablePcap("uav-private-group2", privateDevs3.Get(0), true);
    phyPublic.EnablePcap("uav-public-group2", publicDevs3.Get(0), true);

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    NS_LOG_INFO("Starting Simulation...");
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    // 监控 Flow 1: 组1内通信 (UAV0 -> UAV3)
    // 注意：要确保 app 开始后才会有路由，但我们可以从仿真开始就监控
    Simulator::Schedule(Seconds(5.5), &MonitorFlow,
                        privateIfs.GetAddress(0), privateIfs.GetAddress(3), "Flow1(G1-Intra)");

    // 监控 Flow 2: 跨组通信 (UAV0 -> UAV4)
    Simulator::Schedule(Seconds(5.5), &MonitorFlow,
                        publicIfs.GetAddress(0), publicIfs.GetAddress(4), "Flow2(G1->G2)");

    // 监控 Flow 4: 跨组通信 (UAV4 -> UAV8)
    Simulator::Schedule(Seconds(5.5), &MonitorFlow,
                        publicIfs.GetAddress(4), publicIfs.GetAddress(8), "Flow4(G2->G3)");

    // ==========================================================
    // 输出统计信息
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n=================================================================== Flow Monitor Statistics ===================================================================" << std::endl;
    std::cout << std::string(160, '-') << std::endl;

    // 过滤函数：只显示用户创建的flow（特定端口）
    auto isUserFlow = [](const Ipv4FlowClassifier::FiveTuple &t) -> bool
    {
        // 用户应用的端口：9, 10, 11, 12, 8080
        std::set<uint16_t> userPorts = {9, 10, 11, 12, 8080};

        // 只显示UDP和TCP的应用层流量，过滤掉路由控制包
        return (t.protocol == 17 || t.protocol == 6) && // 17=UDP, 6=TCP
               (userPorts.find(t.sourcePort) != userPorts.end() ||
                userPorts.find(t.destinationPort) != userPorts.end());
    };

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        // 过滤：只显示用户创建的flow
        if (!isUserFlow(t))
            continue;

        std::cout << std::setw(5) << "Flow ID:" << i->first << "   "
                  << std::setw(5) << "Source:" << t.sourceAddress << ":" << t.sourcePort << "   "
                  << std::setw(5) << "Destination:" << t.destinationAddress << ":" << t.destinationPort << "   ";

        double throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1000.0;
        std::cout << std::setw(5) << "Throughput:" << std::fixed << std::setprecision(2) << throughput << " Kbps" << "   ";

        double lossRate = (double)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets * 100.0;
        std::cout << std::setw(5) << "Loss Rate:" << std::fixed << std::setprecision(2) << lossRate << " %" << "   ";

        double avgRtt = (i->second.rxPackets > 0) ? i->second.delaySum.GetSeconds() * 1000.0 / i->second.rxPackets : 0.0;
        std::cout << std::setw(5) << "Avg RTT:" << std::fixed << std::setprecision(2) << avgRtt << " ms" << "   ";

        double jitter = (i->second.rxPackets > 1) ? i->second.jitterSum.GetSeconds() * 1000.0 / (i->second.rxPackets - 1) : 0.0;
        std::cout << std::setw(5) << "Jitter:" << std::fixed << std::setprecision(2) << jitter << " ms" << "   ";

        std::cout
            << std::setw(5) << "Send Packets:" << i->second.txPackets << "   "
            << std::setw(5) << "Receive Packets:" << i->second.rxPackets << std::endl;

        // 输出路由路径
        PrintRoutePath(t.sourceAddress, t.destinationAddress);
        std::cout << std::endl;
    }
    std::cout << "========================================================================================================================================================================" << std::endl
              << std::endl;

    Simulator::Destroy();
    NS_LOG_INFO("Done.");

    return 0;
}
