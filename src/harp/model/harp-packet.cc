/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
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
 * Based on
 *      NS-2 HARP model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      HARP-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/HARP-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
#include "harp-packet.h"
#include "ns3/address-utils.h"
#include "ns3/packet.h"

namespace ns3
{
  namespace harp
  {

    NS_OBJECT_ENSURE_REGISTERED(TypeHeader);

    TypeHeader::TypeHeader(MessageType t)
        : m_type(t),
          m_valid(true)
    {
    }

    TypeId
    TypeHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::harp::TypeHeader")
                              .SetParent<Header>()
                              .SetGroupName("Harp")
                              .AddConstructor<TypeHeader>();
      return tid;
    }

    TypeId
    TypeHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    TypeHeader::GetSerializedSize() const
    {
      return 1;
    }

    void
    TypeHeader::Serialize(Buffer::Iterator i) const
    {
      i.WriteU8((uint8_t)m_type);
    }

    uint32_t
    TypeHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;
      uint8_t type = i.ReadU8();
      m_valid = true;
      switch (type)
      {
      case HARPTYPE_RREQ:
      case HARPTYPE_RREP:
      case HARPTYPE_RERR:
      case HARPTYPE_RREP_ACK:
      case HARPTYPE_HELLO:
      {
        m_type = (MessageType)type;
        break;
      }
      default:
        m_valid = false;
      }
      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT(dist == GetSerializedSize());
      return dist;
    }

    void
    TypeHeader::Print(std::ostream &os) const
    {
      switch (m_type)
      {
      case HARPTYPE_RREQ:
      {
        os << "RREQ";
        break;
      }
      case HARPTYPE_RREP:
      {
        os << "RREP";
        break;
      }
      case HARPTYPE_RERR:
      {
        os << "RERR";
        break;
      }
      case HARPTYPE_RREP_ACK:
      {
        os << "RREP_ACK";
        break;
      }
      case HARPTYPE_HELLO:
      {
        os << "HELLO";
        break;
      }
      default:
        os << "UNKNOWN_TYPE";
      }
    }

    bool
    TypeHeader::operator==(TypeHeader const &o) const
    {
      return (m_type == o.m_type && m_valid == o.m_valid);
    }

    std::ostream &
    operator<<(std::ostream &os, TypeHeader const &h)
    {
      h.Print(os);
      return os;
    }

    //-----------------------------------------------------------------------------
    // RREQ
    //-----------------------------------------------------------------------------
    // RreqHeader::RreqHeader(uint8_t flags, uint8_t reserved, uint32_t requestID, Ipv4Address dst,
    //                        uint32_t dstSeqNo, Ipv4Address origin, uint32_t originSeqNo,
    //                        uint32_t timestamp, double metric)
    //     : m_flags(flags),
    //       m_reserved(reserved),
    //       m_requestID(requestID),
    //       m_dst(dst),
    //       m_dstSeqNo(dstSeqNo),
    //       m_origin(origin),
    //       m_originSeqNo(originSeqNo),
    //       m_timestamp(timestamp),
    //       m_metric(metric)
    // {
    // }

    // NS_OBJECT_ENSURE_REGISTERED(RreqHeader);

    // TypeId
    // RreqHeader::GetTypeId()
    // {
    //   static TypeId tid = TypeId("ns3::harp::RreqHeader")
    //                           .SetParent<Header>()
    //                           .SetGroupName("Harp")
    //                           .AddConstructor<RreqHeader>();
    //   return tid;
    // }

    // TypeId
    // RreqHeader::GetInstanceTypeId() const
    // {
    //   return GetTypeId();
    // }

    // uint32_t
    // RreqHeader::GetSerializedSize() const
    // {
    //   return 34;
    // }

    // void
    // RreqHeader::Serialize(Buffer::Iterator start) const
    // {
    //   start.WriteU8(m_flags);
    //   start.WriteU8(m_reserved);
    //   start.WriteHtonU32(m_requestID);
    //   WriteTo(start, m_dst); // ns-3 推荐的地址写入方式
    //   start.WriteHtonU32(m_dstSeqNo);
    //   WriteTo(start, m_origin);
    //   start.WriteHtonU32(m_originSeqNo);
    //   start.WriteHtonU32(m_timestamp);

    //   // double 类型的处理：将其内存直接拷贝到缓冲区
    //   // 这样可以规避网络序转换对浮点数带来的困扰
    //   start.Write((const uint8_t *)&m_metric, 8);
    // }

    // uint32_t
    // RreqHeader::Deserialize(Buffer::Iterator start)
    // {
    //   Buffer::Iterator i = start;

    //   m_flags = i.ReadU8();
    //   m_reserved = i.ReadU8();
    //   m_requestID = i.ReadNtohU32();
    //   ReadFrom(i, m_dst); // ns-3 推荐的地址读取方式
    //   m_dstSeqNo = i.ReadNtohU32();
    //   ReadFrom(i, m_origin);
    //   m_originSeqNo = i.ReadNtohU32();
    //   m_timestamp = i.ReadNtohU32();

    //   // 必须读取 8 字节还原 double
    //   i.Read((uint8_t *)&m_metric, 8);

    //   return i.GetDistanceFrom(start); // 必须返回 34
    // }

    // void
    // RreqHeader::Print(std::ostream &os) const
    // {
    //   os << "RREQ ID " << m_requestID << " destination: ipv4 " << m_dst
    //      << " sequence number " << m_dstSeqNo << " source: ipv4 "
    //      << m_origin << " sequence number " << m_originSeqNo
    //      << " flags:" << " Gratuitous RREP " << (*this).GetGratuitousRrep()
    //      << " Destination only " << (*this).GetDestinationOnly()
    //      << " Unknown sequence number " << (*this).GetUnknownSeqno();
    // }

    // std::ostream &
    // operator<<(std::ostream &os, RreqHeader const &h)
    // {
    //   h.Print(os);
    //   return os;
    // }

    // void
    // RreqHeader::SetGratuitousRrep(bool f)
    // {
    //   if (f)
    //   {
    //     m_flags |= (1 << 5);
    //   }
    //   else
    //   {
    //     m_flags &= ~(1 << 5);
    //   }
    // }

    // bool
    // RreqHeader::GetGratuitousRrep() const
    // {
    //   return (m_flags & (1 << 5));
    // }

    // void
    // RreqHeader::SetDestinationOnly(bool f)
    // {
    //   if (f)
    //   {
    //     m_flags |= (1 << 4);
    //   }
    //   else
    //   {
    //     m_flags &= ~(1 << 4);
    //   }
    // }

    // bool
    // RreqHeader::GetDestinationOnly() const
    // {
    //   return (m_flags & (1 << 4));
    // }

    // void
    // RreqHeader::SetUnknownSeqno(bool f)
    // {
    //   if (f)
    //   {
    //     m_flags |= (1 << 3);
    //   }
    //   else
    //   {
    //     m_flags &= ~(1 << 3);
    //   }
    // }

    // bool
    // RreqHeader::GetUnknownSeqno() const
    // {
    //   return (m_flags & (1 << 3));
    // }

    // bool
    // RreqHeader::operator==(RreqHeader const &o) const
    // {
    //   return (m_flags == o.m_flags && m_reserved == o.m_reserved && m_requestID == o.m_requestID && m_dst == o.m_dst && m_dstSeqNo == o.m_dstSeqNo && m_origin == o.m_origin && m_originSeqNo == o.m_originSeqNo && m_timestamp == o.m_timestamp && m_metric == o.m_metric);
    // }

    //////////////////================

    RreqHeader::RreqHeader(uint8_t flags, uint8_t reserved, uint32_t requestID,
                           Ipv4Address dst, uint32_t dstSeqNo, Ipv4Address origin,
                           uint32_t originSeqNo, uint32_t timestamp, double metric)
        : m_flags(flags),
          m_reserved(reserved),
          m_requestID(requestID),
          m_dst(dst),
          m_dstSeqNo(dstSeqNo),
          m_origin(origin),
          m_originSeqNo(originSeqNo),
          m_timestamp(timestamp),
          m_metric(metric)
    {
    }

    TypeId
    RreqHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::RreqHeader")
                              .SetParent<Header>()
                              .AddConstructor<RreqHeader>();
      return tid;
    }

    TypeId
    RreqHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    RreqHeader::GetSerializedSize() const
    {
      // 严格计算：1+1+4+4+4+4+4+4+8 = 34 字节
      return 1 + 1 + 4 + 4 + 4 + 4 + 4 + 4 + 8;
    }

    void
    RreqHeader::Serialize(Buffer::Iterator start) const
    {
      start.WriteU8(m_flags);
      start.WriteU8(m_reserved);
      start.WriteHtonU32(m_requestID);
      WriteTo(start, m_dst); // ns-3 推荐的地址写入方式
      start.WriteHtonU32(m_dstSeqNo);
      WriteTo(start, m_origin);
      start.WriteHtonU32(m_originSeqNo);
      start.WriteHtonU32(m_timestamp);

      // double 类型的处理：将其内存直接拷贝到缓冲区
      // 这样可以规避网络序转换对浮点数带来的困扰
      start.Write((const uint8_t *)&m_metric, 8);
    }

    uint32_t
    RreqHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;

      m_flags = i.ReadU8();
      m_reserved = i.ReadU8();
      m_requestID = i.ReadNtohU32();
      ReadFrom(i, m_dst); // ns-3 推荐的地址读取方式
      m_dstSeqNo = i.ReadNtohU32();
      ReadFrom(i, m_origin);
      m_originSeqNo = i.ReadNtohU32();
      m_timestamp = i.ReadNtohU32();

      // 必须读取 8 字节还原 double
      i.Read((uint8_t *)&m_metric, 8);

      return i.GetDistanceFrom(start); // 必须返回 34
    }

    void
    RreqHeader::Print(std::ostream &os) const
    {
      os << "ID: " << m_requestID
         << " Dst: " << m_dst << " (Seq: " << m_dstSeqNo << ")"
         << " Origin: " << m_origin << " (Seq: " << m_originSeqNo << ")"
         << " Metric: " << m_metric;
    }

    // --- Flags 标志位操作 (位运算实现) ---

    void
    RreqHeader::SetGratuitousRrep(bool f)
    {
      if (f)
        m_flags |= 0x04; // 假设第 3 位是 G 标志
      else
        m_flags &= ~0x04;
    }

    bool
    RreqHeader::GetGratuitousRrep() const
    {
      return (m_flags & 0x04);
    }

    void
    RreqHeader::SetDestinationOnly(bool f)
    {
      if (f)
        m_flags |= 0x02; // 假设第 2 位是 D 标志
      else
        m_flags &= ~0x02;
    }

    bool
    RreqHeader::GetDestinationOnly() const
    {
      return (m_flags & 0x02);
    }

    void
    RreqHeader::SetUnknownSeqno(bool f)
    {
      if (f)
        m_flags |= 0x01; // 假设第 1 位是 U 标志
      else
        m_flags &= ~0x01;
    }

    bool
    RreqHeader::GetUnknownSeqno() const
    {
      return (m_flags & 0x01);
    }

    bool
    RreqHeader::operator==(RreqHeader const &o) const
    {
      return (m_flags == o.m_flags &&
              m_reserved == o.m_reserved &&
              m_requestID == o.m_requestID &&
              m_dst == o.m_dst &&
              m_dstSeqNo == o.m_dstSeqNo &&
              m_origin == o.m_origin &&
              m_originSeqNo == o.m_originSeqNo &&
              m_timestamp == o.m_timestamp &&
              m_metric == o.m_metric);
    }

    //-----------------------------------------------------------------------------
    // RREP
    //-----------------------------------------------------------------------------

    RrepHeader::RrepHeader(uint8_t prefixSize, Ipv4Address dst,
                           uint32_t dstSeqNo, Ipv4Address origin, Time lifeTime,
                           uint32_t timestamp, double metric)
        : m_flags(0),
          m_prefixSize(prefixSize),
          m_dst(dst),
          m_dstSeqNo(dstSeqNo),
          m_origin(origin),
          m_timestamp(timestamp),
          m_metric(metric)
    {
      m_lifeTime = uint32_t(lifeTime.GetMilliSeconds());
    }

    NS_OBJECT_ENSURE_REGISTERED(RrepHeader);

    TypeId
    RrepHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::harp::RrepHeader")
                              .SetParent<Header>()
                              .SetGroupName("Harp")
                              .AddConstructor<RrepHeader>();
      return tid;
    }

    TypeId
    RrepHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    RrepHeader::GetSerializedSize() const
    {
      return 31;
    }

    void
    RrepHeader::Serialize(Buffer::Iterator i) const
    {
      i.WriteU8(m_flags);
      i.WriteU8(m_prefixSize);
      i.WriteU8(0);
      WriteTo(i, m_dst);
      i.WriteHtonU32(m_dstSeqNo);
      WriteTo(i, m_origin);
      i.WriteHtonU32(m_lifeTime);
      i.WriteHtonU32(m_timestamp);
      uint64_t metricInt;
      std::memcpy(&metricInt, &m_metric, sizeof(double));
      i.WriteHtonU64(metricInt);
    }

    uint32_t
    RrepHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;

      m_flags = i.ReadU8();
      m_prefixSize = i.ReadU8();
      i.ReadU8();
      ReadFrom(i, m_dst);
      m_dstSeqNo = i.ReadNtohU32();
      ReadFrom(i, m_origin);
      m_lifeTime = i.ReadNtohU32();
      m_timestamp = i.ReadNtohU32();
      uint64_t metricInt = i.ReadNtohU64();
      std::memcpy(&m_metric, &metricInt, sizeof(double));

      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT(dist == GetSerializedSize());
      return dist;
    }

    void
    RrepHeader::Print(std::ostream &os) const
    {
      os << "destination: ipv4 " << m_dst << " sequence number " << m_dstSeqNo;
      if (m_prefixSize != 0)
      {
        os << " prefix size " << m_prefixSize;
      }
      os << " source ipv4 " << m_origin << " lifetime " << m_lifeTime
         << " acknowledgment required flag " << (*this).GetAckRequired();
    }

    void
    RrepHeader::SetLifeTime(Time t)
    {
      m_lifeTime = t.GetMilliSeconds();
    }

    Time
    RrepHeader::GetLifeTime() const
    {
      Time t(MilliSeconds(m_lifeTime));
      return t;
    }

    void
    RrepHeader::SetAckRequired(bool f)
    {
      if (f)
      {
        m_flags |= (1 << 6);
      }
      else
      {
        m_flags &= ~(1 << 6);
      }
    }

    bool
    RrepHeader::GetAckRequired() const
    {
      return (m_flags & (1 << 6));
    }

    void
    RrepHeader::SetPrefixSize(uint8_t sz)
    {
      m_prefixSize = sz;
    }

    uint8_t
    RrepHeader::GetPrefixSize() const
    {
      return m_prefixSize;
    }

    bool
    RrepHeader::operator==(RrepHeader const &o) const
    {
      return (m_flags == o.m_flags && m_prefixSize == o.m_prefixSize && m_dst == o.m_dst && m_dstSeqNo == o.m_dstSeqNo && m_origin == o.m_origin && m_lifeTime == o.m_lifeTime && m_timestamp == o.m_timestamp && m_metric == o.m_metric);
    }

    void
    RrepHeader::SetHello(Ipv4Address origin, uint32_t srcSeqNo, Time lifetime)
    {
      m_flags = 0;
      m_prefixSize = 0;
      m_dst = origin;
      m_dstSeqNo = srcSeqNo;
      m_origin = origin;
      m_lifeTime = lifetime.GetMilliSeconds();
    }

    std::ostream &
    operator<<(std::ostream &os, RrepHeader const &h)
    {
      h.Print(os);
      return os;
    }

    //-----------------------------------------------------------------------------
    // RREP-ACK
    //-----------------------------------------------------------------------------

    RrepAckHeader::RrepAckHeader()
        : m_reserved(0)
    {
    }

    NS_OBJECT_ENSURE_REGISTERED(RrepAckHeader);

    TypeId
    RrepAckHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::harp::RrepAckHeader")
                              .SetParent<Header>()
                              .SetGroupName("Harp")
                              .AddConstructor<RrepAckHeader>();
      return tid;
    }

    TypeId
    RrepAckHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    RrepAckHeader::GetSerializedSize() const
    {
      return 1;
    }

    void
    RrepAckHeader::Serialize(Buffer::Iterator i) const
    {
      i.WriteU8(m_reserved);
    }

    uint32_t
    RrepAckHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;
      m_reserved = i.ReadU8();
      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT(dist == GetSerializedSize());
      return dist;
    }

    void
    RrepAckHeader::Print(std::ostream &os) const
    {
    }

    bool
    RrepAckHeader::operator==(RrepAckHeader const &o) const
    {
      return m_reserved == o.m_reserved;
    }

    std::ostream &
    operator<<(std::ostream &os, RrepAckHeader const &h)
    {
      h.Print(os);
      return os;
    }

    //-----------------------------------------------------------------------------
    // RERR
    //-----------------------------------------------------------------------------
    RerrHeader::RerrHeader()
        : m_flag(0),
          m_reserved(0)
    {
    }

    NS_OBJECT_ENSURE_REGISTERED(RerrHeader);

    TypeId
    RerrHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::harp::RerrHeader")
                              .SetParent<Header>()
                              .SetGroupName("Harp")
                              .AddConstructor<RerrHeader>();
      return tid;
    }

    TypeId
    RerrHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    RerrHeader::GetSerializedSize() const
    {
      return (3 + 8 * GetDestCount());
    }

    void
    RerrHeader::Serialize(Buffer::Iterator i) const
    {
      i.WriteU8(m_flag);
      i.WriteU8(m_reserved);
      i.WriteU8(GetDestCount());
      std::map<Ipv4Address, uint32_t>::const_iterator j;
      for (j = m_unreachableDstSeqNo.begin(); j != m_unreachableDstSeqNo.end(); ++j)
      {
        WriteTo(i, (*j).first);
        i.WriteHtonU32((*j).second);
      }
    }

    uint32_t
    RerrHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;
      m_flag = i.ReadU8();
      m_reserved = i.ReadU8();
      uint8_t dest = i.ReadU8();
      m_unreachableDstSeqNo.clear();
      Ipv4Address address;
      uint32_t seqNo;
      for (uint8_t k = 0; k < dest; ++k)
      {
        ReadFrom(i, address);
        seqNo = i.ReadNtohU32();
        m_unreachableDstSeqNo.insert(std::make_pair(address, seqNo));
      }

      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT(dist == GetSerializedSize());
      return dist;
    }

    void
    RerrHeader::Print(std::ostream &os) const
    {
      os << "Unreachable destination (ipv4 address, seq. number):";
      std::map<Ipv4Address, uint32_t>::const_iterator j;
      for (j = m_unreachableDstSeqNo.begin(); j != m_unreachableDstSeqNo.end(); ++j)
      {
        os << (*j).first << ", " << (*j).second;
      }
      os << "No delete flag " << (*this).GetNoDelete();
    }

    void
    RerrHeader::SetNoDelete(bool f)
    {
      if (f)
      {
        m_flag |= (1 << 0);
      }
      else
      {
        m_flag &= ~(1 << 0);
      }
    }

    bool
    RerrHeader::GetNoDelete() const
    {
      return (m_flag & (1 << 0));
    }

    bool
    RerrHeader::AddUnDestination(Ipv4Address dst, uint32_t seqNo)
    {
      if (m_unreachableDstSeqNo.find(dst) != m_unreachableDstSeqNo.end())
      {
        return true;
      }

      NS_ASSERT(GetDestCount() < 255); // can't support more than 255 destinations in single RERR
      m_unreachableDstSeqNo.insert(std::make_pair(dst, seqNo));
      return true;
    }

    bool
    RerrHeader::RemoveUnDestination(std::pair<Ipv4Address, uint32_t> &un)
    {
      if (m_unreachableDstSeqNo.empty())
      {
        return false;
      }
      std::map<Ipv4Address, uint32_t>::iterator i = m_unreachableDstSeqNo.begin();
      un = *i;
      m_unreachableDstSeqNo.erase(i);
      return true;
    }

    void
    RerrHeader::Clear()
    {
      m_unreachableDstSeqNo.clear();
      m_flag = 0;
      m_reserved = 0;
    }

    bool
    RerrHeader::operator==(RerrHeader const &o) const
    {
      if (m_flag != o.m_flag || m_reserved != o.m_reserved || GetDestCount() != o.GetDestCount())
      {
        return false;
      }

      std::map<Ipv4Address, uint32_t>::const_iterator j = m_unreachableDstSeqNo.begin();
      std::map<Ipv4Address, uint32_t>::const_iterator k = o.m_unreachableDstSeqNo.begin();
      for (uint8_t i = 0; i < GetDestCount(); ++i)
      {
        if ((j->first != k->first) || (j->second != k->second))
        {
          return false;
        }

        j++;
        k++;
      }
      return true;
    }

    std::ostream &
    operator<<(std::ostream &os, RerrHeader const &h)
    {
      h.Print(os);
      return os;
    }

    //-----------------------------------------------------------------------------
    // Hello
    //-----------------------------------------------------------------------------

    NS_OBJECT_ENSURE_REGISTERED(HelloHeader);

    HelloHeader::HelloHeader()
        : m_nodeID(Ipv4Address()),
          m_seqNo(0)
    {
    }

    TypeId
    HelloHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::harp::HelloHeader")
                              .SetParent<Header>()
                              .SetGroupName("Harp")
                              .AddConstructor<HelloHeader>();
      return tid;
    }

    TypeId
    HelloHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    HelloHeader::GetSerializedSize() const
    {
      return 9 + (4 * m_neighbors.size());
    }

    void
    HelloHeader::Serialize(Buffer::Iterator i) const
    {
      WriteTo(i, m_nodeID);
      i.WriteHtonU32(m_seqNo);
      i.WriteHtonU32(m_neighbors.size());
      for (std::vector<Ipv4Address>::const_iterator it = m_neighbors.begin(); it != m_neighbors.end(); ++it)
      {
        WriteTo(i, *it);
      }
    }

    uint32_t
    HelloHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;
      ReadFrom(i, m_nodeID);
      m_seqNo = i.ReadNtohU32();
      uint32_t count = i.ReadNtohU32();
      m_neighbors.clear();
      for (uint32_t j = 0; j < count; ++j)
      {
        Ipv4Address addr;
        ReadFrom(i, addr);
        m_neighbors.push_back(addr);
      }

      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT(dist == GetSerializedSize());
      return dist;
    }

    void
    HelloHeader::Print(std::ostream &os) const
    {
      os << "Node ID: " << m_nodeID << " SeqNo: " << m_seqNo << " Neighbors: ";
      for (std::vector<Ipv4Address>::const_iterator it = m_neighbors.begin(); it != m_neighbors.end(); ++it)
      {
        os << *it << " ";
      }
    }

    bool
    HelloHeader::operator==(HelloHeader const &o) const
    {
      return (m_nodeID == o.m_nodeID && m_seqNo == o.m_seqNo && m_neighbors == o.m_neighbors);
    }

    std::ostream &
    operator<<(std::ostream &os, HelloHeader const &h)
    {
      h.Print(os);
      return os;
    }

  }
}
