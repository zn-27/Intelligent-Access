/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2025 Q-Smart-Hybrid Project
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * \file
 * \ingroup q-smart-hybrid
 * Performance comparison example for Q-Smart-Hybrid vs OLSR vs Smart-AODV.
 *
 * This example compares:
 * - Q-Smart-Hybrid (adaptive routing)
 * - OLSR (proactive routing)
 * - Smart-AODV (reactive routing)
 *
 * Multiple scenarios with different node speeds to demonstrate
 * the adaptive behavior of Q-Smart-Hybrid.
 */

#include <fstream>
#include <iostream>
#include <vector>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/olsr-module.h"
#include "../helper/q-smart-hybrid-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("QSmartHybridCompare");

/**
 * Structure to hold simulation results
 */
struct SimulationResult
{
  std::string protocol;
  double nodeSpeed;
  double pdr;           // Packet Delivery Ratio
  double avgDelay;      // Average end-to-end delay (ms)
  double throughput;    // Throughput (kbps)
  double overhead;      // Routing overhead (packets)
};

/**
 * Run simulation with specified protocol and speed
 */
SimulationResult
RunSimulation (std::string protocol, uint32_t numNodes, double nodeSpeed,
               double simulationTime, uint32_t packetSize, double dataRate)
{
  NS_LOG_INFO ("Running " << protocol << " simulation with speed " << nodeSpeed << " m/s");

  SimulationResult result;
  result.protocol = protocol;
  result.nodeSpeed = nodeSpeed;

  // Create nodes
  NodeContainer nodes;
  nodes.Create (numNodes);

  // Configure WiFi
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211b);

  YansWifiPhyHelper wifiPhy;
  wifiPhy.Set ("TxPowerStart", DoubleValue (15.0));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (15.0));

  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::TwoRayGroundPropagationLossModel");
  wifiPhy.SetChannel (wifiChannel.Create ());

  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");

  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, nodes);

  // Configure mobility
  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
                                  "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1000.0]"),
                                  "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1000.0]"));
  mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue ("ns3::UniformRandomVariable[Min=" +
                                                   std::to_string (nodeSpeed * 0.5) + "|Max=" +
                                                   std::to_string (nodeSpeed) + "]"),
                             "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"));
  mobility.Install (nodes);

  // Install Internet stack with appropriate routing protocol
  InternetStackHelper internet;

  if (protocol == "Q-Smart-Hybrid")
  {
    QSmartHybridHelper qshHelper;
    qshHelper.Set ("QlearningInterval", TimeValue (Seconds (5)));
    internet.SetRoutingHelper (qshHelper);
  }
  else if (protocol == "OLSR")
  {
    OlsrHelper olsrHelper;
    internet.SetRoutingHelper (olsrHelper);
  }
  else if (protocol == "Smart-AODV")
  {
    // Use Q-Smart-Hybrid in pure reactive mode for comparison
    QSmartHybridHelper qshHelper;
    qshHelper.Set ("QlearningInterval", TimeValue (Seconds (10000)));  // Never switch
    internet.SetRoutingHelper (qshHelper);
  }

  internet.Install (nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // Create traffic applications
  uint32_t numFlows = 5;

  for (uint32_t i = 0; i < numFlows; ++i)
  {
    uint32_t srcNode = i;
    uint32_t dstNode = numNodes - 1 - i;

    // Install packet sink
    PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory",
                                  InetSocketAddress (interfaces.GetAddress (dstNode), 8080 + i));
    ApplicationContainer sinkApps = sinkHelper.Install (nodes.Get (dstNode));
    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (Seconds (simulationTime));

    // Install CBR source
    OnOffHelper onOffHelper ("ns3::UdpSocketFactory",
                              InetSocketAddress (interfaces.GetAddress (dstNode), 8080 + i));
    onOffHelper.SetAttribute ("PacketSize", UintegerValue (packetSize));
    onOffHelper.SetAttribute ("DataRate", StringValue (std::to_string (packetSize * 8 * dataRate) + "bps"));
    onOffHelper.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    onOffHelper.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer sourceApps = onOffHelper.Install (nodes.Get (srcNode));
    sourceApps.Start (Seconds (1.0 + i * 0.5));
    sourceApps.Stop (Seconds (simulationTime));
  }

  // Enable Flow Monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  // Run simulation
  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();

  // Calculate statistics
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  double totalPdr = 0;
  double totalDelay = 0;
  double totalThroughput = 0;
  uint32_t totalOverhead = 0;
  uint32_t flowCount = 0;

  for (auto it = stats.begin (); it != stats.end (); ++it)
  {
    if (it->second.txPackets > 0)
    {
      totalPdr += static_cast<double> (it->second.rxPackets) / it->second.txPackets;
    }

    if (it->second.rxPackets > 0)
    {
      totalDelay += it->second.delaySum.GetMilliSeconds () / it->second.rxPackets;
    }

    if (it->second.timeLastRxPacket > it->second.timeFirstTxPacket)
    {
      totalThroughput += it->second.rxBytes * 8.0 /
                         (it->second.timeLastRxPacket - it->second.timeFirstTxPacket).GetSeconds () / 1000.0;
    }

    totalOverhead += it->second.txPackets - it->second.rxPackets;
    flowCount++;
  }

  if (flowCount > 0)
  {
    result.pdr = totalPdr / flowCount;
    result.avgDelay = totalDelay / flowCount;
    result.throughput = totalThroughput / flowCount;
  }
  result.overhead = totalOverhead;

  Simulator::Destroy ();

  return result;
}

