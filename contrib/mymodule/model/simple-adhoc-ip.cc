#include "simple-adhoc-ip.h"
#include "ns3/random-variable-stream.h"
#include "ns3/ipv4-l3-protocol.h"
#include <arpa/inet.h>
#include <cstring>

namespace ns3
{
    NS_LOG_COMPONENT_DEFINE("AdhocSimpleIp");
    // IP 转字符串工具函数实现
    std::string Ipv4ToStr(Ipv4Address ip)
    {
        char buf[16];
        uint32_t ipInt = ip.Get();
        uint32_t hostIpInt = ntohl(ipInt);
        inet_ntop(AF_INET, &hostIpInt, buf, sizeof(buf));
        return std::string(buf);
    }

    TypeId SimpleAdhocIp::GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::SimpleAdhocIp")
                                .SetParent<Application>()
                                .SetGroupName("AdhocIp")
                                .AddConstructor<SimpleAdhocIp>();
        return tid;
    }

    SimpleAdhocIp::SimpleAdhocIp() {}
    SimpleAdhocIp::~SimpleAdhocIp() { m_socket = nullptr; }

    void SimpleAdhocIp::StartApplication(void)
    {
        NS_LOG_INFO("节点" << m_nodeId << "：启动 IP 分配应用");

        // 创建 UDP socket
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
        m_socket->Bind(local);

        m_socket->SetAllowBroadcast(true);
        m_socket->SetRecvCallback(MakeCallback(&SimpleAdhocIp::HandleRead, this));
        Simulator::Schedule(Seconds(1.0), &SimpleAdhocIp::SelectCandidateIp, this);
    }

    void SimpleAdhocIp::StopApplication(void)
    {
        NS_LOG_INFO("节点" << m_nodeId << "：停止 IP 分配应用");
        if (m_socket)
        {
            m_socket->Close();
        }
        Simulator::Cancel(m_timeoutEvent);
    }

    void SimpleAdhocIp::SelectCandidateIp()
    {
        Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
        uint32_t base = m_poolBase.Get();
        uint32_t mask = m_poolMask.Get();
        uint32_t network = base & mask;

        uint32_t minHost = 1;
        uint32_t maxHost = 254;
        uint32_t randomHost = rand->GetInteger(minHost, maxHost);
        m_candidateIp = Ipv4Address(network | randomHost);

        NS_LOG_INFO("节点" << m_nodeId << "：选择候选 IP=" << Ipv4ToStr(m_candidateIp));
        SendRequest();
    }

    void SimpleAdhocIp::SendRequest()
    {
        m_isAllocating = true;

        uint8_t msg[9];
        msg[0] = IP_REQUEST;
        *(uint32_t *)(msg + 1) = htonl(m_nodeId);
        *(uint32_t *)(msg + 5) = m_candidateIp.Get();

        Ptr<Packet> packet = Create<Packet>(msg, 9);
        uint32_t broadcastIp = (m_poolBase.Get() & m_poolMask.Get()) | (~m_poolMask.Get());
        InetSocketAddress broadcast = InetSocketAddress(Ipv4Address(broadcastIp), m_port);
        m_socket->SendTo(packet, 0, broadcast);

        m_timeoutEvent = Simulator::Schedule(m_timeout, &SimpleAdhocIp::OnRequestTimeout, this);
        NS_LOG_INFO("节点" << m_nodeId << "：广播 IP 请求，等待 2 秒冲突检测");
    }

    void SimpleAdhocIp::HandleRead(Ptr<Socket> sock)
    {
        Ptr<Packet> packet;
        Address from;
        while ((packet = sock->RecvFrom(from)))
        {
            if (packet->GetSize() != 9)
                return;

            uint8_t msg[9];
            packet->CopyData(msg, 9);
            uint8_t type = msg[0];
            uint32_t srcNodeId = ntohl(*(uint32_t *)(msg + 1));
            Ipv4Address srcIp = Ipv4Address(*(uint32_t *)(msg + 5));

            if (srcNodeId == m_nodeId)
                return;

            if (type == IP_CONFLICT && srcIp == m_candidateIp && m_isAllocating)
            {
                NS_LOG_WARN("节点" << m_nodeId << "：候选 IP " << Ipv4ToStr(m_candidateIp)
                                   << " 冲突（节点" << srcNodeId << "已占用）");
                Simulator::Cancel(m_timeoutEvent);
                m_isAllocating = false;
                Simulator::Schedule(Seconds(1.0), &SimpleAdhocIp::SelectCandidateIp, this);
            }
            else if (type == IP_REQUEST && srcIp == m_assignedIp && m_assignedIp != Ipv4Address::GetAny())
            {
                NS_LOG_INFO("节点" << m_nodeId << "：向节点" << srcNodeId << "发送冲突通知(IP "
                                   << Ipv4ToStr(srcIp) << " 已占用）");
                uint8_t resp[9];
                resp[0] = IP_CONFLICT;
                *(uint32_t *)(resp + 1) = htonl(m_nodeId);
                *(uint32_t *)(resp + 5) = srcIp.Get();
                Ptr<Packet> respPacket = Create<Packet>(resp, 9);
                m_socket->SendTo(respPacket, 0, from);
            }
        }
    }

    void SimpleAdhocIp::OnRequestTimeout()
    {
        m_assignedIp = m_candidateIp;
        m_isAllocating = false;

        NS_LOG_INFO("节点" << m_nodeId << ":IP 分配成功！已确认 IP=" << Ipv4ToStr(m_assignedIp));

        Ptr<Ipv4L3Protocol> ipv4 = GetNode()->GetObject<Ipv4L3Protocol>();
        for (uint32_t i = 0; i < ipv4->GetNInterfaces(); i++)
        {
            Ptr<Ipv4Interface> iface = ipv4->GetInterface(i);
            if (iface->GetAddress(0).GetLocal().Get() >> 24 == 0x7F)
                continue;

            iface->RemoveAddress(0);
            iface->AddAddress(Ipv4InterfaceAddress(m_assignedIp, m_poolMask));
            iface->SetUp();
            break;
        }
    }

} // namespace ns3
