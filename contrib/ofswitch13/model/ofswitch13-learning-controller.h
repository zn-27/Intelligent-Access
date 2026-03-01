/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef OFSWITCH13_LEARNING_CONTROLLER_H
#define OFSWITCH13_LEARNING_CONTROLLER_H

#include "ofswitch13-controller.h"
#include "ns3/ipv4-address.h"
#include <set>
#include <map>


namespace ns3
{

    struct LinkStats {
    uint64_t mac_address;

    // 当前值
    double throughput;    // 对应报错中的 s.throughput
    double lossRate;      // 对应报错中的 s.lossRate
    double delay;         // 对应报错中的 s.delay
    double jitter;

    // 历史值（用于计算奖励的增量 Improvement）
    double prevThroughput; // 对应报错中的 s.prevThroughput
    double prevLossRate;   // 对应报错中的 s.prevLossRate
    double prevDelay;      // 对应报错中的 s.prevDelay

    // 你之前定义的其他字段可以保留
    uint32_t rxPackets;
    uint32_t txPackets;
    double   throughputKbps;
    double   lastRssi;
    };

    // 端口观察信息（用于临时观察低于阈值的端口）
    struct PortObservationInfo {
        double firstSeenTime;     // 首次发现时间
        double lastSeenTime;      // 最后发现时间
        uint32_t consecutiveHits; // 连续达到阈值次数
        double lastThroughput;    // 最后一次吞吐量

        PortObservationInfo()
            : firstSeenTime(0), lastSeenTime(0), consecutiveHits(0), lastThroughput(0) {}
    };

    // 端口注册信息（已注册的活跃端口）
    struct PortRegistrationInfo {
        int linkIndex;            // 分配的链路索引
        double firstSeenTime;     // 首次注册时间
        double lastUpdateTime;    // 最后更新时间
        uint64_t reportCount;     // 累计上报次数
        uint32_t consecutiveLows; // 连续低于阈值次数
        double lastThroughput;    // 最后一次吞吐量

        PortRegistrationInfo()
            : linkIndex(-1), firstSeenTime(0), lastUpdateTime(0),
              reportCount(0), consecutiveLows(0), lastThroughput(0) {}
    };

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
        float distanceVariance;     // NEW: 节点分散度（方差）
        double totalThroughput;
        double maxLossRate;
    };

    // 前向声明，解决循环依赖
    class OFSwitch13LearningController;

    // Q学习网络模式决策类
    class NetworkModeQLearning {
    public:
        // Action enumeration
        enum Action { MULTI = 0, ADHOC = 1 };
        static constexpr int ACTION_COUNT = 2;

        // 构造函数
        NetworkModeQLearning(double alpha, double gamma, double epsilon);

        // 选择动作（直接使用OFSwitch13LearningController的静态阈值）
        int ChooseAction(const NetworkState& state);

        // 更新Q表（直接使用OFSwitch13LearningController的静态阈值）
        void Update(const NetworkState& state, int action, const NetworkState& newState,
                   double reward);

        // 打印Q表
        void PrintQTable();

        // Initialize Q-table with smart defaults
        void InitializeQTable();

    private:
        // Experience structure for replay buffer
        struct Experience {
            NetworkState state;
            int action;
            double reward;
            NetworkState nextState;
        };

        double alpha;    // 学习率
        double gamma;    // 折扣因子
        double epsilon;  // 探索率
        double baseEpsilon; // 存储基础探索率，用于动态计算ε
        std::vector<std::vector<double>> qTable; // Q表：state x action
        std::deque<Experience> replayBuffer;     // Experience replay buffer
        static constexpr size_t MAX_BUFFER_SIZE = 1000;

        // 将状态转换为ID（0=远距离，1=近距离）
        int StateToId(const NetworkState& state);
        // Replay batch of experiences
        void ReplayBatch(int batchSize);
    };



    class OFSwitch13LearningController : public OFSwitch13Controller
    {
    public:

        // 必须在这里声明这个变量，否则报错 1016:24
        std::vector<LinkStats> m_linkStats;
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



        //  HandleAdhocExtNodeStatusReport
        ofl_err HandleAdhocExtNodeStatusReport(
           struct adhocl_ext_node_status_report *msg, Ptr<const RemoteSwitch> swtch, 
           uint32_t xid);
        //  HandleAdhocExtFlowStatusReport
        ofl_err HandleAdhocExtFlowStatusReport(
           struct adhocl_ext_flow_status_report *msg, Ptr<const RemoteSwitch> swtch, 
           uint32_t xid);            
        // 静态距离阈值（公有，供Q学习类直接访问）
        static constexpr float DISTANCE_THRESHOLD = 25.0f; 
        
    protected:
        void HandshakeSuccessful(Ptr<const RemoteSwitch> swtch);

        // 定义模式：0 代表 MULTI, 1 代表 ADHOC
        int m_currentMode;

        
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
        int m_lastAction = -1;  // Track last action for switching cost
        static constexpr double SWITCHING_COST = 0.1;  // Penalty for mode changes

        // 新增：封装Q表更新逻辑的成员函数
        void UpdateQLearning(NetworkState currentState, int action);

        // Q学习相关方法
        NetworkState EvaluateNetworkState();
        void ExecuteSwitchingAction(int action);
        double CalculateReward();

        // 链路状态管理（新增）
        std::map<uint16_t, int> m_portToLinkIndex;  // 端口到链路索引的映射
        std::vector<bool> m_activeLinks;            // 有效链路标志
        int m_activeLinkCount;                      // 实际有效链路数量
        static constexpr int MAX_LINKS = 64;        // 最大链路数（从10扩展到64）

        // 新增函数声明
        std::vector<double> CalculateThroughputWeights();  // 动态计算吞吐量权重

        // ============== 动态端口捕获相关 ==============
        // 动态阈值参数
        static constexpr double MIN_THROUGHPUT_THRESHOLD = 50.0;   // Kbps，最小阈值
        static constexpr double THRESHOLD_COEFFICIENT = 0.3;        // 平均值系数
        static constexpr uint32_t OBSERVE_COUNT_THRESHOLD = 3;      // 观察阈值：连续N次达到才注册
        static constexpr uint32_t INACTIVE_COUNT_THRESHOLD = 5;     // 失活阈值：连续N次低于才删除

        // 动态阈值管理
        double m_currentThroughputThreshold;  // 当前动态阈值

        // 端口状态管理
        std::map<uint16_t, PortObservationInfo> m_observedPorts;  // 临时观察端口
        std::map<uint16_t, PortRegistrationInfo> m_portRegistry;   // 已注册端口
        std::set<int> m_usedLinkIndices;                           // 已使用的链路索引集合

        // 动态阈值管理方法
        double CalculateDynamicThreshold();                    // 计算动态阈值
        void UpdateDynamicThreshold();                         // 更新动态阈值

        // 端口状态管理方法
        int RegisterPort(uint16_t port);                       // 注册新端口
        void UnregisterPort(uint16_t port);                    // 删除端口注册
        bool IsPortRegistered(uint16_t port) const;            // 检查端口是否已注册
        bool IsPortObserved(uint16_t port) const;              // 检查端口是否在观察列表

        int AllocateLinkIndex();                               // 分配新的链路索引
        void DeallocateLinkIndex(int index);                   // 释放链路索引

        // 端口处理方法
        void ProcessPortReport(uint16_t port, double throughput);  // 处理端口流量上报
        void CheckInactivePorts();                             // 检查并清理失活端口
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
