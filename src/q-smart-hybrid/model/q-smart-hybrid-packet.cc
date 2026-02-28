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

#include "q-smart-hybrid-packet.h"
#include "ns3/address-utils.h"
#include "ns3/packet.h"
#include "ns3/log.h"
#include <cstring>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("QSmartHybridPacket");

namespace qSmartHybrid
{

//-----------------------------------------------------------------------------
// TypeHeader
//-----------------------------------------------------------------------------
TypeHeader::TypeHeader (MessageType t)
  : m_type (t),
    m_valid (true)
{
}

TypeId
TypeHeader::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::qSmartHybrid::TypeHeader")
    .SetParent<Header> ()
    .SetGroupName ("QSmartHybrid")
    .AddConstructor<TypeHeader> ();
  return tid;
}

TypeId
TypeHeader::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint32_t
TypeHeader::GetSerializedSize () const
{
  return 1;
}

void
TypeHeader::Serialize (Buffer::Iterator start) const
{
  start.WriteU8 ((uint8_t)m_type);
}

uint32_t
TypeHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  uint8_t type = i.ReadU8 ();
  m_valid = true;
  switch (type)
  {
    case QSHHYBRIDTYPE_RREQ:
    case QSHHYBRIDTYPE_RREP:
    case QSHHYBRIDTYPE_RERR:
    case QSHHYBRIDTYPE_RREP_ACK:
      m_type = (MessageType)type;
      break;
    default:
      m_valid = false;
  }
  return GetSerializedSize ();
}

void
TypeHeader::Print (std::ostream& os) const
{
  switch (m_type)
  {
    case QSHHYBRIDTYPE_RREQ:
      os << "RREQ";
      break;
    case QSHHYBRIDTYPE_RREP:
      os << "RREP";
      break;
    case QSHHYBRIDTYPE_RERR:
      os << "RERR";
      break;
    case QSHHYBRIDTYPE_RREP_ACK:
      os << "RREP_ACK";
      break;
    default:
      os << "UNKNOWN_TYPE";
  }
}

bool
TypeHeader::operator== (TypeHeader const& o) const
{
  return (m_type == o.m_type && m_valid == o.m_valid);
}

std::ostream&
operator<< (std::ostream& os, TypeHeader const& h)
{
  h.Print (os);
  return os;
}

//-----------------------------------------------------------------------------
// RreqHeader
//-----------------------------------------------------------------------------
RreqHeader::RreqHeader (uint8_t flags, uint8_t reserved, uint8_t hopCount,
                        uint32_t requestID, Ipv4Address dst,
                        uint32_t dstSeqNo, Ipv4Address origin,
                        uint32_t originSeqNo)
  : m_flags (flags),
    m_reserved (reserved),
    m_hopCount (hopCount),
    m_requestID (requestID),
    m_dst (dst),
    m_dstSeqNo (dstSeqNo),
    m_origin (origin),
    m_originSeqNo (originSeqNo),
    m_minSnr (30.0)
{
}

TypeId
RreqHeader::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::qSmartHybrid::RreqHeader")
    .SetParent<Header> ()
    .SetGroupName ("QSmartHybrid")
    .AddConstructor<RreqHeader> ();
  return tid;
}

TypeId
RreqHeader::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint32_t
RreqHeader::GetSerializedSize () const
{
  return 25 + 8; // 25 bytes standard + 8 bytes for minSnr (double)
}

void
RreqHeader::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteU8 (m_flags);
  i.WriteU8 (m_reserved);
  i.WriteU8 (m_hopCount);
  i.WriteHtonU32 (m_requestID);
  WriteTo (i, m_dst);
  i.WriteHtonU32 (m_dstSeqNo);
  WriteTo (i, m_origin);
  i.WriteHtonU32 (m_originSeqNo);
  // Serialize double using memcpy and hton64
  double snr = m_minSnr;
  uint64_t snrRaw;
  std::memcpy (&snrRaw, &snr, sizeof (double));
  i.WriteHtonU64 (snrRaw);
}

uint32_t
RreqHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_flags = i.ReadU8 ();
  m_reserved = i.ReadU8 ();
  m_hopCount = i.ReadU8 ();
  m_requestID = i.ReadNtohU32 ();
  ReadFrom (i, m_dst);
  m_dstSeqNo = i.ReadNtohU32 ();
  ReadFrom (i, m_origin);
  m_originSeqNo = i.ReadNtohU32 ();
  // Deserialize double using memcpy
  uint64_t snrRaw = i.ReadNtohU64 ();
  std::memcpy (&m_minSnr, &snrRaw, sizeof (double));
  return GetSerializedSize ();
}

