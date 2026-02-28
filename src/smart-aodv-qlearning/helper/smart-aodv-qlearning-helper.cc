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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "smart-aodv-qlearning-helper.h"
#include "ns3/smart-aodv-qlearning-routing-protocol.h"
#include "ns3/node-list.h"
#include "ns3/names.h"

namespace ns3 {

SmartAodvQlearningHelper::SmartAodvQlearningHelper ()
    : Ipv4RoutingHelper ()
{
  m_agentFactory.SetTypeId ("ns3::smartAodvQlearning::RoutingProtocol");
}

SmartAodvQlearningHelper::SmartAodvQlearningHelper (const SmartAodvQlearningHelper &o)
    : Ipv4RoutingHelper (o),
      m_agentFactory (o.m_agentFactory)
{
}

SmartAodvQlearningHelper*
SmartAodvQlearningHelper::Copy (void) const
{
  return new SmartAodvQlearningHelper (*this);
}

Ptr<Ipv4RoutingProtocol>
SmartAodvQlearningHelper::Create (Ptr<Node> node) const
{
  Ptr<smartAodvQlearning::RoutingProtocol> agent = m_agentFactory.Create<smartAodvQlearning::RoutingProtocol> ();
  node->AggregateObject (agent);
  return agent;
}

void
SmartAodvQlearningHelper::Set (std::string name, const AttributeValue &value)
{
  m_agentFactory.Set (name, value);
}

void
SmartAodvQlearningHelper::EnableLog (std::string level)
{
  LogComponentEnable ("SmartAodvQlearningRoutingProtocol", LogLevel (LOG_LEVEL_ALL | LOG_PREFIX_ALL));
}

int64_t
SmartAodvQlearningHelper::AssignStreams (NodeContainer c, int64_t stream)
{
  int64_t currentStream = stream;
  Ptr<Node> node;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      node = (*i);
      Ptr<smartAodvQlearning::RoutingProtocol> agent = node->GetObject<smartAodvQlearning::RoutingProtocol> ();
      if (agent)
        {
          currentStream += agent->AssignStreams (currentStream);
        }
    }
  return (currentStream - stream);
}

} // namespace ns3
