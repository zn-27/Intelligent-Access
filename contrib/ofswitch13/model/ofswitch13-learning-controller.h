/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef OFSWITCH13_LEARNING_CONTROLLER_H
#define OFSWITCH13_LEARNING_CONTROLLER_H

#include "ofswitch13-controller.h"
#include "ns3/ipv4-address.h"

namespace ns3
{
  // 主机信息结构体：IP、MAC、端口（域内主机）
  struct HostInfo
  {
    Ipv4Address ip;   // 主机IP地址
    Mac48Address mac; // 主机MAC地址
    uint32_t port;    // 连接的交换机端口号
  };

  // 定义交换机互联端口映射结构体（仅包含目标交换机和对应端口）
  struct SwitchPortMapping
  {
    uint64_t destSwitchDpid; // 目标交换机DPID
    uint32_t outputPort;     // 本交换机的输出端口（连接到目标交换机）
  };

  // 子网-交换机DPID映射结构体
  struct SubnetToSwitchMapping
  {
    std::string subnet;        // IP子网（如"10.1.1.0/24"）
    uint64_t targetSwitchDpid; // 该子网所属的交换机DPID
  };

  class OFSwitch13LearningController : public OFSwitch13Controller
  {
  public:
    OFSwitch13LearningController();
    virtual ~OFSwitch13LearningController();

    static TypeId GetTypeId(void);
    void SetRoutingPriority(void);
    virtual void DoDispose();

    // 根据交换机DPID获取其下所有主机信息
    std::vector<HostInfo> GetHostsBySwitch(uint64_t dpId)
    {
      auto it = m_switchHosts.find(dpId);
      return (it != m_switchHosts.end()) ? it->second : std::vector<HostInfo>();
    }

    // 添加ARP表项（供仿真脚本调用）
    void AddArpEntry(Ipv4Address ip, Mac48Address mac)
    {
      m_arpTable[ip] = mac;
    }

    ofl_err HandlePacketIn(
        struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch,
        uint32_t xid);

    ofl_err HandleFlowRemoved(
        struct ofl_msg_flow_removed *msg, Ptr<const RemoteSwitch> swtch,
        uint32_t xid);
    // //------------------zi ding yi ---------------------------
    // /**
    // * Handle host information message
    // */
    // ofl_err HandleHostInfo(struct ofl_msg_host_info *msg,
    //                              Ptr<const RemoteSwitch> swtch,
    //                              uint32_t xid);
    // //------------------zi ding yi ---------------------------

    //------------------zi ding yi ---------------------------
    /**
     * Handle host information message（手动初始化主机信息，不依赖外部结构体）
     */
    ofl_err HandleHostInfo(void *msg, Ptr<const RemoteSwitch> swtch,
                           uint32_t xid);
    //------------------zi ding yi ---------------------------

  protected:
    void HandshakeSuccessful(Ptr<const RemoteSwitch> swtch);

  private:
    // ARP解析表（网关IP到MAC的映射）
    typedef std::map<Ipv4Address, Mac48Address> IpMacMap_t;
    IpMacMap_t m_arpTable;

    // 交换机-主机映射（key: 交换机DPID，value: 域内主机列表）
    typedef std::map<uint64_t, std::vector<HostInfo>> SwitchHostMap_t;
    SwitchHostMap_t m_switchHosts;

    // 交换机互联端口映射（key: 本交换机DPID，value: 目标交换机+端口列表）
    typedef std::map<uint64_t, std::vector<SwitchPortMapping>> SwitchPortMap_t;
    SwitchPortMap_t m_switchPortMappings;

    // 子网->交换机DPID映射（key: 子网字符串，value: 所属交换机DPID）
    typedef std::map<std::string, uint64_t> SubnetSwitchMap_t;
    SubnetSwitchMap_t m_subnetToSwitchMap;

    // 二层转发表（MAC到端口）
    typedef std::map<Mac48Address, uint32_t> L2Table_t;
    typedef std::map<uint64_t, L2Table_t> DatapathMap_t;
    DatapathMap_t m_learnedInfo;

    // 三层转发表（IP到<MAC, 端口>）
    typedef std::map<Ipv4Address, std::pair<Mac48Address, uint32_t>> L3Table_t;
    typedef std::map<uint64_t, L3Table_t> L3DatapathMap_t;
    L3DatapathMap_t m_l3LearnedInfo;

    void SetRP();
    void SetPriorityToAll();
  };

} // namespace ns3
#endif /* OFSWITCH13_LEARNING_CONTROLLER_H */
