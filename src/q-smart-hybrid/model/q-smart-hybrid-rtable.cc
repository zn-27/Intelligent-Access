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

#include "q-smart-hybrid-rtable.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("QSmartHybridRoutingTable");

namespace qSmartHybrid
{

RoutingTableEntry::RoutingTableEntry (Ptr<NetDevice> dev, Ipv4Address dst,
                                      bool vSeqNo, uint32_t seqNo,
                                      Ipv4InterfaceAddress iface, uint16_t hops,
                                      Ipv4Address nextHop, Time lifetime)
  : m_validSeqNo (vSeqNo),
    m_seqNo (seqNo),
    m_hops (hops),
    m_lifeTime (lifetime + Simulator::Now ()),
    m_ipv4Route (Create<Ipv4Route> ()),
    m_iface (iface),
    m_flag (VALID),
    m_reqCount (0),
    m_blackListState (false),
    m_lastRssi (0.0),
    m_minSnr (30.0),
    m_linkExpiryTime (Time::Max ()),
    m_protocolSource (REACTIVE_SAODV),
    m_routeDiscoveryTime (Simulator::Now ()),
    m_routeUsageCount (0),
    m_qValue (0.0),
    m_consecutiveTxFailures (0)
{
  NS_LOG_FUNCTION (this << dst << nextHop);
  m_ipv4Route->SetDestination (dst);
  m_ipv4Route->SetGateway (nextHop);
  m_ipv4Route->SetSource (iface.GetLocal ());
  m_ipv4Route->SetOutputDevice (dev);
}

RoutingTableEntry::~RoutingTableEntry ()
{
  NS_LOG_FUNCTION (this);
}

bool
RoutingTableEntry::InsertPrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  for (std::vector<Ipv4Address>::const_iterator i = m_precursorList.begin ();
       i != m_precursorList.end (); ++i)
  {
    if (*i == id)
    {
      return false;
    }
  }
  m_precursorList.push_back (id);
  return true;
}

bool
RoutingTableEntry::LookupPrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  for (std::vector<Ipv4Address>::const_iterator i = m_precursorList.begin ();
       i != m_precursorList.end (); ++i)
  {
    if (*i == id)
    {
      return true;
    }
  }
  return false;
}

bool
RoutingTableEntry::DeletePrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  for (std::vector<Ipv4Address>::iterator i = m_precursorList.begin ();
       i != m_precursorList.end (); ++i)
  {
    if (*i == id)
    {
      m_precursorList.erase (i);
      return true;
    }
  }
  return false;
}

void
RoutingTableEntry::DeleteAllPrecursors ()
{
  NS_LOG_FUNCTION (this);
  m_precursorList.clear ();
}

bool
RoutingTableEntry::IsPrecursorListEmpty () const
{
  return m_precursorList.empty ();
}

void
RoutingTableEntry::GetPrecursors (std::vector<Ipv4Address>& prec) const
{
  NS_LOG_FUNCTION (this);
  for (std::vector<Ipv4Address>::const_iterator i = m_precursorList.begin ();
       i != m_precursorList.end (); ++i)
  {
    bool found = false;
    for (std::vector<Ipv4Address>::iterator j = prec.begin (); j != prec.end (); ++j)
    {
      if (*j == *i)
      {
        found = true;
        break;
      }
    }
    if (!found)
    {
      prec.push_back (*i);
    }
  }
}

void
RoutingTableEntry::Invalidate (Time badLinkLifetime)
{
  NS_LOG_FUNCTION (this << badLinkLifetime);
  if (m_flag == INVALID)
  {
    return;
  }
  m_flag = INVALID;
  m_reqCount = 0;
  m_lifeTime = badLinkLifetime + Simulator::Now ();
}

void
RoutingTableEntry::Print (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  NS_LOG_FUNCTION (this);
  std::ostream* os = stream->GetStream ();
  *os << "Destination: " << GetDestination ()
      << " NextHop: " << GetNextHop ()
      << " Hops: " << m_hops
      << " Flag: " << (m_flag == VALID ? "VALID" : m_flag == INVALID ? "INVALID" : "IN_SEARCH")
      << " LifeTime: " << GetLifeTime ().As (unit)
      << " Score: " << GetScore ()
      << " Source: " << (m_protocolSource == PROACTIVE_OLSR ? "OLSR" :
                         m_protocolSource == REACTIVE_SAODV ? "SAODV" : "HYBRID")
      << "\n";
}

RoutingTable::RoutingTable (Time t)
  : m_badLinkLifetime (t)
{
  NS_LOG_FUNCTION (this);
}

bool
RoutingTable::AddRoute (RoutingTableEntry& r)
{
  NS_LOG_FUNCTION (this << r.GetDestination ());
  Purge ();
  // Insert into multimap (allows multiple entries per destination)
  m_ipv4AddressEntry.insert (std::make_pair (r.GetDestination (), r));
  return true;
}

bool
RoutingTable::DeleteRoute (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  Purge ();
  // Delete all routes to destination
  auto range = m_ipv4AddressEntry.equal_range (dst);
  if (range.first == range.second)
  {
    return false;
  }
  m_ipv4AddressEntry.erase (range.first, range.second);
  return true;
}

bool
RoutingTable::LookupRoute (Ipv4Address dst, RoutingTableEntry& rt)
{
  NS_LOG_FUNCTION (this << dst);
  Purge ();

  auto range = m_ipv4AddressEntry.equal_range (dst);
  if (range.first == range.second)
  {
    return false;
  }

  // Return first valid route
  for (auto it = range.first; it != range.second; ++it)
  {
    if (it->second.GetFlag () == VALID)
    {
      rt = it->second;
      return true;
    }
  }
  return false;
}

