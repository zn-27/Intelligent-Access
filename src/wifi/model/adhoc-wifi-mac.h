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

#ifndef ADHOC_WIFI_MAC_H
#define ADHOC_WIFI_MAC_H

#include "regular-wifi-mac.h"
#include "supported-rates.h"

namespace ns3
{

  /**
   * \ingroup wifi
   *
   * \brief Wifi MAC high model for an ad-hoc Wifi MAC
   */
  class AdhocWifiMac : public RegularWifiMac
  {
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    AdhocWifiMac();
    virtual ~AdhocWifiMac();

    void SetAddress(Mac48Address address) override;
    void SetWifiRemoteStationManager(
        const Ptr<WifiRemoteStationManager> stationManager) override;
    void SetLinkUpCallback(Callback<void> linkUp) override;
    void Enqueue(Ptr<Packet> packet, Mac48Address to) override;
    // gai dong: Add Enqueue with from address for OpenFlow switch support
    void Enqueue(Ptr<Packet> packet, Mac48Address to, Mac48Address from) override;
    bool SupportsSendFrom(void) const override;
    void SetCollaborativeBeaconGeneration(bool enable);
    bool GetCollaborativeBeaconGeneration() const;
    void SetCollaborativeBeaconInterval(Time interval);
    Time GetCollaborativeBeaconInterval() const;
    void SetCollaborativeDomainId(uint32_t domainId);
    void SetCollaborativeRole(uint8_t role);
    void SetCollaborativeGateway(uint32_t gateway);
    void SetCollaborativeHops(uint8_t hops);
    void SetCollaborativeLoadPercent(uint8_t load);
    void SetCollaborativeEnergyPercent(uint8_t energy);
    void SetCollaborativeSecurityCapabilities(uint8_t capabilities);
    void EnqueueCollaborativeControl(Ptr<Packet> packet, Mac48Address to);
    uint8_t GetCollaborativeRole() const;
    uint8_t GetCollaborativeHops() const;
    uint32_t GetCollaborativeGateway() const;
    // gai dong
  private:
    void DoInitialize() override;
    void DoDispose() override;
    void SendCollaborativeBeacon();
    void ScheduleNextCollaborativeBeacon();
    SupportedRates GetCollaborativeSupportedRates() const;
    void Receive(Ptr<WifiMacQueueItem> mpdu) override;

    bool m_collaborativeBeaconGeneration;
    Time m_collaborativeBeaconInterval;
    bool m_enableCollaborativeBeaconJitter;
    uint16_t m_collaborativeBeaconMaxBackoffSlots;
    Time m_collaborativeBeaconSlotTime;
    EventId m_collaborativeBeaconEvent;
    Ptr<Txop> m_collaborativeBeaconTxop;
    Ptr<Txop> m_collaborativeControlTxop;
    Ptr<UniformRandomVariable> m_collaborativeBeaconJitter;
    uint32_t m_collaborativeDomainId;
    uint8_t m_collaborativeRole;
    uint32_t m_collaborativeGateway;
    uint8_t m_collaborativeHops;
    uint8_t m_collaborativeLoadPercent;
    uint8_t m_collaborativeEnergyPercent;
    uint8_t m_collaborativeSecurityCapabilities;
  };

} // namespace ns3

#endif /* ADHOC_WIFI_MAC_H */
