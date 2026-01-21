#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include <ns3/internet-apps-module.h>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MixedRoutingWifiExample");

int main(int argc, char *argv[])
{
    bool verbose = true;
    uint32_t nWifi1 = 2; // 第一个WiFi域（AODV）中的工作站数量
    uint32_t nWifi2 = 2; // 第二个WiFi域（OLSR）中的工作站数量
    bool tracing = false;

    CommandLine cmd;
    cmd.AddValue("nWifi1", "Number of wifi STA devices in AODV domain", nWifi1);
    cmd.AddValue("nWifi2", "Number of wifi STA devices in OLSR domain", nWifi2);
    cmd.AddValue("verbose", "Tell echo applications to log if true", verbose);
    cmd.AddValue("tracing", "Enable pcap tracing", tracing);

    cmd.Parse(argc, argv);

    if (nWifi1 > 18 || nWifi2 > 18)
    {
        std::cout << "nWifi should be 18 or less; otherwise grid layout exceeds the bounding box" << std::endl;
        return 1;
    }

    if (verbose)
    {
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    }

    // 创建第一个WiFi域的节点（AODV协议）
    NodeContainer wifiStaNodes1;
    wifiStaNodes1.Create(nWifi1);
    NodeContainer wifiApNode1;
    wifiApNode1.Create(1);
    NodeContainer domain1Nodes = NodeContainer(wifiStaNodes1, wifiApNode1);

    // 创建第二个WiFi域的节点（OLSR协议）
    NodeContainer wifiStaNodes2;
    wifiStaNodes2.Create(nWifi2);
    NodeContainer wifiApNode2;
    wifiApNode2.Create(1);
    NodeContainer domain2Nodes = NodeContainer(wifiStaNodes2, wifiApNode2);

    // 创建连接两个接入点的有线节点
    NodeContainer csmaNodes;
    csmaNodes.Add(wifiApNode1.Get(0));
    csmaNodes.Add(wifiApNode2.Get(0));

    // 配置WiFi信道和物理层
    YansWifiChannelHelper channel1 = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy1;
    phy1.SetChannel(channel1.Create());

    YansWifiChannelHelper channel2 = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy2;
    phy2.SetChannel(channel2.Create());

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);

    WifiMacHelper mac;
    Ssid ssid1 = Ssid("aodv-domain");
    Ssid ssid2 = Ssid("olsr-domain");

    // 配置第一个WiFi域（AODV）
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid1),
                "ActiveProbing", BooleanValue(false));

    NetDeviceContainer staDevices1;
    staDevices1 = wifi.Install(phy1, mac, wifiStaNodes1);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid1));

    NetDeviceContainer apDevices1;
    apDevices1 = wifi.Install(phy1, mac, wifiApNode1);

    // 配置第二个WiFi域（OLSR）
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid2),
                "ActiveProbing", BooleanValue(false));

    NetDeviceContainer staDevices2;
    staDevices2 = wifi.Install(phy2, mac, wifiStaNodes2);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid2));

    NetDeviceContainer apDevices2;
    apDevices2 = wifi.Install(phy2, mac, wifiApNode2);

    // 配置两个接入点之间的有线连接
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    NetDeviceContainer csmaDevices;
    csmaDevices = csma.Install(csmaNodes);

    // 配置移动模型
    MobilityHelper mobility;

    // 第一个WiFi域的移动性配置（AODV域）
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(0.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(10.0),
                                  "DeltaY", DoubleValue(10.0),
                                  "GridWidth", UintegerValue(3),
                                  "LayoutType", StringValue("RowFirst"));

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds", RectangleValue(Rectangle(-50, 50, -50, 50)));
    mobility.Install(wifiStaNodes1);

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode1);

    // 第二个WiFi域的移动性配置（OLSR域）
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(100.0),
                                  "MinY", DoubleValue(100.0),
                                  "DeltaX", DoubleValue(10.0),
                                  "DeltaY", DoubleValue(10.0),
                                  "GridWidth", UintegerValue(3),
                                  "LayoutType", StringValue("RowFirst"));

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds", RectangleValue(Rectangle(50, 150, 50, 150)));
    mobility.Install(wifiStaNodes2);

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode2);

        AodvHelper aodv;
    OlsrHelper olsr;

    // 为不同域安装不同的路由协议
    InternetStackHelper stack;
    InternetStackHelper stack2;
    // 创建AODV和OLSR路由助手
    // AodvHelper aodv;
    // OlsrHelper olsr;

    // 创建列表路由助手
    Ipv4ListRoutingHelper listRouting;

    // 添加路由协议并设置优先级（数值越da优先级越高）
    listRouting.Add(aodv, 100); // AODV优先级高
    listRouting.Add(olsr, 0);   // OLSR优先级低

    // 将列表路由设置为栈的路由助手
    stack.SetRoutingHelper(listRouting);
    stack.Install(wifiApNode1);
    stack.Install(wifiStaNodes1);

    Ipv4ListRoutingHelper listRouting2;
    // 添加路由协议并设置优先级（数值越da优先级越高）
    listRouting2.Add(aodv, 0);   // AODV优先级高
    listRouting2.Add(olsr, 100); // OLSR优先级低

    // 将列表路由设置为栈的路由助手
    stack2.SetRoutingHelper(listRouting2);
    stack2.Install(wifiApNode2);
    stack2.Install(wifiStaNodes2);

    // 分配IP地址
    Ipv4AddressHelper address;

    // 第一个WiFi域（AODV）
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staInterfaces1, apInterface1;
    staInterfaces1 = address.Assign(staDevices1);
    apInterface1 = address.Assign(apDevices1);

    // 两个接入点之间的有线连接
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterfaces;
    csmaInterfaces = address.Assign(csmaDevices);

    // 第二个WiFi域（OLSR）
    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer staInterfaces2, apInterface2;
    staInterfaces2 = address.Assign(staDevices2);
    apInterface2 = address.Assign(apDevices2);

    // Ping from Domain A host 0 to Domain B host 0 (cross-domain ping)
    Ipv4Address dst = staInterfaces2.GetAddress(0); // first host in domain B
    V4PingHelper ping(dst);
    ping.SetAttribute("Verbose", BooleanValue(true));
    ApplicationContainer pingApp = ping.Install(wifiStaNodes1.Get(1));
    pingApp.Start(Seconds(1.0));
    pingApp.Stop(Seconds(10 - 1));

    // 配置仿真结束时间
    Simulator::Stop(Seconds(20.0));

    // 启用追踪
    if (tracing == true)
    {
        phy1.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        phy1.EnablePcap("aodv-domain", apDevices1.Get(0));
        phy2.EnablePcap("olsr-domain", apDevices2.Get(0));
        csma.EnablePcap("cross-domain-csma", csmaDevices.Get(0), true);
    }

    // 运行仿真
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