void
RreqHeader::Print (std::ostream& os) const
{
  os << "RREQ ID " << m_requestID << " destination: ipv4 " << m_dst
     << " sequence number " << m_dstSeqNo << " source: ipv4 " << m_origin
     << " sequence number " << m_originSeqNo << " hop count " << (uint32_t)m_hopCount
     << " minSnr " << m_minSnr;
}

void
RreqHeader::SetGratuitousRrep (bool f)
{
  if (f)
    m_flags |= (1 << 5);
  else
    m_flags &= ~(1 << 5);
}

bool
RreqHeader::GetGratuitousRrep () const
{
  return (m_flags & (1 << 5));
}

void
RreqHeader::SetDestinationOnly (bool f)
{
  if (f)
    m_flags |= (1 << 4);
  else
    m_flags &= ~(1 << 4);
}

bool
RreqHeader::GetDestinationOnly () const
{
  return (m_flags & (1 << 4));
}

void
RreqHeader::SetUnknownSeqno (bool f)
{
  if (f)
    m_flags |= (1 << 3);
  else
    m_flags &= ~(1 << 3);
}

bool
RreqHeader::GetUnknownSeqno () const
{
  return (m_flags & (1 << 3));
}

bool
RreqHeader::operator== (RreqHeader const& o) const
{
  return (m_flags == o.m_flags && m_reserved == o.m_reserved
          && m_hopCount == o.m_hopCount && m_requestID == o.m_requestID
          && m_dst == o.m_dst && m_dstSeqNo == o.m_dstSeqNo
          && m_origin == o.m_origin && m_originSeqNo == o.m_originSeqNo
          && m_minSnr == o.m_minSnr);
}

std::ostream&
operator<< (std::ostream& os, RreqHeader const& h)
{
  h.Print (os);
  return os;
}

//-----------------------------------------------------------------------------
// RrepHeader
//-----------------------------------------------------------------------------
RrepHeader::RrepHeader (uint8_t prefixSize, uint8_t hopCount, Ipv4Address dst,
                        uint32_t dstSeqNo, Ipv4Address origin, Time lifetime)
  : m_flags (0),
    m_prefixSize (prefixSize),
    m_hopCount (hopCount),
    m_dst (dst),
    m_dstSeqNo (dstSeqNo),
    m_origin (origin),
    m_lifeTime (lifetime.GetMilliSeconds ())
{
}

TypeId
RrepHeader::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::qSmartHybrid::RrepHeader")
    .SetParent<Header> ()
    .SetGroupName ("QSmartHybrid")
    .AddConstructor<RrepHeader> ();
  return tid;
}

TypeId
RrepHeader::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint32_t
RrepHeader::GetSerializedSize () const
{
  return 19;
}

void
RrepHeader::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteU8 (m_flags);
  i.WriteU8 (m_prefixSize);
  i.WriteU8 (m_hopCount);
  WriteTo (i, m_dst);
  i.WriteHtonU32 (m_dstSeqNo);
  WriteTo (i, m_origin);
  i.WriteHtonU32 (m_lifeTime);
}

uint32_t
RrepHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_flags = i.ReadU8 ();
  m_prefixSize = i.ReadU8 ();
  m_hopCount = i.ReadU8 ();
  ReadFrom (i, m_dst);
  m_dstSeqNo = i.ReadNtohU32 ();
  ReadFrom (i, m_origin);
  m_lifeTime = i.ReadNtohU32 ();
  return GetSerializedSize ();
}

void
RrepHeader::Print (std::ostream& os) const
{
  os << "RREP destination: ipv4 " << m_dst << " sequence number " << m_dstSeqNo
     << " source: ipv4 " << m_origin << " hop count " << (uint32_t)m_hopCount
     << " lifetime " << m_lifeTime << "ms";
}

void
RrepHeader::SetLifeTime (Time t)
{
  m_lifeTime = t.GetMilliSeconds ();
}

Time
RrepHeader::GetLifeTime () const
{
  return MilliSeconds (m_lifeTime);
}

void
RrepHeader::SetAckRequired (bool f)
{
  if (f)
    m_flags |= (1 << 6);
  else
    m_flags &= ~(1 << 6);
}

bool
RrepHeader::GetAckRequired () const
{
  return (m_flags & (1 << 6));
}

void
RrepHeader::SetPrefixSize (uint8_t sz)
{
  m_prefixSize = sz;
}

uint8_t
RrepHeader::GetPrefixSize () const
{
  return m_prefixSize;
}

void
RrepHeader::SetHello (Ipv4Address src, uint32_t srcSeqNo, Time lifetime)
{
  m_flags = 0;
  m_prefixSize = 0;
  m_hopCount = 0;
  m_dst = src;
  m_dstSeqNo = srcSeqNo;
  m_origin = src;
  m_lifeTime = lifetime.GetMilliSeconds ();
}

