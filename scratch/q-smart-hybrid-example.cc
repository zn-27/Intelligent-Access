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
 * Basic example for Q-Smart-Hybrid routing protocol.
 *
 * This example demonstrates a simple MANET scenario with:
 * - 20 mobile nodes
 * - Random waypoint mobility model
 * - CBR traffic flows
 * - Q-Smart-Hybrid routing protocol
 */

#include <fstream>
#include <iostream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/stats-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/config-store-module.h"
#include "ns3/q-smart-hybrid-helper.h"
#include "ns3/random-variable-stream.h"
#include "ns3/position-allocator.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("QSmartHybridExample");

int
main (int argc, char *argv[])
{
  // Simulation parameters
  uint32_t numNodes = 20;
  double simulationTime = 100.0;  // seconds
  double nodeSpeed = 2.0;         // m/s
  uint32_t packetSize = 512;      // bytes
  double dataRate = 4.0;          // packets per second
  uint32_t seed = 12345;          // Random seed for reproducibility
  uint32_t run = 1;               // Run number

  // Q-Learning parameters
  double alpha = 0.1;             // Learning rate
  double gamma = 0.9;             // Discount factor
  double epsilon = 0.1;           // Exploration rate
  double qlearningInterval = 5.0; // seconds

  // Enable logging
  bool enableLogging = false;
  bool enablePcap = false;
  bool enableFlowMonitor = true;

  // Parse command line
  CommandLine cmd;
  cmd.AddValue ("numNodes", "Number of nodes", numNodes);
  cmd.AddValue ("simTime", "Simulation time (s)", simulationTime);
  cmd.AddValue ("nodeSpeed", "Node speed (m/s)", nodeSpeed);
  cmd.AddValue ("packetSize", "Packet size (bytes)", packetSize);
  cmd.AddValue ("dataRate", "Data rate (packets/s)", dataRate);
  cmd.AddValue ("alpha", "Q-Learning learning rate", alpha);
  cmd.AddValue ("gamma", "Q-Learning discount factor", gamma);
  cmd.AddValue ("epsilon", "Q-Learning exploration rate", epsilon);
  cmd.AddValue ("qlearningInterval", "Q-Learning decision interval (s)", qlearningInterval);
  cmd.AddValue ("seed", "Random seed for reproducibility", seed);
  cmd.AddValue ("run", "Run number", run);
  cmd.AddValue ("logging", "Enable logging", enableLogging);
  cmd.AddValue ("pcap", "Enable PCAP tracing", enablePcap);
  cmd.AddValue ("flowMonitor", "Enable Flow Monitor", enableFlowMonitor);
  cmd.Parse (argc, argv);

  // Set random seed for reproducibility
  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (run);

  if (enableLogging)
  {
    LogComponentEnable ("QSmartHybridExample", LOG_LEVEL_INFO);
    LogComponentEnable ("QSmartHybridRoutingProtocol", LOG_LEVEL_DEBUG);
    LogComponentEnable ("QSmartHybridQlearning", LOG_LEVEL_DEBUG);
  }

  NS_LOG_INFO ("Creating Q-Smart-Hybrid simulation with " << numNodes << " nodes");

  // Create nodes
  NodeContainer nodes;
  nodes.Create (numNodes);

  // Configure WiFi
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211b);

  YansWifiPhyHelper wifiPhy;
  wifiPhy.Set ("TxPowerStart", DoubleValue (15.0));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (15.0));
  wifiPhy.Set ("RxGain", DoubleValue (0));
  wifiPhy.Set ("TxGain", DoubleValue (0));
  wifiPhy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);

  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::TwoRayGroundPropagationLossModel",
                                   "SystemLoss", DoubleValue (1.0),
                                   "HeightAboveZ", DoubleValue (1.5));
  wifiPhy.SetChannel (wifiChannel.Create ());

  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");

  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, nodes);

  // Configure mobility - use smaller area to ensure nodes are in range
  MobilityHelper mobility;
  Ptr<RandomRectanglePositionAllocator> positionAlloc = CreateObject<RandomRectanglePositionAllocator> ();
  Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable> ();
  x->SetAttribute ("Min", DoubleValue (0.0));
  x->SetAttribute ("Max", DoubleValue (300.0));  // 300m to ensure connectivity
  Ptr<UniformRandomVariable> y = CreateObject<UniformRandomVariable> ();
  y->SetAttribute ("Min", DoubleValue (0.0));
  y->SetAttribute ("Max", DoubleValue (300.0));  // 300m to ensure connectivity
  positionAlloc->SetX (x);
  positionAlloc->SetY (y);

  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue ("ns3::UniformRandomVariable[Min=1.0|Max=5.0]"),
                             "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"),
                             "PositionAllocator", PointerValue (positionAlloc));
  mobility.Install (nodes);

  // Install Internet stack with Q-Smart-Hybrid routing
  InternetStackHelper internet;
  QSmartHybridHelper qshHelper;
  qshHelper.Set ("QlearningInterval", TimeValue (Seconds (qlearningInterval)));
  qshHelper.Set ("HelloInterval", TimeValue (Seconds (1)));
  qshHelper.Set ("ActiveRouteTimeout", TimeValue (Seconds (30)));

  internet.SetRoutingHelper (qshHelper);
  internet.Install (nodes);

  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // Create traffic applications
  // Select source and destination nodes
  uint32_t sourceNode = 0;
  uint32_t destNode = numNodes - 1;

  // Install packet sink on destination
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory",
                                InetSocketAddress (interfaces.GetAddress (destNode), 8080));
  ApplicationContainer sinkApps = sinkHelper.Install (nodes.Get (destNode));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (simulationTime));

  // Install CBR source
  OnOffHelper onOffHelper ("ns3::UdpSocketFactory",
                            InetSocketAddress (interfaces.GetAddress (destNode), 8080));
  onOffHelper.SetAttribute ("PacketSize", UintegerValue (packetSize));
  onOffHelper.SetAttribute ("DataRate", StringValue (std::to_string (packetSize * 8 * dataRate) + "bps"));
  onOffHelper.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  onOffHelper.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));

  ApplicationContainer sourceApps = onOffHelper.Install (nodes.Get (sourceNode));
  sourceApps.Start (Seconds (1.0));
  sourceApps.Stop (Seconds (simulationTime));

  // Create additional traffic flows for more realistic scenario
  for (uint32_t i = 1; i < numNodes / 4; ++i)
  {
    uint32_t src = i;
    uint32_t dst = numNodes - 1 - i;

    PacketSinkHelper sink ("ns3::UdpSocketFactory",
                            InetSocketAddress (interfaces.GetAddress (dst), 8080 + i));
    ApplicationContainer sinkApp = sink.Install (nodes.Get (dst));
    sinkApp.Start (Seconds (0.0));
    sinkApp.Stop (Seconds (simulationTime));

    OnOffHelper onOff ("ns3::UdpSocketFactory",
                        InetSocketAddress (interfaces.GetAddress (dst), 8080 + i));
    onOff.SetAttribute ("PacketSize", UintegerValue (packetSize));
    onOff.SetAttribute ("DataRate", StringValue (std::to_string (packetSize * 8 * dataRate) + "bps"));
    onOff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    onOff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer srcApp = onOff.Install (nodes.Get (src));
    srcApp.Start (Seconds (1.0 + i * 0.5));
    srcApp.Stop (Seconds (simulationTime));
  }

  // Enable Flow Monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor;
  if (enableFlowMonitor)
  {
    monitor = flowmon.InstallAll ();
  }

  // Enable PCAP tracing
  if (enablePcap)
  {
    wifiPhy.EnablePcapAll ("q-smart-hybrid");
  }

  NS_LOG_INFO ("Starting simulation for " << simulationTime << " seconds...");

  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();

  // Print Flow Monitor statistics
  if (enableFlowMonitor)
  {
    monitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

    std::cout << "\n====== Flow Monitor Statistics ======\n";
    std::cout << "Flow\tSrc\tDst\tTxPackets\tRxPackets\tLostPackets\tPDR\t\tDelay(ms)\tThroughput(kbps)\n";

    double totalPdr = 0;
    double totalDelay = 0;
    double totalThroughput = 0;
    uint32_t flowCount = 0;

    for (auto it = stats.begin (); it != stats.end (); ++it)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (it->first);

      double pdr = 0;
      if (it->second.txPackets > 0)
      {
        pdr = static_cast<double> (it->second.rxPackets) / it->second.txPackets;
      }

      double avgDelay = 0;
      if (it->second.rxPackets > 0)
      {
        avgDelay = it->second.delaySum.GetMilliSeconds () / it->second.rxPackets;
      }

      double throughput = 0;
      if (it->second.timeLastRxPacket > it->second.timeFirstTxPacket)
      {
        throughput = it->second.rxBytes * 8.0 /
                     (it->second.timeLastRxPacket - it->second.timeFirstTxPacket).GetSeconds () / 1000.0;
      }

      std::cout << it->first << "\t"
                << t.sourceAddress << "\t"
                << t.destinationAddress << "\t"
                << it->second.txPackets << "\t\t"
                << it->second.rxPackets << "\t\t"
                << it->second.lostPackets << "\t\t"
                << pdr << "\t"
                << avgDelay << "\t\t"
                << throughput << "\n";

      totalPdr += pdr;
      totalDelay += avgDelay;
      totalThroughput += throughput;
      flowCount++;
    }

    if (flowCount > 0)
    {
      std::cout << "\n====== Average Statistics ======\n";
      std::cout << "Average PDR: " << totalPdr / flowCount * 100 << "%\n";
      std::cout << "Average Delay: " << totalDelay / flowCount << " ms\n";
      std::cout << "Average Throughput: " << totalThroughput / flowCount << " kbps\n";
    }
  }

  Simulator::Destroy ();

  NS_LOG_INFO ("Simulation completed successfully!");

  return 0;
}
