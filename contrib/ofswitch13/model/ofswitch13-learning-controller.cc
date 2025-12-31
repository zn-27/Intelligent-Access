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

NS_LOG_COMPONENT_DEFINE("OFSwitch13LearningController");

namespace ns3
{

  NS_OBJECT_ENSURE_REGISTERED(OFSwitch13LearningController);

  /********** 公共成员方法 ***********/
  OFSwitch13LearningController::OFSwitch13LearningController()
  {
    // // ==================== 初始化交换机-跨域子网路由 ====================
    // // sw1的跨域路由：直接指定子网对应的端口
    // m_switchSubnets[0x0000000000000001] = {
    //     {"10.2.1.0/24", Mac48Address("02:06:00:00:00:00:00:09"), 3},  // 10.2.1.0/24子网走端口3
    //     {"10.3.1.0/24", Mac48Address("02:06:00:00:00:00:00:0e"), 4}   // 10.3.1.0/24子网走端口4
    // };

    // // sw2的跨域路由：直接指定子网对应的端口
    // m_switchSubnets[0x0000000000000002] = {
    //     {"10.1.1.0/24", Mac48Address("02:06:00:00:00:00:00:0a"), 2},   // 10.1.1.0/24子网走端口2
    //     {"10.3.1.0/24", Mac48Address("02:06:00:00:00:00:00:0b"), 3}   // 10.3.1.0/24子网走端口3
    // };

    // // sw3的跨域路由：直接指定子网对应的端口
    // m_switchSubnets[0x0000000000000003] = {
    //     {"10.1.1.0/24", Mac48Address("02:06:00:00:00:00:00:0c"), 3},   // 10.1.1.0/24子网走端口3
    //     {"10.2.1.0/24", Mac48Address("02:06:00:00:00:00:00:0d"), 2}   // 10.2.1.0/24子网走端口2
    // };

    // ==================== 初始化交换机互联端口映射（仅硬编码端口连接关系）====================
    // 交换机连接拓扑：
    // sw1 (DPID:1) <--端口3--> sw2 (DPID:2)
    // sw1 (DPID:1) <--端口4--> sw3 (DPID:3)
    // sw2 (DPID:2) <--端口2--> sw1 (DPID:1)
    // sw2 (DPID:2) <--端口3--> sw3 (DPID:3)
    // sw3 (DPID:3) <--端口3--> sw1 (DPID:1)
    // sw3 (DPID:3) <--端口2--> sw2 (DPID:2)

    // sw1的互联端口映射
    m_switchPortMappings[0x0000000000000001] = {
        {0x0000000000000002, 3}, // 到sw2，走端口3
        {0x0000000000000003, 4}  // 到sw3，走端口4
    };

    // sw2的互联端口映射
    m_switchPortMappings[0x0000000000000002] = {
        {0x0000000000000001, 2}, // 到sw1，走端口2
        {0x0000000000000003, 3}  // 到sw3，走端口3
    };

    // sw3的互联端口映射
    m_switchPortMappings[0x0000000000000003] = {
        {0x0000000000000001, 3}, // 到sw1，走端口3
        {0x0000000000000002, 2}  // 到sw2，走端口2
    };

    // 手动触发处理预定义的主机信息
    // HandleHostInfo(nullptr, swtch, 0);

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
    OFSwitch13Controller::DoDispose();
  }