int
main (int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 20;
  double simulationTime = 60.0;
  uint32_t packetSize = 512;
  double dataRate = 4.0;

  // Speeds to test
  std::vector<double> speeds = {1.0, 2.0, 3.0, 4.0, 5.0};

  // Protocols to compare
  std::vector<std::string> protocols = {"Q-Smart-Hybrid", "OLSR", "Smart-AODV"};

  bool verbose = false;

  CommandLine cmd;
  cmd.AddValue ("verbose", "Enable verbose logging", verbose);
  cmd.AddValue ("numNodes", "Number of nodes", numNodes);
  cmd.AddValue ("simTime", "Simulation time (s)", simulationTime);
  cmd.Parse (argc, argv);

  if (verbose)
  {
    LogComponentEnable ("QSmartHybridCompare", LOG_LEVEL_INFO);
  }

  std::cout << "====== Q-Smart-Hybrid Performance Comparison ======\n\n";
  std::cout << "Nodes: " << numNodes << ", Simulation Time: " << simulationTime << "s\n\n";

  std::vector<SimulationResult> results;

  // Run simulations
  for (const auto& protocol : protocols)
  {
    for (const auto& speed : speeds)
    {
      SimulationResult result = RunSimulation (protocol, numNodes, speed,
                                                simulationTime, packetSize, dataRate);
      results.push_back (result);
    }
  }

  // Print results table
  std::cout << "====== Results ======\n\n";
  std::cout << "Protocol\t\tSpeed(m/s)\tPDR(%)\t\tDelay(ms)\tThroughput(kbps)\n";
  std::cout << "--------\t\t----------\t-------\t\t---------\t---------------\n";

  for (const auto& result : results)
  {
    std::cout << result.protocol << "\t\t"
              << result.nodeSpeed << "\t\t"
              << result.pdr * 100 << "\t\t"
              << result.avgDelay << "\t\t"
              << result.throughput << "\n";
  }

  // Print summary
  std::cout << "\n====== Summary by Protocol ======\n\n";

  for (const auto& protocol : protocols)
  {
    double avgPdr = 0;
    double avgDelay = 0;
    double avgThroughput = 0;
    int count = 0;

    for (const auto& result : results)
    {
      if (result.protocol == protocol)
      {
        avgPdr += result.pdr;
        avgDelay += result.avgDelay;
        avgThroughput += result.throughput;
        count++;
      }
    }

    if (count > 0)
    {
      std::cout << protocol << ":\n";
      std::cout << "  Average PDR: " << avgPdr / count * 100 << "%\n";
      std::cout << "  Average Delay: " << avgDelay / count << " ms\n";
      std::cout << "  Average Throughput: " << avgThroughput / count << " kbps\n\n";
    }
  }

  return 0;
}