bool
RoutingTable::LookupValidRoute (Ipv4Address dst, RoutingTableEntry& rt)
{
  NS_LOG_FUNCTION (this << dst);
  Purge ();

  auto range = m_ipv4AddressEntry.equal_range (dst);
  for (auto it = range.first; it != range.second; ++it)
  {
    if (it->second.GetFlag () == VALID)
    {
      rt = it->second;
      return true;
    }
  }
  return false;
}

bool
RoutingTable::LookupAllRoutes (Ipv4Address dst, std::vector<RoutingTableEntry>& routes)
{
  NS_LOG_FUNCTION (this << dst);
  Purge ();

  auto range = m_ipv4AddressEntry.equal_range (dst);
  if (range.first == range.second)
  {
    return false;
  }

  routes.clear ();
  for (auto it = range.first; it != range.second; ++it)
  {
    if (it->second.GetFlag () == VALID)
    {
      routes.push_back (it->second);
    }
  }
  return !routes.empty ();
}

bool
RoutingTable::SelectBestRoute (Ipv4Address dst, RoutingTableEntry& rt)
{
  NS_LOG_FUNCTION (this << dst);
  std::vector<RoutingTableEntry> routes;
  if (!LookupAllRoutes (dst, routes))
  {
    return false;
  }

  // Select route with highest score
  double bestScore = -1000.0;
  bool found = false;

  for (const auto& route : routes)
  {
    double score = route.GetScore ();
    if (score > bestScore)
    {
      bestScore = score;
      rt = route;
      found = true;
    }
  }

  NS_LOG_DEBUG ("Selected best route to " << dst << " with score " << bestScore);
  return found;
}

bool
RoutingTable::Update (RoutingTableEntry& rt)
{
  NS_LOG_FUNCTION (this << rt.GetDestination ());

  auto range = m_ipv4AddressEntry.equal_range (rt.GetDestination ());
  for (auto it = range.first; it != range.second; ++it)
  {
    if (it->second.GetNextHop () == rt.GetNextHop ())
    {
      it->second = rt;
      return true;
    }
  }

  // Not found, add new entry
  return AddRoute (rt);
}

bool
RoutingTable::SetEntryState (Ipv4Address dst, RouteFlags state)
{
  NS_LOG_FUNCTION (this << dst << state);

  auto range = m_ipv4AddressEntry.equal_range (dst);
  if (range.first == range.second)
  {
    return false;
  }

  for (auto it = range.first; it != range.second; ++it)
  {
    it->second.SetFlag (state);
  }
  return true;
}

void
RoutingTable::GetListOfDestinationWithNextHop (Ipv4Address nextHop,
                                               std::map<Ipv4Address, uint32_t>& unreachable)
{
  NS_LOG_FUNCTION (this << nextHop);
  Purge ();

  unreachable.clear ();
  for (auto it = m_ipv4AddressEntry.begin (); it != m_ipv4AddressEntry.end (); ++it)
  {
    if (it->second.GetNextHop () == nextHop && !it->second.IsPrecursorListEmpty ())
    {
      unreachable.insert (std::make_pair (it->first, it->second.GetSeqNo ()));
    }
  }
}

void
RoutingTable::InvalidateRoutesWithDst (std::map<Ipv4Address, uint32_t> const& unreachable)
{
  NS_LOG_FUNCTION (this);
  Purge ();

  for (auto it = m_ipv4AddressEntry.begin (); it != m_ipv4AddressEntry.end (); ++it)
  {
    if (unreachable.find (it->first) != unreachable.end ())
    {
      it->second.Invalidate (m_badLinkLifetime);
    }
  }
}

void
RoutingTable::DeleteAllRoutesFromInterface (Ipv4InterfaceAddress iface)
{
  NS_LOG_FUNCTION (this << iface);
  for (auto it = m_ipv4AddressEntry.begin (); it != m_ipv4AddressEntry.end (); )
  {
    if (it->second.GetInterface () == iface)
    {
      it = m_ipv4AddressEntry.erase (it);
    }
    else
    {
      ++it;
    }
  }
}

void
RoutingTable::Purge ()
{
  Purge (m_ipv4AddressEntry);
}

void
RoutingTable::Purge (std::multimap<Ipv4Address, RoutingTableEntry>& table) const
{
  for (auto it = table.begin (); it != table.end (); )
  {
    if (it->second.GetLifeTime ().IsNegative () && it->second.GetFlag () != INVALID)
    {
      it->second.Invalidate (m_badLinkLifetime);
      ++it;
    }
    else if (it->second.GetLifeTime ().IsNegative () && it->second.GetFlag () == INVALID)
    {
      it = table.erase (it);
    }
    else
    {
      ++it;
    }
  }
}

bool
RoutingTable::MarkLinkAsUnidirectional (Ipv4Address neighbor, Time blacklistTimeout)
{
  NS_LOG_FUNCTION (this << neighbor << blacklistTimeout);

  auto range = m_ipv4AddressEntry.equal_range (neighbor);
  for (auto it = range.first; it != range.second; ++it)
  {
    if (it->second.GetNextHop () == neighbor)
    {
      it->second.SetUnidirectional (true);
      it->second.SetBlacklistTimeout (blacklistTimeout);
      it->second.SetFlag (INVALID);
      return true;
    }
  }
  return false;
}

void
RoutingTable::Print (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  NS_LOG_FUNCTION (this);
  std::ostream* os = stream->GetStream ();
  *os << "Q-Smart-Hybrid Routing Table\n";
  *os << "Dest\t\tNextHop\t\tHops\tFlag\tLifeTime\tScore\tSource\n";

  for (const auto& entry : m_ipv4AddressEntry)
  {
    entry.second.Print (stream, unit);
  }
}

} // namespace qSmartHybrid
} // namespace ns3
