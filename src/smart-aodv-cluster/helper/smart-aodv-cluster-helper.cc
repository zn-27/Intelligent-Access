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

#include "smart-aodv-cluster-helper.h"
#include "ns3/smart-aodv-cluster-routing-protocol.h"
#include "ns3/node-list.h"
#include "ns3/names.h"

namespace ns3 {

SmartAodvClusterHelper::SmartAodvClusterHelper ()
    : Ipv4RoutingHelper ()
{
  m_agentFactory.SetTypeId ("ns3::smartAodvCluster::RoutingProtocol");
}

SmartAodvClusterHelper::SmartAodvClusterHelper (const SmartAodvClusterHelper &o)
    : Ipv4RoutingHelper (o),
      m_agentFactory (o.m_agentFactory)
{
}

SmartAodvClusterHelper*
SmartAodvClusterHelper::Copy (void) const
{
  return new SmartAodvClusterHelper (*this);
}

Ptr<Ipv4RoutingProtocol>
SmartAodvClusterHelper::Create (Ptr<Node> node) const
{
  Ptr<smartAodvCluster::RoutingProtocol> agent = m_agentFactory.Create<smartAodvCluster::RoutingProtocol> ();
  node->AggregateObject (agent);
  return agent;
}

void
SmartAodvClusterHelper::Set (std::string name, const AttributeValue &value)
{
  m_agentFactory.Set (name, value);
}

void
SmartAodvClusterHelper::EnableLog (std::string level)
{
  LogComponentEnable ("SmartAodvClusterRoutingProtocol", LogLevel (LOG_LEVEL_ALL | LOG_PREFIX_ALL));
}

int64_t
SmartAodvClusterHelper::AssignStreams (NodeContainer c, int64_t stream)
{
  int64_t currentStream = stream;
  Ptr<Node> node;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      node = (*i);
      Ptr<smartAodvCluster::RoutingProtocol> agent = node->GetObject<smartAodvCluster::RoutingProtocol> ();
      if (agent)
        {
          currentStream += agent->AssignStreams (currentStream);
        }
    }
  return (currentStream - stream);
}

} // namespace ns3
