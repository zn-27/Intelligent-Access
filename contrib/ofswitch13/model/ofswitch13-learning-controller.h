/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef OFSWITCH13_LEARNING_CONTROLLER_H
#define OFSWITCH13_LEARNING_CONTROLLER_H

#include "ofswitch13-controller.h"
#include "ns3/ipv4-address.h"


namespace ns3
{
    // 主机信息结构体：IP、MAC、端口（域内主机）
    struct HostInfo {
        Ipv4Address ip;       // 主机IP地址
        Mac48Address mac;     // 主机MAC地址
        uint32_t port;        // 连接的交换机端口号
    };

     // 定义交换机互联端口映射结构体（仅包含目标交换机和对应端口）
    struct SwitchPortMapping {
        uint64_t destSwitchDpid;  // 目标交换机DPID
        uint32_t outputPort;       // 本交换机的输出端口（连接到目标交换机）
    };

    // 子网-交换机DPID映射结构体
    struct SubnetToSwitchMapping {
        std::string subnet;       // IP子网（如"10.1.1.0/24"）
        uint64_t targetSwitchDpid;// 该子网所属的交换机DPID
    };


    // 节点位置信息结构体（以IP地址作为唯一标识，暂移除更新时间）
    struct NodePositionInfo {
        Ipv4Address ip;          // 节点IPv4地址（唯一标识，替代nodeId）
        float x;                // 三维坐标X
        float y;                // 三维坐标Y
        float z;                // 三维坐标Z（无线场景默认0即可）
    };
    // 网络状态结构体（仅保留平均距离）
    struct NetworkState {
        float averageNodeDistance;  // 节点平均距离（仅用这个判断）
    };

    // 前向声明，解决循环依赖
    class OFSwitch13LearningController;

    // Q学习网络模式决策类
    class NetworkModeQLearning {
    public:
        // 构造函数
        NetworkModeQLearning(double alpha, double gamma, double epsilon);
        
        // 选择动作（直接使用OFSwitch13LearningController的静态阈值）
        int ChooseAction(const NetworkState& state);
        
        // 更新Q表（直接使用OFSwitch13LearningController的静态阈值）
        void Update(const NetworkState& state, int action, const NetworkState& newState, 
                   double reward);
        
        // 打印Q表
        void PrintQTable();
    
    private:
        double alpha;    // 学习率
        double gamma;    // 折扣因子
        double epsilon;  // 探索率
        std::vector<std::vector<double>> qTable; // Q表：state x action
        // 将状态转换为ID（0=远距离，1=近距离）
        int StateToId(const NetworkState& state);
    };



    class OFSwitch13LearningController : public OFSwitch13Controller
    {
    public:
        OFSwitch13LearningController();
        virtual ~OFSwitch13LearningController();

        static TypeId GetTypeId(void);
        void SetRoutingPriority(void);
        virtual void DoDispose();

        void SetPriorityToAll();
        void CDL();

        // 根据交换机DPID获取其下所有主机信息
        std::vector<HostInfo> GetHostsBySwitch(uint64_t dpId) {
            auto it = m_switchHosts.find(dpId);
            return (it != m_switchHosts.end()) ? it->second : std::vector<HostInfo>();
        }

        // 添加ARP表项（供仿真脚本调用）
        void AddArpEntry(Ipv4Address ip, Mac48Address mac) {
            m_arpTable[ip] = mac;
        }
        
        // Q学习核心：周期性决策（暴露给仿真脚本，可直接调度）
        void PeriodicDecisionMaking();

        ofl_err HandlePacketIn(
            struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch,
            uint32_t xid);

        ofl_err HandleFlowRemoved(
            struct ofl_msg_flow_removed *msg, Ptr<const RemoteSwitch> swtch,
            uint32_t xid);
        // ------------------zi ding yi ---------------------------
        /**
         * Handle ADHOC_EXT_STAINFO message (处理ADHOC扩展的STA信息消息)
         * @param msg 实际消息结构体（adhocl_ext_stainfo类型）
         * @param swtch 远程交换机指针
         * @param xid 事务ID
         */
        ofl_err HandleAdhocExtStaInfo(
            struct adhocl_ext_stainfo *msg, Ptr<const RemoteSwitch> swtch, 
            uint32_t xid);

        
                 //------------------zi ding yi ---------------------------
        /**
        * Handle host information message（手动初始化主机信息，不依赖外部结构体）
        */
        ofl_err HandleHostInfo(void *msg, Ptr<const RemoteSwitch> swtch, 
                               uint32_t xid);
        //------------------zi ding yi ---------------------------



        // 新增：声明 HandleAdhocExtNodeStatusReport（参数类型暂时先和 .cc 一致，后续解决权限问题）
        ofl_err HandleAdhocExtNodeStatusReport(
           struct adhocl_ext_node_status_report *msg, Ptr<const RemoteSwitch> swtch, 
           uint32_t xid);

        // 静态距离阈值（公有，供Q学习类直接访问）
        static constexpr float DISTANCE_THRESHOLD = 50.0f; 
        
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

        // 私有辅助函数
        void SetRP();
        uint32_t FindNextHopPort(uint64_t srcDpid, uint64_t dstDpid);
        void GenerateSwitchFlowRules(Ptr<const RemoteSwitch> swtch, const HostInfo& newHost);
        void GenerateCrossDomainRulesForAllSwitches();
        

        //手动触发 添加以下新的私有成员函数声明
        void GenerateSwitchFlowRules(Ptr<const RemoteSwitch> swtch);
    
