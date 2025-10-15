/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块
 *
 * - 域 A: hostsA (2 主机) -- sw1
 * - 域 B: hostsB (2 主机) -- sw2
 * - 路由器节点连接 sw1 和 sw2（有两个子网的 IP）
 * - 单 OpenFlow 控制器管理 sw1 和 sw2
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
int main(int argc, char *argv[])
{
    uint16_t simTime = 10;
    bool verbose = true;
    bool trace = false;

    CommandLine cmd;
    cmd.AddValue("simTime", "simulate time ", simTime); // 设置仿真时间
    cmd.AddValue("verbose", "enable verbose logs", verbose);
    cmd.AddValue("trace", "enable trace /pcap", trace);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        /* code */
    }

    // 启用校验和计算（ofswitch13 模块所需）
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    NodeContainer hostsA;
    hostsA.Create(2);
    NodeContainer hostsB;
    hostsB.Create(2);

    Ptr<Node> sw1 = CreateObject<Node>();
    Ptr<Node> sw2 = CreateObject<Node>();
    Ptr<Node> sw3 = CreateObject<Node>();
    Ptr<Node> routerNode1 = CreateObject<Node>();
    // Ptr<Node> routerNode2 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer hostDevsA, hostDevsB, ApDevsC;
    NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports;
    NetDeviceContainer routerDevsA, routerDevsB, routerDevsC;
    // 连接域 A 主机到 sw1
    for (uint32_t i = 0; i < hostsA.GetN(); ++i)
    {
        NodeContainer pair(hostsA.Get(i), sw1);
        NetDeviceContainer link = csma.Install(pair);
        hostDevsA.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }
    // 连接域 B 主机到 sw2
    for (uint32_t i = 0; i < hostsB.GetN(); ++i)
    {
        NodeContainer pair(hostsB.Get(i), sw2);
        NetDeviceContainer link = csma.Install(pair);
        hostDevsB.Add(link.Get(0));
        sw2Devsports.Add(link.Get(1));
    }

    // 连接路由器到 sw1（域 A 网络）
    {
        NodeContainer pair(routerNode1, sw1);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsA.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }
    // 连接路由器到 sw2（域 B 网络）
    {
        NodeContainer pair(routerNode1, sw2);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsB.Add(link.Get(0));  // 路由器在 B 网络的接口
        sw2Devsports.Add(link.Get(1)); // 将这个端口加入sw2
    }
    // 连接路由器到 sw3（域 C 网络）
    {
        NodeContainer pair(routerNode1, sw3);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsC.Add(link.Get(0));  // 路由器在 C 网络的接口
        sw3Devsports.Add(link.Get(1)); // 将这个端口添加到sw3
    }
    csma.EnablePcapAll("csma-trace", true);
    // wifi配置部分
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(3);
    NodeContainer wifiApNode;
    wifiApNode.Create(1);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");
    WifiMacHelper mac;
    Ssid ssid = Ssid("C");

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer adhocDevices = wifi.Install(phy, mac, wifiStaNodes);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);
    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Time", TimeValue(Seconds(1.0)),                                      // 每次移动的间隔时间
                              "Speed", StringValue("ns3::UniformRandomVariable[Min=0.1|Max=0.5]")); // 移动速度
    mobility.Install(wifiStaNodes);

    // 连接域 C 主机到 sw3
    {
        NodeContainer pair(wifiApNode.Get(0), sw3);
        NetDeviceContainer link = csma.Install(pair);
        ApDevsC.Add(link.Get(0));
        sw3Devsports.Add(link.Get(1));
    }
    BridgeHelper bridge;
    NetDeviceContainer bridgeDev;
    bridgeDev = bridge.Install(wifiApNode.Get(0),
                               NetDeviceContainer(apDevice.Get(0), ApDevsC.Get(0)));

    Ptr<OFSwitch13InternalHelper> of13Helper =
        CreateObject<OFSwitch13InternalHelper>();

    of13Helper->InstallController(controllerNode);
    of13Helper->InstallSwitch(sw1, sw1Devsports);
    of13Helper->InstallSwitch(sw2, sw2Devsports);
    of13Helper->InstallSwitch(sw3, sw3Devsports);
    of13Helper->CreateOpenFlowChannels();
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

    InternetStackHelper stack;

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
    stack2.Install(hostsA);
    stack2.Install(hostsB);
    stack.Install(routerNode1);

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true)); // 启用转发
    }
    // 分配IPv4地址
    Ipv4AddressHelper ipv4;

    Ipv4InterfaceContainer ifA; // 域A主机和路由器接口
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    // 给 hostsA 设备和 routerDevsA 分配地址
    {
        NetDeviceContainer netA = NetDeviceContainer();
        // 主机先分配
        for (uint32_t i = 0; i < hostDevsA.GetN(); ++i)
            netA.Add(hostDevsA.Get(i));
        // 为A域配置路由器接口
        for (uint32_t i = 0; i < routerDevsA.GetN(); ++i)
            netA.Add(routerDevsA.Get(i));
        ifA = ipv4.Assign(netA);
    }

    Ipv4InterfaceContainer ifB;
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    {
        NetDeviceContainer netB = NetDeviceContainer();
        for (uint32_t i = 0; i < hostDevsB.GetN(); ++i)
            netB.Add(hostDevsB.Get(i));
        for (uint32_t i = 0; i < routerDevsB.GetN(); ++i)
            netB.Add(routerDevsB.Get(i));
        ifB = ipv4.Assign(netB);
    }

    // wifi 网络配置
    Ipv4InterfaceContainer ifC;
    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    {
        NetDeviceContainer netC = NetDeviceContainer();
        // 先配置主机
        for (uint32_t i = 0; i < staDevices.GetN(); ++i)
            netC.Add(staDevices.Get(i));

        // 为A域配置路由器接口
        for (uint32_t i = 0; i < routerDevsC.GetN(); ++i)
            netC.Add(routerDevsC.Get(i));
        for (uint32_t i = 0; i < adhocDevices.GetN(); ++i)
            netC.Add(adhocDevices.Get(i)); // AdHoc 接口
        ifC = ipv4.Assign(netC);
    } // wifi 网络

    ///----------------------------------------///
    // 路由器的 IP 地址配置
    Ipv4Address routerA = ifA.GetAddress(hostDevsA.GetN()); // 路由器A的地址
    Ipv4Address routerB = ifB.GetAddress(hostDevsB.GetN());
    Ipv4Address routerC = ifC.GetAddress(staDevices.GetN());
    // 为 C 网络的主机设置默认路由到路由器 C
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Node> h = wifiStaNodes.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        // uint32_t staIfIndex = wifiStaNodes.Get(i)->GetObject<Ipv4>()->GetInterfaceForDevice(staDevices.Get(i));
        staticRouting->SetDefaultRoute(routerC, 1);
    }

    // 为 A 网络的主机设置默认路由到路由器 A
    for (uint32_t i = 0; i < hostsA.GetN(); ++i)
    {
        Ptr<Node> h = hostsA.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerA, 1);
    }

    // 为 B 网络的主机设置默认路由到路由器 B
    for (uint32_t i = 0; i < hostsB.GetN(); ++i)
    {
        Ptr<Node> h = hostsB.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerB, 1);
    }
    phy.EnablePcap("adhocpcap", adhocDevices);
    phy.EnablePcap("stapcap", staDevices);
    phy.EnablePcap("appcap", apDevice);

    // 启用pcap追踪
    if (true)
    {
        of13Helper->EnableOpenFlowPcap("openflow-interdomain");
        of13Helper->EnableDatapathStats("switch-stats");
        csma.EnablePcap("sw1", sw1Devsports, true);
        csma.EnablePcap("sw2", sw2Devsports, true);

        csma.EnablePcap("hostA", hostDevsA);
        csma.EnablePcap("hostB", hostDevsB);
        // 开启 PCAP
    }

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
    {
        Ipv4StaticRoutingHelper staticRoutingHelper2;
        Ptr<Ipv4> ipv42 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);

        s->AddHostRouteTo(ifC.GetAddress(0), 1);
        s->AddHostRouteTo(ifC.GetAddress(1), 1);
        s->AddHostRouteTo(ifC.GetAddress(2), 1);
        s->AddHostRouteTo(ifC.GetAddress(3), 1);
        s->AddHostRouteTo(ifC.GetAddress(i + 4), 0);
    }
    Ipv4StaticRoutingHelper staticRoutingHelper2;
    Ptr<Ipv4> ipv42 = wifiStaNodes.Get(1)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);

    // s->AddHostRouteTo(ifC.GetAddress(3), 1);
    //   Ping from Domain A hos1t 0 to Domain B host 0 (cross-domain ping)
    //   Ipv4Address dst1 = ifB.GetAddress(1); // first host in domain C
    //   V4PingHelper ping2(dst1);
    //   ping2.SetAttribute("Verbose", BooleanValue(true));
    //   ApplicationContainer pingApp2 = ping2.Install(wifiStaNodes.Get(1));

    // Ping from Domain A host 0 to Domain B host 0 (cross-domain ping)
    Ipv4Address dst = ifC.GetAddress(2); // first host in domain C
    V4PingHelper ping(dst);
    ping.SetAttribute("Verbose", BooleanValue(true));
    ApplicationContainer pingApp = ping.Install(wifiStaNodes.Get(1));

    pingApp.Start(Seconds(1.0));
    pingApp.Stop(Seconds(simTime - 1));
    // guan bi adhoc

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
    {
        Ptr<Node> node = wifiStaNodes.Get(i);
        Ptr<NetDevice> dev = adhocDevices.Get(i); // 假设每个 STA 的 AdHoc 接口索引相同

        // 关闭
        Simulator::Schedule(Seconds(0.0), &DisableDeviceLogical, node, dev);

        // 第 7 秒开启
        Simulator::Schedule(Seconds(7.0), &EnableDeviceLogical, node, dev);
    }
    {
        std::cout << "ap mac" << std::endl;
        for (uint32_t j = 0; j < apDevice.GetN(); ++j)
        {
            Ptr<NetDevice> dev = apDevice.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        std::cout << "sta mac" << std::endl;
        for (uint32_t j = 0; j < staDevices.GetN(); ++j)
        {
            Ptr<NetDevice> dev = staDevices.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        //
        std::cout << "adhoc mac" << std::endl;
        for (uint32_t j = 0; j < adhocDevices.GetN(); ++j)
        {
            Ptr<NetDevice> dev = adhocDevices.Get(j);
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
    Simulator::Schedule(Seconds(2.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.1), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.3), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}