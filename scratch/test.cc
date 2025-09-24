#include "ns3/mymodule-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"

// 为当前文件定义独立的日志组件
NS_LOG_COMPONENT_DEFINE("AdhocSimpleIpTest");

using namespace ns3;

void PrintResult(NodeContainer nodes)
{
    NS_LOG_INFO("\n=== 仿真 10 秒时 IP 分配结果 ===");
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        Ptr<SimpleAdhocIp> app = nodes.Get(i)->GetApplication(0)->GetObject<SimpleAdhocIp>();
        Ipv4Address ip = app->GetAssignedIp();
        NS_LOG_INFO("节点" << i << ":" << (ip == Ipv4Address::GetAny() ? "分配失败" : Ipv4ToStr(ip)));
    }
    NS_LOG_INFO("===========================\n");
}

int main(int argc, char *argv[])
{
    uint32_t numNodes = 5;
    std::string ipBase = "192.168.1.0";
    std::string ipMask = "255.255.255.0";
    double simTime = 15.0;

    CommandLine cmd;
    cmd.AddValue("numNodes", "节点数量", numNodes);
    cmd.AddValue("simTime", "仿真时长(秒)", simTime);
    cmd.Parse(argc, argv);

    // 启用当前文件的日志组件
    LogComponentEnable("AdhocSimpleIpTest", LOG_LEVEL_INFO);
    // 同时启用SimpleAdhocIp类的日志组件
    LogComponentEnable("AdhocSimpleIp", LOG_LEVEL_INFO);

    NodeContainer nodes;
    nodes.Create(numNodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::AarfWifiManager");

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < numNodes; i++) {
        posAlloc->Add(Vector(i * 5.0, 0.0, 0.0));
    }
    mobility.SetPositionAllocator(posAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4Address base(ipBase.c_str());
    Ipv4Mask mask(ipMask.c_str());
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        Ptr<SimpleAdhocIp> app = CreateObject<SimpleAdhocIp>();
        app->SetNodeId(i);
        app->SetIpPool(base, mask);
        nodes.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(0.0));
        app->SetStopTime(Seconds(simTime));
    }

    Simulator::Schedule(Seconds(10.0), &PrintResult, nodes);

    NS_LOG_INFO("=== 启动仿真（" << numNodes << " 节点，时长 " << simTime << " 秒）===");
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
    