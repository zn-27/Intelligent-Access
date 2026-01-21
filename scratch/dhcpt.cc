#include "ns3/mymodule-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/bridge-helper.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-80211p-helper.h" // 802.11p支持，用于Adhoc模式
// Îªµ±Ç°ÎÄ¼þ¶¨Òå¶ÀÁ¢µÄÈÕÖ¾×é¼þ
NS_LOG_COMPONENT_DEFINE("AdhocSimpleIpTest");

using namespace ns3;

void PrintResult(NodeContainer nodes)
{
    NS_LOG_INFO("\n=== ·ÂÕæ 10 ÃëÊ± IP ·ÖÅä½á¹û ===");
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        Ptr<SimpleAdhocIp> app = nodes.Get(i)->GetApplication(0)->GetObject<SimpleAdhocIp>();
        Ipv4Address ip = app->GetAssignedIp();
        NS_LOG_INFO("½Úµã" << i << ":" << (ip == Ipv4Address::GetAny() ? "·ÖÅäÊ§°Ü" : Ipv4ToStr(ip)));
    }
    NS_LOG_INFO("===========================\n");
}

int main(int argc, char *argv[])
{
    LogComponentEnable("AdhocSimpleIpTest", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    uint32_t numNodes = 5;
    std::string ipBase = "192.168.1.0";
    std::string ipMask = "255.255.255.0";
    double simTime = 15.0;

    CommandLine cmd;
    cmd.AddValue("numNodes", "节点数量", numNodes);
    cmd.AddValue("simTime", "仿真时长(秒)", simTime);
    cmd.Parse(argc, argv);

    LogComponentEnable("AdhocSimpleIpTest", LOG_LEVEL_INFO);
    
    LogComponentEnable("AdhocSimpleIp", LOG_LEVEL_INFO);

    // 创建节点
    NodeContainer staNodes;
    staNodes.Create(2); // STA0: DHCP客户端, STA1: DHCP服务器
    NodeContainer apNode;
    apNode.Create(1);   // AP作为中继
    NodeContainer allnodes;
    allnodes.Add(apNode);
    allnodes.Add(staNodes);
    // 设置Wi-Fi信道
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // Wi-Fi设置
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);

    WifiMacHelper mac;
    Ssid ssid = Ssid("ns3-ssid");

    // 安装STA
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    // 安装AP
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < numNodes; i++) {
        posAlloc->Add(Vector(i * 5.0, 0.0, 0.0));
    }
    mobility.SetPositionAllocator(posAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allnodes);

    InternetStackHelper stack;
    stack.Install(staNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("192.168.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs = ipv4.Assign(staDevices);
    // 模拟DHCP Discover: UDP广播 68 -> 67
    uint16_t dhcpServerPort = 67;
    //uint16_t dhcpClientPort = 68;

    UdpEchoServerHelper server(dhcpServerPort);
    ApplicationContainer serverApp = server.Install(staNodes.Get(1)); // STA1作为DHCP服务器
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(10.0));

    UdpEchoClientHelper client(Ipv4Address("255.255.255.255"), dhcpServerPort);
    client.SetAttribute("MaxPackets", UintegerValue(1));
    client.SetAttribute("Interval", TimeValue(Seconds(0.3)));
    client.SetAttribute("PacketSize", UintegerValue(100));
    ApplicationContainer clientApp = client.Install(staNodes.Get(0)); // STA0作为DHCP客户端
    clientApp.Start(Seconds(2.0));
    clientApp.Stop(Seconds(5.0));
    

    

    NS_LOG_INFO("=== 启动仿真（" << numNodes << " 节点，时长 " << simTime << " 秒）===");
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
    