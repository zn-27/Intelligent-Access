/*
 * SPDX-License-Identifier: GPL-2.0-only
 */
/*
 * 参考ns3，tuns-3-dev/examples/tutorial/third.cc，拓展升级
   本代码为两个WiFi_aodv网络，相连通讯，
   每个wifi域内，有一个担任ap节点与外部通讯
   测试了左侧最后一个节点发送给右侧最后一个节点跨域通讯

   作者：伍一鑫
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ssid.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/flow-monitor-module.h"  // 引入FlowMonitor
#include "ns3/flow-monitor-helper.h"
#include "ns3/aodv-helper.h" // AODV路由协议
#include "ns3/olsr-helper.h"
#include "ns3/wifi-80211p-helper.h" // 802.11p支持，用于Adhoc模式
//#include "ns3/openflow-module.h"//openflow

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiToWifiWirelessExample");

int main(int argc, char* argv[])
{
    bool verbose = true;
    uint32_t nWifiLeft = 3;  // 左侧WiFi STA数量
    uint32_t nWifiRight = 3; // 右侧WiFi STA数量
    bool tracing = false;
    double distance = 20.0;  // 两个AP之间的距离
    //=============cmd==================
    CommandLine cmd(__FILE__);
    cmd.AddValue("nWifiLeft", "Number of left wifi STA devices", nWifiLeft);
    cmd.AddValue("nWifiRight", "Number of right wifi STA devices", nWifiRight);
    cmd.AddValue("distance", "Distance between two APs (m)", distance);
    cmd.AddValue("verbose", "Tell echo applications to log if true", verbose);
    cmd.AddValue("tracing", "Enable pcap tracing", tracing);
    //======================
    cmd.Parse(argc, argv);

    // 限制节点数量，避免超出网格布局
    if (nWifiLeft > 18 || nWifiRight > 18)
    {
        std::cout << "nWifi should be 18 or less; otherwise grid layout exceeds the bounding box"
                  << std::endl;
        return 1;
    }

    if (verbose)
    {
        LogComponentEnable("WifiToWifiWirelessExample", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
        // LogComponentEnable("AodvRoutingProtocol", LOG_LEVEL_DEBUG);  // 可选：启用AODV日志
    }

    // 创建两个AP节点（原点对点链路的两个节点）
    NodeContainer apNodes;
    apNodes.Create(2);

    // 左侧WiFi网络
    NodeContainer leftWifiStaNodes;
    leftWifiStaNodes.Create(nWifiLeft);
    NodeContainer leftWifiApNode = apNodes.Get(0);  // 左侧AP

    // 右侧WiFi网络
    NodeContainer rightWifiStaNodes;
    rightWifiStaNodes.Create(nWifiRight);
    NodeContainer rightWifiApNode = apNodes.Get(1);  // 右侧AP

    // 配置主无线信道（用于AP之间的通信和各自的STA）
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    

    // 配置WiFiHelper，使用Adhoc模式
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a); // 使用802.11a标准，支持更高数据率
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate54Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps"));

    //============跨域组网==================
    // 配置AP之间的Adhoc模式
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    Ssid adhocSsid = Ssid("ns3-adhoc-ssid");
    mac.SetType("ns3::AdhocWifiMac", "Ssid", SsidValue(adhocSsid));
    // 安装AP之间的无线设备
    NetDeviceContainer apDevices;
    apDevices = wifi.Install(phy, mac, apNodes);
    //======================================

    //============左侧组网==================
    phy.SetChannel(channel.Create());
    // 重新配置MAC，用于左侧STA
    Ssid ssidLeft = Ssid("ns3-left-ssid");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidLeft), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer leftStaDevices = wifi.Install(phy, mac, leftWifiStaNodes);

    // 左侧AP设备（使用AP模式）
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidLeft));
    NetDeviceContainer leftApDevices = wifi.Install(phy, mac, leftWifiApNode);
    //======================================

    //============右侧组网==================
    phy.SetChannel(channel.Create());
    // 右侧STA设备
    Ssid ssidRight = Ssid("ns3-right-ssid");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidRight), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer rightStaDevices = wifi.Install(phy, mac, rightWifiStaNodes);

    // 右侧AP设备
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidRight));
    NetDeviceContainer rightApDevices = wifi.Install(phy, mac, rightWifiApNode);
    //======================================

    // 配置移动模型
    MobilityHelper mobility;

    // 左侧STA节点移动性
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(-40.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(5.0),
                                  "DeltaY", DoubleValue(10.0),
                                  "GridWidth", UintegerValue(3),
                                  "LayoutType", StringValue("RowFirst"));

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds", RectangleValue(Rectangle(-80, -20, -50, 50)));
    mobility.Install(leftWifiStaNodes);

    // 右侧STA节点移动性
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(25.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(5.0),
                                  "DeltaY", DoubleValue(10.0),
                                  "GridWidth", UintegerValue(3),
                                  "LayoutType", StringValue("RowFirst"));

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds", RectangleValue(Rectangle(20, 80, -50, 50)));
    mobility.Install(rightWifiStaNodes);

    // AP节点固定位置
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNodes);

    // 设置AP位置，相距distance米
    Ptr<ConstantPositionMobilityModel> leftApMobility = 
        leftWifiApNode.Get(0)->GetObject<ConstantPositionMobilityModel>();
    leftApMobility->SetPosition(Vector(-distance/2, 0.0, 0.0));  // 左侧AP位置

    Ptr<ConstantPositionMobilityModel> rightApMobility = 
        rightWifiApNode.Get(0)->GetObject<ConstantPositionMobilityModel>();
    rightApMobility->SetPosition(Vector(distance/2, 0.0, 0.0));  // 右侧AP位置
    //sdn的控制器和交换机节点
    //=====================

    // =============安装互联网协议栈，使用AODV路由协议=========================
    InternetStackHelper stack;
    AodvHelper aodv;  // AODV适用于无线自组织网络
    stack.SetRoutingHelper(aodv);
    stack.Install(leftWifiStaNodes);
    stack.Install(leftWifiApNode);
    stack.Install(rightWifiStaNodes);
    stack.Install(rightWifiApNode);
    //=======================================================================
    // 分配IP地址
    Ipv4AddressHelper address;

    // 左侧WiFi网络
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer leftStaInterfaces = address.Assign(leftStaDevices);
    Ipv4InterfaceContainer leftApInterface = address.Assign(leftApDevices);

    // AP之间的无线链路（新网段）
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterfaces = address.Assign(apDevices);

    // 右侧WiFi网络
    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer rightStaInterfaces = address.Assign(rightStaDevices);
    Ipv4InterfaceContainer rightApInterface = address.Assign(rightApDevices);


    //================应用层=====================
    //左侧最后一个节点发送给右侧最后一个节点
    //==========================================
    // 安装UDP回显服务器在右侧的一个STA节点上



    UdpEchoServerHelper echoServer(9);
    ApplicationContainer serverApps = echoServer.Install(rightWifiStaNodes.Get(nWifiRight - 1));
    serverApps.Start(Seconds(1));
    serverApps.Stop(Seconds(28));

    // 安装UDP回显客户端在左侧的一个STA节点上
    UdpEchoClientHelper echoClient(rightStaInterfaces.GetAddress(nWifiRight - 1), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(10));  // 增加包数量以便更好地观察
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(leftWifiStaNodes.Get(nWifiLeft - 1));
    clientApps.Start(Seconds(5));
    clientApps.Stop(Seconds(25));



    //==========================================



    // // 配置FlowMonitor
    // FlowMonitorHelper flowmon;
    // Ptr<FlowMonitor> monitor = flowmon.InstallAll();

// ======================启用流量监控======================
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll ();
    Simulator::Stop(Seconds(30));
// ======================启用流量监控======================
    if (tracing)
    {
        phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        phy.EnablePcap("pcapfile/wifitoleft", leftApDevices.Get(0));
        phy.EnablePcap("pcapfile/wifitoright", rightApDevices.Get(0));
        phy.EnablePcap("pcapfile/wifitowifiadhoc", apDevices);
    }

    NS_LOG_INFO("=============================Run Simulation======================================");

    Simulator::Run();

    // // 输出FlowMonitor统计结果
    // monitor->CheckForLostPackets();
    // Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    // std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    // for (auto &it : stats) {
    //     Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it.first);

    //     std::cout << "Flow " << it.first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
    //     std::cout << "  发送包数: " << it.second.txPackets << " 收到包数: " << it.second.rxPackets << "\n";
    //     if (it.second.rxPackets > 0) {
    //         std::cout << "  吞吐量: " << it.second.rxBytes * 8.0 / 
    //             (it.second.timeLastRxPacket - it.second.timeFirstTxPacket).GetSeconds() / 1e6 << " Mbps\n";
    //         std::cout << "  平均延迟: " << it.second.delaySum.GetSeconds() / it.second.rxPackets << " s\n";
    //     }
    //     std::cout << "  丢包率: " << (it.second.txPackets - it.second.rxPackets) / 
    //         (double)it.second.txPackets << "\n\n";
    // }
  // ===============输出流量统计信息================
    NS_LOG_INFO("===================输出流量统计信息====================");

    flowMonitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i) 
 {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    if(t.protocol != 97)
    {
        
        NS_LOG_UNCOND ("Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")");
        NS_LOG_UNCOND ("  Tx Packets: " << i->second.txPackets);
        NS_LOG_UNCOND ("  Rx Packets: " << i->second.rxPackets);
        NS_LOG_UNCOND ("  Lost Packets: " << i->second.lostPackets);
        NS_LOG_UNCOND ("  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 << " Kbps");
  
    }
    
 }
//========================================================
    Simulator::Destroy();
    return 0;
}
