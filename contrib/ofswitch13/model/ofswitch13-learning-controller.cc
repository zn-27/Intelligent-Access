
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * 本程序为自由软件，您可以重新分发和/或修改
 * 本程序遵循 GNU 通用公共许可证版本 2 的条款，该许可证由
 * 自由软件基金会发布；
 *
 * 发布本程序的目的是希望它能发挥作用，但
 * 不提供任何明示或暗示的担保，包括但不限于
 * 适销性担保和特定用途适用性担保。如需了解更多细节，
 * 请查阅 GNU 通用公共许可证。
 *
 * 您应已收到一份 GNU 通用公共许可证的副本，
 * 若未收到，请向自由软件基金会（Free Software Foundation, Inc.）
 * 邮寄信件，地址：美国马萨诸塞州波士顿市坦普尔广场 59 号 330 室，邮编：02111-1307
 *
 * 作者：卢西亚诺·沙维斯（Luciano Chaves），邮箱：luciano@lrc.ic.unicamp.br
 */

// 仅在定义了 NS3_OFSWITCH13 宏时才编译以下代码
#ifdef NS3_OFSWITCH13

#include "ofswitch13-learning-controller.h"
#include "ns3/ipv4-address.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <deque> // For experience replay buffer
#include <set>   // For std::set in dynamic port management
#include "ns3/mobility-model.h"
#include <iomanip>

NS_LOG_COMPONENT_DEFINE("OFSwitch13LearningController");

namespace ns3
{

    NS_OBJECT_ENSURE_REGISTERED(OFSwitch13LearningController);

    // 静态常量成员定义（C++11/14需要，C++17可省略）
    constexpr double OFSwitch13LearningController::MIN_THROUGHPUT_THRESHOLD;
    constexpr double OFSwitch13LearningController::THRESHOLD_COEFFICIENT;
    constexpr uint32_t OFSwitch13LearningController::OBSERVE_COUNT_THRESHOLD;
    constexpr uint32_t OFSwitch13LearningController::INACTIVE_COUNT_THRESHOLD;
    constexpr int OFSwitch13LearningController::MAX_LINKS;

    // const float OFSwitch13LearningController::DISTANCE_THRESHOLD = 50.0f;
    OFSwitch13LearningController::OFSwitch13LearningController()
        : m_networkQLearning(0.4, 0.6, 0.2), m_activeLinkCount(0),
          m_currentThroughputThreshold(MIN_THROUGHPUT_THRESHOLD) // alpha, gamma, epsilon
    {
        // 动态初始化链路统计数组
        m_linkStats.resize(MAX_LINKS);
        m_activeLinks.resize(MAX_LINKS, false); // 初始化为false

        // 移除硬编码端口映射，改为动态发现
        std::cout << "[动态端口] 端口自动发现机制已启用" << std::endl;
        std::cout << "[动态端口] 最大链路数: " << MAX_LINKS << std::endl;
        std::cout << "[动态阈值] 初始阈值: " << m_currentThroughputThreshold << " Kbps" << std::endl;

        m_currentMode = 0;
        // 端口映射不再硬编码，由各拓扑脚本通过 SetSwitchPortMapping() 配置
        m_switchPortMappings.clear();

        NS_LOG_FUNCTION(this);
    }

    OFSwitch13LearningController::~OFSwitch13LearningController()
    {
        NS_LOG_FUNCTION(this);
    }