  void
  OFSwitch13LearningController::SetPriorityToAll()
  {
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
      Ipv4Address arpSenderIp, arpTargetIp; // 新增：ARP包专用IP变量

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
              std::cout << "[ARP包] 交换机DPID: " << swDpId << "  ARP操作码: " << arpOp << " | 发送端IP: " << arpSenderIp
                        << " 目标端IP: " << arpTargetIp << std::endl;
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
                  << ",ip_dst=" << srcIp // 使用逗号
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

  ofl_err
  OFSwitch13LearningController::HandleHostInfo(
      //   struct ofl_msg_host_info *msg,
      void *msg,
      Ptr<const RemoteSwitch> swtch,
      uint32_t xid)
  {
    NS_LOG_FUNCTION(this << swtch << xid);
    uint64_t dpId = swtch->GetDpId(); // 交换机DPID

    //    // 如果需要处理真实的msg数据，需要确保msg不为空
    // if (msg == nullptr) {
    //     std::cout << "还未定义消息msg_host_info，手动触发" << std::endl;
    // }
    // // 1. 解析消息中的主机信息，写入交换机-域内主机映射表
    // std::vector<HostInfo> hostList;
    // for (size_t i = 0; i < msg->host_count; i++) {
    //     HostInfo host;
    //     // 从消息中提取主机IP（注意网络字节序转换）
    //     host.ip = msg->hosts[i].ip;
    //     // 从消息中提取主机MAC
    //     host.mac = msg->hosts[i].mac;
    //     // 从消息中提取交换机连接端口
    //     host.port = msg->hosts[i].port;

    //     hostList.push_back(host);
    //     std::cout << "  主机" << i+1 << ": IP=" << host.ip
    //               << ", MAC=" << host.mac << ", 端口=" << host.port << std::endl;
    // }
    // m_switchHosts[dpId] = hostList;  // 写入域内主机表

    // ==================== 初始化交换机-域内主机映射 ====================
    std::vector<HostInfo> hostList; // 声明hostList变量
    if (dpId == 0x0000000000000001)
    { // sw1（10.1.1.0/24）
      hostList = {
          {Ipv4Address("10.1.1.1"), Mac48Address("00:00:00:00:00:0f"), 1},
          {Ipv4Address("10.1.1.2"), Mac48Address("00:00:00:00:00:10"), 1},
          {Ipv4Address("10.1.1.3"), Mac48Address("00:00:00:00:00:11"), 1},
          {Ipv4Address("10.1.1.4"), Mac48Address("00:00:00:00:00:12"), 2}};
    }
    else if (dpId == 0x0000000000000002)
    { // sw2（10.2.1.0/24）
      hostList = {
          {Ipv4Address("10.2.1.1"), Mac48Address("00:00:00:00:00:17"), 1},
          {Ipv4Address("10.2.1.2"), Mac48Address("00:00:00:00:00:18"), 1}};
    }
    else if (dpId == 0x0000000000000003)
    { // sw3（10.3.1.0/24）
      hostList = {
          {Ipv4Address("10.3.1.1"), Mac48Address("00:00:00:00:00:1a"), 1},
          {Ipv4Address("10.3.1.2"), Mac48Address("00:00:00:00:00:1b"), 1},
          {Ipv4Address("10.3.1.3"), Mac48Address("00:00:00:00:00:1c"), 1},
          {Ipv4Address("10.3.1.4"), Mac48Address("00:00:00:00:00:1e"), 1},
          {Ipv4Address("10.3.1.5"), Mac48Address("00:00:00:00:00:1f"), 1},
          {Ipv4Address("10.3.1.6"), Mac48Address("00:00:00:00:00:20"), 1}};
    }
    else
    {
      std::cout << "  → 未知交换机DPID，无主机信息" << std::endl;
      return 0;
    }

    m_switchHosts[dpId] = hostList; // 写入域内主机表

    // // ==================== 初始化IP网段→交换机DPID映射 ====================
    // // 每个子网对应到其所属的交换机DPID
    // m_subnetToSwitchMap = {
    //     {"10.1.1.0/24", 0x0000000000000001},  // 10.1.1.0/24 属于 sw1 (DPID:1)
    //     {"10.2.1.0/24", 0x0000000000000002},  // 10.2.1.0/24 属于 sw2 (DPID:2)
    //     {"10.3.1.0/24", 0x0000000000000003}   // 10.3.1.0/24 属于 sw3 (DPID:3)
    // };

    // 输出主机信息
    std::cout << "交换机DPID(" << dpId << ") 域内主机信息：" << std::endl;
    for (size_t i = 0; i < hostList.size(); i++)
    {
      const auto &host = hostList[i];
      std::cout << "  主机" << i + 1 << ": IP=" << host.ip
                << ", MAC=" << host.mac << ", 端口=" << host.port << std::endl;
    }

    // // ==================== 自动识别网段（从m_switchHosts提取，非硬编码）====================
    // if (hostList.empty()) {
    //     std::cout << "  → 无主机信息，无法识别网段" << std::endl;
    //     return 0;
    // }

    // 步骤1：提取第一个主机的IP，计算/24网段（子网掩码255.255.255.0）
    Ipv4Address firstHostIp = hostList[0].ip;
    uint32_t ipHost = firstHostIp.Get();          // 主机字节序的IP
    uint32_t subnetMask = 0xFFFFFF00;             // /24子网掩码（255.255.255.0）
    uint32_t networkIpHost = ipHost & subnetMask; // 网络地址（主机字节序）
    Ipv4Address networkIp(networkIpHost);         // 转换为Ipv4Address对象

    // // 步骤2：验证所有主机是否在同一/24网段（避免配置错误）
    // bool allInSameSubnet = true;
    // for (const auto& host : hostList) {
    //     uint32_t currentIpHost = host.ip.Get();
    //     if ((currentIpHost & subnetMask) != networkIpHost) {
    //         allInSameSubnet = false;
    //         std::cerr << "  ⚠️  警告：主机IP " << host.ip << " 与其他主机不在同一/24网段" << std::endl;
    //     }
    // }

    // if (!allInSameSubnet) {
    //     std::cerr << "  → 网段识别失败：域内主机不在同一/24网段" << std::endl;
    //     return 0;
    // }

    // 步骤3：生成网段字符串（格式：xxx.xxx.xxx.0/24）
    // 替换为：
    std::ostringstream os;
    networkIp.Print(os);
    std::string subnetStr = os.str() + "/24";

    // 步骤4：更新子网-交换机映射表（key：网段，value：交换机DPID）
    m_subnetToSwitchMap[subnetStr] = dpId;

    // 输出自动识别的网段信息
    std::cout << "  → 自动识别网段：" << subnetStr << " （对应交换机DPID：" << dpId << "）" << std::endl;

    // ==================== 补充所有交换机的网段映射（确保跨域路由完整）====================
    // 检查是否已收集所有交换机的网段，避免重复添加
    std::cout << "\n当前完整的子网-交换机映射表：" << std::endl;
    for (const auto &entry : m_subnetToSwitchMap)
    {
      std::cout << "  " << entry.first << " → 交换机DPID：" << entry.second << std::endl;
    }

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
    HandleHostInfo(nullptr, swtch, 0);

    // 触发流表规则生成
    uint64_t dpId = swtch->GetDpId(); // 交换机DPID
    std::cout << "\n开始为交换机 " << dpId << " 生成流表规则..." << std::endl;

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
        std::cout << "  ✅ 域内规则: " << cmd.str() << std::endl;
      }
    }

