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
 * Cluster Control Application for Smart-AODV-V2
 */

#ifndef CLUSTER_CONTROL_APP_H
#define CLUSTER_CONTROL_APP_H

#include "ns3/application.h"
#include "ns3/ipv4-address.h"
#include "ns3/traced-callback.h"
#include "ns3/nstime.h"
#include "ns3/smart-aodv-v2-cluster.h"

namespace ns3 {
namespace smartAodvV2 {

class RoutingProtocol;

/**
 * \brief Application-level cluster control interface
 *
 * This application provides an interface for applications to dynamically
 * switch cluster modes and monitor cluster status.
 */
class ClusterControlApp : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId ();

  /**
   * \brief Constructor
   */
  ClusterControlApp ();

  /**
   * \brief Destructor
   */
  virtual ~ClusterControlApp ();

  /**
   * \brief Switch cluster mode
   * \param mode New cluster mode (SELF_ORG or CENTRALIZED)
   */
  void SwitchMode (ClusterMode mode);

  /**
   * \brief Get current cluster mode
   * \return Current cluster mode
   */
  ClusterMode GetMode () const;

  /**
   * \brief Get local cluster ID
   * \return Current cluster ID
   */
  uint32_t GetClusterId () const;

  /**
   * \brief Set local cluster ID
   * \param clusterId New cluster ID
   */
  void SetClusterId (uint32_t clusterId);

  /**
   * \brief Get cluster head address
   * \return Cluster head IP address
   */
  Ipv4Address GetClusterHead () const;

  /**
   * \brief Set cluster head address
   * \param head Cluster head IP address
   */
  void SetClusterHead (Ipv4Address head);

  /**
   * \brief Check if local node is cluster head
   * \return true if cluster head
   */
  bool IsClusterHead () const;

  /**
   * \brief Get cluster member count
   * \return Number of known cluster members
   */
  uint32_t GetClusterSize () const;

  /**
   * \brief Schedule a mode switch at a future time
   * \param delay Time until mode switch
   * \param mode New cluster mode
   */
  void ScheduleModeSwitch (Time delay, ClusterMode mode);

  /**
   * Typedef for mode switched callback
   */
  typedef void (*ModeSwitchedTracedCallback)(uint32_t clusterId, ClusterMode newMode);

  /**
   * Typedef for cluster changed callback
   */
  typedef void (*ClusterChangedTracedCallback)(uint32_t oldCluster, uint32_t newCluster);

  /**
   * Typedef for member event callback
   */
  typedef void (*MemberTracedCallback)(Ipv4Address member);

private:
  virtual void StartApplication ();
  virtual void StopApplication ();

  /// Pointer to the routing protocol
  Ptr<RoutingProtocol> m_routing;

  /// Trace source for mode switched events
  TracedCallback<uint32_t, ClusterMode> m_modeSwitchedTrace;

  /// Trace source for cluster changed events
  TracedCallback<uint32_t, uint32_t> m_clusterChangedTrace;

  /// Trace source for member joined events
  TracedCallback<Ipv4Address> m_memberJoinedTrace;

  /// Trace source for member left events
  TracedCallback<Ipv4Address> m_memberLeftTrace;
};

} // namespace smartAodvV2
} // namespace ns3

#endif /* CLUSTER_CONTROL_APP_H */
