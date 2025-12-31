#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-apps-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ApStaDhcpExample");

int main(int argc, char *argv[])
{
    double simTime = 20.0;
    bool verbose = true;

    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation Time (s)", simTime);
    cmd.AddValue("verbose", "Enable Log", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("ApStaDhcpExample", LOG_LEVEL_INFO);
        LogComponentEnable("StaWifiMac", LOG_LEVEL_INFO);
        LogComponentEnable("ApWifiMac", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
        LogComponentEnable("DhcpServer", LOG_LEVEL_ALL);
        LogComponentEnable("DhcpClient", LOG_LEVEL_ALL);
    }

    // ===== 1. 创建节点 =====
    NodeContainer existingStaNodes;
    existingStaNodes.Create(3);                 // 0,1,2
    Ptr<Node> newNode = CreateObject<Node>();   // 3
    Ptr<Node> apNode = existingStaNodes.Get(0); // StaC-1 作为 AP

    // ===== 2. WiFi PHY + Channel =====
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(18.0));
    phy.Set("TxPowerEnd", DoubleValue(18.0));

    // ===== 3. WiFi MAC (AP–STA) =====
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::MinstrelWifiManager");

    WifiMacHelper mac;
    Ssid ssid = Ssid("DomainC-WiFi");

    // AP
    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "BeaconGeneration", BooleanValue(true));
    NetDeviceContainer apDev = wifi.Install(phy, mac, apNode);

    // STA（原 StaC-2, StaC-3 + newNode）
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(true));
    NodeContainer staNodes;
    staNodes.Add(existingStaNodes.Get(1));
    staNodes.Add(existingStaNodes.Get(2));
    staNodes.Add(newNode);
    NetDeviceContainer staDevs = wifi.Install(phy, mac, staNodes);

    // ===== 4. Mobility =====
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(100.0, 100.0, 0.0)); // AP
    posAlloc->Add(Vector(120.0, 100.0, 0.0)); // STA-1
    posAlloc->Add(Vector(140.0, 100.0, 0.0)); // STA-2
    posAlloc->Add(Vector(160.0, 100.0, 0.0)); // new STA
    mobility.SetPositionAllocator(posAlloc);
    mobility.Install(apNode);
    mobility.Install(staNodes);

    // ===== 5. Internet Stack =====
    InternetStackHelper stack;
    stack.Install(apNode);
    stack.Install(staNodes);

    // ===== 6. DHCP 配置 =====
    DhcpHelper dhcpHelper;

    // AP 作为 DHCP 服务器，固定 IP
    Ipv4Address apFixedIp("10.3.1.1");
    Ipv4InterfaceContainer apIf = dhcpHelper.InstallFixedAddress(apDev.Get(0), apFixedIp, Ipv4Mask("/24"));

    // DHCP server: IP pool 10.3.1.100 ~ 10.3.1.150
    ApplicationContainer dhcpServerApp = dhcpHelper.InstallDhcpServer(
        apDev.Get(0),
        apFixedIp,                 // Server IP
        Ipv4Address("10.3.1.0"),   // Network address
        Ipv4Mask("/24"),           // Netmask
        Ipv4Address("10.3.1.100"), // Pool start
        Ipv4Address("10.3.1.150"), // Pool end
        apFixedIp);                // Gateway
    dhcpServerApp.Start(Seconds(0.0));
    dhcpServerApp.Stop(Seconds(simTime));

    // DHCP client（新节点 STA）
    NetDeviceContainer dhcpClientDev;
    dhcpClientDev.Add(staDevs.Get(0));
    dhcpClientDev.Add(staDevs.Get(1));
    dhcpClientDev.Add(staDevs.Get(2)); // newNode 对应设备
    ApplicationContainer dhcpClientApps = dhcpHelper.InstallDhcpClient(dhcpClientDev);
    dhcpClientApps.Start(Seconds(1.0));
    // dhcpClientApps.Stop(Seconds(simTime));

    // ===== 7. UDP Echo 验证 =====
    uint16_t port = 9;
    // AP 作为服务器
    UdpEchoServerHelper server(port);
    ApplicationContainer serverApp = server.Install(apNode);
    serverApp.Start(Seconds(3.0));
    // serverApp.Stop(Seconds(simTime));

    // 新节点作为客户端
    UdpEchoClientHelper client(apFixedIp, port);
    client.SetAttribute("MaxPackets", UintegerValue(10));
    client.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    client.SetAttribute("PacketSize", UintegerValue(512));
    ApplicationContainer clientApp = client.Install(newNode);
    clientApp.Start(Seconds(7.0));
    clientApp.Stop(Seconds(simTime));

    // ===== 8. 运行仿真 =====
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("AP–STA DHCP Detection End.");
    return 0;
}
