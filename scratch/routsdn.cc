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

using namespace ns3;

int main(int argc, char *argv[])
{
  uint16_t simTime = 10;
  bool verbose = false;
  bool trace = false;

  // Parse command line
  CommandLine cmd;
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("verbose", "Enable verbose logs", verbose);
  cmd.AddValue("trace", "Enable pcap/traces", trace);
  cmd.Parse(argc, argv);

  if (verbose)
  {
    OFSwitch13Helper::EnableDatapathLogs();
    LogComponentEnable("OFSwitch13Interface", LOG_LEVEL_ALL);
    LogComponentEnable("OFSwitch13Device", LOG_LEVEL_ALL);
    LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_ALL);
    LogComponentEnable("OFSwitch13LearningController", LOG_LEVEL_ALL);
  }

  // Enable checksum (required by ofswitch13)
  GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

  // Create nodes
  NodeContainer hostsA;
  hostsA.Create(2); // Domain A hosts
  NodeContainer hostsB;
  hostsB.Create(2); // Domain B hosts
  Ptr<Node> sw1 = CreateObject<Node>();
  Ptr<Node> sw2 = CreateObject<Node>();
  Ptr<Node> routerNode = CreateObject<Node>();
  Ptr<Node> controllerNode = CreateObject<Node>();

  // Csma helper for links
  CsmaHelper csma;
  csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
  csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

  // Containers for net devices
  NetDeviceContainer hostDevsA, hostDevsB;
  NetDeviceContainer switchPortsSw1, switchPortsSw2;
  NetDeviceContainer routerDevsA, routerDevsB;

  // Connect Domain A hosts to sw1
  for (uint32_t i = 0; i < hostsA.GetN(); ++i)
  {
    NodeContainer pair(hostsA.Get(i), sw1);
    NetDeviceContainer link = csma.Install(pair);
    hostDevsA.Add(link.Get(0));
    switchPortsSw1.Add(link.Get(1));
  }

  // Connect Domain B hosts to sw2
  for (uint32_t i = 0; i < hostsB.GetN(); ++i)
  {
    NodeContainer pair(hostsB.Get(i), sw2);
    NetDeviceContainer link = csma.Install(pair);
    hostDevsB.Add(link.Get(0));
    switchPortsSw2.Add(link.Get(1));
  }

  // Connect router to sw1 (network A)
  {
    NodeContainer pair(routerNode, sw1);
    NetDeviceContainer link = csma.Install(pair);
    routerDevsA.Add(link.Get(0));    // router interface in net A
    switchPortsSw1.Add(link.Get(1)); // add this port to sw1
  }

  // Connect router to sw2 (network B)
  {
    NodeContainer pair(routerNode, sw2);
    NetDeviceContainer link = csma.Install(pair);
    routerDevsB.Add(link.Get(0));    // router interface in net B
    switchPortsSw2.Add(link.Get(1)); // add this port to sw2
  }

  // Create ofswitch13 helper and set optional datapath attrs
  Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
  // e.g. of13Helper->SetDeviceAttribute ("FlowTableSize", UintegerValue (100));
  of13Helper->InstallController(controllerNode); // mo ren learning controller
  of13Helper->InstallSwitch(sw1, switchPortsSw1);
  of13Helper->InstallSwitch(sw2, switchPortsSw2);
  of13Helper->CreateOpenFlowChannels();

  // Install Internet stack on hosts and router
  InternetStackHelper internet;
  internet.Install(hostsA);
  internet.Install(hostsB);
  internet.Install(routerNode);

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

  // Set up static default routes on hosts to the router
  Ipv4StaticRoutingHelper staticRoutingHelper;

  // Router's IP in net A is the *last* assigned in ifA (we assigned hosts then router)
  Ipv4Address routerA = ifA.GetAddress(hostDevsA.GetN()); // index after hosts
  Ipv4Address routerB = ifB.GetAddress(hostDevsB.GetN());

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

  // (Optional) Enable pcap/traces
  if (trace)
  {
    of13Helper->EnableOpenFlowPcap("openflow-interdomain");
    of13Helper->EnableDatapathStats("switch-stats");
    csma.EnablePcap("sw1", switchPortsSw1, true);
    csma.EnablePcap("sw2", switchPortsSw2, true);
    csma.EnablePcap("hostA", hostDevsA);
    csma.EnablePcap("hostB", hostDevsB);
  }

  // Ping from Domain A host 0 to Domain B host 0 (cross-domain ping)
  Ipv4Address dst = ifB.GetAddress(0); // first host in domain B
  V4PingHelper ping(dst);
  ping.SetAttribute("Verbose", BooleanValue(true));
  ApplicationContainer pingApp = ping.Install(hostsA.Get(0));
  pingApp.Start(Seconds(1.0));
  pingApp.Stop(Seconds(simTime - 1));

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}