bool
RrepHeader::operator== (RrepHeader const& o) const
{
  return (m_flags == o.m_flags && m_prefixSize == o.m_prefixSize
          && m_hopCount == o.m_hopCount && m_dst == o.m_dst
          && m_dstSeqNo == o.m_dstSeqNo && m_origin == o.m_origin
          && m_lifeTime == o.m_lifeTime);
}

std::ostream&
operator<< (std::ostream& os, RrepHeader const& h)
{
  h.Print (os);
  return os;
}

//-----------------------------------------------------------------------------
// RrepAckHeader
//-----------------------------------------------------------------------------
RrepAckHeader::RrepAckHeader ()
  : m_reserved (0)
{
}

TypeId
RrepAckHeader::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::qSmartHybrid::RrepAckHeader")
    .SetParent<Header> ()
    .SetGroupName ("QSmartHybrid")
    .AddConstructor<RrepAckHeader> ();
  return tid;
}

TypeId
RrepAckHeader::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint32_t
RrepAckHeader::GetSerializedSize () const
{
  return 1;
}

void
RrepAckHeader::Serialize (Buffer::Iterator start) const
{
  start.WriteU8 (m_reserved);
}

uint32_t
RrepAckHeader::Deserialize (Buffer::Iterator start)
{
  m_reserved = start.ReadU8 ();
  return GetSerializedSize ();
}

void
RrepAckHeader::Print (std::ostream& os) const
{
  os << "RREP_ACK";
}

bool
RrepAckHeader::operator== (RrepAckHeader const& o) const
{
  return m_reserved == o.m_reserved;
}

std::ostream&
operator<< (std::ostream& os, RrepAckHeader const& h)
{
  h.Print (os);
  return os;
}

//-----------------------------------------------------------------------------
// RerrHeader
//-----------------------------------------------------------------------------
RerrHeader::RerrHeader ()
  : m_flag (0),
    m_reserved (0)
{
}

TypeId
RerrHeader::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::qSmartHybrid::RerrHeader")
    .SetParent<Header> ()
    .SetGroupName ("QSmartHybrid")
    .AddConstructor<RerrHeader> ();
  return tid;
}

TypeId
RerrHeader::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint32_t
RerrHeader::GetSerializedSize () const
{
  return 2 + 8 * GetDestCount ();
}

void
RerrHeader::Serialize (Buffer::Iterator i) const
{
  i.WriteU8 (m_flag);
  i.WriteU8 (GetDestCount ());

  for (auto it = m_unreachableDstSeqNo.begin (); it != m_unreachableDstSeqNo.end (); ++it)
  {
    WriteTo (i, it->first);
    i.WriteHtonU32 (it->second);
  }
}

uint32_t
RerrHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_flag = i.ReadU8 ();
  uint8_t destCount = i.ReadU8 ();

  m_unreachableDstSeqNo.clear ();
  for (uint8_t n = 0; n < destCount; ++n)
  {
    Ipv4Address dst;
    ReadFrom (i, dst);
    uint32_t seqNo = i.ReadNtohU32 ();
    m_unreachableDstSeqNo.insert (std::make_pair (dst, seqNo));
  }

  return GetSerializedSize ();
}

void
RerrHeader::Print (std::ostream& os) const
{
  os << "RERR destinations: " << (uint32_t)GetDestCount ();
}

void
RerrHeader::SetNoDelete (bool f)
{
  if (f)
    m_flag |= (1 << 0);
  else
    m_flag &= ~(1 << 0);
}

bool
RerrHeader::GetNoDelete () const
{
  return (m_flag & (1 << 0));
}

bool
RerrHeader::AddUnDestination (Ipv4Address dst, uint32_t seqNo)
{
  if (GetDestCount () == 255)
    return false;

  m_unreachableDstSeqNo.insert (std::make_pair (dst, seqNo));
  return true;
}

bool
RerrHeader::RemoveUnDestination (std::pair<Ipv4Address, uint32_t>& un)
{
  if (m_unreachableDstSeqNo.empty ())
    return false;

  un = *m_unreachableDstSeqNo.begin ();
  m_unreachableDstSeqNo.erase (m_unreachableDstSeqNo.begin ());
  return true;
}

void
RerrHeader::Clear ()
{
  m_unreachableDstSeqNo.clear ();
  m_flag = 0;
}

bool
RerrHeader::operator== (RerrHeader const& o) const
{
  if (m_flag != o.m_flag || GetDestCount () != o.GetDestCount ())
    return false;

  return m_unreachableDstSeqNo == o.m_unreachableDstSeqNo;
}

std::ostream&
operator<< (std::ostream& os, RerrHeader const& h)
{
  h.Print (os);
  return os;
}

} // namespace qSmartHybrid
} // namespace ns3
