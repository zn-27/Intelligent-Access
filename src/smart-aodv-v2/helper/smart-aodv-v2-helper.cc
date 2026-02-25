/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Pavel Boyko <boyko@iitp.ru>, written after OlsrHelper by Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "smart-aodv-v2-helper.h"
#include "../model/smart-aodv-v2-routing-protocol.h"
#include "ns3/node-list.h"
#include "ns3/names.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-list-routing.h"

namespace ns3
{

SmartAodvV2Helper::SmartAodvV2Helper ():
  Ipv4RoutingHelper ()
{
  m_agentFactory.SetTypeId ("ns3::smartAodvV2::RoutingProtocol");
}

SmartAodvV2Helper*
SmartAodvV2Helper::Copy (void) const
{
  return new SmartAodvV2Helper (*this);
}

Ptr<Ipv4RoutingProtocol>
SmartAodvV2Helper::Create (Ptr<Node> node) const
{
  Ptr<smartAodvV2::RoutingProtocol> agent = m_agentFactory.Create<smartAodvV2::RoutingProtocol> ();
  node->AggregateObject (agent);
  return agent;
}

void
SmartAodvV2Helper::Set (std::string name, const AttributeValue &value)
{
  m_agentFactory.Set (name, value);
}

int64_t
SmartAodvV2Helper::AssignStreams (NodeContainer c, int64_t stream)
{
  int64_t currentStream = stream;
  Ptr<Node> node;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      node = (*i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
      NS_ASSERT_MSG (ipv4, "Ipv4 not installed on node");
      Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol ();
      NS_ASSERT_MSG (proto, "Ipv4 routing not installed on node");
      Ptr<smartAodvV2::RoutingProtocol> smartaodv = DynamicCast<smartAodvV2::RoutingProtocol> (proto);
      if (smartaodv)
        {
          currentStream += smartaodv->AssignStreams (currentStream);
          continue;
        }
      // SmartAodv may also be in a list
      Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting> (proto);
      if (list)
        {
          int16_t priority;
          Ptr<Ipv4RoutingProtocol> listProto;
          Ptr<smartAodvV2::RoutingProtocol> listSmartAodv;
          for (uint32_t i = 0; i < list->GetNRoutingProtocols (); i++)
            {
              listProto = list->GetRoutingProtocol (i, priority);
              listSmartAodv = DynamicCast<smartAodvV2::RoutingProtocol> (listProto);
              if (listSmartAodv)
                {
                  currentStream += listSmartAodv->AssignStreams (currentStream);
                  break;
                }
            }
        }
    }
  return (currentStream - stream);
}

}
