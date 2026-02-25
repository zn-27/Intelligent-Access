#ifndef SIMPLE_ADHOC_IP_H
#define SIMPLE_ADHOC_IP_H

#include "ns3/core-module.h"
#include "ns3/application.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-interface.h"
#include <string>

namespace ns3
{

    // IP 转字符串工具函数声明
    std::string Ipv4ToStr(Ipv4Address ip);

    class SimpleAdhocIp : public Application
    {
    public:
        static TypeId GetTypeId(void);
        SimpleAdhocIp();
        virtual ~SimpleAdhocIp();

        void SetNodeId(uint32_t id) { m_nodeId = id; }
        void SetIpPool(Ipv4Address base, Ipv4Mask mask)
        {
            m_poolBase = base;
            m_poolMask = mask;
        }
        Ipv4Address GetAssignedIp() const { return m_assignedIp; }

    private:
        virtual void StartApplication(void) override;
        virtual void StopApplication(void) override;

        void SelectCandidateIp();
        void SendRequest();
        void HandleRead(Ptr<Socket> sock);
        void OnRequestTimeout();

        enum MsgType
        {
            IP_REQUEST = 1,
            IP_CONFLICT = 2
        };

        Ptr<Socket> m_socket;
        uint16_t m_port = 49153;
        uint32_t m_nodeId;
        Ipv4Address m_poolBase;
        Ipv4Mask m_poolMask;
        Ipv4Address m_candidateIp;
        Ipv4Address m_assignedIp;
        EventId m_timeoutEvent;
        Time m_timeout = Seconds(2.0);
        bool m_isAllocating = false;
    };

} // namespace ns3

#endif /* SIMPLE_ADHOC_IP_H */