    TypeId
    OFSwitch13LearningController::GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::OFSwitch13LearningController")
                                .SetParent<OFSwitch13Controller>()
                                .SetGroupName("OFSwitch13")
                                .AddConstructor<OFSwitch13LearningController>();
        return tid;
    }

    void
    OFSwitch13LearningController::SetRoutingPriority()
    {
        std::cout << "设置routinglist优先级中-----------" << std::endl;
        NS_LOG_FUNCTION(this);
        SetRP();
    }

    void
    OFSwitch13LearningController::DoDispose()
    {
        NS_LOG_FUNCTION(this);
        m_learnedInfo.clear();
        m_l3LearnedInfo.clear(); // 清空L3层学习信息
        m_arpTable.clear();
        m_switchHosts.clear();        // 交换机主机信息
        m_switchPortMappings.clear(); // 交换机互联端口映射
        m_subnetToSwitchMap.clear();  // 子网-交换机映射
        m_nodePositionMap.clear();    // 清空节点位置信息
        OFSwitch13Controller::DoDispose();
    }
    // 用于设置全域的路由协议优先级
    void
    OFSwitch13LearningController::SetPriorityToAll()
    {
        NS_LOG_FUNCTION(this);
        SetRPtoAll();
    }
    //   用于切换组网模式
    void
    OFSwitch13LearningController::CDL()
    {
        NS_LOG_FUNCTION(this);
        ChangeDeviceLogical(1); // 1打开自组织模式关闭apwifi，0关闭自组织模式打开apwifi，2... 3...
    }

    void
    OFSwitch13LearningController::SetSwitchPortMapping(uint64_t localDpid, uint64_t destDpid, uint32_t outputPort)
    {
        auto it = m_switchPortMappings.find(localDpid);
        if (it != m_switchPortMappings.end())
        {
            for (auto &mapping : it->second)
            {
                if (mapping.destSwitchDpid == destDpid)
                {
                    mapping.outputPort = outputPort;
                    return;
                }
            }
            it->second.push_back({destDpid, outputPort});
        }
        else
        {
            m_switchPortMappings[localDpid] = {{destDpid, outputPort}};
        }
    }

    void
    OFSwitch13LearningController::RegisterHost(uint64_t dpId, Ipv4Address ip, Mac48Address mac, uint32_t port)
    {
        HostInfo host{ip, mac, port};
        m_arpTable[ip] = mac;
        m_switchHosts[dpId].push_back(host);

        uint32_t networkIpHost = ip.Get() & 0xFFFFFF00;
        Ipv4Address networkIp(networkIpHost);
        std::ostringstream subnet;
        networkIp.Print(subnet);
        m_subnetToSwitchMap[subnet.str() + "/24"] = dpId;

        std::ostringstream cmd;
        cmd << "flow-mod cmd=add,table=0,prio=200,idle=30,hard=60 "
            << "eth_type=0x0800,ip_dst=" << ip << " "
            << "apply:set_field=eth_dst:" << mac << ",output=" << port;
        DpctlExecute(dpId, cmd.str());
        GenerateCrossDomainRulesForAllSwitches();

        std::cout << "[RegisterHost] DPID=" << dpId << " IP=" << ip
                  << " MAC=" << mac << " Port=" << port << std::endl;
    }

    ofl_err
    OFSwitch13LearningController::HandlePacketIn(
        struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch,
        uint32_t xid)
    {
        NS_LOG_FUNCTION(this << swtch << xid);

        static int prio = 100;
        uint32_t outPort = OFPP_FLOOD;
        enum ofp_packet_in_reason reason = msg->reason;
        uint64_t swDpId = swtch->GetDpId();

        char *msgStr = ofl_structs_match_to_string((struct ofl_match_header *)msg->match, 0);
        NS_LOG_DEBUG("Packet in match: " << msgStr);
        free(msgStr);

        if (reason == OFPR_NO_MATCH)
        {
            // 提取入端口信息
            uint32_t inPort;
            size_t portLen = OXM_LENGTH(OXM_OF_IN_PORT);
            struct ofl_match_tlv *input = oxm_match_lookup(OXM_OF_IN_PORT, (struct ofl_match *)msg->match);
            memcpy(&inPort, input->value, portLen);

            // 提取源MAC和目的MAC
            Mac48Address src48;
            struct ofl_match_tlv *ethSrc = oxm_match_lookup(OXM_OF_ETH_SRC, (struct ofl_match *)msg->match);
            src48.CopyFrom(ethSrc->value);

            Mac48Address dst48;
            struct ofl_match_tlv *ethDst = oxm_match_lookup(OXM_OF_ETH_DST, (struct ofl_match *)msg->match);
            dst48.CopyFrom(ethDst->value);

            // 提取IP地址信息
            Ipv4Address srcIp, dstIp;
            bool isIpv4 = false;
            Ipv4Address arpSenderIp, arpTargetIp; // ARP包专用IP变量

            // 检查是否为IPv4包
            struct ofl_match_tlv *ethType = oxm_match_lookup(OXM_OF_ETH_TYPE, (struct ofl_match *)msg->match);
            if (ethType != nullptr)
            {
                uint16_t type;
                memcpy(&type, ethType->value, 2);

                // 1. 输出当前数据包的以太网类型（方便确认是否为IPv4）
                // std::cout << type<< std::endl;
                // uint16_t ntohsType = ntohs(type);
                // 处理ARP包（0x0806）
                // if (type == 0x0806)
                if (type == 2054)
                {
                    NS_LOG_DEBUG("检测到ARP包");
                    // std::cout <<"检测到ARP包"<< std::endl;

                    // std::cout << "[ARP包] 交换机DPID: " << swDpId << " 源MAC: " << src48 << " 目的MAC: " << dst48
                    //           << " 入端口: " << inPort << std::endl;

                    if (msg->data_length >= 42) // 确保数据长度足够（以太网头14 + ARP头28）
                    {
                        uint8_t *arpHeader = (uint8_t *)msg->data + 14; // 跳过以太网头
                        // 解析ARP操作码（偏移6-7字节：1=请求，2=响应）
                        uint16_t arpOp = (arpHeader[6] << 8) | arpHeader[7];
                        // 解析发送端IP和目标端IP
                        arpSenderIp.Set((arpHeader[14] << 24) | (arpHeader[15] << 16) | (arpHeader[16] << 8) | arpHeader[17]);
                        arpTargetIp.Set((arpHeader[24] << 24) | (arpHeader[25] << 16) | (arpHeader[26] << 8) | arpHeader[27]);
                        // 处理ARP请求（操作码1）
                        if (arpOp == 1)
                        {
                            // std::cout <<  "[ARP包] 交换机DPID: " << swDpId << "  ARP操作码: " << arpOp << " | 发送端IP: " << arpSenderIp
                            //     <<" 目标端IP: " << arpTargetIp  << std::endl;
                            // 判断目标IP是否为网关IP（通过查询ARP表）
                            bool isGwArp = false;
                            for (const auto &arpEntry : m_arpTable)
                            {
                                if (arpEntry.first == arpTargetIp)
                                {
                                    isGwArp = true;
                                    break;
                                }
                            }
                            if (isGwArp)
                            {
                                // std::cout << "  → 准备返回响应" << std::endl;

                                // 构造ARP响应包（以太网头 + ARP响应头）
                                uint8_t *arpReply = (uint8_t *)xmalloc(46);
                                memset(arpReply, 0, 46);

                                // 构造以太网头
                                src48.CopyTo(arpReply); // 目的MAC：请求端主机MAC
                                Mac48Address gwMac;

                                // 获取交换机真实网关MAC
                                auto itGwMac = m_arpTable.find(arpTargetIp);
                                if (itGwMac != m_arpTable.end())
                                {
                                    gwMac = itGwMac->second; // 使用预配置的网关MAC
                                    // std::cout << "使用预配置的网关MAC: " << gwMac << std::endl;
                                }
                                else
                                {
                                    std::cout << "ARP表中未找到网关IP " << arpTargetIp << " 的MAC，使用默认值";
                                    gwMac = Mac48Address("00:00:00:00:01:00"); //  fallback默认MAC
                                }

                                gwMac.CopyTo(arpReply + 6); // 源MAC：网关MAC
                                arpReply[12] = 0x08;
                                arpReply[13] = 0x06; // 以太网类型：ARP

                                // 构造ARP响应头
                                uint8_t *arpReplyHeader = arpReply + 14;
                                arpReplyHeader[0] = 0x00;
                                arpReplyHeader[1] = 0x01; // 硬件类型：以太网
                                arpReplyHeader[2] = 0x08;
                                arpReplyHeader[3] = 0x00; // 协议类型：IPv4
                                arpReplyHeader[4] = 0x06; // 硬件地址长度
                                arpReplyHeader[5] = 0x04; // 协议地址长度
                                arpReplyHeader[6] = 0x00;
                                arpReplyHeader[7] = 0x02; // 操作码：响应

                                // 发送端信息（网关）
                                gwMac.CopyTo(arpReplyHeader + 8);
                                arpTargetIp.Serialize(arpReplyHeader + 14);

                                // 目标端信息（请求主机）
                                src48.CopyTo(arpReplyHeader + 18);
                                uint32_t senderIpNet = htonl(arpSenderIp.Get());
                                memcpy(arpReplyHeader + 24, &senderIpNet, 4);

                                // 发送ARP响应包
                                struct ofl_msg_packet_out reply;
                                reply.header.type = OFPT_PACKET_OUT;
                                reply.buffer_id = NO_BUFFER;
                                reply.in_port = OFPP_CONTROLLER;
                                reply.data_length = 46;
                                reply.data = arpReply;

                                struct ofl_action_output *a = (struct ofl_action_output *)xmalloc(sizeof(struct ofl_action_output));
                                a->header.type = OFPAT_OUTPUT;
                                a->port = inPort; // 从入端口返回响应
                                a->max_len = 0;

                                reply.actions_num = 1;
                                reply.actions = (struct ofl_action_header **)&a;

                                SendToSwitch(swtch, (struct ofl_msg_header *)&reply, xid);
                                free(a);
                                free(arpReply);
                                return 0;
                            }
                        }
                    }
                    return 0;
                }
                // if (ntohs(type) == 0x0800)  // IPv4类型
                else if (type == 2048) // IPv4类型
                {
                    isIpv4 = true;

                    // 提取源IP
                    struct ofl_match_tlv *ipSrc = oxm_match_lookup(OXM_OF_IPV4_SRC, (struct ofl_match *)msg->match);
                    if (ipSrc != nullptr)
                    {
                        uint32_t ipAddrNet = *(uint32_t *)ipSrc->value; // 网络字节序
                        uint32_t ipAddrHost = ntohl(ipAddrNet);         // 转换为主机字节序
                        srcIp.Set(ipAddrHost);
                    }

                    // 提取目的IP
                    struct ofl_match_tlv *ipDst = oxm_match_lookup(OXM_OF_IPV4_DST, (struct ofl_match *)msg->match);
                    if (ipDst != nullptr)
                    {
                        uint32_t ipAddrNet = *(uint32_t *)ipDst->value; // 网络字节序
                        uint32_t ipAddrHost = ntohl(ipAddrNet);         // 转换为主机字节序
                        dstIp.Set(ipAddrHost);
                    }

                    // 判断是否为广播（源IP或目的IP）
                    // bool isSrcBroadcast = srcIp.IsBroadcast() || srcIp.IsMulticast();
                    // bool isDstBroadcast = dstIp.IsBroadcast() || dstIp.IsMulticast();

                    // // 仅在非广播场景输出详细IP信息
                    // if ( !isDstBroadcast)
                    // {
                    //     std::cout << "交换机DPID: " << swDpId
                    //               << "[IPv4非广播包] 源IP: " << srcIp
                    //               << " | 目的IP: " << dstIp
                    //               << " | 入端口: " << inPort << std::endl;
                    // IPv4包继续进入后续 L2/L3 学习和 PacketOut 逻辑。
                }
            }

            // 查找交换机对应的L2和L3表
            auto itL2 = m_learnedInfo.find(swDpId);
            auto itL3 = m_l3LearnedInfo.find(swDpId);

            // 确保表存在
            if (itL2 == m_learnedInfo.end())
            {
                m_learnedInfo[swDpId] = L2Table_t();
                itL2 = m_learnedInfo.find(swDpId);
            }

            if (itL3 == m_l3LearnedInfo.end())
            {
                m_l3LearnedInfo[swDpId] = L3Table_t();
                itL3 = m_l3LearnedInfo.find(swDpId);
            }

            L2Table_t *l2Table = &itL2->second;
            L3Table_t *l3Table = &itL3->second;

            // 处理L2层学习 (始终学习源MAC，非广播包)
            if (!src48.IsBroadcast())
            {
                auto itSrcL2 = l2Table->find(src48);
                if (itSrcL2 == l2Table->end())
                {
                    l2Table->insert(std::make_pair(src48, inPort));
                    // std::cout << "L2学习: MAC地址 " << src48 << " 对应端口 " << inPort << std::endl;

                    // 添加L2流表项（更高优先级）
                    std::ostringstream cmdL2;
                    cmdL2 << "flow-mod cmd=add,table=0,idle=10,flags=0x0001"
                          << ",prio=" << ++prio
                          << " eth_dst=" << src48
                          << " apply:output=" << inPort;
                    DpctlExecute(swDpId, cmdL2.str());
                }
                else if (itSrcL2->second != inPort)
                {
                    // NS_ASSERT_MSG(false, "L2转发表不一致");
                }
            }

            // 处理L3层学习 (仅对IPv4包且源非广播包进行学习)
            if (isIpv4 && !src48.IsBroadcast())
            {
                // 判断源IP是否为广播
                bool isSrcBroadcast = srcIp.IsBroadcast() || (srcIp.Get() & 0xFF) == 0xFF;
                if (!isSrcBroadcast)
                {
                    auto itSrcL3 = l3Table->find(srcIp);
                    if (itSrcL3 == l3Table->end())
                    {
                        // 存储IP->(MAC, 端口)映射
                        l3Table->insert(std::make_pair(srcIp, std::make_pair(src48, inPort)));
                        std::cout << "L3学习: IP地址 " << srcIp << " 对应MAC " << src48 << " 和端口 " << inPort << std::endl;

                        // 添加L3流表项（优先级低于L2）
                        std::ostringstream cmdL3;
                        cmdL3 << "flow-mod cmd=add,table=0,idle=10,flags=0x0001"
                              << ",prio=" << (prio - 50)
                              << " eth_type=0x0800"  // 使用空格
                              << ",ip_dst=" << srcIp // 使用逗号
                              << " apply:output=" << inPort;

                        std::cout << "L3 Flow-Mod Command: " << cmdL3.str() << std::endl;
                        DpctlExecute(swDpId, cmdL3.str());
                    }
                    else if (itSrcL3->second.second != inPort)
                    {
                        // 移动节点切换域时端口变化属正常行为，更新L3表项
                        std::cout << "L3更新: IP " << srcIp << " 端口从 " << itSrcL3->second.second
                                  << " 迁移到 " << inPort << " (MAC " << src48 << ")" << std::endl;
                        itSrcL3->second.first = src48;
                        itSrcL3->second.second = inPort;
                    }
                }
            }

            // 确定输出端口（L2优先于L3）
            if (!dst48.IsBroadcast())
            {
                // 优先检查L2表
                auto itDstL2 = l2Table->find(dst48);
                if (itDstL2 != l2Table->end())
                {
                    outPort = itDstL2->second;
                    NS_LOG_DEBUG("使用L2转发表转发至端口 " << outPort);
                }
                // L2表未命中时检查L3表
                else if (isIpv4)
                {
                    auto itDstL3 = l3Table->find(dstIp);
                    if (itDstL3 != l3Table->end())
                    {
                        outPort = itDstL3->second.second;
                        NS_LOG_DEBUG("使用L3转发表转发至端口 " << outPort);
                    }
                    else
                    {
                        std::cout << "[PacketIn] L3 MISS for ip " << dstIp
                                  << " on switch " << swDpId << " -> FLOOD" << std::endl;
                        NS_LOG_DEBUG("No L3 info for ip " << dstIp << ". Flood.");
                    }
                }
                else
                {
                    NS_LOG_DEBUG("No L2 info for mac " << dst48 << ". Flood.");
                }
            }

            // 发送Packet-Out消息
            struct ofl_msg_packet_out reply;
            reply.header.type = OFPT_PACKET_OUT;
            reply.buffer_id = msg->buffer_id;
            reply.in_port = inPort;
            reply.data_length = 0;
            reply.data = nullptr;

            if (msg->buffer_id == NO_BUFFER)
            {
                reply.data_length = msg->data_length;
                reply.data = msg->data;
            }

            struct ofl_action_output *a = (struct ofl_action_output *)xmalloc(sizeof(struct ofl_action_output));
            a->header.type = OFPAT_OUTPUT;
            a->port = outPort;
            a->max_len = 0;

            reply.actions_num = 1;
            reply.actions = (struct ofl_action_header **)&a;

            SendToSwitch(swtch, (struct ofl_msg_header *)&reply, xid);
            free(a);
        }
        else
        {
            NS_LOG_WARN("This controller can't handle the packet. Unknown reason.");
        }

        ofl_msg_free((struct ofl_msg_header *)msg, 0);
        return 0;
    }

    ofl_err
    OFSwitch13LearningController::HandleFlowRemoved(
        struct ofl_msg_flow_removed *msg, Ptr<const RemoteSwitch> swtch,
        uint32_t xid)
    {
        NS_LOG_FUNCTION(this << swtch << xid);

        uint64_t swDpId = swtch->GetDpId();
        NS_LOG_DEBUG("Flow entry expired. Removing from tables.");

        // 处理L2流表项删除
        auto itL2 = m_learnedInfo.find(swDpId);
        if (itL2 != m_learnedInfo.end())
        {
            Mac48Address mac48;
            struct ofl_match_tlv *ethDst = oxm_match_lookup(OXM_OF_ETH_DST, (struct ofl_match *)msg->stats->match);
            if (ethDst != nullptr)
            {
                mac48.CopyFrom(ethDst->value);
                itL2->second.erase(mac48);
            }
        }

        // 处理L3流表项删除
        auto itL3 = m_l3LearnedInfo.find(swDpId);
        if (itL3 != m_l3LearnedInfo.end())
        {
            struct ofl_match_tlv *ipDst = oxm_match_lookup(OXM_OF_IPV4_DST, (struct ofl_match *)msg->stats->match);
            if (ipDst != nullptr)
            {
                Ipv4Address ipAddr(*(uint32_t *)ipDst->value);
                itL3->second.erase(ipAddr);
            }
        }

        ofl_msg_free_flow_removed(msg, true, 0);
        return 0;
    }

    // 获取节点的子网信息（ip-mac-port）
    ofl_err
    OFSwitch13LearningController::HandleAdhocExtStaInfo(
        struct adhocl_ext_stainfo *msg,
        Ptr<const RemoteSwitch> swtch,
        uint32_t xid)
    {
        // 添加空指针检查
        if (!msg)
        {
            std::cout << "HandleAdhocExtStaInfo: msg is null" << std::endl;
            return 0;
        }
        if (!swtch)
        {
            std::cout << "HandleAdhocExtStaInfo: swtch is null" << std::endl;
            return 0;
        }
        NS_LOG_FUNCTION(this << swtch << xid);
        uint64_t dpId = swtch->GetDpId(); // 交换机DPID

        // 如果需要处理真实的msg数据，需要确保msg不为空
        if (msg == nullptr)
        {
            std::cout << "消息为空" << std::endl;
        }
        // 解析单个STA信息
        HostInfo host;
        host.ip = Ipv4Address((msg->ip_address)); // 注意网络字节序转换
        host.mac = Mac48Address();                // 需要从uint64_t转换为Mac48Address
        host.port = msg->port_number;

        // 将uint64_t MAC地址转换为Mac48Address
        uint8_t macBytes[6];
        uint64_t macAddr = msg->mac_address;
        macBytes[0] = (macAddr >> 40) & 0xFF;
        macBytes[1] = (macAddr >> 32) & 0xFF;
        macBytes[2] = (macAddr >> 24) & 0xFF;
        macBytes[3] = (macAddr >> 16) & 0xFF;
        macBytes[4] = (macAddr >> 8) & 0xFF;
        macBytes[5] = macAddr & 0xFF;
        host.mac.CopyFrom(macBytes);

        // 添加到交换机主机列表
        m_switchHosts[dpId].push_back(host);

        // 添加到 ARP 表，确保跨域 ARP 解析可用
        m_arpTable[host.ip] = host.mac;

        std::cout << "[Stainfo] DPID=" << dpId << " IP=" << host.ip
                  << " MAC=" << host.mac << " Port=" << host.port << std::endl;

        // 自动识别网段
        uint32_t ipHost = host.ip.Get();
        uint32_t subnetMask = 0xFFFFFF00; // /24子网掩码
        uint32_t networkIpHost = ipHost & subnetMask;
        Ipv4Address networkIp(networkIpHost);

        std::ostringstream os;
        networkIp.Print(os);
        std::string subnetStr = os.str() + "/24";

        // 更新子网-交换机映射表
        m_subnetToSwitchMap[subnetStr] = dpId;

        // std::cout << "  → 自动识别网段：" << subnetStr << " （对应交换机DPID：" << dpId << "）" << std::endl;

        std::cout << "\n当前完整的子网-交换机映射表：" << std::endl;
        for (const auto &entry : m_subnetToSwitchMap)
        {
            std::cout << "  " << entry.first << " → 交换机DPID：" << entry.second << std::endl;
        }

        // 触发流表规则生成：传递当前新主机
        GenerateSwitchFlowRules(swtch, host); // 传入当前新主机

        ofl_msg_free((struct ofl_msg_header *)msg, 0);
        return 0;
    }

    //
    ofl_err
    OFSwitch13LearningController::HandleHostInfo(
        //   struct ofl_msg_host_info *msg,
        void *msg,
        Ptr<const RemoteSwitch> swtch,
        uint32_t xid)
    {
        NS_LOG_FUNCTION(this << swtch << xid);
        uint64_t dpId = swtch->GetDpId(); // 交换机DPID

        // ==================== 初始化交换机-域内主机映射 ====================
        std::vector<HostInfo> hostList; // 声明hostList变量
        if (dpId == 0x0000000000000001)
        { // sw1（10.1.1.0/24）
            hostList = {
                {Ipv4Address("10.1.1.1"), Mac48Address("00:00:00:00:00:01"), 1},
                {Ipv4Address("10.1.1.2"), Mac48Address("00:00:00:00:00:01"), 1},
                {Ipv4Address("10.1.1.3"), Mac48Address("00:00:00:00:00:01"), 1},
                {Ipv4Address("10.1.1.4"), Mac48Address("00:00:00:00:00:01"), 2}};
        }
        else if (dpId == 0x0000000000000002)
        { // sw2（10.2.1.0/24）
            hostList = {
                {Ipv4Address("10.2.1.1"), Mac48Address("00:00:00:00:00:05"), 1},
                {Ipv4Address("10.2.1.2"), Mac48Address("00:00:00:00:00:05"), 1}};
        }
        else if (dpId == 0x0000000000000003)
        { // sw3（10.3.1.0/24）
            hostList = {
                {Ipv4Address("10.3.1.1"), Mac48Address("00:00:00:00:00:07"), 1},
                {Ipv4Address("10.3.1.2"), Mac48Address("00:00:00:00:00:07"), 1},
                {Ipv4Address("10.3.1.3"), Mac48Address("00:00:00:00:00:07"), 1},

            };
        }
        else
        {
            std::cout << "  → 未知交换机DPID，无主机信息" << std::endl;
            return 0;
        }

        m_switchHosts[dpId] = hostList; // 写入域内主机表

        // // 输出主机信息
        // std::cout << "交换机DPID(" << dpId << ") 域内主机信息：" << std::endl;
        // for (size_t i = 0; i < hostList.size(); i++) {
        //     const auto& host = hostList[i];
        //     std::cout << "  主机" << i+1 << ": IP=" << host.ip
        //               << ", MAC=" << host.mac << ", 端口=" << host.port << std::endl;
        // }

        // ofl_msg_free((struct ofl_msg_header *)msg, 0);
        return 0;
    }

    /********** 私有成员方法 **********/

    void
    OFSwitch13LearningController::SetRP()
    {
        NS_LOG_FUNCTION(this);
    }

    void
    OFSwitch13LearningController::HandshakeSuccessful(
        Ptr<const RemoteSwitch> swtch)
    {
        NS_LOG_FUNCTION(this << swtch);

        uint64_t swDpId = swtch->GetDpId();

        // 安装表项缺失流表项
        DpctlExecute(swDpId, "flow-mod cmd=add,table=0,prio=0 "
                             "apply:output=ctrl:128");
        DpctlExecute(swDpId, "set-config miss=128");

        // 初始化L2和L3表
        m_learnedInfo.insert(std::make_pair(swDpId, L2Table_t()));
        m_l3LearnedInfo.insert(std::make_pair(swDpId, L3Table_t()));

        // 手动触发处理预定义的主机信息
        // HandleHostInfo(nullptr, swtch, 0);

        //  // 触发流表规则生成
        // GenerateSwitchFlowRules(swtch);
    }

    void
    OFSwitch13LearningController::GenerateSwitchFlowRules(Ptr<const RemoteSwitch> swtch)
    {
        uint64_t dpId = swtch->GetDpId(); // 交换机DPID
        // std::cout << "\n开始为交换机 " << dpId << " 生成流表规则..." << std::endl;

        // 3.1 生成域内流量规则（高优先级：200）
        if (m_switchHosts.count(dpId))
        {
            for (const auto &host : m_switchHosts[dpId])
            {
                std::ostringstream cmd;
                cmd << "flow-mod cmd=add,table=0,prio=200,idle=30,hard=60 "
                    << "eth_type=0x0800,ip_dst=" << host.ip << " "                        // 匹配IPv4
                    << "apply:set_field=eth_dst:" << host.mac << ",output=" << host.port; // 修改目的MAC+转发
                DpctlExecute(dpId, cmd.str());
                // std::cout << "  ✅ 域内规则: " << cmd.str() << std::endl;
            }
        }

        // std::cout << "  所有交换机: " << m_switchHosts.size() << std::endl;
        // 检查是否所有交换机的主机信息都已处理完成
        // 这里设置有3个交换机，可以根据实际情况调整
        if (m_switchHosts.size() >= 3)
        {
            // std::cout << "\n所有交换机主机信息已处理完成，生成跨域规则..." << std::endl;
            GenerateCrossDomainRulesForAllSwitches();
        }
    }

    void
    OFSwitch13LearningController::GenerateSwitchFlowRules(Ptr<const RemoteSwitch> swtch, const HostInfo &newHost)
    {
        uint64_t dpId = swtch->GetDpId(); // 交换机DPID
        // std::cout << "\n开始为交换机 " << dpId << " 生成新主机流表规则..." << std::endl;

        // 3.1 仅为当前新主机生成域内流量规则（高优先级：200）
        std::ostringstream cmd;
        cmd << "flow-mod cmd=add,table=0,prio=200,idle=30,hard=60 "
            << "eth_type=0x0800,ip_dst=" << newHost.ip << " "                           // 匹配新主机IP
            << "apply:set_field=eth_dst:" << newHost.mac << ",output=" << newHost.port; // 修改目的MAC+转发
        DpctlExecute(dpId, cmd.str());
        std::cout << "  ✅ 域内规则: " << cmd.str() << std::endl;

        // std::cout << "  已收集主机数: " << m_switchHosts[dpId].size() <<"  交换机: " << m_switchHosts.size() << std::endl;
        // 检查是否所有交换机的主机信息都已处理完成（保留跨域规则触发逻辑）
        if (m_switchHosts.size() >= 3)
        {
            std::cout << "\n所有交换机主机信息已处理完成，生成跨域规则..." << std::endl;
            GenerateCrossDomainRulesForAllSwitches();
        }
    }
    void
    OFSwitch13LearningController::GenerateCrossDomainRulesForAllSwitches()
    {
        // 为所有交换机生成跨域规则
        for (const auto &switchEntry : m_switchHosts)
        {
            uint64_t dpId = switchEntry.first;

            // std::cout << "\n为交换机 " << dpId << " 生成跨域流表规则..." << std::endl;

            // 3.2 生成跨域流量规则（中优先级：190）
            for (const auto &subnetEntry : m_subnetToSwitchMap)
            {
                const std::string &targetSubnet = subnetEntry.first;
                uint64_t targetSwitchDpid = subnetEntry.second;

                // 如果目标子网属于当前交换机，则跳过（由域内规则处理）
                if (targetSwitchDpid == dpId)
                {
                    continue;
                }
                // 查找从当前交换机到目标交换机的最短路径
                uint32_t nextHopPort = FindNextHopPort(dpId, targetSwitchDpid);

                if (nextHopPort != 0)
                {
                    // 生成跨域流量规则
                    std::ostringstream cmd;
                    cmd << "flow-mod cmd=add,table=0,prio=150,idle=30,hard=60 "
                        << "eth_type=0x0800,ip_dst=" << targetSubnet << " "
                        << "apply:output=" << nextHopPort;
                    DpctlExecute(dpId, cmd.str());
                    // std::cout << "  ✅ 跨域规则: " << cmd.str() << std::endl;
                }
                else
                {
                    std::cout << "  ❌ 无法找到到交换机 " << targetSwitchDpid
                              << " 的路径，跳过子网 " << targetSubnet << std::endl;
                }
            }
        }
    }

    uint32_t
    OFSwitch13LearningController::FindNextHopPort(uint64_t srcDpid, uint64_t dstDpid)
    {
        // 使用广度优先搜索(BFS)查找从源交换机到目标交换机的最短路径
        std::map<uint64_t, uint64_t> previous; // 记录路径
        std::queue<uint64_t> queue;
        std::set<uint64_t> visited;

        queue.push(srcDpid);
        visited.insert(srcDpid);
        previous[srcDpid] = 0; // 源节点没有前驱

        while (!queue.empty())
        {
            uint64_t currentDpid = queue.front();
            queue.pop();

            // 如果找到了目标节点
            if (currentDpid == dstDpid)
            {
                // 回溯找到下一跳
                uint64_t nextHopDpid = dstDpid;
                uint64_t prevDpid = previous[dstDpid];

                // 回溯到源节点的直接邻居
                while (prevDpid != srcDpid && prevDpid != 0)
                {
                    nextHopDpid = prevDpid;
                    prevDpid = previous[prevDpid];
                }

                //  std::cout << "  ➤ 确定下一跳: " << srcDpid << " → " << nextHopDpid << std::endl;

                // 找到从源节点到下一跳节点的端口
                auto it = m_switchPortMappings.find(srcDpid);
                if (it != m_switchPortMappings.end())
                {
                    for (const auto &portMapping : it->second)
                    {
                        if (portMapping.destSwitchDpid == nextHopDpid)
                        {
                            return portMapping.outputPort;
                        }
                    }
                }
                return 0; // 未找到端口
            }

            // 探索当前节点的所有邻居
            auto it = m_switchPortMappings.find(currentDpid);
            if (it != m_switchPortMappings.end())
            {
                for (const auto &portMapping : it->second)
                {
                    uint64_t neighborDpid = portMapping.destSwitchDpid;

                    if (visited.find(neighborDpid) == visited.end())
                    {
                        visited.insert(neighborDpid);
                        previous[neighborDpid] = currentDpid;
                        queue.push(neighborDpid);
                    }
                }
            }
        }
        return 0; // 未找到路径
    }

    // 获取节点的子网位置
    ofl_err
    OFSwitch13LearningController::HandleAdhocExtNodeStatusReport(
        struct adhocl_ext_node_status_report *msg,
        Ptr<const RemoteSwitch> swtch,
        uint32_t xid)
    {
        NS_LOG_FUNCTION(this << swtch << xid);

        // 0. 校验安全性
        if (msg == nullptr)
        {
            NS_LOG_ERROR("接收到空的节点状态上报消息！");
            return -1;
        }
        // ====================== 处理位置上报 ======================
        Ipv4Address nodeIp(msg->ip_address);

        NodePositionInfo currentNode = {
            .ip = nodeIp,
            .x = msg->x,
            .y = msg->y,
            .z = msg->z};

        // 更新位置映射表
        auto it = m_nodePositionMap.find(nodeIp);
        if (it != m_nodePositionMap.end())
        {
            it->second = currentNode;
        }
        else
        {
            m_nodePositionMap.insert(std::make_pair(nodeIp, currentNode));
            std::cout << "[Controller] 新增节点位置: " << nodeIp << " ("
                      << currentNode.x << ", " << currentNode.y << ")" << std::endl;
        }
        m_nodePositionInfo.push_back(currentNode);

        // 2. 释放消息内存（这步非常重要，否则会内存泄漏）
        ofl_msg_header *msg_header = (struct ofl_msg_header *)msg;
        ofl_msg_free(msg_header, 0);

        return 0;
    }

    /**
     * 处理全新的 37 号消息：ADHOC_EXT_FLOW_STATUS_REPORT
     * 作用：从流量报告消息中提取吞吐量、延迟、抖动、丢包率，并更新链路统计
     */
    ofl_err
    OFSwitch13LearningController::HandleAdhocExtFlowStatusReport(
        struct adhocl_ext_flow_status_report *msg,
        Ptr<const RemoteSwitch> swtch,
        uint32_t xid)
    {
        NS_LOG_FUNCTION(this << swtch << xid);

        // 1. 安全校验
        if (msg == nullptr)
        {
            NS_LOG_ERROR("接收到空的流量上报消息！");
            return -1;
        }

        // 2. 提取数据
        uint16_t port = msg->port;
        uint32_t thr = msg->throughput; // Kbps
        uint32_t delay = msg->delay;    // ms
        uint32_t jitter = msg->jitter;  // ms
        double actualLoss = static_cast<double>(msg->loss_rate) / 10000.0;
        double throughput = static_cast<double>(thr);

        // 3. 更新动态阈值
        UpdateDynamicThreshold();

        // 4. 处理端口流量上报（内部决定是否注册）
        ProcessPortReport(port, throughput);

        // 5. 获取有效的链路索引（可能为-1如果未注册）
        int linkIndex = -1;
        if (IsPortRegistered(port))
        {
            linkIndex = m_portToLinkIndex[port];
        }

        // 6. 只有已注册的端口才更新统计
        if (linkIndex >= 0 && linkIndex < MAX_LINKS)
        {
            m_linkStats[linkIndex].throughput = throughput;
            m_linkStats[linkIndex].delay = static_cast<double>(delay);
            m_linkStats[linkIndex].lossRate = actualLoss;
            m_linkStats[linkIndex].jitter = static_cast<double>(jitter);

            if (!m_activeLinks[linkIndex])
            {
                m_activeLinks[linkIndex] = true;
                m_activeLinkCount++;
            }
        }

        // 7. 释放消息内存 (必须手动释放，否则会导致内存泄漏)
        ofl_msg_free((struct ofl_msg_header *)msg, 0);

        return 0;
    }

    // 实现周期性决策方法
    void OFSwitch13LearningController::PeriodicDecisionMaking()
    {
        double now = Simulator::Now().GetSeconds();
        std::cout << "\n[控制器Q学习] === 周期性决策 (时间：" << now << "s) ===" << std::endl;

        // --- 1. 获取当前环境最真实的状态 ---
        NetworkState currentState = EvaluateNetworkState();

        // --- 2. 结算上一轮动作 (如果不是第一次运行) ---
        static bool isFirstRun = true;
        static NetworkState lastState;
        static int lastAction = 0; // FIX: Initialize to prevent undefined behavior

        if (!isFirstRun)
        {
            // 计算从上次动作到现在产生的奖励
            double reward = CalculateReward();

            // NEW: Add switching cost penalty
            if (m_lastAction != -1 && m_lastAction != lastAction)
            {
                reward -= SWITCHING_COST;
                std::cout << "[切换惩罚] 动作改变 (" << m_lastAction << "->" << lastAction
                          << ")，扣除 " << SWITCHING_COST << std::endl;
            }

            // 使用上次的状态、上次的动作、当前的新状态和奖励来更新Q表
            m_networkQLearning.Update(lastState, lastAction, currentState, reward);
        }

        // --- 3. 基于当前状态做出新决策 ---
        int action = m_networkQLearning.ChooseAction(currentState);

        // --- 4. 执行动作 ---
        ExecuteSwitchingAction(action);

        // --- 5. 记录状态供下次结算使用 ---
        m_lastAction = lastAction; // Save previous action for switching cost
        lastState = currentState;
        lastAction = action;
        isFirstRun = false;

        // --- 6. 定期打印 Q 表 ---
        if (fmod(now, 10.0) < 1.0)
        {
            m_networkQLearning.PrintQTable();
        }

        // 自调度：每3秒决策一次
        Simulator::Schedule(Seconds(3.0), &OFSwitch13LearningController::PeriodicDecisionMaking, this);
    }

    void OFSwitch13LearningController::UpdateQLearning(NetworkState currentState, int action)
    {
        // 原lambda中的所有逻辑
        double reward = CalculateReward();
        NetworkState newState = EvaluateNetworkState();
        m_networkQLearning.Update(currentState, action, newState, reward);

        // 定期打印Q表
        if (fmod(Simulator::Now().GetSeconds(), 10.0) < 1.0)
        {
            m_networkQLearning.PrintQTable();
        }
    }

    // NetworkModeQLearning类实现
    NetworkModeQLearning::NetworkModeQLearning(double alpha, double gamma, double epsilon)
        : alpha(alpha), gamma(gamma), epsilon(epsilon), baseEpsilon(epsilon),
          // 状态：(距离2种 * 方差2种 * 丢包2种) = 8种状态；动作：2种(MULTI, ADHOC)
          qTable(8, std::vector<double>(ACTION_COUNT, 0.0))
    {
        InitializeQTable(); // Smart initialization
    }

    void NetworkModeQLearning::InitializeQTable()
    {
        // State encoding: [lossBit][varBit][distBit]
        // State 1 (near=1, low var=0, good loss=0): 0b001 = 1 -> bias MULTI
        qTable[1][MULTI] = 0.3;
        // State 6 (far=0, high var=1, bad loss=1): 0b110 = 6 -> bias ADHOC
        qTable[6][ADHOC] = 0.3;
        // State 7 (far=0, high var=1, bad loss=1, near=1): 0b111 = 7 -> bias ADHOC
        qTable[7][ADHOC] = 0.3;

        std::cout << "[Q表初始化] 已设置初始偏向: 状态1->MULTI, 状态6,7->ADHOC" << std::endl;
    }

    int NetworkModeQLearning::StateToId(const NetworkState &state)
    {
        // 3-bit state encoding for 8 states:
        // bit0: distance (1=near<=25, 0=far>25)
        // bit1: variance (1=high>100, 0=low<=100)
        // bit2: loss (1=bad>5%, 0=good<=5%)
        int distBit = (state.averageNodeDistance <= 25.0f) ? 1 : 0; // bit0
        int varBit = (state.distanceVariance > 100.0f) ? 1 : 0;     // bit1 (NEW)
        int lossBit = (state.maxLossRate > 0.05) ? 1 : 0;           // bit2

        // Combine into state ID (0-7)
        return (lossBit << 2) | (varBit << 1) | distBit;
    }

    // 实现网络状态评估方法
    NetworkState OFSwitch13LearningController::EvaluateNetworkState()
    {
        NetworkState state;

        // Initialize new field
        state.distanceVariance = 0.0f;

        // 1. 提取所有节点位置信息
        std::vector<NodePositionInfo> allNodes;
        for (const auto &entry : m_nodePositionMap)
        {
            allNodes.push_back(entry.second);
        }

        // 2. 节点数不足2个时，返回默认状态
        uint32_t nodeCount = allNodes.size();
        if (nodeCount < 2)
        {
            NS_LOG_WARN("当前有效节点数：" << nodeCount << "，不足2个");
            std::cout << "[网络状态评估] 节点数不足，" << std::endl;
            return state;
        }

        // 3. 计算所有节点对的欧氏距离总和
        float totalDistance = 0.0f;
        uint64_t pairCount = 0;
        std::vector<float> distances; // Store individual distances for variance calculation

        for (uint32_t i = 0; i < nodeCount; ++i)
        {
            const NodePositionInfo &nodeA = allNodes[i];
            for (uint32_t j = i + 1; j < nodeCount; ++j)
            {
                const NodePositionInfo &nodeB = allNodes[j];

                // 三维欧氏距离计算
                float dx = nodeA.x - nodeB.x;
                float dy = nodeA.y - nodeB.y;
                float dz = nodeA.z - nodeB.z;
                float singlePairDistance = sqrtf(dx * dx + dy * dy + dz * dz);

                totalDistance += singlePairDistance;
                distances.push_back(singlePairDistance);
                pairCount++;
            }
        }

        // 4. 计算平均距离
        if (pairCount > 0)
        {
            state.averageNodeDistance = totalDistance / pairCount;
        }

        // 5. Calculate distance variance (NEW)
        if (distances.size() > 1)
        {
            float sumSquaredDiff = 0.0f;
            for (float d : distances)
            {
                sumSquaredDiff += (d - state.averageNodeDistance) * (d - state.averageNodeDistance);
            }
            state.distanceVariance = sumSquaredDiff / distances.size();
        }

        // 提取链路质量：动态遍历活跃链路
        state.maxLossRate = 0.0;
        state.totalThroughput = 0.0;

        for (int i = 0; i < MAX_LINKS; ++i)
        {
            if (m_activeLinks[i])
            { // 只处理活跃链路
                state.totalThroughput += m_linkStats[i].throughput;
                if (m_linkStats[i].lossRate > state.maxLossRate)
                {
                    state.maxLossRate = m_linkStats[i].lossRate;
                }
            }
        }

        std::cout << "[状态感知] 活跃链路数: " << m_activeLinkCount
                  << " | 均距: " << state.averageNodeDistance
                  << " | 方差: " << state.distanceVariance
                  << " | 最大丢包: " << (state.maxLossRate * 100.0) << "%"
                  << " | 总吞吐: " << state.totalThroughput << " Kbps" << std::endl;
        return state;
    }

    int NetworkModeQLearning::ChooseAction(const NetworkState &state)
    {
        int stateId = StateToId(state);

        // // 1. Epsilon-Greedy 探索机制
        // // 产生一个 0 到 1 之间的随机数，如果小于 epsilon，则随机选一个动作（探索）
        // if (((double)rand() / RAND_MAX) < epsilon) {
        //     return rand() % 3; // 随机返回 0, 1, 或 2
        // }

        // ========== 关键改动1：动态计算ε值 ==========
        double dynamicEpsilon;
        if (state.maxLossRate <= 0.05)
        {                                       // 稳定态：丢包≤5%
            dynamicEpsilon = baseEpsilon * 0.5; // 基础ε*0.5，比如原0.2→0.1
        }
        else if (state.maxLossRate <= 0.2)
        {                                       // 轻度拥塞：5%<丢包≤20%
            dynamicEpsilon = baseEpsilon * 1.5; // 基础ε*1.5，比如原0.2→0.3
        }
        else
        {                                       // 重度拥塞：丢包>20%
            dynamicEpsilon = baseEpsilon * 3.0; // 基础ε*3，比如原0.2→0.6
        }
        // 限制ε的上下限，避免超出[0,1]
        dynamicEpsilon = std::min(1.0, std::max(0.0, dynamicEpsilon));

        // ========== 关键改动2：用动态ε执行探索 ==========
        if (((double)rand() / RAND_MAX) < dynamicEpsilon)
        {
            std::cout << "[Q学习-探索] 动态ε=" << dynamicEpsilon
                      << "，随机选动作（当前丢包率=" << state.maxLossRate * 100 << "%）" << std::endl;
            return rand() % ACTION_COUNT; // 随机返回 0(MULTI), 1(ADHOC)
        }

        // 2. 利用机制：从 Q 表中找当前状态下分值最高的动作索引
        int bestAction = 0;
        double maxQ = qTable[stateId][0];

        for (int a = 1; a < ACTION_COUNT; a++)
        {
            if (qTable[stateId][a] > maxQ)
            {
                maxQ = qTable[stateId][a];
                bestAction = a;
            }
        }

        return bestAction;
    }

    // 实现执行切换动作的方法
    void OFSwitch13LearningController::ExecuteSwitchingAction(int action)
    {
        switch (action)
        {
        case 0: // 切换到 MULTI
            if (m_currentMode != 0)
            { // 只有模式真的改变时才打印和执行
                std::cout << "[决策执行] 模式改变：ADHOC -> MULTI" << std::endl;
                // 执行切换到 MULTI 的逻辑...
                m_currentMode = 0; // 更新当前模式记录
            }
            break;

        case 1: // 切换到 ADHOC
            if (m_currentMode != 1)
            {
                std::cout << "[决策执行] 模式改变：MULTI -> ADHOC" << std::endl;
                SetRPtoAll();
                ChangeDeviceLogical(1);
                m_currentMode = 1; // 更新当前模式记录
            }
            break;

        case 2: // KEEP
            std::cout << "[决策执行] 保持现状，当前模式仍为: "
                      << (m_currentMode == 0 ? "MULTI" : "ADHOC") << std::endl;
            // 无需更新 m_currentMode
            break;
        }
    }

    // double OFSwitch13LearningController::CalculateReward() {
    //     double totalReward = 0.0;
    //     // 1. 修改为四条链路权重，平均分配 (0.25 * 4 = 1.0)
    //     const double linkWeights[4] = {0.25, 0.25, 0.25, 0.25};
    //     double maxLoss = 0.0;

    //     for (int i = 0; i < 4; ++i) { // 遍历 4 条链路
    //         LinkStats& s = m_linkStats[i];

    //         // --- A. 基础改善量计算 (Improvement) ---
    //         double tImp = (s.prevThroughput > 0) ? (s.throughput - s.prevThroughput) / s.prevThroughput : 0.0;
    //         double lImp = s.prevLossRate - s.lossRate;
    //         double dImp = (s.prevDelay > 0) ? (s.prevDelay - s.delay) / s.prevDelay : 0.0;

    //         // --- B. 强化丢包惩罚 (Absolute Loss Penalty) ---
    //         double lossPenalty = 0.0;
    //         if (s.lossRate > 0.05) {
    //             // 只要丢包超过 5%，就开始产生基础惩罚
    //             // 丢包越严重，惩罚呈线性或指数级增加
    //             lossPenalty = - (s.lossRate * 2.0); // 丢包 50% 会产生 -1.0 的惩罚
    //         } else {
    //             lossPenalty = 0.1; // 丢包极低时，给一点点正面奖励奖励
    //         }

    //         // --- C. 综合该链路奖励 ---
    //         // 增大丢包权比（原 0.5 -> 现 0.7），并加入绝对惩罚项
    //         // 结构：改善量(30%) + 绝对惩罚项
    //         double linkReward = (tImp * 0.1) + (lImp * 0.2) + (dImp * 0.1) + lossPenalty;

    //         totalReward += linkReward * linkWeights[i];
    //         if (s.lossRate > maxLoss) maxLoss = s.lossRate;

    //         // --- D. 更新历史值 ---
    //         s.prevThroughput = s.throughput;
    //         s.prevLossRate = s.lossRate;
    //         s.prevDelay = s.delay;
    //     }

    //     // --- E. 全局强力干预 (Critical Intervention) ---
    //     // 如果任何一条链路丢包极其严重（例如 > 50%），进一步压低总奖励
    //     if (maxLoss > 0.5) {
    //         totalReward -= 0.3;
    //     }

    //     // 归一化限制在 [-1.0, 1.0]
    //     return (totalReward > 1.0) ? 1.0 : (totalReward < -1.0 ? -1.0 : totalReward);
    // }

    std::vector<double>
    OFSwitch13LearningController::CalculateThroughputWeights()
    {
        std::vector<double> weights(MAX_LINKS, 0.0);

        // 1. 计算所有活跃链路的总吞吐量
        double totalThroughput = 0.0;
        for (int i = 0; i < MAX_LINKS; ++i)
        {
            if (m_activeLinks[i])
            {
                totalThroughput += m_linkStats[i].throughput;
            }
        }

        // 2. 边界情况：总吞吐量为0 -> 使用均匀权重
        if (totalThroughput <= 0.0 || m_activeLinkCount == 0)
        {
            double uniformWeight = (m_activeLinkCount > 0) ? 1.0 / m_activeLinkCount : 0.0;
            for (int i = 0; i < MAX_LINKS; ++i)
            {
                if (m_activeLinks[i])
                {
                    weights[i] = uniformWeight;
                }
            }
            return weights;
        }

        // 3. 正常情况：根据吞吐量比例计算权重
        for (int i = 0; i < MAX_LINKS; ++i)
        {
            if (m_activeLinks[i])
            {
                weights[i] = m_linkStats[i].throughput / totalThroughput;
            }
        }

        return weights;
    }

    double OFSwitch13LearningController::CalculateReward()
    {
        double totalReward = 0.0;
        double maxLoss = 0.0;
        const double LOSS_THRESHOLD = 0.25;

        // 1. 动态计算吞吐量加权权重
        std::vector<double> linkWeights = CalculateThroughputWeights();

        // 2. 遍历活跃链路计算奖励
        for (int i = 0; i < MAX_LINKS; ++i)
        {
            if (!m_activeLinks[i])
                continue; // 跳过非活跃链路

            LinkStats &s = m_linkStats[i];

            // A. 吞吐量/延迟改善量
            double tImp = (s.prevThroughput > 0) ? (s.throughput - s.prevThroughput) / s.prevThroughput : 0.0;
            double dImp = (s.prevDelay > 0) ? (s.prevDelay - s.delay) / s.prevDelay : 0.0;

            // B. 丢包分档处理
            double lossContribution = 0.0;
            if (s.lossRate < LOSS_THRESHOLD)
            {
                double lImp = s.prevLossRate - s.lossRate;
                lossContribution = lImp * 0.5;
            }
            else
            {
                lossContribution = -0.5 * (1 - exp(-(s.lossRate - LOSS_THRESHOLD) * 15));
            }

            // C. 单链路综合奖励
            double linkReward = (tImp * 0.2) + (dImp * 0.3) + lossContribution;
            linkReward = std::min(0.5, std::max(-1.0, linkReward));

            // D. 使用动态权重累加
            totalReward += linkReward * linkWeights[i];
            maxLoss = std::max(maxLoss, s.lossRate);

            // E. 更新历史值
            s.prevThroughput = s.throughput;
            s.prevLossRate = s.lossRate;
            s.prevDelay = s.delay;
        }

        // F. 全局惩罚
        if (maxLoss > 0.4)
        {
            totalReward -= 0.3;
        }
        else if (maxLoss > LOSS_THRESHOLD)
        {
            totalReward -= 0.15;
        }

        // 归一化
        totalReward = std::min(1.0, std::max(-1.0, totalReward));

        std::cout << "[奖励计算] 总奖励: " << totalReward
                  << " | 最大丢包: " << maxLoss * 100 << "%"
                  << " | 活跃链路: " << m_activeLinkCount << std::endl;

        return totalReward;
    }

    void NetworkModeQLearning::Update(const NetworkState &state, int action, const NetworkState &newState, double reward)
    {
        int stateId = StateToId(state);
        int newStateId = StateToId(newState);

        // Store experience in replay buffer
        Experience exp = {state, action, reward, newState};
        replayBuffer.push_back(exp);
        if (replayBuffer.size() > MAX_BUFFER_SIZE)
        {
            replayBuffer.pop_front();
        }

        // Standard Q-table update
        double qPredict = qTable[stateId][action];
        double qTarget = reward + gamma * *std::max_element(qTable[newStateId].begin(), qTable[newStateId].end());
        qTable[stateId][action] += alpha * (qTarget - qPredict); // Q 表更新公式

        std::cout << "[控制器Q学习] Q表更新 - 动作: " << action
                  << " | 奖励: " << reward
                  << " | 更新后Q值: " << qTable[stateId][action] << std::endl;

        // Replay every 10 experiences to break correlation
        if (replayBuffer.size() >= 10 && replayBuffer.size() % 10 == 0)
        {
            ReplayBatch(5);
        }
    }

    void NetworkModeQLearning::ReplayBatch(int batchSize)
    {
        if (replayBuffer.size() < static_cast<size_t>(batchSize))
            return;

        double replayAlpha = alpha * 0.5; // Smaller learning rate for replay
        for (int i = 0; i < batchSize; ++i)
        {
            int idx = rand() % replayBuffer.size();
            const Experience &exp = replayBuffer[idx];

            int stateId = StateToId(exp.state);
            int newStateId = StateToId(exp.nextState);

            double qPredict = qTable[stateId][exp.action];
            double qTarget = exp.reward + gamma * *std::max_element(qTable[newStateId].begin(), qTable[newStateId].end());
            qTable[stateId][exp.action] += replayAlpha * (qTarget - qPredict);
        }
        std::cout << "[经验回放] 已回放 " << batchSize << " 条经验" << std::endl;
    }

    void NetworkModeQLearning::PrintQTable()
    {
        std::cout << "--- [Q表状态说明：D(距离 0远1近) | V(方差 0低1高) | L(丢包 0好1差)] ---" << std::endl;
        for (int s = 0; s < 8; ++s)
        {
            int dist = s & 1;
            int var = (s >> 1) & 1;
            int loss = (s >> 2) & 1;
            printf("状态 %d (D:%d V:%d L:%d) -> MULTI:%.3f | ADHOC:%.3f\n",
                   s, dist, var, loss, qTable[s][0], qTable[s][1]);
        }
    }

    // ============== 动态阈值管理 ==============

    double
    OFSwitch13LearningController::CalculateDynamicThreshold()
    {
        if (m_portRegistry.empty())
        {
            return MIN_THROUGHPUT_THRESHOLD;
        }

        // 计算所有已注册端口的平均吞吐量
        double totalThroughput = 0.0;
        for (const auto &entry : m_portRegistry)
        {
            totalThroughput += entry.second.lastThroughput;
        }
        double avgThroughput = totalThroughput / m_portRegistry.size();

        // 动态阈值 = max(最小阈值, 平均值 × 系数)
        double threshold = std::max(MIN_THROUGHPUT_THRESHOLD,
                                    avgThroughput * THRESHOLD_COEFFICIENT);

        return threshold;
    }

    void
    OFSwitch13LearningController::UpdateDynamicThreshold()
    {
        double newThreshold = CalculateDynamicThreshold();

        // 阈值变化超过10%时才更新（避免抖动）
        if (fabs(newThreshold - m_currentThroughputThreshold) / m_currentThroughputThreshold > 0.1)
        {
            m_currentThroughputThreshold = newThreshold;
            std::cout << "[动态阈值] 更新阈值: " << m_currentThroughputThreshold << " Kbps" << std::endl;
        }
    }

    // ============== 端口状态检查 ==============

    bool
    OFSwitch13LearningController::IsPortRegistered(uint16_t port) const
    {
        return m_portToLinkIndex.find(port) != m_portToLinkIndex.end();
    }

    bool
    OFSwitch13LearningController::IsPortObserved(uint16_t port) const
    {
        return m_observedPorts.find(port) != m_observedPorts.end();
    }

    // ============== 链路索引管理 ==============

    int
    OFSwitch13LearningController::AllocateLinkIndex()
    {
        if (m_activeLinkCount >= MAX_LINKS)
        {
            NS_LOG_WARN("无法分配链路索引: 已达最大链路数 " << MAX_LINKS);
            return -1;
        }

        for (int i = 0; i < MAX_LINKS; ++i)
        {
            if (m_usedLinkIndices.find(i) == m_usedLinkIndices.end())
            {
                m_usedLinkIndices.insert(i);
                return i;
            }
        }

        return -1;
    }

    void
    OFSwitch13LearningController::DeallocateLinkIndex(int index)
    {
        m_usedLinkIndices.erase(index);
        m_activeLinks[index] = false;
        m_activeLinkCount--;
    }

    // ============== 端口注册/注销 ==============

    int
    OFSwitch13LearningController::RegisterPort(uint16_t port)
    {
        if (IsPortRegistered(port))
        {
            return m_portToLinkIndex[port];
        }

        int linkIndex = AllocateLinkIndex();
        if (linkIndex < 0)
        {
            return -1;
        }

        // 创建注册信息
        PortRegistrationInfo info;
        info.linkIndex = linkIndex;
        info.firstSeenTime = Simulator::Now().GetSeconds();
        info.lastUpdateTime = info.firstSeenTime;
        info.reportCount = 1;
        info.consecutiveLows = 0;
        info.lastThroughput = 0.0;

        m_portToLinkIndex[port] = linkIndex;
        m_portRegistry[port] = info;

        // 初始化链路统计
        m_linkStats[linkIndex] = LinkStats();

        std::cout << "[端口注册] 端口 " << port << " -> 链路索引 " << linkIndex
                  << " (活跃: " << m_activeLinkCount << "/" << MAX_LINKS << ")" << std::endl;

        return linkIndex;
    }

    void
    OFSwitch13LearningController::UnregisterPort(uint16_t port)
    {
        auto it = m_portRegistry.find(port);
        if (it == m_portRegistry.end())
        {
            return;
        }

        int linkIndex = it->second.linkIndex;

        std::cout << "[端口注销] 端口 " << port << " (链路索引 " << linkIndex << ")" << std::endl;

        DeallocateLinkIndex(linkIndex);
        m_portToLinkIndex.erase(port);
        m_portRegistry.erase(it);
    }

    // ============== 端口流量处理核心逻辑 ==============

    void
    OFSwitch13LearningController::ProcessPortReport(uint16_t port, double throughput)
    {
        double now = Simulator::Now().GetSeconds();

        // 情况1：端口已注册
        if (IsPortRegistered(port))
        {
            auto &info = m_portRegistry[port];
            info.lastUpdateTime = now;
            info.reportCount++;
            info.lastThroughput = throughput;

            if (throughput < m_currentThroughputThreshold)
            {
                info.consecutiveLows++;

                // 连续N次低于阈值，注销端口
                if (info.consecutiveLows >= INACTIVE_COUNT_THRESHOLD)
                {
                    std::cout << "[端口失活] 端口 " << port << " 连续 " << info.consecutiveLows
                              << " 次低于阈值 (" << throughput << " < " << m_currentThroughputThreshold << ")" << std::endl;
                    UnregisterPort(port);
                }
            }
            else
            {
                // 达到阈值，重置计数
                info.consecutiveLows = 0;
            }
            return;
        }

        // 情况2：新端口，超过阈值直接注册（移除观察步骤）
        if (throughput >= m_currentThroughputThreshold)
        {
            int linkIndex = RegisterPort(port);
            if (linkIndex >= 0)
            {
                // 更新注册信息
                m_portRegistry[port].lastThroughput = throughput;
                std::cout << "[端口注册] 端口 " << port << " 直接注册"
                          << " (吞吐: " << throughput << " Kbps >= 阈值: " << m_currentThroughputThreshold << ")" << std::endl;
            }
        }
        else
        {
            NS_LOG_DEBUG("忽略低吞吐端口 " << port << " (吞吐: " << throughput << " < 阈值: " << m_currentThroughputThreshold << ")");
        }
    }

    // ============== 失活端口检查（可选，定期调用）=============

    void
    OFSwitch13LearningController::CheckInactivePorts()
    {
        std::vector<uint16_t> toRemove;

        for (auto &entry : m_observedPorts)
        {
            double now = Simulator::Now().GetSeconds();
            // 观察列表超过60秒未更新，移除
            if (now - entry.second.lastSeenTime > 60.0)
            {
                toRemove.push_back(entry.first);
            }
        }

        for (uint16_t port : toRemove)
        {
            std::cout << "[观察超时] 端口 " << port << " 从观察列表移除" << std::endl;
            m_observedPorts.erase(port);
        }
    }

} // namespace ns3
#endif