        // Q学习相关成员变量（关键：添加缺失的成员声明）
        std::map<Ipv4Address, NodePositionInfo> m_nodePositionMap; // IP->位置映射
        std::vector<NodePositionInfo> m_nodePositionInfo; // 位置信息列表
        NetworkModeQLearning m_networkQLearning;  // Q学习实例

        // 新增：封装Q表更新逻辑的成员函数
        void UpdateQLearning(NetworkState currentState, int action);

        // Q学习相关方法
        NetworkState EvaluateNetworkState();
        void ExecuteSwitchingAction(int action);
        double CalculateReward();
    };

} // namespace ns3
#endif /* OFSWITCH13_LEARNING_CONTROLLER_H */




// =======
// /* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// #ifndef OFSWITCH13_LEARNING_CONTROLLER_H
// #define OFSWITCH13_LEARNING_CONTROLLER_H

// #include "ofswitch13-controller.h"
// #include "ns3/ipv4-address.h"

// namespace ns3
// {
//   // 主机信息结构体：IP、MAC、端口（域内主机）
//   struct HostInfo
//   {
//     Ipv4Address ip;   // 主机IP地址
//     Mac48Address mac; // 主机MAC地址
//     uint32_t port;    // 连接的交换机端口号
//   };

//   // 定义交换机互联端口映射结构体（仅包含目标交换机和对应端口）
//   struct SwitchPortMapping
//   {
//     uint64_t destSwitchDpid; // 目标交换机DPID
//     uint32_t outputPort;     // 本交换机的输出端口（连接到目标交换机）
//   };

//   // 子网-交换机DPID映射结构体
//   struct SubnetToSwitchMapping
//   {
//     std::string subnet;        // IP子网（如"10.1.1.0/24"）
//     uint64_t targetSwitchDpid; // 该子网所属的交换机DPID
//   };

//   class OFSwitch13LearningController : public OFSwitch13Controller
//   {
//   public:
//     OFSwitch13LearningController();
//     virtual ~OFSwitch13LearningController();

//     static TypeId GetTypeId(void);
//     void SetRoutingPriority(void);
//     virtual void DoDispose();

//     // 根据交换机DPID获取其下所有主机信息
//     std::vector<HostInfo> GetHostsBySwitch(uint64_t dpId)
//     {
//       auto it = m_switchHosts.find(dpId);
//       return (it != m_switchHosts.end()) ? it->second : std::vector<HostInfo>();
//     }

//     // 添加ARP表项（供仿真脚本调用）
//     void AddArpEntry(Ipv4Address ip, Mac48Address mac)
//     {
//       m_arpTable[ip] = mac;
//     }

//     ofl_err HandlePacketIn(
//         struct ofl_msg_packet_in *msg, Ptr<const RemoteSwitch> swtch,
//         uint32_t xid);

//     ofl_err HandleFlowRemoved(
//         struct ofl_msg_flow_removed *msg, Ptr<const RemoteSwitch> swtch,
//         uint32_t xid);
//     // //------------------zi ding yi ---------------------------
//     // /**
//     // * Handle host information message
//     // */
//     // ofl_err HandleHostInfo(struct ofl_msg_host_info *msg,
//     //                              Ptr<const RemoteSwitch> swtch,
//     //                              uint32_t xid);
//     // //------------------zi ding yi ---------------------------

//     //------------------zi ding yi ---------------------------
//     /**
//      * Handle host information message（手动初始化主机信息，不依赖外部结构体）
//      */
//     ofl_err HandleHostInfo(void *msg, Ptr<const RemoteSwitch> swtch,
//                            uint32_t xid);
//     //------------------zi ding yi ---------------------------

//   protected:
//     void HandshakeSuccessful(Ptr<const RemoteSwitch> swtch);

//   private:
//     // ARP解析表（网关IP到MAC的映射）
//     typedef std::map<Ipv4Address, Mac48Address> IpMacMap_t;
//     IpMacMap_t m_arpTable;

//     // 交换机-主机映射（key: 交换机DPID，value: 域内主机列表）
//     typedef std::map<uint64_t, std::vector<HostInfo>> SwitchHostMap_t;
//     SwitchHostMap_t m_switchHosts;

//     // 交换机互联端口映射（key: 本交换机DPID，value: 目标交换机+端口列表）
//     typedef std::map<uint64_t, std::vector<SwitchPortMapping>> SwitchPortMap_t;
//     SwitchPortMap_t m_switchPortMappings;

//     // 子网->交换机DPID映射（key: 子网字符串，value: 所属交换机DPID）
//     typedef std::map<std::string, uint64_t> SubnetSwitchMap_t;
//     SubnetSwitchMap_t m_subnetToSwitchMap;

//     // 二层转发表（MAC到端口）
//     typedef std::map<Mac48Address, uint32_t> L2Table_t;
//     typedef std::map<uint64_t, L2Table_t> DatapathMap_t;
//     DatapathMap_t m_learnedInfo;

//     // 三层转发表（IP到<MAC, 端口>）
//     typedef std::map<Ipv4Address, std::pair<Mac48Address, uint32_t>> L3Table_t;
//     typedef std::map<uint64_t, L3Table_t> L3DatapathMap_t;
//     L3DatapathMap_t m_l3LearnedInfo;

//     void SetRP();
//     void SetPriorityToAll();
//   };

// } // namespace ns3
// #endif /* OFSWITCH13_LEARNING_CONTROLLER_H */
// >>>>>>> 96620a9f6415e74cbfc07275f0890e519cafe6a0
