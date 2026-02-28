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
#ifndef HARPPACKET_H
#define HARPPACKET_H

#include <iostream>
#include "ns3/header.h"
#include "ns3/enum.h"
#include "ns3/ipv4-address.h"
#include <map>
#include "ns3/nstime.h"

namespace ns3
{
  namespace harp
  {

    /**
     * \ingroup harp
     * \brief MessageType enumeration
     */
    enum MessageType
    {
      HARPTYPE_RREQ = 1,     //!< HARPTYPE_RREQ
      HARPTYPE_RREP = 2,     //!< HARPTYPE_RREP
      HARPTYPE_RERR = 3,     //!< HARPTYPE_RERR
      HARPTYPE_RREP_ACK = 4, //!< HARPTYPE_RREP_ACK
      HARPTYPE_HELLO = 5     //!< HARPTYPE_HELLO
    };

    /**
     * \ingroup harp
     * \brief HARP types
     */
    class TypeHeader : public Header
    {
    public:
      /**
       * constructor
       * \param t the HARP RREQ type
       */
      TypeHeader(MessageType t = HARPTYPE_RREQ);

      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      /**
       * \returns the type
       */
      MessageType Get() const
      {
        return m_type;
      }
      /**
       * Check that type if valid
       * \returns true if the type is valid
       */
      bool IsValid() const
      {
        return m_valid;
      }
      /**
       * \brief Comparison operator
       * \param o header to compare
       * \return true if the headers are equal
       */
      bool operator==(TypeHeader const &o) const;

    private:
      MessageType m_type; ///< type of the message
      bool m_valid;       ///< Indicates if the message is valid
    };

    /**
     * \brief Stream output operator
     * \param os output stream
     * \return updated stream
     */
    std::ostream &operator<<(std::ostream &os, TypeHeader const &h);

    /**
    * \ingroup harp
    * \brief   Route Request (RREQ) Message Format
      \verbatim
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |     Type      |J|R|G|D|U|   Reserved          |      0        |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                            RREQ ID                            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                    Destination IP Address                     |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                  Destination Sequence Number                  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                    Originator IP Address                      |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                  Originator Sequence Number                   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                         Timestamp                            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           Metric                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      \endverbatim
    */
    class RreqHeader : public Header
    {
    public:
      /**
       * constructor
       *
       * \param flags the message flags (0)
       * \param reserved the reserved bits (0)
       * \param requestID the request ID
       * \param dst the destination IP address
       * \param dstSeqNo the destination sequence number
       * \param origin the origin IP address
       * \param originSeqNo the origin sequence number
       * \param timestamp the timestamp
       * \param metric the metric
       */
      RreqHeader(uint8_t flags = 0, uint8_t reserved = 0,
                 uint32_t requestID = 0, Ipv4Address dst = Ipv4Address(),
                 uint32_t dstSeqNo = 0, Ipv4Address origin = Ipv4Address(),
                 uint32_t originSeqNo = 0, uint32_t timestamp = 0, double metric = 0.0);

      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      // Fields
      /**
       * \brief Set the request ID
       * \param id the request ID
       */
      void SetId(uint32_t id)
      {
        m_requestID = id;
      }
      /**
       * \brief Get the request ID
       * \return the request ID
       */
      uint32_t GetId() const
      {
        return m_requestID;
      }
      /**
       * \brief Set the destination address
       * \param a the destination address
       */
      void SetDst(Ipv4Address a)
      {
        m_dst = a;
      }
      /**
       * \brief Get the destination address
       * \return the destination address
       */
      Ipv4Address GetDst() const
      {
        return m_dst;
      }
      /**
       * \brief Set the destination sequence number
       * \param s the destination sequence number
       */
      void SetDstSeqno(uint32_t s)
      {
        m_dstSeqNo = s;
      }
      /**
       * \brief Get the destination sequence number
       * \return the destination sequence number
       */
      uint32_t GetDstSeqno() const
      {
        return m_dstSeqNo;
      }
      /**
       * \brief Set the origin address
       * \param a the origin address
       */
      void SetOrigin(Ipv4Address a)
      {
        m_origin = a;
      }
      /**
       * \brief Get the origin address
       * \return the origin address
       */
      Ipv4Address GetOrigin() const
      {
        return m_origin;
      }
      /**
       * \brief Set the origin sequence number
       * \param s the origin sequence number
       */
      void SetOriginSeqno(uint32_t s)
      {
        m_originSeqNo = s;
      }
      /**
       * \brief Get the origin sequence number
       * \return the origin sequence number
       */
      uint32_t GetOriginSeqno() const
      {
        return m_originSeqNo;
      }
      /**
       * \brief Set the timestamp
       * \param timestamp the timestamp
       */
      void SetTimestamp(uint32_t timestamp)
      {
        m_timestamp = timestamp;
      }
      /**
       * \brief Get the timestamp
       * \return the timestamp
       */
      uint32_t GetTimestamp() const
      {
        return m_timestamp;
      }
      /**
       * \brief Set the metric
       * \param metric the metric
       */
      void SetMetric(double metric)
      {
        m_metric = metric;
      }
      /**
       * \brief Get the metric
       * \return the metric
       */
      double GetMetric() const
      {
        return m_metric;
      }

