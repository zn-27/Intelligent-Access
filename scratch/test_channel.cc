/*
 * Copyright (c) 2018 University of Campinas (Unicamp)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Luciano Jerez Chaves <ljerezchaves@gmail.com>
 */

/*
 * Two hosts connected to a single OpenFlow switch.
 * The switch is managed by the default learning controller application.
 * The switch datapath can be customized by the command line parameters.
 *
 *                       Learning Controller
 *                                |
 *                       +-----------------+
 *            Host 0 === | OpenFlow switch | === Host 1
 *                       +-----------------+
 */

#include <ns3/core-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-apps-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>
#include <ns3/ofswitch13-module.h>

#include "ns3/aodv-helper.h" // AODV路由协议
#include "ns3/olsr-helper.h"// OLSR路由协议
#include "ns3/olsr-module.h"
#include "ns3/aodv-module.h"
#include "ns3/wifi-80211p-helper.h" // 802.11p支持，用于Adhoc模式
#include "ns3/ipv4-list-routing-helper.h"
using namespace ns3;

int
main(int argc, char* argv[])
{
    uint16_t simTime = 10;
    bool verbose = false;
    bool trace = false;

    // Custom switch datapath attributes
    uint32_t flowSize = 10;
    uint32_t groupSize = 10;
    uint32_t meterSize = 10;
    uint32_t pipeTabs = 1;
    DataRate pipeLoad = DataRate("1Mbps");

    // Configure command line parameters
    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("verbose", "Enable verbose output", verbose);
    cmd.AddValue("trace", "Enable datapath stats and pcap traces", trace);
    cmd.AddValue("flowSize", "The flow table size", flowSize);
    cmd.AddValue("groupSize", "The group table size", groupSize);
    cmd.AddValue("meterSize", "The meter table size", meterSize);
    cmd.AddValue("pipeTabs", "The number of pipeline flow tables", pipeTabs);
    cmd.AddValue("pipeLoad", "The pipeline processing capacity", pipeLoad);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        OFSwitch13Helper::EnableDatapathLogs();
        LogComponentEnable("OFSwitch13Interface", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Device", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Port", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Queue", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13SocketHandler", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13LearningController", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Helper", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13InternalHelper", LOG_LEVEL_ALL);
    }


    // Create two host nodes
    NodeContainer hosts;
    hosts.Create(2);

    // Create the switch node
    Ptr<Node> switchNode = CreateObject<Node>();

    // Use the CsmaHelper to connect host nodes to the switch node
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csmaHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer hostDevices;
    NetDeviceContainer switchPorts;
    for (size_t i = 0; i < hosts.GetN(); i++)
    {
        NodeContainer pair(hosts.Get(i), switchNode);
        NetDeviceContainer link = csmaHelper.Install(pair);
        hostDevices.Add(link.Get(0));
        switchPorts.Add(link.Get(1));
    }

    // Create the controller node
    Ptr<Node> controllerNode = CreateObject<Node>();

    // Create the OpenFlow helper with custom switch attributes.
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    of13Helper->SetDeviceAttribute("CpuCapacity", DataRateValue(pipeLoad));
    of13Helper->SetDeviceAttribute("PipelineTables", UintegerValue(pipeTabs));
    of13Helper->SetDeviceAttribute("FlowTableSize", UintegerValue(flowSize));
    of13Helper->SetDeviceAttribute("GroupTableSize", UintegerValue(groupSize));
    of13Helper->SetDeviceAttribute("MeterTableSize", UintegerValue(meterSize));

    // Configure the OpenFlow network domain
    of13Helper->InstallController(controllerNode);
    of13Helper->InstallSwitch(switchNode, switchPorts);
    of13Helper->CreateOpenFlowChannels();
    //=================================================
    // =============安装互联网协议栈，使用AODV路由协议=========================
    InternetStackHelper stack;
   // 创建AODV和OLSR路由助手
    AodvHelper aodv;
    OlsrHelper olsr;

    // 创建列表路由助手
    Ipv4ListRoutingHelper listRouting;

    // 添加路由协议并设置优先级（数值越da优先级越高）
    listRouting.Add(aodv, 0);  // AODV优先级高
    listRouting.Add(olsr, 100);  // OLSR优先级低

    // 将列表路由设置为栈的路由助手
    stack.SetRoutingHelper(listRouting);
    stack.Install(hosts);
    // ================================================
    // Install the TCP/IP stack into hosts nodes
    
    

    // Set IPv4 host addresses
    Ipv4AddressHelper ipv4Helper;
    Ipv4InterfaceContainer hostIpIfaces;
    ipv4Helper.SetBase("10.1.1.0", "255.255.255.0");
    hostIpIfaces = ipv4Helper.Assign(hostDevices);

    // Configure ping application between hosts
    // pingHelper(Ipv4Address(hostIpIfaces.GetAddress(1)));
    //pingHelper.SetAttribute("VerboseMode", EnumValue(Ping::VerboseMode::VERBOSE));
    //ApplicationContainer pingApps = pingHelper.Install(hosts.Get(0));
    //pingApps.Start(Seconds(1));

    // Enable datapath stats and pcap traces at hosts, switch(es), and controller(s)
    if (trace)
    {
        of13Helper->EnableOpenFlowPcap("openflow");
        of13Helper->EnableDatapathStats("switch-stats");
        csmaHelper.EnablePcap("switch", switchPorts, true);
        csmaHelper.EnablePcap("host", hostDevices);
    }
    //----------------这里是测试的内容------------------------------------------
    for (uint32_t i = 0; i < switchPorts.GetN(); i++)
    {
        Ptr<NetDevice> switchPortDevice = switchPorts.Get(i);
    
        // 获取连接到交换机端口的通道
        Ptr<Channel> channel = switchPortDevice->GetChannel();
    
        // 对于CSMA通道，获取另一端的设备（主机）
        Ptr<CsmaChannel> csmaChannel = DynamicCast<CsmaChannel>(channel);
        if (csmaChannel)
        {
            // 遍历通道上的所有设备，找到不是交换机端口的那个（即主机设备）
            for (uint32_t j = 0; j < csmaChannel->GetNDevices(); j++)
            {
                Ptr<NetDevice> device = csmaChannel->GetDevice(j);
                if (device != switchPortDevice)
                {
                    std::cout << "Switch port " << i << " is connected to host device: "
                              << device->GetAddress() << std::endl;

                    //--------------测试二-----------------------------------
                    
                    // 1. 获取设备所属的节点
                    Ptr<Node> hostNode = device->GetNode();
                    if (!hostNode)
                    {
                        std::cout << "Device does not belong to any node!" << std::endl;
                        continue;
                    }

                    // 2. 获取节点上的IPv4协议栈
                    Ptr<Ipv4> ipv4 = hostNode->GetObject<Ipv4>();
                    if (!ipv4)
                    {
                        std::cout << "Node does not have an IPv4 stack!" << std::endl;
                        continue;
                    }

                    // 3. 获取路由协议（通常是Ipv4ListRouting，支持多路由协议）
                    Ptr<Ipv4RoutingProtocol> routingProtocol = ipv4->GetRoutingProtocol();
                    Ptr<Ipv4ListRouting> listRouting = DynamicCast<Ipv4ListRouting>(routingProtocol);
                    if (!listRouting)
                    {
                        std::cout << "Node does not use Ipv4ListRouting!" << std::endl;
                        continue;
                    }
                    std::cout<<"have routing";
                    // 4. 获取当前路由协议及其优先级列表
                    std::cout << "Routing protocols and their priorities on host node:" << std::endl;
                    for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
                    {
                        int16_t priority = 0;
                        Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, priority);
                        std::cout << "  Protocol: " << proto->GetInstanceTypeId().GetName()
                                  << ", Priority: " << priority << std::endl;
                    }

                    
                    // 5. 示例：修改第一个路由协议的优先级（假设存在）
                    if (listRouting->GetNRoutingProtocols() > 0)
                    {

    // // 创建列表路由助手
    // Ipv4ListRoutingHelper nlistRouting;

    // // 添加路由协议并设置优先级（数值越da优先级越高）
    // nlistRouting.Add(aodv, 10);  // AODV优先级高
    // nlistRouting.Add(olsr, 0);  // OLSR优先级低

    // // 将列表路由设置为栈的路由助手
    // stack.SetRoutingHelper(nlistRouting);
    // stack.Install(hosts);
                    
                    //     // Ptr<Ipv4RoutingProtocol> newProtocol;
                    //     // newProtocol = CreateObject<olsr::RoutingProtocol>();
                    //     // ipv4->SetRoutingProtocol(newProtocol);

                    //     // 创建新的 list
                    //     // 新建一个 list
                    //       Ptr<Ipv4ListRouting> newList = CreateObject<Ipv4ListRouting> ();

                    //       // 新建 OLSR 和 AODV 对象（不要复用旧的）
                    //       Ptr<aodv::RoutingProtocol> aodv = CreateObject<aodv::RoutingProtocol> ();
                    //       Ptr<olsr::RoutingProtocol> olsr = CreateObject<olsr::RoutingProtocol> ();

                    //       // 这里把 OLSR 提升为高优先级 (100)，AODV 降为低优先级 (10)
                    //       newList->AddRoutingProtocol (olsr, 100);
                    //       newList->AddRoutingProtocol (aodv, 10);

                    //       // 替换节点上的路由协议栈
                    //       ipv4->SetRoutingProtocol (newList);
                    //     
                    // }
                        // bu wen ding xiu gai 
                        listRouting->SetRoutingProtocolPriorityByType(olsr::RoutingProtocol::GetTypeId(),0);
                        listRouting->SetRoutingProtocolPriorityByType(aodv::RoutingProtocol::GetTypeId(),100);
                        //listRouting->SetRoutingProtocolPriority(1,100);
                        std::cout<<"--------------------------------"<<std::endl;
                        std::cout<<"---------xiu gai chen gong-----------"<<std::endl;
                        std::cout<<"--------------------------------"<<std::endl;
                    }
                     // 4. 获取当前路由协议及其优先级列表
                    std::cout << "Routing protocols and their priorities on host node:" << std::endl;
                    for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
                    {
                        // 用 std::cout 创建一个 OutputStreamWrapper
                        Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(&std::cout);

                        // 调用打印函数
                        listRouting->PrintRoutingTable(stream, Time::S);
                    }
                    //--------------------------------------------------------------------
                    break;
                }
            }
        }
    }
    //------------------------------------------------------------------------------
    // Run the simulation
    //Simulator::Stop(Seconds(simTime));
   // Simulator::Run();
   // Simulator::Destroy();
}