    // 3.2 生成跨域流量规则（中优先级：190）
    // 遍历所有子网，排除当前交换机所属的子网（避免本地流量跨域），生成转发到对应端口的流表项
    for (const auto &subnetEntry : m_subnetToSwitchMap)
    {
      const std::string &targetSubnet = subnetEntry.first;
      uint64_t targetSwitchDpid = subnetEntry.second;

      // 如果目标子网属于当前交换机，则跳过（由域内规则处理）
      if (targetSwitchDpid == dpId)
      {
        continue;
      }

      // 查找当前交换机到目标交换机的端口
      auto switchMappingIt = m_switchPortMappings.find(dpId);
      if (switchMappingIt != m_switchPortMappings.end())
      {
        for (const auto &portMapping : switchMappingIt->second)
        {
          // 查找连接到目标交换机的端口
          if (portMapping.destSwitchDpid == targetSwitchDpid)
          {
            uint32_t outputPort = portMapping.outputPort;

            // 生成跨域流量规则：只根据目的IP子网匹配并转发到指定端口
            std::ostringstream cmd;
            cmd << "flow-mod cmd=add,table=0,prio=190,idle=30,hard=60 "
                << "eth_type=0x0800,ip_dst=" << targetSubnet << " "
                << "apply:output=" << outputPort;
            DpctlExecute(dpId, cmd.str());
            std::cout << "  ✅ 跨域规则: " << cmd.str() << std::endl;
            break;
          }
        }
      }
    }
  }

} // namespace ns3
#endif // NS3_OFSWITCH13