      // Flags
      /**
       * \brief Set the gratuitous RREP flag
       * \param f the gratuitous RREP flag
       */
      void SetGratuitousRrep(bool f);
      /**
       * \brief Get the gratuitous RREP flag
       * \return the gratuitous RREP flag
       */
      bool GetGratuitousRrep() const;
      /**
       * \brief Set the Destination only flag
       * \param f the Destination only flag
       */
      void SetDestinationOnly(bool f);
      /**
       * \brief Get the Destination only flag
       * \return the Destination only flag
       */
      bool GetDestinationOnly() const;
      /**
       * \brief Set the unknown sequence number flag
       * \param f the unknown sequence number flag
       */
      void SetUnknownSeqno(bool f);
      /**
       * \brief Get the unknown sequence number flag
       * \return the unknown sequence number flag
       */
      bool GetUnknownSeqno() const;

      /**
       * \brief Comparison operator
       * \param o RREQ header to compare
       * \return true if the RREQ headers are equal
       */
      bool operator==(RreqHeader const &o) const;

    private:
      uint8_t m_flags;        ///< |J|R|G|D|U| bit flags, see RFC
      uint8_t m_reserved;     ///< Not used (must be 0)
      uint32_t m_requestID;   ///< RREQ ID
      Ipv4Address m_dst;      ///< Destination IP Address
      uint32_t m_dstSeqNo;    ///< Destination Sequence Number
      Ipv4Address m_origin;   ///< Originator IP Address
      uint32_t m_originSeqNo; ///< Source Sequence Number
      uint32_t m_timestamp;   ///< Timestamp for RTT calculation
      double m_metric;        ///< Metric (replaces hop count)
    };

    /**
     * \brief Stream output operator
     * \param os output stream
     * \return updated stream
     */
    std::ostream &operator<<(std::ostream &os, RreqHeader const &);

