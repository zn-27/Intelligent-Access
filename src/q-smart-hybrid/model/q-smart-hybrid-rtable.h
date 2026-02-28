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
 *
 * Authors: Q-Smart-Hybrid Implementation
 */

#ifndef Q_SMART_HYBRID_RTABLE_H
#define Q_SMART_HYBRID_RTABLE_H

#include <stdint.h>
#include <cassert>
#include <map>
#include <list>
#include <vector>
#include <sys/types.h>
#include "ns3/ipv4.h"
#include "ns3/ipv4-route.h"
#include "ns3/timer.h"
#include "ns3/net-device.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"

namespace ns3
{
namespace qSmartHybrid
{

/**
 * \ingroup qSmartHybrid
 * \brief Route record states
 */
enum RouteFlags
{
  VALID = 0,      //!< Route is valid
  INVALID = 1,    //!< Route is invalid
  IN_SEARCH = 2   //!< Route discovery in progress
};

/**
 * \ingroup qSmartHybrid
 * \brief Protocol source for route entries
 */
enum ProtocolSource
{
  PROACTIVE_OLSR = 0,   ///< Route from OLSR TC message
  REACTIVE_SAODV = 1,   ///< Route from Smart-AODV RREP
  HYBRID_LEARNED = 2    ///< Route learned by Q-Learning
};

/**
 * \ingroup qSmartHybrid
 * \brief Unified routing table entry
 *
 * Supports both OLSR and Smart-AODV route entries with
 * link quality metrics and route scoring.
 */
class RoutingTableEntry
{
public:
  /**
   * \brief Constructor
   *
   * \param dev the device
   * \param dst the destination IP address
   * \param vSeqNo verify sequence number flag
   * \param seqNo the sequence number
   * \param iface the interface
   * \param hops the number of hops
   * \param nextHop the IP address of the next hop
   * \param lifetime the lifetime of the entry
   */
  RoutingTableEntry (Ptr<NetDevice> dev = 0, Ipv4Address dst = Ipv4Address (),
                     bool vSeqNo = false, uint32_t seqNo = 0,
                     Ipv4InterfaceAddress iface = Ipv4InterfaceAddress (),
                     uint16_t hops = 0, Ipv4Address nextHop = Ipv4Address (),
                     Time lifetime = Simulator::Now ());

  ~RoutingTableEntry ();

  ///\name Precursors management
  //\{
  /**
   * \brief Insert precursor in precursor list if it doesn't yet exist
   * \param id precursor address
   * \return true on success
   */
  bool InsertPrecursor (Ipv4Address id);

  /**
   * \brief Lookup precursor by address
   * \param id precursor address
   * \return true on success
   */
  bool LookupPrecursor (Ipv4Address id);

  /**
   * \brief Delete precursor
   * \param id precursor address
   * \return true on success
   */
  bool DeletePrecursor (Ipv4Address id);

  /**
   * \brief Delete all precursors
   */
  void DeleteAllPrecursors ();

  /**
   * \brief Check that precursor list is empty
   * \return true if precursor list is empty
   */
  bool IsPrecursorListEmpty () const;

  /**
   * \brief Inserts precursors in output parameter prec if they do not yet exist in vector
   * \param prec vector of precursor addresses
   */
  void GetPrecursors (std::vector<Ipv4Address>& prec) const;
  //\}

  /**
   * \brief Mark entry as "down" (i.e. disable it)
   * \param badLinkLifetime duration to keep entry marked as invalid
   */
  void Invalidate (Time badLinkLifetime);

  // === Basic fields ===
  /**
   * \brief Get destination address
   * \return The IPv4 destination address
   */
  Ipv4Address GetDestination () const
  {
    return m_ipv4Route->GetDestination ();
  }

  /**
   * \brief Get route
   * \return The IPv4 route
   */
  Ptr<Ipv4Route> GetRoute () const
  {
    return m_ipv4Route;
  }

