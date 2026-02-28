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

#ifndef SMART_AODV_CLUSTER_HELPER_H
#define SMART_AODV_CLUSTER_HELPER_H

#include "ns3/object-factory.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ipv4-routing-helper.h"

namespace ns3 {

/**
 * \ingroup smartAodvCluster
 * \brief Helper class that adds Smart-AODV-Cluster routing to nodes.
 */
class SmartAodvClusterHelper : public Ipv4RoutingHelper
{
public:
  /**
   * Create an SmartAodvClusterHelper that makes life easier for people who want to install
   * Smart-AODV-Cluster routing to nodes.
   */
  SmartAodvClusterHelper ();

  /**
   * \brief Construct an SmartAodvClusterHelper from another previously initialized instance
   * (Copy Constructor).
   */
  SmartAodvClusterHelper (const SmartAodvClusterHelper &);

  /**
   * \returns pointer to clone of this SmartAodvClusterHelper
   *
   * This method is mainly for internal use by the other helpers;
   * clients are expected to free the dynamic memory allocated by this method
   */
  SmartAodvClusterHelper* Copy (void) const;

  /**
   * \param node the node for which the routing protocol is to be created
   * \returns a newly-created routing protocol
   *
   * This method will be called by ns3::InternetStackHelper::Install
   */
  virtual Ptr<Ipv4RoutingProtocol> Create (Ptr<Node> node) const;

  /**
   * \param name the name of the attribute to set
   * \param value the value of the attribute to set.
   *
   * This method controls the attributes of ns3::smartAodvCluster::RoutingProtocol
   */
  void Set (std::string name, const AttributeValue &value);

  /**
   * \param stream the output stream to be used for logging
   * \param level the log level
   *
   * Enable/Disable logging
   */
  void EnableLog (std::string level = "all");

  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model.  Return the number of streams (possibly zero) that
   * have been assigned.  The Install() method should have previously been
   * called by the user.
   *
   * \param stream first stream index to use
   * \param c NodeContainer of the set of nodes for which the stream
   *          numbers should be set
   * \return the number of stream indices assigned by this helper
   */
  int64_t AssignStreams (NodeContainer c, int64_t stream);

private:
  ObjectFactory m_agentFactory; //!< Object factory
};

} // namespace ns3

#endif /* SMART_AODV_CLUSTER_HELPER_H */
