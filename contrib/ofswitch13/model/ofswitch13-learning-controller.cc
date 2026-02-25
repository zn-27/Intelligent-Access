
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
#include "ns3/mobility-model.h"
#include <iomanip> 


NS_LOG_COMPONENT_DEFINE("OFSwitch13LearningController");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(OFSwitch13LearningController);

// const float OFSwitch13LearningController::DISTANCE_THRESHOLD = 50.0f;
OFSwitch13LearningController::OFSwitch13LearningController()
                 : m_networkQLearning(0.1, 0.9, 0.1) // alpha, gamma, epsilon
{
     // ==================== 初始化交换机互联端口映射（仅硬编码端口连接关系）====================
    // 交换机连接拓扑：
    // sw1 (DPID:1) <--端口3--> sw2 (DPID:2)
    // sw1 (DPID:1) <--端口4--> sw3 (DPID:3)
    // sw2 (DPID:2) <--端口2--> sw1 (DPID:1)
    // sw2 (DPID:2) <--端口3--> sw3 (DPID:3)
    // sw3 (DPID:3) <--端口3--> sw1 (DPID:1)
    // sw3 (DPID:3) <--端口2--> sw2 (DPID:2)
    

    // // sw1的互联端口映射
    m_switchPortMappings[0x0000000000000001] = {
        {0x0000000000000002, 3},  // 到sw2，走端口3
        {0x0000000000000003, 4},  // 到sw3，走端口4
    };

    // sw2的互联端口映射
    m_switchPortMappings[0x0000000000000002] = {
        {0x0000000000000001, 2},  // 到sw1，走端口2
        {0x0000000000000003, 3} // 到sw3，走端口3
    };

    // sw3的互联端口映射
    m_switchPortMappings[0x0000000000000003] = {
        {0x0000000000000001, 3},  // 到sw1，走端口3
        {0x0000000000000002, 2},   // 到sw2，走端口2
    };

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
    m_l3LearnedInfo.clear();  // 清空L3层学习信息
    m_arpTable.clear();
    m_switchHosts.clear();// 交换机主机信息
    m_switchPortMappings.clear();// 交换机互联端口映射
    m_subnetToSwitchMap.clear();// 子网-交换机映射
    m_nodePositionMap.clear(); // 清空节点位置信息
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
    ChangeDeviceLogical(1);//1打开自组织模式，2... 3...
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
        Ipv4Address arpSenderIp, arpTargetIp;  // ARP包专用IP变量
        
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
            if(type == 2054)
            {
                NS_LOG_DEBUG("检测到ARP包");
                // std::cout <<"检测到ARP包"<< std::endl;
                
                // std::cout << "[ARP包] 交换机DPID: " << swDpId << " 源MAC: " << src48 << " 目的MAC: " << dst48 
                //           << " 入端口: " << inPort << std::endl;

                if (msg->data_length >= 42)   // 确保数据长度足够（以太网头14 + ARP头28）
                {
                    uint8_t *arpHeader = (uint8_t *)msg->data + 14;  // 跳过以太网头
                    // 解析ARP操作码（偏移6-7字节：1=请求，2=响应）
                    uint16_t arpOp = (arpHeader[6] << 8) | arpHeader[7];
                    // 解析发送端IP和目标端IP
                    arpSenderIp.Set( (arpHeader[14] << 24) | (arpHeader[15] << 16) | (arpHeader[16] << 8) | arpHeader[17] );
                    arpTargetIp.Set( (arpHeader[24] << 24) | (arpHeader[25] << 16) | (arpHeader[26] << 8) | arpHeader[27] );
                    // 处理ARP请求（操作码1）
                    if (arpOp == 1)
                    {
                        std::cout <<  "[ARP包] 交换机DPID: " << swDpId << "  ARP操作码: " << arpOp << " | 发送端IP: " << arpSenderIp 
                            <<" 目标端IP: " << arpTargetIp  << std::endl; 
                        // 判断目标IP是否为网关IP（通过查询ARP表）
                        bool isGwArp = false;
                        for (const auto& arpEntry : m_arpTable) {
                        if (arpEntry.first == arpTargetIp) {
                            isGwArp = true;
                            break;
                            }
                        }
                        if (isGwArp)
                        {
                            // std::cout << "  → 准备返回响应" << std::endl;

                            // 构造ARP响应包（以太网头 + ARP响应头）
                            uint8_t* arpReply = (uint8_t*)xmalloc(46);  
                            memset(arpReply, 0, 46);

                            // 构造以太网头
                            src48.CopyTo(arpReply);                // 目的MAC：请求端主机MAC
                            Mac48Address gwMac;


                            // 获取交换机真实网关MAC
                            auto itGwMac = m_arpTable.find(arpTargetIp);
                            if (itGwMac != m_arpTable.end()) {
                                gwMac = itGwMac->second; // 使用预配置的网关MAC
                                std::cout << "使用预配置的网关MAC: " << gwMac << std::endl;
                                } else {
                                     std::cout << "ARP表中未找到网关IP " << arpTargetIp << " 的MAC，使用默认值";
                                    gwMac = Mac48Address("00:00:00:00:01:00"); //  fallback默认MAC
                            }

                            gwMac.CopyTo(arpReply + 6);            // 源MAC：网关MAC
                            arpReply[12] = 0x08; arpReply[13] = 0x06; // 以太网类型：ARP

                            // 构造ARP响应头
                            uint8_t* arpReplyHeader = arpReply + 14;
                            arpReplyHeader[0] = 0x00; arpReplyHeader[1] = 0x01; // 硬件类型：以太网
                            arpReplyHeader[2] = 0x08; arpReplyHeader[3] = 0x00; // 协议类型：IPv4
                            arpReplyHeader[4] = 0x06; // 硬件地址长度
                            arpReplyHeader[5] = 0x04; // 协议地址长度
                            arpReplyHeader[6] = 0x00; arpReplyHeader[7] = 0x02; // 操作码：响应

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

                            struct ofl_action_output* a = (struct ofl_action_output*)xmalloc(sizeof(struct ofl_action_output));
                            a->header.type = OFPAT_OUTPUT;
                            a->port = inPort; // 从入端口返回响应
                            a->max_len = 0;

                            reply.actions_num = 1;
                            reply.actions = (struct ofl_action_header**)&a;

                            SendToSwitch(swtch, (struct ofl_msg_header*)&reply, xid);
                            free(a);
                            free(arpReply);
                            return 0;
                        }
                    }
                }
                return 0;
            }   
            // if (ntohs(type) == 0x0800)  // IPv4类型
            else if (type == 2048)  // IPv4类型
            {
                isIpv4 = true;
                
                // 提取源IP
                struct ofl_match_tlv *ipSrc = oxm_match_lookup(OXM_OF_IPV4_SRC, (struct ofl_match *)msg->match);
                if (ipSrc != nullptr)
                {
                    uint32_t ipAddrNet = *(uint32_t *)ipSrc->value;  // 网络字节序
                    uint32_t ipAddrHost = ntohl(ipAddrNet);          // 转换为主机字节序
                    srcIp.Set(ipAddrHost);
                }
                
                // 提取目的IP
                struct ofl_match_tlv *ipDst = oxm_match_lookup(OXM_OF_IPV4_DST, (struct ofl_match *)msg->match);
                if (ipDst != nullptr)
                {
                    uint32_t ipAddrNet = *(uint32_t *)ipDst->value;  // 网络字节序
                    uint32_t ipAddrHost = ntohl(ipAddrNet);          // 转换为主机字节序
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
            // }
            return 0;

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
                std::cout << "L2学习: MAC地址 " << src48 << " 对应端口 " << inPort << std::endl;
                
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
                      << ",ip_dst=" << srcIp  // 使用逗号
                      << " apply:output=" << inPort;

                    std::cout << "L3 Flow-Mod Command: " << cmdL3.str() << std::endl;
                    DpctlExecute(swDpId, cmdL3.str());
                }
                else if (itSrcL3->second.second != inPort)
                {
                    NS_ASSERT_MSG(false, "L3转发表不一致");
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


//获取节点的子网信息（ip-mac-port）,可扩充位置
ofl_err
OFSwitch13LearningController::HandleAdhocExtStaInfo(
      struct adhocl_ext_stainfo *msg,  
      Ptr<const RemoteSwitch> swtch, 
      uint32_t xid)
  {
     // 添加空指针检查
    if (!msg) {
        std::cout << "HandleAdhocExtStaInfo: msg is null" << std::endl;
        return 0;
    }
    if (!swtch) {
        std::cout << "HandleAdhocExtStaInfo: swtch is null" << std::endl;
        return 0;
    }
      NS_LOG_FUNCTION(this << swtch << xid);
      uint64_t dpId = swtch->GetDpId();   // 交换机DPID

       // 如果需要处理真实的msg数据，需要确保msg不为空
    if (msg == nullptr) {
        std::cout << "消息为空" << std::endl;
    }
    // 解析单个STA信息
    HostInfo host;
    host.ip = Ipv4Address((msg->ip_address));  // 注意网络字节序转换
    host.mac = Mac48Address();  // 需要从uint64_t转换为Mac48Address
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

    std::cout << "交换机DPID(" << dpId << ") 域内主机信息：" << std::endl;
    std::cout << "  主机: IP=" << host.ip 
              << ", MAC=" << host.mac << ", 端口=" << host.port << std::endl;

    // 自动识别网段
    uint32_t ipHost = host.ip.Get();
    uint32_t subnetMask = 0xFFFFFF00;   // /24子网掩码
    uint32_t networkIpHost = ipHost & subnetMask;
    Ipv4Address networkIp(networkIpHost);
    
    std::ostringstream os;
    networkIp.Print(os);
    std::string subnetStr = os.str() + "/24";
    
    // 更新子网-交换机映射表
    m_subnetToSwitchMap[subnetStr] = dpId;
    
    std::cout << "  → 自动识别网段：" << subnetStr << " （对应交换机DPID：" << dpId << "）" << std::endl;
    
    std::cout << "\n当前完整的子网-交换机映射表：" << std::endl;
    for (const auto& entry : m_subnetToSwitchMap) {
        std::cout << "  " << entry.first << " → 交换机DPID：" << entry.second << std::endl;
    }

// 触发流表规则生成：传递当前新主机
    GenerateSwitchFlowRules(swtch, host); // 传入当前新主机

    ofl_msg_free((struct ofl_msg_header *)msg, 0);
    return 0;
  }

 ofl_err
  OFSwitch13LearningController::HandleHostInfo(
    //   struct ofl_msg_host_info *msg, 
      void *msg,
      Ptr<const RemoteSwitch> swtch, 
      uint32_t xid)
  {
      NS_LOG_FUNCTION(this << swtch << xid);
      uint64_t dpId = swtch->GetDpId();   // 交换机DPID

    // ==================== 初始化交换机-域内主机映射 ====================
    std::vector<HostInfo> hostList; // 声明hostList变量
    if (dpId == 0x0000000000000001) { // sw1（10.1.1.0/24）
        hostList = {
            {Ipv4Address("10.1.1.1"), Mac48Address("00:00:00:00:00:0f"), 1},
            {Ipv4Address("10.1.1.2"), Mac48Address("00:00:00:00:00:10"), 1},
            {Ipv4Address("10.1.1.3"), Mac48Address("00:00:00:00:00:11"), 1},
            {Ipv4Address("10.1.1.4"), Mac48Address("00:00:00:00:00:12"), 2}
        };
    } else if (dpId == 0x0000000000000002) { // sw2（10.2.1.0/24）
        hostList = {
            {Ipv4Address("10.2.1.1"), Mac48Address("00:00:00:00:00:17"), 1},
            {Ipv4Address("10.2.1.2"), Mac48Address("00:00:00:00:00:18"), 1}
        };
    } else if (dpId == 0x0000000000000003) { // sw3（10.3.1.0/24）
        hostList = {
            {Ipv4Address("10.3.1.1"), Mac48Address("00:00:00:00:00:1a"), 1},
            {Ipv4Address("10.3.1.2"), Mac48Address("00:00:00:00:00:1b"), 1},
            {Ipv4Address("10.3.1.3"), Mac48Address("00:00:00:00:00:2c"), 1},

        };
    } else {
        std::cout << "  → 未知交换机DPID，无主机信息" << std::endl;
        return 0;
    }

    m_switchHosts[dpId] = hostList;  // 写入域内主机表


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
    std::cout << "\n开始为交换机 " << dpId << " 生成流表规则..." << std::endl;

    // 3.1 生成域内流量规则（高优先级：200）
    if (m_switchHosts.count(dpId)) {
        for (const auto& host : m_switchHosts[dpId]) {
            std::ostringstream cmd;
            cmd << "flow-mod cmd=add,table=0,prio=200,idle=30,hard=60 "
                << "eth_type=0x0800,ip_dst=" << host.ip << " "  // 匹配IPv4
                << "apply:set_field=eth_dst:" << host.mac << ",output=" << host.port; // 修改目的MAC+转发
            DpctlExecute(dpId, cmd.str());
            // std::cout << "  ✅ 域内规则: " << cmd.str() << std::endl;
        }
    }

    std::cout << "  所有交换机: " << m_switchHosts.size() << std::endl;
    // 检查是否所有交换机的主机信息都已处理完成
    // 这里设置有3个交换机，可以根据实际情况调整
    if (m_switchHosts.size() >= 3){
        std::cout << "\n所有交换机主机信息已处理完成，生成跨域规则..." << std::endl;
        GenerateCrossDomainRulesForAllSwitches();
    }
}
void
OFSwitch13LearningController::GenerateSwitchFlowRules(Ptr<const RemoteSwitch> swtch, const HostInfo& newHost)
{
    uint64_t dpId = swtch->GetDpId(); // 交换机DPID
    std::cout << "\n开始为交换机 " << dpId << " 生成新主机流表规则..." << std::endl;

    // 3.1 仅为当前新主机生成域内流量规则（高优先级：200）
    std::ostringstream cmd;
    cmd << "flow-mod cmd=add,table=0,prio=200,idle=30,hard=60 "
        << "eth_type=0x0800,ip_dst=" << newHost.ip << " "  // 匹配新主机IP
        << "apply:set_field=eth_dst:" << newHost.mac << ",output=" << newHost.port; // 修改目的MAC+转发
    DpctlExecute(dpId, cmd.str());
    std::cout << "  ✅ 域内规则: " << cmd.str() << std::endl;

    std::cout << "  已收集主机数: " << m_switchHosts[dpId].size() << std::endl;
    std::cout << "  所有交换机: " << m_switchHosts.size() << std::endl;
    // 检查是否所有交换机的主机信息都已处理完成（保留跨域规则触发逻辑）
    if (m_switchHosts.size() >= 3){
        std::cout << "\n所有交换机主机信息已处理完成，生成跨域规则..." << std::endl;
        GenerateCrossDomainRulesForAllSwitches();
    }
}
void 
OFSwitch13LearningController::GenerateCrossDomainRulesForAllSwitches()
{
    // 为所有交换机生成跨域规则
    for (const auto& switchEntry : m_switchHosts) {
        uint64_t dpId = switchEntry.first;
        
        std::cout << "\n为交换机 " << dpId << " 生成跨域流表规则..." << std::endl;
        
        // 3.2 生成跨域流量规则（中优先级：190）
        for (const auto& subnetEntry : m_subnetToSwitchMap) {
            const std::string& targetSubnet = subnetEntry.first;
            uint64_t targetSwitchDpid = subnetEntry.second;
        
            // 如果目标子网属于当前交换机，则跳过（由域内规则处理）
            if (targetSwitchDpid == dpId) {
                continue;
            }
            // 查找从当前交换机到目标交换机的最短路径
            uint32_t nextHopPort = FindNextHopPort(dpId, targetSwitchDpid);
            
            if (nextHopPort != 0) {
                // 生成跨域流量规则
                std::ostringstream cmd;
                cmd << "flow-mod cmd=add,table=0,prio=150,idle=30,hard=60 "
                    << "eth_type=0x0800,ip_dst=" << targetSubnet << " "
                    << "apply:output=" << nextHopPort;
                DpctlExecute(dpId, cmd.str());
                std::cout << "  ✅ 跨域规则: " << cmd.str() << std::endl;
            } else {
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
    
    while (!queue.empty()) {
        uint64_t currentDpid = queue.front();
        queue.pop();
        
        // 如果找到了目标节点
        if (currentDpid == dstDpid) {
            // 回溯找到下一跳
            uint64_t nextHopDpid = dstDpid;
            uint64_t prevDpid = previous[dstDpid];
            
            // 回溯到源节点的直接邻居
            while (prevDpid != srcDpid && prevDpid != 0) {
                nextHopDpid = prevDpid;
                prevDpid = previous[prevDpid];
            }

            //  std::cout << "  ➤ 确定下一跳: " << srcDpid << " → " << nextHopDpid << std::endl;
            
            // 找到从源节点到下一跳节点的端口
            auto it = m_switchPortMappings.find(srcDpid);
            if (it != m_switchPortMappings.end()) {
                for (const auto& portMapping : it->second) {
                    if (portMapping.destSwitchDpid == nextHopDpid) {
                        return portMapping.outputPort;
                    }
                }
            }
            return 0; // 未找到端口
        }
        
        // 探索当前节点的所有邻居
        auto it = m_switchPortMappings.find(currentDpid);
        if (it != m_switchPortMappings.end()) {
            for (const auto& portMapping : it->second) {
                uint64_t neighborDpid = portMapping.destSwitchDpid;
                
                if (visited.find(neighborDpid) == visited.end()) {
                    visited.insert(neighborDpid);
                    previous[neighborDpid] = currentDpid;
                    queue.push(neighborDpid);
                }
            }
        }
    }
    return 0; // 未找到路径
}




//获取节点的子网信息（ip-mac-port）,可扩充位置
ofl_err
OFSwitch13LearningController::HandleAdhocExtNodeStatusReport(
    struct adhocl_ext_node_status_report *msg, 
    Ptr<const RemoteSwitch> swtch, 
    uint32_t xid)
  {

   std::cout << "\n===== 开始处理节点位置上报 =====" << std::endl;
    NS_LOG_FUNCTION(this << swtch << xid);

    // 校验入参合法性（避免空指针）
    if (msg == nullptr) {
        NS_LOG_ERROR("接收到空的节点状态上报消息！");
        std::cerr << "错误：上报消息为空，无法处理" << std::endl;
        std::cout << "===== 节点位置上报处理失败 =====" << std::endl;
        return -1; // 返回错误码
    }

    // 1. 从 msg 中提取动态节点信息（完全消除硬编码）
    Ipv4Address nodeIp(msg->ip_address); // 从消息中解析IP
    float nodeX = msg->x;                // 动态获取X坐标
    float nodeY = msg->y;                // 动态获取Y坐标
    float nodeZ = msg->z;                // 动态获取Z坐标

    // 2. 封装为 NodePositionInfo 结构体
    NodePositionInfo currentNode = {
        .ip = nodeIp,
        .x = nodeX,
        .y = nodeY,
        .z = nodeZ
    };

     // 3. 存入/更新 m_nodePositionMap（IP为唯一键）
    auto it = m_nodePositionMap.find(currentNode.ip);
    if (it != m_nodePositionMap.end()) {
        // 节点已存在，更新位置
        it->second = currentNode;
        // std::cout << "更新节点 [IP:" << currentNode.ip << "] 位置：(" 
        //           << currentNode.x << "," << currentNode.y << "," << currentNode.z << ")" << std::endl;
    } else {
        // 节点不存在，插入新记录
        m_nodePositionMap.insert(std::make_pair(currentNode.ip, currentNode));
        // std::cout << "新增节点 [IP:" << currentNode.ip << "] 位置：(" 
        //           << currentNode.x << "," << currentNode.y << "," << currentNode.z << ")" << std::endl;
        // std::cout << "新增节点 [IP:" << currentNode.ip << "] 位置：(" 
        //   << std::fixed << std::setprecision(2) << currentNode.x << "," 
        //   << std::fixed << std::setprecision(2) << currentNode.y << "," 
        //   << std::fixed << std::setprecision(2) << currentNode.z << ")" << std::endl;
    }

    // 恢复默认格式
    // std::cout.unsetf(std::ios::fixed);

    // 可选：将节点信息存入列表（保留原逻辑）
    m_nodePositionInfo.push_back(currentNode);

    // 4. 释放消息内存（避免内存泄漏）
    ofl_msg_free((struct ofl_msg_header *)msg, 0);

    // std::cout << "===== 节点位置上报处理完成 =====" << std::endl;
    return 0; // 返回成功码
  }



// 实现周期性决策方法
void OFSwitch13LearningController::PeriodicDecisionMaking() {
    std::cout << "\n[控制器Q学习] === 周期性决策 (当前时间：" << Simulator::Now().GetSeconds() << "s) ===" << std::endl;
    
    // 1. 评估当前网络状态
    NetworkState currentState = EvaluateNetworkState();
    
    // 2. 选择动作
    int action = m_networkQLearning.ChooseAction(currentState);
    
    // 3. 执行动作
    ExecuteSwitchingAction(action);
    
    // 4. 计算奖励并更新Q表
    // 4. 替换lambda：直接调度成员函数（核心修改）
    Simulator::Schedule(
        Seconds(0.5),                  // 延迟时间（保留原0.5秒）
        &OFSwitch13LearningController::UpdateQLearning,  // 成员函数指针
        this,                          // 当前对象指针（必须传）
        currentState,                  // 原lambda捕获的currentState
        action                         // 原lambda捕获的action
    );
    //  // 2. 核心：自调度实现周期性调用（每3秒执行一次）
    // Simulator::Schedule(Seconds(2.0), // 下次调用间隔
    //                     &OFSwitch13LearningController::PeriodicDecisionMaking, 
    //                     this); // 注意：这里传 this 而非外部的 controllerApp
}

void OFSwitch13LearningController::UpdateQLearning(NetworkState currentState, int action) {
    // 原lambda中的所有逻辑
    double reward = CalculateReward();
    NetworkState newState = EvaluateNetworkState();
    m_networkQLearning.Update(currentState, action, newState, reward);
    
    // 定期打印Q表
    if (fmod(Simulator::Now().GetSeconds(), 10.0) < 1.0) {
        m_networkQLearning.PrintQTable();
    }
}

// NetworkModeQLearning类实现
NetworkModeQLearning::NetworkModeQLearning(double alpha, double gamma, double epsilon)
    : alpha(alpha), gamma(gamma), epsilon(epsilon),
      qTable(2, std::vector<double>(2, 0.0)) // 直接初始化：2个状态，每个状态2个动作，初始值0.0
{
    
}

int NetworkModeQLearning::StateToId(const NetworkState& state) {
    // 根据平均距离判断状态：0=远距离，1=近距离
    return (state.averageNodeDistance <= OFSwitch13LearningController::DISTANCE_THRESHOLD) ? 1 : 0;
}



// 实现网络状态评估方法
NetworkState OFSwitch13LearningController::EvaluateNetworkState() {
    NetworkState state;
    
    // 1. 提取所有节点位置信息
    std::vector<NodePositionInfo> allNodes;
    for (const auto& entry : m_nodePositionMap) {
        allNodes.push_back(entry.second);
    }

    // 2. 节点数不足2个时，返回默认状态
    uint32_t nodeCount = allNodes.size();
    if (nodeCount < 2) {
        NS_LOG_WARN("当前有效节点数：" << nodeCount << "，不足2个");
        std::cout << "[网络状态评估] 节点数不足，" << std::endl;
        return state;
    }

    // 3. 计算所有节点对的欧氏距离总和
    float totalDistance = 0.0f;
    uint64_t pairCount = 0;

    for (uint32_t i = 0; i < nodeCount; ++i) {
        const NodePositionInfo& nodeA = allNodes[i];
        for (uint32_t j = i + 1; j < nodeCount; ++j) {
            const NodePositionInfo& nodeB = allNodes[j];

            // 三维欧氏距离计算
            float dx = nodeA.x - nodeB.x;
            float dy = nodeA.y - nodeB.y;
            float dz = nodeA.z - nodeB.z;
            float singlePairDistance = sqrtf(dx*dx + dy*dy + dz*dz);

            totalDistance += singlePairDistance;
            pairCount++;
        }
    }

    // 4. 计算平均距离
    if (pairCount > 0) {
        state.averageNodeDistance = totalDistance / pairCount;
    }
    std::cout << "[网络状态评估] 平均节点距离: " << state.averageNodeDistance << std::endl;

    return state;
}

int NetworkModeQLearning::ChooseAction(const NetworkState& state) {
    // int stateId = StateToId(state);
    int action = 2; // 默认动作：保持当前模式

    // 基于距离阈值决策模式
    if (state.averageNodeDistance <= OFSwitch13LearningController::DISTANCE_THRESHOLD) {
        // 适合多中心模式
        action = 1; // 切换到MULTI

    } else {
        // 适合无中心模式
        action = 1; // 切换到ADHOC
    }

    return action;
}

// 实现执行切换动作的方法
void OFSwitch13LearningController::ExecuteSwitchingAction(int action) {
    std::string actionNames[] = {"MULTI_TO_ADHOC", "ADHOC_TO_MULTI", "KEEP_MODE"};
    std::cout << "[控制器Q学习] 执行切换: " << actionNames[action] << std::endl;
    
    switch (action) {
        case 0: // ADHOC_TO_MULTI
            std::cout << "[控制器Q学习] 切换到MULTI模式" << std::endl;
            // 实际切换逻辑
            break;
        case 1: // MULTI_TO_ADHOC
            std::cout << "[控制器Q学习] 切换到ADHOC模式" << std::endl;
            // SetRPtoAll();
            ChangeDeviceLogical(1);//1打开自组织模式，2... 3...
            // 实际切换逻辑
            break;
        // case 2: // KEEP_MODE
        //     std::cout << "[控制器Q学习] 保持当前模式" << std::endl;
        //     break;
        default:
            std::cout << "[Q学习] 保持当前组网模式" << std::endl;
            break;
    }
}


// 实现奖励计算方法
double OFSwitch13LearningController::CalculateReward() {
    // 基于网络性能计算奖励值
    // 可以使用吞吐量、丢包率、延迟等指标
    double reward = 0.0;
    
    // 示例实现，需要根据实际性能监控数据进行计算
    /*
    // 获取流量统计数据
    // 计算吞吐量、丢包率改进等
    reward += throughputImprovement * 0.3;      // 吞吐量权重 30%
    reward += lossRateImprovement * 0.5;        // 丢包率权重 50%
    reward += delayImprovement * 0.2;           // 延迟权重 20%
    */
    
    return reward;
}

void NetworkModeQLearning::Update(const NetworkState& state, int action, const NetworkState& newState, double reward) {
    int stateId = StateToId(state);
    int newStateId = StateToId(newState);

    double qPredict = qTable[stateId][action];
    double qTarget = reward + gamma * *std::max_element(qTable[newStateId].begin(), qTable[newStateId].end());
    qTable[stateId][action] += alpha * (qTarget - qPredict);  // Q 表更新公式

    std::cout << "[控制器Q学习] Q表更新 - 动作: " << action 
              << " | 奖励: " << reward 
              << " | 更新后Q值: " << qTable[stateId][action] << std::endl;
}

void NetworkModeQLearning::PrintQTable() {
    std::cout << "[控制器Q学习] 当前Q表:" << std::endl;
    std::cout << "  状态定义：0=低密度，1=高密度" << std::endl;
    std::cout << "  动作定义：0=ADHOC（无中心），1=MULTI（多中心）" << std::endl;
    for (int stateId = 0; stateId < 2; ++stateId) {
        std::cout << "  状态 " << stateId << ": "
                  << qTable[stateId][0] << " | "  // 动作0的Q值
                  << qTable[stateId][1] << std::endl;  // 动作1的Q值
    }
    std::cout << std::endl;
}



} // namespace ns3
#endif 

// =======
// /* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// /*
//  * 本程序为自由软件，您可以重新分发和/或修改
//  * 本程序遵循 GNU 通用公共许可证版本 2 的条款，该许可证由
//  * 自由软件基金会发布；
//  *
//  * 发布本程序的目的是希望它能发挥作用，但
//  * 不提供任何明示或暗示的担保，包括但不限于
//  * 适销性担保和特定用途适用性担保。如需了解更多细节，
//  * 请查阅 GNU 通用公共许可证。
//  *
//  * 您应已收到一份 GNU 通用公共许可证的副本，
//  * 若未收到，请向自由软件基金会（Free Software Foundation, Inc.）
//  * 邮寄信件，地址：美国马萨诸塞州波士顿市坦普尔广场 59 号 330 室，邮编：02111-1307
//  *
//  * 作者：卢西亚诺·沙维斯（Luciano Chaves），邮箱：luciano@lrc.ic.unicamp.br
//  */

// // 仅在定义了 NS3_OFSWITCH13 宏时才编译以下代码
// #ifdef NS3_OFSWITCH13

// #include "ofswitch13-learning-controller.h"
// #include "ns3/ipv4-address.h"

// NS_LOG_COMPONENT_DEFINE("OFSwitch13LearningController");

// namespace ns3
// {

//   NS_OBJECT_ENSURE_REGISTERED(OFSwitch13LearningController);

//   /********** 公共成员方法 ***********/
//   OFSwitch13LearningController::OFSwitch13LearningController()
//   {
//     // // ==================== 初始化交换机-跨域子网路由 ====================
//     // // sw1的跨域路由：直接指定子网对应的端口
//     // m_switchSubnets[0x0000000000000001] = {
//     //     {"10.2.1.0/24", Mac48Address("02:06:00:00:00:00:00:09"), 3},  // 10.2.1.0/24子网走端口3
//     //     {"10.3.1.0/24", Mac48Address("02:06:00:00:00:00:00:0e"), 4}   // 10.3.1.0/24子网走端口4
//     // };

//     // // sw2的跨域路由：直接指定子网对应的端口
//     // m_switchSubnets[0x0000000000000002] = {
//     //     {"10.1.1.0/24", Mac48Address("02:06:00:00:00:00:00:0a"), 2},   // 10.1.1.0/24子网走端口2
//     //     {"10.3.1.0/24", Mac48Address("02:06:00:00:00:00:00:0b"), 3}   // 10.3.1.0/24子网走端口3
//     // };

//     // // sw3的跨域路由：直接指定子网对应的端口
//     // m_switchSubnets[0x0000000000000003] = {
//     //     {"10.1.1.0/24", Mac48Address("02:06:00:00:00:00:00:0c"), 3},   // 10.1.1.0/24子网走端口3
//     //     {"10.2.1.0/24", Mac48Address("02:06:00:00:00:00:00:0d"), 2}   // 10.2.1.0/24子网走端口2
//     // };

//     // ==================== 初始化交换机互联端口映射（仅硬编码端口连接关系）====================
//     // 交换机连接拓扑：
//     // sw1 (DPID:1) <--端口3--> sw2 (DPID:2)
//     // sw1 (DPID:1) <--端口4--> sw3 (DPID:3)
//     // sw2 (DPID:2) <--端口2--> sw1 (DPID:1)
//     // sw2 (DPID:2) <--端口3--> sw3 (DPID:3)
//     // sw3 (DPID:3) <--端口3--> sw1 (DPID:1)
//     // sw3 (DPID:3) <--端口2--> sw2 (DPID:2)

//     // sw1的互联端口映射
//     m_switchPortMappings[0x0000000000000001] = {
//         {0x0000000000000002, 3}, // 到sw2，走端口3
//         {0x0000000000000003, 4}  // 到sw3，走端口4
//     };

//     // sw2的互联端口映射
//     m_switchPortMappings[0x0000000000000002] = {
//         {0x0000000000000001, 2}, // 到sw1，走端口2
//         {0x0000000000000003, 3}  // 到sw3，走端口3
//     };

//     // sw3的互联端口映射
//     m_switchPortMappings[0x0000000000000003] = {
//         {0x0000000000000001, 3}, // 到sw1，走端口3
//         {0x0000000000000002, 2}  // 到sw2，走端口2
//     };

//     // 手动触发处理预定义的主机信息
//     // HandleHostInfo(nullptr, swtch, 0);

//     NS_LOG_FUNCTION(this);
//   }

//   OFSwitch13LearningController::~OFSwitch13LearningController()
//   {
//     NS_LOG_FUNCTION(this);
//   }

//   TypeId
//   OFSwitch13LearningController::GetTypeId(void)
//   {
//     static TypeId tid = TypeId("ns3::OFSwitch13LearningController")
//                             .SetParent<OFSwitch13Controller>()
//                             .SetGroupName("OFSwitch13")
//                             .AddConstructor<OFSwitch13LearningController>();
//     return tid;
//   }

//   void
//   OFSwitch13LearningController::SetRoutingPriority()
//   {
//     std::cout << "设置routinglist优先级中-----------" << std::endl;
//     NS_LOG_FUNCTION(this);
//     SetRP();
//   }

//   void
//   OFSwitch13LearningController::DoDispose()
//   {
//     NS_LOG_FUNCTION(this);
//     m_learnedInfo.clear();
//     m_l3LearnedInfo.clear(); // 清空L3层学习信息
//     m_arpTable.clear();
//     m_switchHosts.clear();        // 交换机主机信息
//     m_switchPortMappings.clear(); // 交换机互联端口映射
//     m_subnetToSwitchMap.clear();  // 子网-交换机映射
//     OFSwitch13Controller::DoDispose();
//   }

//   void
//   OFSwitch13LearningController::SetPriorityToAll()
//   {
//   }

//   ofl_err
//   OFSwitch13LearningController::HandlePacketIn(
//       struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch,
//       uint32_t xid)
//   {
//     NS_LOG_FUNCTION(this << swtch << xid);

//     static int prio = 100;
//     uint32_t outPort = OFPP_FLOOD;
//     enum ofp_packet_in_reason reason = msg->reason;
//     uint64_t swDpId = swtch->GetDpId();

//     char *msgStr = ofl_structs_match_to_string((struct ofl_match_header *)msg->match, 0);
//     NS_LOG_DEBUG("Packet in match: " << msgStr);
//     free(msgStr);

//     if (reason == OFPR_NO_MATCH)
//     {
//       // 提取入端口信息
//       uint32_t inPort;
//       size_t portLen = OXM_LENGTH(OXM_OF_IN_PORT);
//       struct ofl_match_tlv *input = oxm_match_lookup(OXM_OF_IN_PORT, (struct ofl_match *)msg->match);
//       memcpy(&inPort, input->value, portLen);

//       // 提取源MAC和目的MAC
//       Mac48Address src48;
//       struct ofl_match_tlv *ethSrc = oxm_match_lookup(OXM_OF_ETH_SRC, (struct ofl_match *)msg->match);
//       src48.CopyFrom(ethSrc->value);

//       Mac48Address dst48;
//       struct ofl_match_tlv *ethDst = oxm_match_lookup(OXM_OF_ETH_DST, (struct ofl_match *)msg->match);
//       dst48.CopyFrom(ethDst->value);

//       // 提取IP地址信息
//       Ipv4Address srcIp, dstIp;
//       bool isIpv4 = false;
//       Ipv4Address arpSenderIp, arpTargetIp; // 新增：ARP包专用IP变量

//       // 检查是否为IPv4包
//       struct ofl_match_tlv *ethType = oxm_match_lookup(OXM_OF_ETH_TYPE, (struct ofl_match *)msg->match);
//       if (ethType != nullptr)
//       {
//         uint16_t type;
//         memcpy(&type, ethType->value, 2);

//         // 1. 输出当前数据包的以太网类型（方便确认是否为IPv4）
//         // std::cout << type<< std::endl;
//         // uint16_t ntohsType = ntohs(type);
//         // 处理ARP包（0x0806）
//         // if (type == 0x0806)
//         if (type == 2054)
//         {
//           NS_LOG_DEBUG("检测到ARP包");
//           // std::cout <<"检测到ARP包"<< std::endl;

//           // std::cout << "[ARP包] 交换机DPID: " << swDpId << " 源MAC: " << src48 << " 目的MAC: " << dst48
//           //           << " 入端口: " << inPort << std::endl;

//           if (msg->data_length >= 42) // 确保数据长度足够（以太网头14 + ARP头28）
//           {
//             uint8_t *arpHeader = (uint8_t *)msg->data + 14; // 跳过以太网头
//             // 解析ARP操作码（偏移6-7字节：1=请求，2=响应）
//             uint16_t arpOp = (arpHeader[6] << 8) | arpHeader[7];
//             // 解析发送端IP和目标端IP
//             arpSenderIp.Set((arpHeader[14] << 24) | (arpHeader[15] << 16) | (arpHeader[16] << 8) | arpHeader[17]);
//             arpTargetIp.Set((arpHeader[24] << 24) | (arpHeader[25] << 16) | (arpHeader[26] << 8) | arpHeader[27]);
//             // 处理ARP请求（操作码1）
//             if (arpOp == 1)
//             {
//               std::cout << "[ARP包] 交换机DPID: " << swDpId << "  ARP操作码: " << arpOp << " | 发送端IP: " << arpSenderIp
//                         << " 目标端IP: " << arpTargetIp << std::endl;
//               // 判断目标IP是否为网关IP（通过查询ARP表）
//               bool isGwArp = false;
//               for (const auto &arpEntry : m_arpTable)
//               {
//                 if (arpEntry.first == arpTargetIp)
//                 {
//                   isGwArp = true;
//                   break;
//                 }
//               }
//               if (isGwArp)
//               {
//                 // std::cout << "  → 准备返回响应" << std::endl;

//                 // 构造ARP响应包（以太网头 + ARP响应头）
//                 uint8_t *arpReply = (uint8_t *)xmalloc(46);
//                 memset(arpReply, 0, 46);

//                 // 构造以太网头
//                 src48.CopyTo(arpReply); // 目的MAC：请求端主机MAC
//                 Mac48Address gwMac;

//                 // 获取交换机真实网关MAC
//                 auto itGwMac = m_arpTable.find(arpTargetIp);
//                 if (itGwMac != m_arpTable.end())
//                 {
//                   gwMac = itGwMac->second; // 使用预配置的网关MAC
//                   // std::cout << "使用预配置的网关MAC: " << gwMac << std::endl;
//                 }
//                 else
//                 {
//                   std::cout << "ARP表中未找到网关IP " << arpTargetIp << " 的MAC，使用默认值";
//                   gwMac = Mac48Address("00:00:00:00:01:00"); //  fallback默认MAC
//                 }

//                 gwMac.CopyTo(arpReply + 6); // 源MAC：网关MAC
//                 arpReply[12] = 0x08;
//                 arpReply[13] = 0x06; // 以太网类型：ARP

//                 // 构造ARP响应头
//                 uint8_t *arpReplyHeader = arpReply + 14;
//                 arpReplyHeader[0] = 0x00;
//                 arpReplyHeader[1] = 0x01; // 硬件类型：以太网
//                 arpReplyHeader[2] = 0x08;
//                 arpReplyHeader[3] = 0x00; // 协议类型：IPv4
//                 arpReplyHeader[4] = 0x06; // 硬件地址长度
//                 arpReplyHeader[5] = 0x04; // 协议地址长度
//                 arpReplyHeader[6] = 0x00;
//                 arpReplyHeader[7] = 0x02; // 操作码：响应

//                 // 发送端信息（网关）
//                 gwMac.CopyTo(arpReplyHeader + 8);
//                 arpTargetIp.Serialize(arpReplyHeader + 14);

//                 // 目标端信息（请求主机）
//                 src48.CopyTo(arpReplyHeader + 18);
//                 uint32_t senderIpNet = htonl(arpSenderIp.Get());
//                 memcpy(arpReplyHeader + 24, &senderIpNet, 4);

//                 // 发送ARP响应包
//                 struct ofl_msg_packet_out reply;
//                 reply.header.type = OFPT_PACKET_OUT;
//                 reply.buffer_id = NO_BUFFER;
//                 reply.in_port = OFPP_CONTROLLER;
//                 reply.data_length = 46;
//                 reply.data = arpReply;

//                 struct ofl_action_output *a = (struct ofl_action_output *)xmalloc(sizeof(struct ofl_action_output));
//                 a->header.type = OFPAT_OUTPUT;
//                 a->port = inPort; // 从入端口返回响应
//                 a->max_len = 0;

//                 reply.actions_num = 1;
//                 reply.actions = (struct ofl_action_header **)&a;

//                 SendToSwitch(swtch, (struct ofl_msg_header *)&reply, xid);
//                 free(a);
//                 free(arpReply);
//                 return 0;
//               }
//             }
//           }
//           return 0;
//         }
//         // if (ntohs(type) == 0x0800)  // IPv4类型
//         else if (type == 2048) // IPv4类型
//         {
//           isIpv4 = true;

//           // 提取源IP
//           struct ofl_match_tlv *ipSrc = oxm_match_lookup(OXM_OF_IPV4_SRC, (struct ofl_match *)msg->match);
//           if (ipSrc != nullptr)
//           {
//             uint32_t ipAddrNet = *(uint32_t *)ipSrc->value; // 网络字节序
//             uint32_t ipAddrHost = ntohl(ipAddrNet);         // 转换为主机字节序
//             srcIp.Set(ipAddrHost);
//           }

//           // 提取目的IP
//           struct ofl_match_tlv *ipDst = oxm_match_lookup(OXM_OF_IPV4_DST, (struct ofl_match *)msg->match);
//           if (ipDst != nullptr)
//           {
//             uint32_t ipAddrNet = *(uint32_t *)ipDst->value; // 网络字节序
//             uint32_t ipAddrHost = ntohl(ipAddrNet);         // 转换为主机字节序
//             dstIp.Set(ipAddrHost);
//           }

//           // 判断是否为广播（源IP或目的IP）
//           // bool isSrcBroadcast = srcIp.IsBroadcast() || srcIp.IsMulticast();
//           // bool isDstBroadcast = dstIp.IsBroadcast() || dstIp.IsMulticast();

//           // // 仅在非广播场景输出详细IP信息
//           // if ( !isDstBroadcast)
//           // {
//           //     std::cout << "交换机DPID: " << swDpId
//           //               << "[IPv4非广播包] 源IP: " << srcIp
//           //               << " | 目的IP: " << dstIp
//           //               << " | 入端口: " << inPort << std::endl;
//           // }
//           return 0;
//         }
//       }

//       // 查找交换机对应的L2和L3表
//       auto itL2 = m_learnedInfo.find(swDpId);
//       auto itL3 = m_l3LearnedInfo.find(swDpId);

//       // 确保表存在
//       if (itL2 == m_learnedInfo.end())
//       {
//         m_learnedInfo[swDpId] = L2Table_t();
//         itL2 = m_learnedInfo.find(swDpId);
//       }

//       if (itL3 == m_l3LearnedInfo.end())
//       {
//         m_l3LearnedInfo[swDpId] = L3Table_t();
//         itL3 = m_l3LearnedInfo.find(swDpId);
//       }

//       L2Table_t *l2Table = &itL2->second;
//       L3Table_t *l3Table = &itL3->second;

//       // 处理L2层学习 (始终学习源MAC，非广播包)
//       if (!src48.IsBroadcast())
//       {
//         auto itSrcL2 = l2Table->find(src48);
//         if (itSrcL2 == l2Table->end())
//         {
//           l2Table->insert(std::make_pair(src48, inPort));
//           std::cout << "L2学习: MAC地址 " << src48 << " 对应端口 " << inPort << std::endl;

//           // 添加L2流表项（更高优先级）
//           std::ostringstream cmdL2;
//           cmdL2 << "flow-mod cmd=add,table=0,idle=10,flags=0x0001"
//                 << ",prio=" << ++prio
//                 << " eth_dst=" << src48
//                 << " apply:output=" << inPort;
//           DpctlExecute(swDpId, cmdL2.str());
//         }
//         else if (itSrcL2->second != inPort)
//         {
//           // NS_ASSERT_MSG(false, "L2转发表不一致");
//         }
//       }

//       // 处理L3层学习 (仅对IPv4包且源非广播包进行学习)
//       if (isIpv4 && !src48.IsBroadcast())
//       {
//         // 判断源IP是否为广播
//         bool isSrcBroadcast = srcIp.IsBroadcast() || (srcIp.Get() & 0xFF) == 0xFF;
//         if (!isSrcBroadcast)
//         {
//           auto itSrcL3 = l3Table->find(srcIp);
//           if (itSrcL3 == l3Table->end())
//           {
//             // 存储IP->(MAC, 端口)映射
//             l3Table->insert(std::make_pair(srcIp, std::make_pair(src48, inPort)));
//             std::cout << "L3学习: IP地址 " << srcIp << " 对应MAC " << src48 << " 和端口 " << inPort << std::endl;

//             // 添加L3流表项（优先级低于L2）
//             std::ostringstream cmdL3;
//             cmdL3 << "flow-mod cmd=add,table=0,idle=10,flags=0x0001"
//                   << ",prio=" << (prio - 50)
//                   << " eth_type=0x0800"  // 使用空格
//                   << ",ip_dst=" << srcIp // 使用逗号
//                   << " apply:output=" << inPort;

//             std::cout << "L3 Flow-Mod Command: " << cmdL3.str() << std::endl;
//             DpctlExecute(swDpId, cmdL3.str());
//           }
//           else if (itSrcL3->second.second != inPort)
//           {
//             NS_ASSERT_MSG(false, "L3转发表不一致");
//           }
//         }
//       }

//       // 确定输出端口（L2优先于L3）
//       if (!dst48.IsBroadcast())
//       {
//         // 优先检查L2表
//         auto itDstL2 = l2Table->find(dst48);
//         if (itDstL2 != l2Table->end())
//         {
//           outPort = itDstL2->second;
//           NS_LOG_DEBUG("使用L2转发表转发至端口 " << outPort);
//         }
//         // L2表未命中时检查L3表
//         else if (isIpv4)
//         {
//           auto itDstL3 = l3Table->find(dstIp);
//           if (itDstL3 != l3Table->end())
//           {
//             outPort = itDstL3->second.second;
//             NS_LOG_DEBUG("使用L3转发表转发至端口 " << outPort);
//           }
//           else
//           {
//             NS_LOG_DEBUG("No L3 info for ip " << dstIp << ". Flood.");
//           }
//         }
//         else
//         {
//           NS_LOG_DEBUG("No L2 info for mac " << dst48 << ". Flood.");
//         }
//       }

//       // 发送Packet-Out消息
//       struct ofl_msg_packet_out reply;
//       reply.header.type = OFPT_PACKET_OUT;
//       reply.buffer_id = msg->buffer_id;
//       reply.in_port = inPort;
//       reply.data_length = 0;
//       reply.data = nullptr;

//       if (msg->buffer_id == NO_BUFFER)
//       {
//         reply.data_length = msg->data_length;
//         reply.data = msg->data;
//       }

//       struct ofl_action_output *a = (struct ofl_action_output *)xmalloc(sizeof(struct ofl_action_output));
//       a->header.type = OFPAT_OUTPUT;
//       a->port = outPort;
//       a->max_len = 0;

//       reply.actions_num = 1;
//       reply.actions = (struct ofl_action_header **)&a;

//       SendToSwitch(swtch, (struct ofl_msg_header *)&reply, xid);
//       free(a);
//     }
//     else
//     {
//       NS_LOG_WARN("This controller can't handle the packet. Unknown reason.");
//     }

//     ofl_msg_free((struct ofl_msg_header *)msg, 0);
//     return 0;
//   }

//   ofl_err
//   OFSwitch13LearningController::HandleFlowRemoved(
//       struct ofl_msg_flow_removed *msg, Ptr<const RemoteSwitch> swtch,
//       uint32_t xid)
//   {
//     NS_LOG_FUNCTION(this << swtch << xid);

//     uint64_t swDpId = swtch->GetDpId();
//     NS_LOG_DEBUG("Flow entry expired. Removing from tables.");

//     // 处理L2流表项删除
//     auto itL2 = m_learnedInfo.find(swDpId);
//     if (itL2 != m_learnedInfo.end())
//     {
//       Mac48Address mac48;
//       struct ofl_match_tlv *ethDst = oxm_match_lookup(OXM_OF_ETH_DST, (struct ofl_match *)msg->stats->match);
//       if (ethDst != nullptr)
//       {
//         mac48.CopyFrom(ethDst->value);
//         itL2->second.erase(mac48);
//       }
//     }

//     // 处理L3流表项删除
//     auto itL3 = m_l3LearnedInfo.find(swDpId);
//     if (itL3 != m_l3LearnedInfo.end())
//     {
//       struct ofl_match_tlv *ipDst = oxm_match_lookup(OXM_OF_IPV4_DST, (struct ofl_match *)msg->stats->match);
//       if (ipDst != nullptr)
//       {
//         Ipv4Address ipAddr(*(uint32_t *)ipDst->value);
//         itL3->second.erase(ipAddr);
//       }
//     }

//     ofl_msg_free_flow_removed(msg, true, 0);
//     return 0;
//   }

//   ofl_err
//   OFSwitch13LearningController::HandleHostInfo(
//       //   struct ofl_msg_host_info *msg,
//       void *msg,
//       Ptr<const RemoteSwitch> swtch,
//       uint32_t xid)
//   {
//     NS_LOG_FUNCTION(this << swtch << xid);
//     uint64_t dpId = swtch->GetDpId(); // 交换机DPID

//     //    // 如果需要处理真实的msg数据，需要确保msg不为空
//     // if (msg == nullptr) {
//     //     std::cout << "还未定义消息msg_host_info，手动触发" << std::endl;
//     // }
//     // // 1. 解析消息中的主机信息，写入交换机-域内主机映射表
//     // std::vector<HostInfo> hostList;
//     // for (size_t i = 0; i < msg->host_count; i++) {
//     //     HostInfo host;
//     //     // 从消息中提取主机IP（注意网络字节序转换）
//     //     host.ip = msg->hosts[i].ip;
//     //     // 从消息中提取主机MAC
//     //     host.mac = msg->hosts[i].mac;
//     //     // 从消息中提取交换机连接端口
//     //     host.port = msg->hosts[i].port;

//     //     hostList.push_back(host);
//     //     std::cout << "  主机" << i+1 << ": IP=" << host.ip
//     //               << ", MAC=" << host.mac << ", 端口=" << host.port << std::endl;
//     // }
//     // m_switchHosts[dpId] = hostList;  // 写入域内主机表

//     // ==================== 初始化交换机-域内主机映射 ====================
//     std::vector<HostInfo> hostList; // 声明hostList变量
//     if (dpId == 0x0000000000000001)
//     { // sw1（10.1.1.0/24）
//       hostList = {
//           {Ipv4Address("10.1.1.1"), Mac48Address("00:00:00:00:00:0f"), 1},
//           {Ipv4Address("10.1.1.2"), Mac48Address("00:00:00:00:00:10"), 1},
//           {Ipv4Address("10.1.1.3"), Mac48Address("00:00:00:00:00:11"), 1},
//           {Ipv4Address("10.1.1.4"), Mac48Address("00:00:00:00:00:12"), 2}};
//     }
//     else if (dpId == 0x0000000000000002)
//     { // sw2（10.2.1.0/24）
//       hostList = {
//           {Ipv4Address("10.2.1.1"), Mac48Address("00:00:00:00:00:17"), 1},
//           {Ipv4Address("10.2.1.2"), Mac48Address("00:00:00:00:00:18"), 1}};
//     }
//     else if (dpId == 0x0000000000000003)
//     { // sw3（10.3.1.0/24）
//       hostList = {
//           {Ipv4Address("10.3.1.1"), Mac48Address("00:00:00:00:00:1a"), 1},
//           {Ipv4Address("10.3.1.2"), Mac48Address("00:00:00:00:00:1b"), 1},
//           {Ipv4Address("10.3.1.3"), Mac48Address("00:00:00:00:00:1c"), 1},
//           {Ipv4Address("10.3.1.4"), Mac48Address("00:00:00:00:00:1e"), 1},
//           {Ipv4Address("10.3.1.5"), Mac48Address("00:00:00:00:00:1f"), 1},
//           {Ipv4Address("10.3.1.6"), Mac48Address("00:00:00:00:00:20"), 1}};
//     }
//     else
//     {
//       std::cout << "  → 未知交换机DPID，无主机信息" << std::endl;
//       return 0;
//     }

//     m_switchHosts[dpId] = hostList; // 写入域内主机表

//     // // ==================== 初始化IP网段→交换机DPID映射 ====================
//     // // 每个子网对应到其所属的交换机DPID
//     // m_subnetToSwitchMap = {
//     //     {"10.1.1.0/24", 0x0000000000000001},  // 10.1.1.0/24 属于 sw1 (DPID:1)
//     //     {"10.2.1.0/24", 0x0000000000000002},  // 10.2.1.0/24 属于 sw2 (DPID:2)
//     //     {"10.3.1.0/24", 0x0000000000000003}   // 10.3.1.0/24 属于 sw3 (DPID:3)
//     // };

//     // 输出主机信息
//     std::cout << "交换机DPID(" << dpId << ") 域内主机信息：" << std::endl;
//     for (size_t i = 0; i < hostList.size(); i++)
//     {
//       const auto &host = hostList[i];
//       std::cout << "  主机" << i + 1 << ": IP=" << host.ip
//                 << ", MAC=" << host.mac << ", 端口=" << host.port << std::endl;
//     }

//     // // ==================== 自动识别网段（从m_switchHosts提取，非硬编码）====================
//     // if (hostList.empty()) {
//     //     std::cout << "  → 无主机信息，无法识别网段" << std::endl;
//     //     return 0;
//     // }

//     // 步骤1：提取第一个主机的IP，计算/24网段（子网掩码255.255.255.0）
//     Ipv4Address firstHostIp = hostList[0].ip;
//     uint32_t ipHost = firstHostIp.Get();          // 主机字节序的IP
//     uint32_t subnetMask = 0xFFFFFF00;             // /24子网掩码（255.255.255.0）
//     uint32_t networkIpHost = ipHost & subnetMask; // 网络地址（主机字节序）
//     Ipv4Address networkIp(networkIpHost);         // 转换为Ipv4Address对象

//     // // 步骤2：验证所有主机是否在同一/24网段（避免配置错误）
//     // bool allInSameSubnet = true;
//     // for (const auto& host : hostList) {
//     //     uint32_t currentIpHost = host.ip.Get();
//     //     if ((currentIpHost & subnetMask) != networkIpHost) {
//     //         allInSameSubnet = false;
//     //         std::cerr << "  ⚠️  警告：主机IP " << host.ip << " 与其他主机不在同一/24网段" << std::endl;
//     //     }
//     // }

//     // if (!allInSameSubnet) {
//     //     std::cerr << "  → 网段识别失败：域内主机不在同一/24网段" << std::endl;
//     //     return 0;
//     // }

//     // 步骤3：生成网段字符串（格式：xxx.xxx.xxx.0/24）
//     // 替换为：
//     std::ostringstream os;
//     networkIp.Print(os);
//     std::string subnetStr = os.str() + "/24";

//     // 步骤4：更新子网-交换机映射表（key：网段，value：交换机DPID）
//     m_subnetToSwitchMap[subnetStr] = dpId;

//     // 输出自动识别的网段信息
//     std::cout << "  → 自动识别网段：" << subnetStr << " （对应交换机DPID：" << dpId << "）" << std::endl;

//     // ==================== 补充所有交换机的网段映射（确保跨域路由完整）====================
//     // 检查是否已收集所有交换机的网段，避免重复添加
//     std::cout << "\n当前完整的子网-交换机映射表：" << std::endl;
//     for (const auto &entry : m_subnetToSwitchMap)
//     {
//       std::cout << "  " << entry.first << " → 交换机DPID：" << entry.second << std::endl;
//     }

//     // ofl_msg_free((struct ofl_msg_header *)msg, 0);
//     return 0;
//   }

//   /********** 私有成员方法 **********/

//   void
//   OFSwitch13LearningController::SetRP()
//   {
//     NS_LOG_FUNCTION(this);
//   }

//   void
//   OFSwitch13LearningController::HandshakeSuccessful(
//       Ptr<const RemoteSwitch> swtch)
//   {
//     NS_LOG_FUNCTION(this << swtch);

//     uint64_t swDpId = swtch->GetDpId();

//     // 安装表项缺失流表项
//     DpctlExecute(swDpId, "flow-mod cmd=add,table=0,prio=0 "
//                          "apply:output=ctrl:128");
//     DpctlExecute(swDpId, "set-config miss=128");

//     // 初始化L2和L3表
//     m_learnedInfo.insert(std::make_pair(swDpId, L2Table_t()));
//     m_l3LearnedInfo.insert(std::make_pair(swDpId, L3Table_t()));

//     // 手动触发处理预定义的主机信息
//     HandleHostInfo(nullptr, swtch, 0);

//     // 触发流表规则生成
//     uint64_t dpId = swtch->GetDpId(); // 交换机DPID
//     std::cout << "\n开始为交换机 " << dpId << " 生成流表规则..." << std::endl;

//     // 3.1 生成域内流量规则（高优先级：200）
//     if (m_switchHosts.count(dpId))
//     {
//       for (const auto &host : m_switchHosts[dpId])
//       {
//         std::ostringstream cmd;
//         cmd << "flow-mod cmd=add,table=0,prio=200,idle=30,hard=60 "
//             << "eth_type=0x0800,ip_dst=" << host.ip << " "                        // 匹配IPv4
//             << "apply:set_field=eth_dst:" << host.mac << ",output=" << host.port; // 修改目的MAC+转发
//         DpctlExecute(dpId, cmd.str());
//         std::cout << "  ✅ 域内规则: " << cmd.str() << std::endl;
//       }
//     }

//     // 3.2 生成跨域流量规则（中优先级：190）
//     // 遍历所有子网，排除当前交换机所属的子网（避免本地流量跨域），生成转发到对应端口的流表项
//     for (const auto &subnetEntry : m_subnetToSwitchMap)
//     {
//       const std::string &targetSubnet = subnetEntry.first;
//       uint64_t targetSwitchDpid = subnetEntry.second;

//       // 如果目标子网属于当前交换机，则跳过（由域内规则处理）
//       if (targetSwitchDpid == dpId)
//       {
//         continue;
//       }

//       // 查找当前交换机到目标交换机的端口
//       auto switchMappingIt = m_switchPortMappings.find(dpId);
//       if (switchMappingIt != m_switchPortMappings.end())
//       {
//         for (const auto &portMapping : switchMappingIt->second)
//         {
//           // 查找连接到目标交换机的端口
//           if (portMapping.destSwitchDpid == targetSwitchDpid)
//           {
//             uint32_t outputPort = portMapping.outputPort;

//             // 生成跨域流量规则：只根据目的IP子网匹配并转发到指定端口
//             std::ostringstream cmd;
//             cmd << "flow-mod cmd=add,table=0,prio=190,idle=30,hard=60 "
//                 << "eth_type=0x0800,ip_dst=" << targetSubnet << " "
//                 << "apply:output=" << outputPort;
//             DpctlExecute(dpId, cmd.str());
//             std::cout << "  ✅ 跨域规则: " << cmd.str() << std::endl;
//             break;
//           }
//         }
//       }
//     }
//   }

// } // namespace ns3
// #endif // NS3_OFSWITCH13
// >>>>>>> 96620a9f6415e74cbfc07275f0890e519cafe6a0
