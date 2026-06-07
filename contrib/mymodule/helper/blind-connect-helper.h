#ifndef BLIND_CONNECT_HELPER_H
#define BLIND_CONNECT_HELPER_H

#include "ns3/object-factory.h"
#include "ns3/node.h"
#include "ns3/wifi-net-device.h"
#include "ns3/application-container.h"

namespace ns3 {

class BlindConnectHelper
{
public:
    BlindConnectHelper();

    void SetAttribute(std::string name, const AttributeValue &value);

    // 【修改点】：传入两个 NetDevice 指针
    ApplicationContainer Install(Ptr<Node> node,
                                 Ptr<NetDevice> staDev,
                                 Ptr<NetDevice> adhocDev);

private:
    ObjectFactory m_factory;
};

}

#endif