  /**
   * \brief Set route
   * \param r the IPv4 route
   */
  void SetRoute (Ptr<Ipv4Route> r)
  {
    m_ipv4Route = r;
  }

  /**
   * \brief Set next hop address
   * \param nextHop the next hop IPv4 address
   */
  void SetNextHop (Ipv4Address nextHop)
  {
    m_ipv4Route->SetGateway (nextHop);
  }

  /**
   * \brief Get next hop address
   * \return The next hop address
   */
  Ipv4Address GetNextHop () const
  {
    return m_ipv4Route->GetGateway ();
  }

  /**
   * \brief Set output device
   * \param dev The output device
   */
  void SetOutputDevice (Ptr<NetDevice> dev)
  {
    m_ipv4Route->SetOutputDevice (dev);
  }

  /**
   * \brief Get output device
   * \return The output device
   */
  Ptr<NetDevice> GetOutputDevice () const
  {
    return m_ipv4Route->GetOutputDevice ();
  }

  /**
   * \brief Get the Ipv4InterfaceAddress
   * \return The Ipv4InterfaceAddress
   */
  Ipv4InterfaceAddress GetInterface () const
  {
    return m_iface;
  }

  /**
   * \brief Set the Ipv4InterfaceAddress
   * \param iface The Ipv4InterfaceAddress
   */
  void SetInterface (Ipv4InterfaceAddress iface)
  {
    m_iface = iface;
  }

  /**
   * \brief Set the valid sequence number flag
   * \param s the sequence number flag
   */
  void SetValidSeqNo (bool s)
  {
    m_validSeqNo = s;
  }

  /**
   * \brief Get the valid sequence number flag
   * \return The valid sequence number flag
   */
  bool GetValidSeqNo () const
  {
    return m_validSeqNo;
  }

  /**
   * \brief Set the sequence number
   * \param sn the sequence number
   */
  void SetSeqNo (uint32_t sn)
  {
    m_seqNo = sn;
  }

  /**
   * \brief Get the sequence number
   * \return The sequence number
   */
  uint32_t GetSeqNo () const
  {
    return m_seqNo;
  }

  /**
   * \brief Set the number of hops
   * \param hop the number of hops
   */
  void SetHop (uint16_t hop)
  {
    m_hops = hop;
  }

  /**
   * \brief Get the number of hops
   * \return The number of hops
   */
  uint16_t GetHop () const
  {
    return m_hops;
  }

  /**
   * \brief Set the lifetime
   * \param lt The lifetime
   */
  void SetLifeTime (Time lt)
  {
    m_lifeTime = lt + Simulator::Now ();
  }

  /**
   * \brief Get the lifetime
   * \return The lifetime
   */
  Time GetLifeTime () const
  {
    return m_lifeTime - Simulator::Now ();
  }

  /**
   * \brief Get the remaining lifetime
   * \return Remaining time until expiration
   */
  Time GetRemainingLife () const
  {
    Time remaining = m_lifeTime - Simulator::Now ();
    return remaining.IsPositive () ? remaining : Seconds (0);
  }

  /**
   * \brief Set the route flags
   * \param flag the route flags
   */
  void SetFlag (RouteFlags flag)
  {
    m_flag = flag;
  }

  /**
   * \brief Get the route flags
   * \return The route flags
   */
  RouteFlags GetFlag () const
  {
    return m_flag;
  }

  /**
   * \brief Set the RREQ count
   * \param n the RREQ count
   */
  void SetRreqCnt (uint8_t n)
  {
    m_reqCount = n;
  }

  /**
   * \brief Get the RREQ count
   * \return The RREQ count
   */
  uint8_t GetRreqCnt () const
  {
    return m_reqCount;
  }

  /**
   * \brief Increment the RREQ count
   */
  void IncrementRreqCnt ()
  {
    m_reqCount++;
  }