    /**
    * \ingroup harp
    * \brief Route Reply (RREP) Message Format
      \verbatim
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |     Type      |R|A|    Reserved     |Prefix Sz|      0       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                     Destination IP address                    |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                  Destination Sequence Number                  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                    Originator IP address                      |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           Lifetime                            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                         Timestamp                            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           Metric                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      \endverbatim
    */
    class RrepHeader : public Header
    {
    public:
      /**
       * constructor
       *
       * \param prefixSize the prefix size (0)
       * \param dst the destination IP address
       * \param dstSeqNo the destination sequence number
       * \param origin the origin IP address
       * \param lifetime the lifetime
       * \param timestamp the timestamp
       * \param metric the metric
       */
      RrepHeader(uint8_t prefixSize = 0, Ipv4Address dst = Ipv4Address(), uint32_t dstSeqNo = 0, Ipv4Address origin = Ipv4Address(), Time lifetime = MilliSeconds(0), uint32_t timestamp = 0, double metric = 0.0);
      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      // Fields
      /**
       * \brief Set the destination address
       * \param a the destination address
       */
      void SetDst(Ipv4Address a)
      {
        m_dst = a;
      }
      /**
       * \brief Get the destination address
       * \return the destination address
       */
      Ipv4Address GetDst() const
      {
        return m_dst;
      }
      /**
       * \brief Set the destination sequence number
       * \param s the destination sequence number
       */
      void SetDstSeqno(uint32_t s)
      {
        m_dstSeqNo = s;
      }
      /**
       * \brief Get the destination sequence number
       * \return the destination sequence number
       */
      uint32_t GetDstSeqno() const
      {
        return m_dstSeqNo;
      }
      /**
       * \brief Set the origin address
       * \param a the origin address
       */
      void SetOrigin(Ipv4Address a)
      {
        m_origin = a;
      }
      /**
       * \brief Get the origin address
       * \return the origin address
       */
      Ipv4Address GetOrigin() const
      {
        return m_origin;
      }
      /**
       * \brief Set the lifetime
       * \param t the lifetime
       */
      void SetLifeTime(Time t);
      /**
       * \brief Get the lifetime
       * \return the lifetime
       */
      Time GetLifeTime() const;
      /**
       * \brief Set the timestamp
       * \param timestamp the timestamp
       */
      void SetTimestamp(uint32_t timestamp)
      {
        m_timestamp = timestamp;
      }
      /**
       * \brief Get the timestamp
       * \return the timestamp
       */
      uint32_t GetTimestamp() const
      {
        return m_timestamp;
      }
      /**
       * \brief Set the metric
       * \param metric the metric
       */
      void SetMetric(double metric)
      {
        m_metric = metric;
      }
      /**
       * \brief Get the metric
       * \return the metric
       */
      double GetMetric() const
      {
        return m_metric;
      }

      // Flags
      /**
       * \brief Set the ack required flag
       * \param f the ack required flag
       */
      void SetAckRequired(bool f);
      /**
       * \brief get the ack required flag
       * \return the ack required flag
       */
      bool GetAckRequired() const;
      /**
       * \brief Set the prefix size
       * \param sz the prefix size
       */
      void SetPrefixSize(uint8_t sz);
      /**
       * \brief Set the pefix size
       * \return the prefix size
       */
      uint8_t GetPrefixSize() const;

      /**
       * Configure RREP to be a Hello message
       *
       * \param src the source IP address
       * \param srcSeqNo the source sequence number
       * \param lifetime the lifetime of the message
       */
      void SetHello(Ipv4Address src, uint32_t srcSeqNo, Time lifetime);

      /**
       * \brief Comparison operator
       * \param o RREP header to compare
       * \return true if the RREP headers are equal
       */
      bool operator==(RrepHeader const &o) const;

    private:
      uint8_t m_flags;      ///< A - acknowledgment required flag
      uint8_t m_prefixSize; ///< Prefix Size
      Ipv4Address m_dst;    ///< Destination IP Address
      uint32_t m_dstSeqNo;  ///< Destination Sequence Number
      Ipv4Address m_origin; ///< Source IP Address
      uint32_t m_lifeTime;  ///< Lifetime (in milliseconds)
      uint32_t m_timestamp; ///< Timestamp for RTT calculation
      double m_metric;      ///< Metric (replaces hop count)
    };

    /**
     * \brief Stream output operator
     * \param os output stream
     * \return updated stream
     */
    std::ostream &operator<<(std::ostream &os, RrepHeader const &);

    /**
    * \ingroup harp
    * \brief Route Reply Acknowledgment (RREP-ACK) Message Format
      \verbatim
      0                   1
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |     Type      |   Reserved    |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      \endverbatim
    */
    class RrepAckHeader : public Header
    {
    public:
      /// constructor
      RrepAckHeader();

      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      /**
       * \brief Comparison operator
       * \param o RREP header to compare
       * \return true if the RREQ headers are equal
       */
      bool operator==(RrepAckHeader const &o) const;

    private:
      uint8_t m_reserved; ///< Not used (must be 0)
    };

    /**
     * \brief Stream output operator
     * \param os output stream
     * \return updated stream
     */
    std::ostream &operator<<(std::ostream &os, RrepAckHeader const &);

