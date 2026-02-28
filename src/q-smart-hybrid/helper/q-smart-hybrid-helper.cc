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

#include "q-smart-hybrid-helper.h"
#include "ns3/q-smart-hybrid-routing-protocol.h"
#include "ns3/node-list.h"
#include "ns3/names.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("QSmartHybridHelper");

QSmartHybridHelper::QSmartHybridHelper ()
{
  m_agentFactory.SetTypeId ("ns3::qSmartHybrid::RoutingProtocol");
}

QSmartHybridHelper::QSmartHybridHelper (const QSmartHybridHelper& o)
  : m_agentFactory (o.m_agentFactory)
{
}

QSmartHybridHelper*
QSmartHybridHelper::Copy () const
{
  return new QSmartHybridHelper (*this);
}

Ptr<Ipv4RoutingProtocol>
QSmartHybridHelper::Create (Ptr<Node> node) const
{
  NS_LOG_LOGIC ("Creating Q-Smart-Hybrid routing protocol for node " << node->GetId ());
  Ptr<qSmartHybrid::RoutingProtocol> agent = m_agentFactory.Create<qSmartHybrid::RoutingProtocol> ();
  node->AggregateObject (agent);
  return agent;
}

void
QSmartHybridHelper::Set (std::string name, const AttributeValue& value)
{
  m_agentFactory.Set (name, value);
}

void
QSmartHybridHelper::EnableLog (std::string level)
{
  // Parse log level string
  LogLevel lvl;
  if (level == "all")
    lvl = LOG_LEVEL_ALL;
  else if (level == "debug")
    lvl = LOG_LEVEL_DEBUG;
  else if (level == "info")
    lvl = LOG_LEVEL_INFO;
  else if (level == "warn")
    lvl = LOG_LEVEL_WARN;
  else if (level == "error")
    lvl = LOG_LEVEL_ERROR;
  else
    lvl = LOG_LEVEL_ALL;

  LogComponentEnable ("QSmartHybridRoutingProtocol", lvl);
  LogComponentEnable ("QSmartHybridRoutingTable", lvl);
  LogComponentEnable ("QSmartHybridPacket", lvl);
  LogComponentEnable ("QSmartHybridQlearning", lvl);
}

int64_t
QSmartHybridHelper::AssignStreams (NodeContainer c, int64_t stream)
{
  int64_t currentStream = stream;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
  {
    Ptr<Node> node = *i;
    Ptr<qSmartHybrid::RoutingProtocol> qsh = node->GetObject<qSmartHybrid::RoutingProtocol> ();
    if (qsh)
    {
      currentStream += qsh->AssignStreams (currentStream);
    }
  }
  return (currentStream - stream);
}

} // namespace ns3