  /**
   * \brief Set the unidirectional flag
   * \param u the uni directional flag
   */
  void SetUnidirectional (bool u)
  {
    m_blackListState = u;
  }

  /**
   * \brief Get the unidirectional flag
   * \return The unidirectional flag
   */
  bool IsUnidirectional () const
  {
    return m_blackListState;
  }

  /**
   * \brief Set the blacklist timeout
   * \param t the blacklist timeout value
   */
  void SetBlacklistTimeout (Time t)
  {
    m_blackListTimeout = t;
  }

  /**
   * \brief Get the blacklist timeout value
   * \return The blacklist timeout value
   */
  Time GetBlacklistTimeout () const
  {
    return m_blackListTimeout;
  }

  /// RREP_ACK timer
  Timer m_ackTimer;

  // === Link quality metrics (Smart-AODV) ===
  /**
   * \brief Set the last RSSI value received from this route
   * \param rssi the RSSI value in dBm
   */
  void SetLastRssi (double rssi)
  {
    m_lastRssi = rssi;
  }

  /**
   * \brief Get the last RSSI value
   * \return The last RSSI value in dBm
   */
  double GetLastRssi () const
  {
    return m_lastRssi;
  }

  /**
   * \brief Set the minimum SNR along the path
   * \param snr the SNR value in dB
   */
  void SetMinSnr (double snr)
  {
    m_minSnr = snr;
  }

  /**
   * \brief Get the minimum SNR along the path
   * \return The minimum SNR value in dB
   */
  double GetMinSnr () const
  {
    return m_minSnr;
  }

  /**
   * \brief Set the link expiry time (predicted link lifetime)
   * \param expiry the predicted expiry time
   */
  void SetLinkExpiryTime (Time expiry)
  {
    m_linkExpiryTime = expiry;
  }

  /**
   * \brief Get the link expiry time
   * \return The predicted link expiry time
   */
  Time GetLinkExpiryTime () const
  {
    return m_linkExpiryTime;
  }

  // === Q-Smart-Hybrid specific fields ===
  /**
   * \brief Set the protocol source
   * \param source the protocol source
   */
  void SetProtocolSource (ProtocolSource source)
  {
    m_protocolSource = source;
  }

  /**
   * \brief Get the protocol source
   * \return The protocol source
   */
  ProtocolSource GetProtocolSource () const
  {
    return m_protocolSource;
  }

  /**
   * \brief Set the route discovery time
   * \param time the discovery time
   */
  void SetRouteDiscoveryTime (Time time)
  {
    m_routeDiscoveryTime = time;
  }

  /**
   * \brief Get the route discovery time
   * \return The route discovery time
   */
  Time GetRouteDiscoveryTime () const
  {
    return m_routeDiscoveryTime;
  }

  /**
   * \brief Set the route usage count
   * \param count the usage count
   */
  void SetRouteUsageCount (uint32_t count)
  {
    m_routeUsageCount = count;
  }

  /**
   * \brief Get the route usage count
   * \return The route usage count
   */
  uint32_t GetRouteUsageCount () const
  {
    return m_routeUsageCount;
  }

  /**
   * \brief Increment the route usage count
   */
  void IncrementUsageCount ()
  {
    m_routeUsageCount++;
  }

  /**
   * \brief Set the Q-value for this route
   * \param qValue the Q-value
   */
  void SetQValue (double qValue)
  {
    m_qValue = qValue;
  }

  /**
   * \brief Get the Q-value for this route
   * \return The Q-value
   */
  double GetQValue () const
  {
    return m_qValue;
  }

  /**
   * \brief Set the consecutive transmission failure count
   * \param count the failure count
   */
  void SetConsecutiveTxFailures (uint32_t count)
  {
    m_consecutiveTxFailures = count;
  }

  /**
   * \brief Get the consecutive transmission failure count
   * \return The failure count
   */
  uint32_t GetConsecutiveTxFailures () const
  {
    return m_consecutiveTxFailures;
  }

