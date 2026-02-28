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

#include "smart-aodv-cluster-cluster.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <cmath>

namespace ns3 {
namespace smartAodvClusterV2 {

NS_LOG_COMPONENT_DEFINE ("SmartAodvClusterTable");

ClusterTable::ClusterTable ()
{
  NS_LOG_FUNCTION (this);
}

ClusterTable::~ClusterTable ()
{
  NS_LOG_FUNCTION (this);
}

uint32_t
ClusterTable::GetClusterId (Ipv4Address addr) const
{
  std::map<Ipv4Address, ClusterInfo>::const_iterator it = m_table.find (addr);
  if (it != m_table.end ())
    {
      return it->second.m_clusterId;
    }
  return 0;  // Unknown cluster
}

void
ClusterTable::SetClusterId (Ipv4Address addr, uint32_t clusterId)
{
  NS_LOG_FUNCTION (this << addr << clusterId);

  std::map<Ipv4Address, ClusterInfo>::iterator it = m_table.find (addr);
  if (it != m_table.end ())
    {
      it->second.m_clusterId = clusterId;
      it->second.m_lastSeen = Simulator::Now ();
    }
  else
    {
      ClusterInfo info;
      info.m_address = addr;
      info.m_clusterId = clusterId;
      info.m_lastSeen = Simulator::Now ();
      m_table[addr] = info;
    }
}

bool
ClusterTable::IsSameCluster (Ipv4Address addr1, Ipv4Address addr2) const
{
  uint32_t c1 = GetClusterId (addr1);
  uint32_t c2 = GetClusterId (addr2);

  // Both in same cluster (and both known, and not cluster 0)
  if (c1 == 0 || c2 == 0)
    {
      return false;  // Unknown clusters are not same
    }
  return (c1 == c2);
}

void
ClusterTable::UpdateRssi (Ipv4Address addr, double rssi)
{
  NS_LOG_FUNCTION (this << addr << rssi);

  std::map<Ipv4Address, ClusterInfo>::iterator it = m_table.find (addr);
  if (it != m_table.end ())
    {
      // Exponential moving average: new = 0.3 * new + 0.7 * old
      if (it->second.m_avgRssi < -50.0)  // First real value (not initialized)
        {
          it->second.m_avgRssi = rssi;
        }
      else
        {
          it->second.m_avgRssi = 0.3 * rssi + 0.7 * it->second.m_avgRssi;
        }
      it->second.m_lastSeen = Simulator::Now ();
      NS_LOG_DEBUG ("Updated RSSI for " << addr << ": " << rssi
                    << " dBm (avg: " << it->second.m_avgRssi << " dBm)");
    }
  else
    {
      // Create new entry with RSSI but unknown cluster
      NS_LOG_DEBUG ("Creating new cluster entry for " << addr << " with RSSI " << rssi);
      ClusterInfo info;
      info.m_address = addr;
      info.m_clusterId = 0;  // Unknown cluster
      info.m_avgRssi = rssi;
      info.m_lastSeen = Simulator::Now ();
      m_table[addr] = info;
    }
}

double
ClusterTable::GetRssi (Ipv4Address addr) const
{
  std::map<Ipv4Address, ClusterInfo>::const_iterator it = m_table.find (addr);
  if (it != m_table.end ())
    {
      return it->second.m_avgRssi;
    }
  return -100.0;  // Unknown
}

bool
ClusterTable::HasEntry (Ipv4Address addr) const
{
  return m_table.find (addr) != m_table.end ();
}

ClusterInfo
ClusterTable::GetClusterInfo (Ipv4Address addr) const
{
  std::map<Ipv4Address, ClusterInfo>::const_iterator it = m_table.find (addr);
  if (it != m_table.end ())
    {
      return it->second;
    }
  return ClusterInfo ();  // Return empty info
}

void
ClusterTable::SetClusterHead (Ipv4Address addr, bool isHead)
{
  NS_LOG_FUNCTION (this << addr << isHead);

  std::map<Ipv4Address, ClusterInfo>::iterator it = m_table.find (addr);
  if (it != m_table.end ())
    {
      it->second.m_isClusterHead = isHead;
      it->second.m_lastSeen = Simulator::Now ();
    }
  else
    {
      ClusterInfo info;
      info.m_address = addr;
      info.m_isClusterHead = isHead;
      info.m_lastSeen = Simulator::Now ();
      m_table[addr] = info;
    }
}

std::vector<Ipv4Address>
ClusterTable::GetClusterMembers (uint32_t clusterId) const
{
  std::vector<Ipv4Address> members;

  for (std::map<Ipv4Address, ClusterInfo>::const_iterator it = m_table.begin ();
       it != m_table.end (); ++it)
    {
      if (it->second.m_clusterId == clusterId)
        {
          members.push_back (it->first);
        }
    }

  return members;
}

double
ClusterTable::GetAverageRssiByCluster (uint32_t clusterId) const
{
  double totalRssi = 0.0;
  uint32_t count = 0;

  for (std::map<Ipv4Address, ClusterInfo>::const_iterator it = m_table.begin ();
       it != m_table.end (); ++it)
    {
      if (it->second.m_clusterId == clusterId && it->second.m_avgRssi > -100.0)
        {
          totalRssi += it->second.m_avgRssi;
          count++;
        }
    }

  if (count > 0)
    {
      return totalRssi / count;
    }
  return -100.0;
}

void
ClusterTable::Purge (Time expireTime)
{
  NS_LOG_FUNCTION (this << expireTime);
  Time now = Simulator::Now ();

  for (std::map<Ipv4Address, ClusterInfo>::iterator it = m_table.begin ();
       it != m_table.end (); )
    {
      if (now - it->second.m_lastSeen > expireTime)
        {
          NS_LOG_DEBUG ("Purging expired cluster entry for " << it->first);
          m_table.erase (it++);
        }
      else
        {
          ++it;
        }
    }
}

uint32_t
ClusterTable::GetSize () const
{
  return m_table.size ();
}

void
ClusterTable::Clear ()
{
  NS_LOG_FUNCTION (this);
  m_table.clear ();
}

uint32_t
ClusterTable::GetClusterMemberCount (uint32_t clusterId) const
{
  uint32_t count = 0;

  for (std::map<Ipv4Address, ClusterInfo>::const_iterator it = m_table.begin ();
       it != m_table.end (); ++it)
    {
      if (it->second.m_clusterId == clusterId)
        {
          count++;
        }
    }

  return count;
}

Ipv4Address
ClusterTable::GetClusterHeadAddress (uint32_t clusterId) const
{
  for (std::map<Ipv4Address, ClusterInfo>::const_iterator it = m_table.begin ();
       it != m_table.end (); ++it)
    {
      if (it->second.m_clusterId == clusterId && it->second.m_isClusterHead)
        {
          return it->first;
        }
    }
  return Ipv4Address::GetZero ();
}

ClusterTable::ConstIterator
ClusterTable::Begin () const
{
  return m_table.begin ();
}

ClusterTable::ConstIterator
ClusterTable::End () const
{
  return m_table.end ();
}

} // namespace smartAodvClusterV2
} // namespace ns3
