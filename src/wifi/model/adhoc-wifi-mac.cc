/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2006, 2009 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
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
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Mirko Banchi <mk.banchi@gmail.com>
 */

#include "ns3/log.h"
#include "ns3/packet.h"
#include "adhoc-wifi-mac.h"
#include "ns3/ht-capabilities.h"
#include "ns3/vht-capabilities.h"
#include "ns3/he-capabilities.h"
#include "ns3/boolean.h"
#include "ns3/capability-information.h"
#include "ns3/mgt-headers.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/supported-rates.h"
#include "ns3/nstime.h"
#include "ns3/wifi-phy.h"
#include "ns3/uinteger.h"
#include "channel-access-manager.h"
#include "mac-tx-middle.h"
#include "wifi-mac-queue.h"

namespace ns3
{

  NS_LOG_COMPONENT_DEFINE("AdhocWifiMac");

  NS_OBJECT_ENSURE_REGISTERED(AdhocWifiMac);

  TypeId
  AdhocWifiMac::GetTypeId(void)
  {
    static TypeId tid = TypeId("ns3::AdhocWifiMac")
                            .SetParent<RegularWifiMac>()
                            .SetGroupName("Wifi")
                            .AddConstructor<AdhocWifiMac>()
                            .AddAttribute(
                                "CollaborativeBeaconGeneration",
                                "Enable distributed Ad-Hoc management beacons.",
                                BooleanValue(false),
                                MakeBooleanAccessor(
                                    &AdhocWifiMac::SetCollaborativeBeaconGeneration,
                                    &AdhocWifiMac::GetCollaborativeBeaconGeneration),
                                MakeBooleanChecker())
                            .AddAttribute(
                                "CollaborativeBeaconInterval",
                                "Nominal interval of distributed Ad-Hoc beacons.",
                                TimeValue(Seconds(1.0)),
                                MakeTimeAccessor(
                                    &AdhocWifiMac::SetCollaborativeBeaconInterval,
                                    &AdhocWifiMac::GetCollaborativeBeaconInterval),
                                MakeTimeChecker())
                            .AddAttribute(
                                "EnableCollaborativeBeaconJitter",
                                "Use an IBSS-style random backoff after every TBTT.",
                                BooleanValue(true),
                                MakeBooleanAccessor(
                                    &AdhocWifiMac::m_enableCollaborativeBeaconJitter),
                                MakeBooleanChecker())
                            .AddAttribute(
                                "CollaborativeBeaconMaxBackoffSlots",
                                "Maximum random backoff slots used for the distributed "
                                "beacon election after each TBTT.",
                                UintegerValue(15),
                                MakeUintegerAccessor(
                                    &AdhocWifiMac::m_collaborativeBeaconMaxBackoffSlots),
                                MakeUintegerChecker<uint16_t>())
                            .AddAttribute(
                                "CollaborativeBeaconSlotTime",
                                "Slot duration used by the simulated IBSS beacon election.",
                                TimeValue(MicroSeconds(9)),
                                MakeTimeAccessor(
                                    &AdhocWifiMac::m_collaborativeBeaconSlotTime),
                                MakeTimeChecker())
                            .AddAttribute("CollaborativeDomainId", "Logical domain identifier.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&AdhocWifiMac::m_collaborativeDomainId),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("CollaborativeRole", "0=peer, 1=gateway, 2=backbone.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&AdhocWifiMac::m_collaborativeRole),
                                          MakeUintegerChecker<uint8_t>())
                            .AddAttribute("CollaborativeGateway", "Gateway IPv4 value in host byte order.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&AdhocWifiMac::m_collaborativeGateway),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("CollaborativeHops", "Advertised hops to gateway.",
                                          UintegerValue(255),
                                          MakeUintegerAccessor(&AdhocWifiMac::m_collaborativeHops),
                                          MakeUintegerChecker<uint8_t>())
                            .AddAttribute("CollaborativeLoadPercent", "Advertised load, 0..100.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&AdhocWifiMac::m_collaborativeLoadPercent),
                                          MakeUintegerChecker<uint8_t>(0, 100))
                            .AddAttribute("CollaborativeEnergyPercent", "Advertised energy, 0..100.",
                                          UintegerValue(100),
                                          MakeUintegerAccessor(&AdhocWifiMac::m_collaborativeEnergyPercent),
                                          MakeUintegerChecker<uint8_t>(0, 100))
                            .AddAttribute("CollaborativeSecurityCapabilities", "Advertised security bitmap.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&AdhocWifiMac::m_collaborativeSecurityCapabilities),
                                          MakeUintegerChecker<uint8_t>());
    return tid;
  }

  AdhocWifiMac::AdhocWifiMac()
      : m_collaborativeBeaconGeneration(false),
        m_collaborativeBeaconInterval(Seconds(1.0)),
        m_enableCollaborativeBeaconJitter(true),
        m_collaborativeBeaconMaxBackoffSlots(15),
        m_collaborativeBeaconSlotTime(MicroSeconds(9)),
        m_collaborativeDomainId(0),
        m_collaborativeRole(0),
        m_collaborativeGateway(0),
        m_collaborativeHops(255),
        m_collaborativeLoadPercent(0),
        m_collaborativeEnergyPercent(100),
        m_collaborativeSecurityCapabilities(0)
  {
    NS_LOG_FUNCTION(this);
    m_collaborativeBeaconTxop = CreateObject<Txop>();
    m_collaborativeBeaconTxop->SetAifsn(1);
    m_collaborativeBeaconTxop->SetMinCw(0);
    m_collaborativeBeaconTxop->SetMaxCw(0);
    m_collaborativeBeaconTxop->SetChannelAccessManager(m_channelAccessManager);
    m_collaborativeBeaconTxop->SetTxMiddle(m_txMiddle);
    m_collaborativeControlTxop = CreateObject<Txop>();
    m_collaborativeControlTxop->SetAifsn(1);
    m_collaborativeControlTxop->SetMinCw(3);
    m_collaborativeControlTxop->SetMaxCw(7);
    m_collaborativeControlTxop->SetChannelAccessManager(m_channelAccessManager);
    m_collaborativeControlTxop->SetTxMiddle(m_txMiddle);
    // Let the lower layers know that we are acting in an IBSS
    SetTypeOfStation(ADHOC_STA);
    m_collaborativeBeaconJitter = CreateObject<UniformRandomVariable>();
  }

  AdhocWifiMac::~AdhocWifiMac()
  {
    NS_LOG_FUNCTION(this);
  }

  void
  AdhocWifiMac::DoInitialize()
  {
    m_collaborativeBeaconTxop->Initialize();
    m_collaborativeControlTxop->Initialize();
    RegularWifiMac::DoInitialize();
    if (m_collaborativeBeaconGeneration)
    {
      ScheduleNextCollaborativeBeacon();
    }
  }

  void
  AdhocWifiMac::DoDispose()
  {
    Simulator::Cancel(m_collaborativeBeaconEvent);
    m_collaborativeBeaconTxop->Dispose();
    m_collaborativeBeaconTxop = nullptr;
    m_collaborativeControlTxop->Dispose();
    m_collaborativeControlTxop = nullptr;
    m_collaborativeBeaconJitter = nullptr;
    RegularWifiMac::DoDispose();
  }

  void
  AdhocWifiMac::SetCollaborativeBeaconGeneration(bool enable)
  {
    if (enable == m_collaborativeBeaconGeneration)
    {
      return;
    }
    m_collaborativeBeaconGeneration = enable;
    if (!enable)
    {
      Simulator::Cancel(m_collaborativeBeaconEvent);
    }
    else if (IsInitialized() && !m_collaborativeBeaconEvent.IsRunning())
    {
      m_collaborativeBeaconEvent =
          Simulator::ScheduleNow(&AdhocWifiMac::SendCollaborativeBeacon, this);
    }
  }

  bool
  AdhocWifiMac::GetCollaborativeBeaconGeneration() const
  {
    return m_collaborativeBeaconGeneration;
  }

  void
  AdhocWifiMac::SetCollaborativeBeaconInterval(Time interval)
  {
    NS_ABORT_MSG_IF(interval.IsZero() || interval.IsNegative(),
                    "Collaborative beacon interval must be positive");
    m_collaborativeBeaconInterval = interval;
  }

  Time
  AdhocWifiMac::GetCollaborativeBeaconInterval() const
  {
    return m_collaborativeBeaconInterval;
  }

  void AdhocWifiMac::SetCollaborativeDomainId(uint32_t value) { m_collaborativeDomainId = value; }
  void AdhocWifiMac::SetCollaborativeRole(uint8_t value) { m_collaborativeRole = value; }
  void AdhocWifiMac::SetCollaborativeGateway(uint32_t value) { m_collaborativeGateway = value; }
  void AdhocWifiMac::SetCollaborativeHops(uint8_t value) { m_collaborativeHops = value; }
  void AdhocWifiMac::SetCollaborativeLoadPercent(uint8_t value) { m_collaborativeLoadPercent = value; }
  void AdhocWifiMac::SetCollaborativeEnergyPercent(uint8_t value) { m_collaborativeEnergyPercent = value; }
  void AdhocWifiMac::SetCollaborativeSecurityCapabilities(uint8_t value) { m_collaborativeSecurityCapabilities = value; }
  uint8_t AdhocWifiMac::GetCollaborativeRole() const { return m_collaborativeRole; }
  uint8_t AdhocWifiMac::GetCollaborativeHops() const { return m_collaborativeHops; }
  uint32_t AdhocWifiMac::GetCollaborativeGateway() const { return m_collaborativeGateway; }
  void
  AdhocWifiMac::ScheduleNextCollaborativeBeacon()
  {
    if (m_collaborativeBeaconGeneration)
    {
      const int64_t intervalNs = m_collaborativeBeaconInterval.GetNanoSeconds();
      const int64_t nowNs = Simulator::Now().GetNanoSeconds();
      const int64_t nextTbttNs = ((nowNs / intervalNs) + 1) * intervalNs;
      uint32_t slots = 0;
      if (m_enableCollaborativeBeaconJitter)
      {
        slots = m_collaborativeBeaconJitter->GetInteger(
            0, m_collaborativeBeaconMaxBackoffSlots);
      }
      Time delay = NanoSeconds(nextTbttNs - nowNs) +
                   m_collaborativeBeaconSlotTime * slots;
      m_collaborativeBeaconEvent =
          Simulator::Schedule(delay,
                              &AdhocWifiMac::SendCollaborativeBeacon, this);
    }
  }

  void
  AdhocWifiMac::SendCollaborativeBeacon()
  {
    NS_LOG_FUNCTION(this);
    if (!m_collaborativeBeaconGeneration)
    {
      return;
    }

    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_MGT_BEACON);
    hdr.SetAddr1(Mac48Address::GetBroadcast());
    hdr.SetAddr2(GetAddress());
    hdr.SetAddr3(GetBssid());
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    MgtBeaconHeader beacon;
    beacon.SetSsid(GetSsid());
    beacon.SetSupportedRates(GetCollaborativeSupportedRates());
    beacon.SetBeaconIntervalUs(m_collaborativeBeaconInterval.GetMicroSeconds());
    CapabilityInformation capabilities;
    capabilities.SetIbss();
    capabilities.SetShortSlotTime(GetShortSlotTimeSupported());
    beacon.SetCapabilities(capabilities);

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(beacon);
    m_collaborativeBeaconTxop->Queue(packet, hdr);
    ScheduleNextCollaborativeBeacon();
  }

  SupportedRates
  AdhocWifiMac::GetCollaborativeSupportedRates() const
  {
    SupportedRates rates;
    for (const auto& mode : m_phy->GetModeList())
    {
      const uint64_t dataRate = mode.GetDataRate(m_phy->GetChannelWidth());
      rates.AddSupportedRate(dataRate);
      if (mode.IsMandatory() &&
          mode.GetModulationClass() != WIFI_MOD_CLASS_HR_DSSS)
      {
        m_stationManager->AddBasicMode(mode);
      }
    }
    for (uint8_t i = 0; i < m_stationManager->GetNBasicModes(); ++i)
    {
      WifiMode mode = m_stationManager->GetBasicMode(i);
      rates.SetBasicRate(mode.GetDataRate(m_phy->GetChannelWidth()));
    }
    return rates;
  }

  void
  AdhocWifiMac::SetWifiRemoteStationManager(
      const Ptr<WifiRemoteStationManager> stationManager)
  {
    m_collaborativeBeaconTxop->SetWifiRemoteStationManager(stationManager);
    m_collaborativeControlTxop->SetWifiRemoteStationManager(stationManager);
    RegularWifiMac::SetWifiRemoteStationManager(stationManager);
  }

  void
  AdhocWifiMac::EnqueueCollaborativeControl(Ptr<Packet> packet, Mac48Address to)
  {
    WifiMacHeader hdr;
    hdr.SetType(WIFI_MAC_DATA);
    hdr.SetAddr1(to);
    hdr.SetAddr2(GetAddress());
    hdr.SetAddr3(GetBssid());
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();
    m_collaborativeControlTxop->Queue(packet, hdr);
  }

  void
  AdhocWifiMac::SetAddress(Mac48Address address)
  {
    NS_LOG_FUNCTION(this << address);
    // In an IBSS, the BSSID is supposed to be generated per Section
    // 11.1.3 of IEEE 802.11. We don't currently do this - instead we
    // make an IBSS STA a bit like an AP, with the BSSID for frames
    // transmitted by each STA set to that STA's address.
    //
    // This is why we're overriding this method.
    RegularWifiMac::SetAddress(address);
    RegularWifiMac::SetBssid(address);
  }

  void
  AdhocWifiMac::Enqueue(Ptr<Packet> packet, Mac48Address to)
  {
    NS_LOG_FUNCTION(this << packet << to);
    if (m_stationManager->IsBrandNew(to))
    {
      // In ad hoc mode, we assume that every destination supports all the rates we support.
      if (GetHtSupported())
      {
        m_stationManager->AddAllSupportedMcs(to);
        m_stationManager->AddStationHtCapabilities(to, GetHtCapabilities());
      }
      if (GetVhtSupported())
      {
        m_stationManager->AddStationVhtCapabilities(to, GetVhtCapabilities());
      }
      if (GetHeSupported())
      {
        m_stationManager->AddStationHeCapabilities(to, GetHeCapabilities());
      }
      m_stationManager->AddAllSupportedModes(to);
      m_stationManager->RecordDisassociated(to);
    }

    WifiMacHeader hdr;

    // If we are not a QoS STA then we definitely want to use AC_BE to
    // transmit the packet. A TID of zero will map to AC_BE (through \c
    // QosUtilsMapTidToAc()), so we use that as our default here.
    uint8_t tid = 0;

    // For now, a STA that supports QoS does not support non-QoS
    // associations, and vice versa. In future the STA model should fall
    // back to non-QoS if talking to a peer that is also non-QoS. At
    // that point there will need to be per-station QoS state maintained
    // by the association state machine, and consulted here.
    if (GetQosSupported())
    {
      hdr.SetType(WIFI_MAC_QOSDATA);
      hdr.SetQosAckPolicy(WifiMacHeader::NORMAL_ACK);
      hdr.SetQosNoEosp();
      hdr.SetQosNoAmsdu();
      // Transmission of multiple frames in the same TXOP is not
      // supported for now
      hdr.SetQosTxopLimit(0);

      // Fill in the QoS control field in the MAC header
      tid = QosUtilsGetTidForPacket(packet);
      // Any value greater than 7 is invalid and likely indicates that
      // the packet had no QoS tag, so we revert to zero, which will
      // mean that AC_BE is used.
      if (tid > 7)
      {
        tid = 0;
      }
      hdr.SetQosTid(tid);
    }
    else
    {
      hdr.SetType(WIFI_MAC_DATA);
    }

    if (GetHtSupported())
    {
      hdr.SetNoOrder(); // explicitly set to 0 for the time being since HT control field is not yet implemented (set it to 1 when implemented)
    }
    hdr.SetAddr1(to);
    hdr.SetAddr2(GetAddress());
    hdr.SetAddr3(GetBssid());
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    if (GetQosSupported())
    {
      // Sanity check that the TID is valid
      NS_ASSERT(tid < 8);
      m_edca[QosUtilsMapTidToAc(tid)]->Queue(packet, hdr);
    }
    else
    {
      m_txop->Queue(packet, hdr);
    }
  }

  // gai dong: Implement Enqueue with from address for OpenFlow switch support
  void
  AdhocWifiMac::Enqueue(Ptr<Packet> packet, Mac48Address to, Mac48Address from)
  {
    NS_LOG_FUNCTION(this << packet << to << from);
    if (m_stationManager->IsBrandNew(to))
    {
      // In ad hoc mode, we assume that every destination supports all the rates we support.
      if (GetHtSupported())
      {
        m_stationManager->AddAllSupportedMcs(to);
        m_stationManager->AddStationHtCapabilities(to, GetHtCapabilities());
      }
      if (GetVhtSupported())
      {
        m_stationManager->AddStationVhtCapabilities(to, GetVhtCapabilities());
      }
      if (GetHeSupported())
      {
        m_stationManager->AddStationHeCapabilities(to, GetHeCapabilities());
      }
      m_stationManager->AddAllSupportedModes(to);
      m_stationManager->RecordDisassociated(to);
    }

    WifiMacHeader hdr;

    uint8_t tid = 0;

    if (GetQosSupported())
    {
      hdr.SetType(WIFI_MAC_QOSDATA);
      hdr.SetQosAckPolicy(WifiMacHeader::NORMAL_ACK);
      hdr.SetQosNoEosp();
      hdr.SetQosNoAmsdu();
      hdr.SetQosTxopLimit(0);

      tid = QosUtilsGetTidForPacket(packet);
      if (tid > 7)
      {
        tid = 0;
      }
      hdr.SetQosTid(tid);
    }
    else
    {
      hdr.SetType(WIFI_MAC_DATA);
    }

    if (GetHtSupported())
    {
      hdr.SetNoOrder();
    }
    hdr.SetAddr1(to);
    hdr.SetAddr2(from);  // Use the provided from address instead of GetAddress()
    hdr.SetAddr3(GetBssid());
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    if (GetQosSupported())
    {
      NS_ASSERT(tid < 8);
      m_edca[QosUtilsMapTidToAc(tid)]->Queue(packet, hdr);
    }
    else
    {
      m_txop->Queue(packet, hdr);
    }
  }
  // gai dong end

  void
  AdhocWifiMac::SetLinkUpCallback(Callback<void> linkUp)
  {
    NS_LOG_FUNCTION(this << &linkUp);
    RegularWifiMac::SetLinkUpCallback(linkUp);

    // The approach taken here is that, from the point of view of a STA
    // in IBSS mode, the link is always up, so we immediately invoke the
    // callback if one is set
    linkUp();
  }

  void
  AdhocWifiMac::Receive(Ptr<WifiMacQueueItem> mpdu)
  {
    NS_LOG_FUNCTION(this << *mpdu);
    const WifiMacHeader *hdr = &mpdu->GetHeader();
    NS_ASSERT(!hdr->IsCtl());
    Mac48Address from = hdr->GetAddr2();
    Mac48Address to = hdr->GetAddr1();

    if (hdr->IsBeacon())
    {
      Ptr<Packet> packet = mpdu->GetPacket()->Copy();
      MgtBeaconHeader beacon;
      if (packet->RemoveHeader(beacon) && beacon.GetSsid() == GetSsid())
      {
        // In an IBSS, receiving a valid beacon before our attempt suppresses
        // this station for the current beacon period. Flush a beacon that may
        // already be waiting for channel access, then rejoin at the next TBTT.
        if (m_collaborativeBeaconGeneration)
        {
          Simulator::Cancel(m_collaborativeBeaconEvent);
          m_collaborativeBeaconTxop->GetWifiMacQueue()->Flush();
          ScheduleNextCollaborativeBeacon();
        }
      }
      return;
    }

    if (m_stationManager->IsBrandNew(from))
    {
      // In ad hoc mode, we assume that every destination supports all the rates we support.
      if (GetHtSupported())
      {
        m_stationManager->AddAllSupportedMcs(from);
        m_stationManager->AddStationHtCapabilities(from, GetHtCapabilities());
      }
      if (GetVhtSupported())
      {
        m_stationManager->AddStationVhtCapabilities(from, GetVhtCapabilities());
      }
      if (GetHeSupported())
      {
        m_stationManager->AddStationHeCapabilities(from, GetHeCapabilities());
      }
      m_stationManager->AddAllSupportedModes(from);
      m_stationManager->RecordDisassociated(from);
    }
    if (hdr->IsData())
    {
      if (hdr->IsQosData() && hdr->IsQosAmsdu())
      {
        NS_LOG_DEBUG("Received A-MSDU from" << from);
        DeaggregateAmsduAndForward(mpdu);
      }
      else
      {
        ForwardUp(mpdu->GetPacket()->Copy(), from, to);
      }
      return;
    }

    // Invoke the receive handler of our parent class to deal with any
    // other frames. Specifically, this will handle Block Ack-related
    // Management Action frames.
    RegularWifiMac::Receive(mpdu);
  }
  // gai dong
  bool
  AdhocWifiMac::SupportsSendFrom(void) const
  {
    NS_LOG_FUNCTION(this);
    return true;
  }
  // gai dong

} // namespace ns3