  /**
   * \brief Increment the consecutive transmission failure count
   */
  void IncrementTxFailures ()
  {
    m_consecutiveTxFailures++;
  }

  /**
   * \brief Reset the consecutive transmission failure count
   */
  void ResetTxFailures ()
  {
    m_consecutiveTxFailures = 0;
  }

  /**
   * \brief Calculate route score for selection
   *
   * Score = 0.5 * snrScore + 0.3 * lifeScore - 0.2 * hopPenalty
   * \return Route score (higher is better)
   */
  double GetScore () const
  {
    // Normalize SNR (assume max 30dB)
    double snrScore = m_minSnr / 30.0;
    if (snrScore > 1.0) snrScore = 1.0;
    if (snrScore < 0.0) snrScore = 0.0;

    // Normalize remaining lifetime (assume max 30s)
    double lifeScore = GetRemainingLife ().GetSeconds () / 30.0;
    if (lifeScore > 1.0) lifeScore = 1.0;
    if (lifeScore < 0.0) lifeScore = 0.0;

    // Normalize hop penalty (assume max 10 hops)
    double hopPenalty = m_hops / 10.0;
    if (hopPenalty > 1.0) hopPenalty = 1.0;

    return 0.5 * snrScore + 0.3 * lifeScore - 0.2 * hopPenalty;
  }

  /**
   * \brief Compare destination address
   * \param dst IP address to compare
   * \return true if equal
   */
  bool operator== (Ipv4Address const dst) const
  {
    return (m_ipv4Route->GetDestination () == dst);
  }

  /**
   * \brief Print packet to trace file
   * \param stream The output stream
   * \param unit The time unit to use (default Time::S)
   */
  void Print (Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;

private:
  // === Basic AODV fields ===
  bool m_validSeqNo;          ///< Valid Destination Sequence Number flag
  uint32_t m_seqNo;           ///< Destination Sequence Number
  uint16_t m_hops;            ///< Hop Count
  Time m_lifeTime;            ///< Expiration or deletion time
  Ptr<Ipv4Route> m_ipv4Route; ///< IP route
  Ipv4InterfaceAddress m_iface; ///< Output interface address
  RouteFlags m_flag;          ///< Routing flags

  std::vector<Ipv4Address> m_precursorList; ///< List of precursors
  Time m_routeRequestTimout;  ///< When I can send another request
  uint8_t m_reqCount;         ///< Number of route requests
  bool m_blackListState;      ///< Indicate if in "blacklist"
  Time m_blackListTimeout;    ///< Blacklist timeout

  // === Smart-AODV link quality metrics ===
  double m_lastRssi;          ///< Last RSSI value (dBm)
  double m_minSnr;            ///< Minimum SNR along path (dB)
  Time m_linkExpiryTime;      ///< Predicted link expiry time

  // === Q-Smart-Hybrid specific fields ===
  ProtocolSource m_protocolSource;  ///< Protocol source
  Time m_routeDiscoveryTime;        ///< When route was discovered
  uint32_t m_routeUsageCount;       ///< Number of times used
  double m_qValue;                  ///< Q-value for this route
  uint32_t m_consecutiveTxFailures; ///< Consecutive TX failures
};

/**
 * \ingroup qSmartHybrid
 * \brief The Unified Routing table
 */
class RoutingTable
{
public:
  /**
   * \brief constructor
   * \param t the routing table entry lifetime
   */
  RoutingTable (Time t);

  //\name Handle lifetime of invalid route
  //\{
  /**
   * \brief Get the lifetime of a bad link
   * \return The lifetime of a bad link
   */
  Time GetBadLinkLifetime () const
  {
    return m_badLinkLifetime;
  }

  /**
   * \brief Set the lifetime of a bad link
   * \param t the lifetime of a bad link
   */
  void SetBadLinkLifetime (Time t)
  {
    m_badLinkLifetime = t;
  }
  //\}

