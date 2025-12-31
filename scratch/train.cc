/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Single-controller, cross-domain SDN example with ns-3 + ofswitch13
 *
 * - Domain A: hostsA (2 hosts) -- sw1
 * - Domain B: hostsB (2 hosts) -- sw2
 * - Router node connects to sw1 and sw2 (has IPs in both subnets)
 * - Single OpenFlow controller manages sw1 and sw2
 *
 * Build: make sure ns-3 is built with ofswitch13 module.
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
    cmd.AddValue("simTime", "simulate time ", simTime); //
    cmd.AddValue("verbose", "enable verbose logs", verbose);
    cmd.AddValue("trace", "enable trace /pcap", trace);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        /* code */
    }

    // Enable checksum computations (required by OFSwitch13 module)
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
    // Connect Domain A hosts to sw1
    for (uint32_t i = 0; i < hostsA.GetN(); ++i)
    {
        NodeContainer pair(hostsA.Get(i), sw1);
        NetDeviceContainer link = csma.Install(pair);
        hostDevsA.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }
    // Connect Domain B hosts to sw1
    for (uint32_t i = 0; i < hostsB.GetN(); ++i)
    {
        NodeContainer pair(hostsB.Get(i), sw2);
        NetDeviceContainer link = csma.Install(pair);
        hostDevsB.Add(link.Get(0));
        sw2Devsports.Add(link.Get(1));
    }

    // Connect router to sw1 (network A)
    {
        NodeContainer pair(routerNode1, sw1);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsA.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }
    // Connect router to sw2 (network B)
    {
        NodeContainer pair(routerNode1, sw2);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsB.Add(link.Get(0));  // router interface in net B
        sw2Devsports.Add(link.Get(1)); // add this port to sw2
    }
    // Connect router to sw3 (network C)
    {
        NodeContainer pair(routerNode1, sw3);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsC.Add(link.Get(0));  // router interface in net C
        sw3Devsports.Add(link.Get(1)); // add this port to sw3
    }
    csma.EnablePcapAll("csma-trace", true);
    // wifi
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
    mobility.Install(wifiStaNodes);
    mobility.Install(wifiApNode);

    // Connect Domain C hosts to sw3
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
    // 4. Internet Stack
    // --------------------------
    // Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));
    // Config::SetDefault("ns3::Ipv4::IpForward", BooleanValue(true));

    // Set up static default routes on hosts to the router
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
    // Assign IPv4 addresses
    Ipv4AddressHelper ipv4;

    Ipv4InterfaceContainer ifA; // domain A hosts + router interface
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    // assign addresses to hostsA devices + routerDevsA (must combine)
    {
        NetDeviceContainer netA = NetDeviceContainer();
        // host devices first
        for (uint32_t i = 0; i < hostDevsA.GetN(); ++i)
            netA.Add(hostDevsA.Get(i));
        // router interface for net A
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

    // wifi 网络
    Ipv4InterfaceContainer ifC;
    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    {
        NetDeviceContainer netC = NetDeviceContainer();
        // host devices first
        for (uint32_t i = 0; i < staDevices.GetN(); ++i)
            netC.Add(staDevices.Get(i));

        // router interface for net A
        for (uint32_t i = 0; i < routerDevsC.GetN(); ++i)
            netC.Add(routerDevsC.Get(i));
        for (uint32_t i = 0; i < adhocDevices.GetN(); ++i)
            netC.Add(adhocDevices.Get(i)); // AdHoc 接口
        ifC = ipv4.Assign(netC);
    } // wifi 网络

    ///----------------------------------------///
    // Router's IP in net A is the *last* assigned in ifA (we assigned hosts then router)
    Ipv4Address routerA = ifA.GetAddress(hostDevsA.GetN()); // index after hosts
    Ipv4Address routerB = ifB.GetAddress(hostDevsB.GetN());
    Ipv4Address routerC = ifC.GetAddress(staDevices.GetN());
    // For hosts in C: set default route to routerC
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Node> h = wifiStaNodes.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        // uint32_t staIfIndex = wifiStaNodes.Get(i)->GetObject<Ipv4>()->GetInterfaceForDevice(staDevices.Get(i));
        staticRouting->SetDefaultRoute(routerC, 1);
    }

    // For hosts in A: set default route to routerA
    for (uint32_t i = 0; i < hostsA.GetN(); ++i)
    {
        Ptr<Node> h = hostsA.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerA, 1);
    }

    // For hosts in B: set default route to routerB
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

    // (Optional) Enable pcap/traces
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

    //
    Ipv4Address dst = ifB.GetAddress(2); // first host in domain C
    V4PingHelper ping(dst);
    ping.SetAttribute("Verbose", BooleanValue(true));
    ApplicationContainer pingApp = ping.Install(wifiStaNodes.Get(1)); //

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