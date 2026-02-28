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
 * Cluster Management for Smart-AODV-V2
 */

#ifndef SMART_AODV_V2_CLUSTER_H
#define SMART_AODV_V2_CLUSTER_H

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include <map>
#include <vector>

namespace ns3 {
namespace smartAodvV2 {

/**
 * \brief Cluster mode enumeration
 */
enum ClusterMode
{
  MODE_SELF_ORG = 0,        ///< Self-organizing: intra-cluster direct, inter-cluster via head
  MODE_CENTRALIZED = 1      ///< Centralized: all traffic via cluster head
};

/**
 * \brief Cluster member information
 */
struct ClusterInfo
{
  Ipv4Address m_address;    ///< Node IP address
  uint32_t m_clusterId;     ///< Cluster ID (0 = unknown/unassigned)
  bool m_isClusterHead;     ///< Whether this node is cluster head
  Time m_lastSeen;          ///< Last update time
  double m_avgRssi;         ///< Average RSSI to this neighbor (dBm)

  ClusterInfo ()
    : m_clusterId (0),
      m_isClusterHead (false),
      m_lastSeen (Seconds (0)),
      m_avgRssi (-100.0) {}
};

/**
 * \brief Cluster management table
 *
 * This class maintains information about neighbors and their cluster assignments,
 * as well as RSSI values for cluster switching decisions.
 */
class ClusterTable
{
public:
  /**
   * \brief Constructor
   */
  ClusterTable ();

  /**
   * \brief Destructor
   */
  ~ClusterTable ();

  /**
   * \brief Get cluster ID for an address
   * \param addr IPv4 address
   * \return Cluster ID, or 0 if unknown
   */
  uint32_t GetClusterId (Ipv4Address addr) const;

  /**
   * \brief Set cluster ID for an address
   * \param addr IPv4 address
   * \param clusterId Cluster ID
   */
  void SetClusterId (Ipv4Address addr, uint32_t clusterId);

  /**
   * \brief Check if two addresses are in the same cluster
   * \param addr1 First address
   * \param addr2 Second address
   * \return true if same cluster (both known and same ID)
   */
  bool IsSameCluster (Ipv4Address addr1, Ipv4Address addr2) const;

  /**
   * \brief Update RSSI for a neighbor
   * \param addr Neighbor address
   * \param rssi RSSI value in dBm
   */
  void UpdateRssi (Ipv4Address addr, double rssi);

  /**
   * \brief Get RSSI for a neighbor
   * \param addr Neighbor address
   * \return RSSI in dBm, or -100 if unknown
   */
  double GetRssi (Ipv4Address addr) const;

  /**
   * \brief Check if an entry exists for the address
   * \param addr Address to check
   * \return true if entry exists
   */
  bool HasEntry (Ipv4Address addr) const;

  /**
   * \brief Get cluster info for an address
   * \param addr Address to look up
   * \return ClusterInfo struct (copy)
   */
  ClusterInfo GetClusterInfo (Ipv4Address addr) const;

  /**
   * \brief Set cluster head flag for an address
   * \param addr Address of the node
   * \param isHead true if cluster head
   */
  void SetClusterHead (Ipv4Address addr, bool isHead);

  /**
   * \brief Get all addresses in a specific cluster
   * \param clusterId Cluster ID to query
   * \return Vector of addresses in the cluster
   */
  std::vector<Ipv4Address> GetClusterMembers (uint32_t clusterId) const;

  /**
   * \brief Get average RSSI for a specific cluster
   * \param clusterId Cluster ID to query
   * \return Average RSSI in dBm for known members of that cluster
   */
  double GetAverageRssiByCluster (uint32_t clusterId) const;

  /**
   * \brief Remove expired entries
   * \param expireTime Entries older than this are removed
   */
  void Purge (Time expireTime);

  /**
   * \brief Get number of entries
   * \return Number of entries in the table
   */
  uint32_t GetSize () const;

  /**
   * \brief Clear all entries
   */
  void Clear ();

  /**
   * \brief Get number of members in a specific cluster
   * \param clusterId Cluster ID to query
   * \return Number of known members in that cluster
   */
  uint32_t GetClusterMemberCount (uint32_t clusterId) const;

  /**
   * \brief Get the cluster head address for a specific cluster
   * \param clusterId Cluster ID to query
   * \return Cluster head address, or Ipv4Address::GetZero() if not found
   */
  Ipv4Address GetClusterHeadAddress (uint32_t clusterId) const;

  /**
   * \brief Iterator type for external iteration
   */
  typedef std::map<Ipv4Address, ClusterInfo>::const_iterator ConstIterator;

  /**
   * \brief Get begin iterator
   */
  ConstIterator Begin () const;

  /**
   * \brief Get end iterator
   */
  ConstIterator End () const;

private:
  std::map<Ipv4Address, ClusterInfo> m_table;  ///< The cluster table
};

} // namespace smartAodvV2
} // namespace ns3

#endif /* SMART_AODV_V2_CLUSTER_H */