  /**
   * \brief Add routing table entry if it doesn't yet exist
   * \param r routing table entry
   * \return true on success
   */
  bool AddRoute (RoutingTableEntry& r);

  /**
   * \brief Delete routing table entry with destination address dst
   * \param dst destination address
   * \return true on success
   */
  bool DeleteRoute (Ipv4Address dst);

  /**
   * \brief Lookup routing table entry with destination address dst
   * \param dst destination address
   * \param rt entry with destination address dst, if exists
   * \return true on success
   */
  bool LookupRoute (Ipv4Address dst, RoutingTableEntry& rt);

  /**
   * \brief Lookup route in VALID state
   * \param dst destination address
   * \param rt entry with destination address dst, if exists
   * \return true on success
   */
  bool LookupValidRoute (Ipv4Address dst, RoutingTableEntry& rt);

  /**
   * \brief Lookup all routes to a destination
   * \param dst destination address
   * \param routes vector to store matching routes
   * \return true if at least one route found
   */
  bool LookupAllRoutes (Ipv4Address dst, std::vector<RoutingTableEntry>& routes);

  /**
   * \brief Select best route to destination using scoring
   * \param dst destination address
   * \param rt best route entry
   * \return true if route found
   */
  bool SelectBestRoute (Ipv4Address dst, RoutingTableEntry& rt);

  /**
   * \brief Update routing table
   * \param rt entry with destination address dst, if exists
   * \return true on success
   */
  bool Update (RoutingTableEntry& rt);

  /**
   * \brief Set routing table entry flags
   * \param dst destination address
   * \param state the routing flags
   * \return true on success
   */
  bool SetEntryState (Ipv4Address dst, RouteFlags state);

  /**
   * \brief Lookup routing entries with next hop Address dst
   * \param nextHop the next hop IP address
   * \param unreachable map of unreachable destinations
   */
  void GetListOfDestinationWithNextHop (Ipv4Address nextHop, std::map<Ipv4Address, uint32_t>& unreachable);

  /**
   * \brief Invalidate routes with unreachable destinations
   * \param unreachable routes to invalidate
   */
  void InvalidateRoutesWithDst (std::map<Ipv4Address, uint32_t> const& unreachable);

  /**
   * \brief Delete all route from interface with address iface
   * \param iface the interface IP address
   */
  void DeleteAllRoutesFromInterface (Ipv4InterfaceAddress iface);

  /**
   * \brief Delete all entries from routing table
   */
  void Clear ()
  {
    m_ipv4AddressEntry.clear ();
  }

  /**
   * \brief Delete all outdated entries and invalidate valid entry if Lifetime is expired
   */
  void Purge ();

  /**
   * \brief Mark link as unidirectional
   * \param neighbor neighbor address
   * \param blacklistTimeout blacklist timeout
   * \return true on success
   */
  bool MarkLinkAsUnidirectional (Ipv4Address neighbor, Time blacklistTimeout);

  /**
   * \brief Print routing table
   * \param stream the output stream
   * \param unit The time unit to use (default Time::S)
   */
  void Print (Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;

  /**
   * \brief Get number of entries
   * \return Number of entries in routing table
   */
  uint32_t GetSize () const
  {
    return m_ipv4AddressEntry.size ();
  }

private:
  /// The routing table (supports multiple routes per destination)
  std::multimap<Ipv4Address, RoutingTableEntry> m_ipv4AddressEntry;
  /// Deletion time for invalid routes
  Time m_badLinkLifetime;

  /**
   * \brief const version of Purge, for use by Print() method
   * \param table the routing table entry to purge
   */
  void Purge (std::multimap<Ipv4Address, RoutingTableEntry>& table) const;
};

} // namespace qSmartHybrid
} // namespace ns3

#endif /* Q_SMART_HYBRID_RTABLE_H */
