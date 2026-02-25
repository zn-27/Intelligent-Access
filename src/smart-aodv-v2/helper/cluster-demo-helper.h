/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 NUS
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
 * Helper utilities for cluster demonstration scripts
 */

#ifndef CLUSTER_DEMO_HELPER_H
#define CLUSTER_DEMO_HELPER_H

#include "ns3/node-container.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/mobility-helper.h"
#include "ns3/rectangle.h"
#include "ns3/smart-aodv-v2-cluster.h"
#include "ns3/application-container.h"
#include <string>
#include <vector>

namespace ns3 {
namespace smartAodvV2 {

/**
 * \brief Helper class for cluster demonstration scenarios
 *
 * This class provides utility functions to simplify the setup
 * of cluster demonstration scripts.
 */
class ClusterDemoHelper
{
public:
  /**
   * \brief Constructor
   */
  ClusterDemoHelper ();

  /**
   * \brief Destructor
   */
  ~ClusterDemoHelper ();

  /**
   * \brief Create mobile nodes in a grid layout with random walk mobility
   * \param numNodes Total number of nodes
   * \param numClusters Number of clusters
   * \param bounds Simulation area bounds (width x height)
   * \param speedMin Minimum speed (m/s)
   * \param speedMax Maximum speed (m/s)
   * \return NodeContainer with created nodes
   */
  static NodeContainer CreateMobileNodes (uint32_t numNodes,
                                          uint32_t numClusters,
                                          Rectangle bounds,
                                          double speedMin = 2.0,
                                          double speedMax = 5.0);

  /**
   * \brief Configure cluster assignments for nodes
   * \param nodes Node container
   * \param interfaces IP interface container
   * \param numClusters Number of clusters
   * \param clusterHeads Output: vector of cluster head node indices
   */
  static void ConfigureClusters (NodeContainer& nodes,
                                 Ipv4InterfaceContainer& interfaces,
                                 uint32_t numClusters,
                                 std::vector<uint32_t>& clusterHeads);

  /**
   * \brief Install ClusterControlApp on all nodes
   * \param nodes Node container
   * \param startTime Application start time
   * \param stopTime Application stop time
   * \return ApplicationContainer with installed apps
   */
  static ApplicationContainer InstallClusterApps (NodeContainer& nodes,
                                                   Time startTime,
                                                   Time stopTime);

  /**
   * \brief Print current cluster status for all nodes
   * \param nodes Node container
   * \param interfaces IP interface container
   */
  static void PrintClusterStatus (NodeContainer& nodes,
                                  Ipv4InterfaceContainer& interfaces);

  /**
   * \brief Set cluster mode for all nodes
   * \param nodes Node container
   * \param mode Cluster mode to set
   */
  static void SetClusterMode (NodeContainer& nodes, ClusterMode mode);

  /**
   * \brief Print simulation header with scenario description
   * \param title Scenario title
   * \param numNodes Number of nodes
   * \param numClusters Number of clusters
   * \param simTime Simulation time in seconds
   */
  static void PrintHeader (const std::string& title,
                           uint32_t numNodes,
                           uint32_t numClusters,
                           double simTime);

  /**
   * \brief Schedule periodic cluster status printing
   * \param nodes Node container
   * \param interfaces IP interface container
   * \param interval Print interval
   * \param duration Total duration to print
   */
  static void SchedulePeriodicStatusPrint (NodeContainer& nodes,
                                           Ipv4InterfaceContainer& interfaces,
                                           Time interval,
                                           Time duration);

  /**
   * \brief Get cluster ID for a node
   * \param node Node pointer
   * \return Cluster ID
   */
  static uint32_t GetNodeClusterId (Ptr<Node> node);

  /**
   * \brief Get cluster mode for a node
   * \param node Node pointer
   * \return Current cluster mode
   */
  static ClusterMode GetNodeClusterMode (Ptr<Node> node);

  /**
   * \brief Check if node is cluster head
   * \param node Node pointer
   * \return true if cluster head
   */
  static bool IsNodeClusterHead (Ptr<Node> node);

private:
  /**
   * \brief Internal callback for periodic status printing
   */
  static void PeriodicStatusCallback (NodeContainer* nodes,
                                      Ipv4InterfaceContainer* interfaces);
};

} // namespace smartAodvV2
} // namespace ns3

#endif /* CLUSTER_DEMO_HELPER_H */