    /**
    * \ingroup harp
    * \brief Route Error (RERR) Message Format
      \verbatim
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |     Type      |N|          Reserved           |   DestCount   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |            Unreachable Destination IP Address (1)             |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |         Unreachable Destination Sequence Number (1)           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
      |  Additional Unreachable Destination IP Addresses (if needed)  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |Additional Unreachable Destination Sequence Numbers (if needed)|
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      \endverbatim
    */
    class RerrHeader : public Header
    {
    public:
      /// constructor
      RerrHeader();

      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator i) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      // No delete flag
      /**
       * \brief Set the no delete flag
       * \param f the no delete flag
       */
      void SetNoDelete(bool f);
      /**
       * \brief Get the no delete flag
       * \return the no delete flag
       */
      bool GetNoDelete() const;

      /**
       * \brief Add unreachable node address and its sequence number in RERR header
       * \param dst unreachable IPv4 address
       * \param seqNo unreachable sequence number
       * \return false if we already added maximum possible number of unreachable destinations
       */
      bool AddUnDestination(Ipv4Address dst, uint32_t seqNo);
      /**
       * \brief Delete pair (address + sequence number) from REER header, if the number of unreachable destinations > 0
       * \param un unreachable pair (address + sequence number)
       * \return true on success
       */
      bool RemoveUnDestination(std::pair<Ipv4Address, uint32_t> &un);
      /// Clear header
      void Clear();
      /**
       * \returns number of unreachable destinations in RERR message
       */
      uint8_t GetDestCount() const
      {
        return (uint8_t)m_unreachableDstSeqNo.size();
      }

      /**
       * \brief Comparison operator
       * \param o RERR header to compare
       * \return true if the RERR headers are equal
       */
      bool operator==(RerrHeader const &o) const;

    private:
      uint8_t m_flag;     ///< No delete flag
      uint8_t m_reserved; ///< Not used (must be 0)

      /// List of Unreachable destination: IP addresses and sequence numbers
      std::map<Ipv4Address, uint32_t> m_unreachableDstSeqNo;
    };

    /**
     * \brief Stream output operator
     * \param os output stream
     * \return updated stream
     */
    std::ostream &operator<<(std::ostream &os, RerrHeader const &);

    /**
    * \ingroup harp
    * \brief Hello Message Format
      \verbatim
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                    Node ID (IP Address)                     |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                   Sequence Number                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                   Neighbor Count                             |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                   Neighbor IP Address (1)                   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                   Neighbor IP Address (2)                   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      :                               ...                               :
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      \endverbatim
    */
    class HelloHeader : public Header
    {
    public:
      /**
       * constructor
       */
      HelloHeader();

      /**
       * \brief Get the type ID.
       * \return the object TypeId
       */
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      /**
       * \brief Set the node ID (IP address)
       * \param nodeID the node ID
       */
      void SetNodeId(Ipv4Address nodeID)
      {
        m_nodeID = nodeID;
      }
      /**
       * \brief Get the node ID
       * \return the node ID
       */
      Ipv4Address GetNodeId() const
      {
        return m_nodeID;
      }
      /**
       * \brief Set the sequence number
       * \param seqNo the sequence number
       */
      void SetSeqNo(uint32_t seqNo)
      {
        m_seqNo = seqNo;
      }
      /**
       * \brief Get the sequence number
       * \return the sequence number
       */
      uint32_t GetSeqNo() const
      {
        return m_seqNo;
      }
      /**
       * \brief Add a neighbor to the list
       * \param neighbor the neighbor IP address
       */
      void AddNeighbor(Ipv4Address neighbor)
      {
        m_neighbors.push_back(neighbor);
      }
      /**
       * \brief Get the neighbors list
       * \return the neighbors list
       */
      const std::vector<Ipv4Address> &GetNeighbors() const
      {
        return m_neighbors;
      }
      /**
       * \brief Clear the neighbors list
       */
      void ClearNeighbors()
      {
        m_neighbors.clear();
      }

      /**
       * \brief Comparison operator
       * \param o Hello header to compare
       * \return true if the Hello headers are equal
       */
      bool operator==(HelloHeader const &o) const;

    private:
      Ipv4Address m_nodeID;                 ///< Node ID
      uint32_t m_seqNo;                     ///< Sequence Number
      std::vector<Ipv4Address> m_neighbors; ///< List of neighbors
    };

    /**
     * \brief Stream output operator
     * \param os output stream
     * \return updated stream
     */
    std::ostream &operator<<(std::ostream &os, HelloHeader const &);

  } // namespace harp
} // namespace ns3

#endif /* HARPPACKET_H */
