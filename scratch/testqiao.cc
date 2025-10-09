/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/ofswitch13-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/bridge-helper.h"
#include "ns3/applications-module.h"
#include <ns3/internet-apps-module.h>

using namespace ns3;

int main(int argc, char *argv[])
{
    uint16_t simTime = 10;
    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.Parse(argc, argv);

    // ----------------------
    // 1. 创建节点
    // ----------------------
    NodeContainer adhocNodes;
    adhocNodes.Create(3);

    NodeContainer apNode;
    apNode.Create(1);

    Ptr<Node> sdnSwitchNode = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    NodeContainer externalNode;
    externalNode.Create(1);

    // ----------------------
    // 2. WiFi 自组织网络
    // ----------------------
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");

    WifiMacHelper mac;
    Ssid ssid = Ssid("adhoc-net");

    // Adhoc STA 节点
    mac.SetType("ns3::AdhocWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer adhocDevs = wifi.Install(phy, mac, adhocNodes);

    // AP 节点作为接入点（Adhoc 接口）
    mac.SetType("ns3::AdhocWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apAdhocDev = wifi.Install(phy, mac, apNode);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(adhocNodes);
    mobility.Install(apNode);

    // ----------------------
    // 3. SDN 网络接口（CSMA）并桥接
    // ----------------------
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer apCsmaDev = csma.Install(NodeContainer(apNode.Get(0), sdnSwitchNode));
    NetDeviceContainer externalDevs = csma.Install(NodeContainer(sdnSwitchNode, externalNode.Get(0)));

    // 桥接 AP 的 Adhoc 接口和 CSMA 接口
    BridgeHelper bridge;
    bridge.Install(apNode.Get(0), NetDeviceContainer(apAdhocDev.Get(0), apCsmaDev.Get(0)));

    // ----------------------
    // 4. 安装 Internet 栈
    // ----------------------
    InternetStackHelper stack;
    stack.Install(adhocNodes);
    stack.Install(apNode);
    stack.Install(sdnSwitchNode);
    stack.Install(controllerNode);
    stack.Install(externalNode);

    // ----------------------
    // 5. IP 地址分配
    // ----------------------
    Ipv4AddressHelper ipv4;
    Ipv4InterfaceContainer adhocIfs;
    ipv4.SetBase("10.10.0.0", "255.255.255.0");
    adhocIfs = ipv4.Assign(adhocDevs);
    ipv4.Assign(apAdhocDev);

    Ipv4InterfaceContainer sdnIfs;
    ipv4.SetBase("192.168.1.0", "255.255.255.0");
    sdnIfs = ipv4.Assign(apCsmaDev);
    ipv4.Assign(externalDevs);

    // ----------------------
    // 6. SDN 控制器安装
    // ----------------------
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    of13Helper->InstallController(controllerNode);
    of13Helper->InstallSwitch(sdnSwitchNode, apCsmaDev);
    of13Helper->CreateOpenFlowChannels();

    // ----------------------
    // 7. 配置自组织节点默认路由到 AP（网关）
    // ----------------------
    Ipv4StaticRoutingHelper staticRouting;
    Ipv4Address apAdhocIp = adhocIfs.GetAddress(adhocDevs.GetN() - 1); // AP 的 Adhoc IP
    for (uint32_t i = 0; i < adhocNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = adhocNodes.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> sr = staticRouting.GetStaticRouting(ipv4);
        sr->SetDefaultRoute(apAdhocIp, 1);
    }

    // ----------------------
    // 8. 测试：ping 外部节点
    // ----------------------
    Ipv4Address externalIp = ipv4.Assign(externalDevs).GetAddress(1);
    V4PingHelper ping(externalIp);
    ping.SetAttribute("Verbose", BooleanValue(true));
    ApplicationContainer pingApp = ping.Install(adhocNodes.Get(0));
    pingApp.Start(Seconds(1.0));
    pingApp.Stop(Seconds(simTime - 1));

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
