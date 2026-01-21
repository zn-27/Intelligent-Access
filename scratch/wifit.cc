/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * 基础 ns-3 示例：包含 PointToPoint, CSMA, WiFi
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include <ns3/internet-apps-module.h>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("BasicMixedExample");

int main(int argc, char *argv[])
{
    bool verbose = true;
    bool tracing = false;
    uint32_t nCsma = 3; // CSMA hosts 数量
    uint32_t nWifi = 3; // WiFi STA 数量

    CommandLine cmd;
    cmd.AddValue("nCsma", "Number of extra CSMA nodes", nCsma);
    cmd.AddValue("nWifi", "Number of wifi STA devices", nWifi);
    cmd.AddValue("verbose", "Tell echo applications to log if true", verbose);
    cmd.AddValue("tracing", "Enable pcap tracing", tracing);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("V4Ping", LOG_LEVEL_INFO);
    }

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
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiStaNodes);
    mobility.Install(wifiApNode);

    // --------------------------
    // 4. Internet Stack
    // --------------------------
    InternetStackHelper stack;
    stack.Install(wifiStaNodes);

    Ipv4AddressHelper address;

    // wifi 网络
    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);

    // --------------------------
    // 5. 应用 (Ping)
    // --------------------------
    Ipv4Address serverAddr = staInterfaces.GetAddress(0);
    V4PingHelper ping(serverAddr);
    ping.SetAttribute("Verbose", BooleanValue(true));
    // ApplicationContainer apps = ping.Install(p2pNodes.Get(0));
    //  apps.Start(Seconds(2.0));
    //  apps.Stop(Seconds(10.0));

    // --------------------------
    // 6. Tracing / Pcap
    // --------------------------
    if (tracing)
    {

        phy.EnablePcap("mixed-wifi", apDevice.Get(0));
    }

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
