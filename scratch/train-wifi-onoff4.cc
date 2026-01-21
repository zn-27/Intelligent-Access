// // /* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
// // /*
// //  * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块
// //  *
// //  * - 域 A: hostsA (2 主机) -- sw1
// //  * - 域 B: hostsB (2 主机) -- sw2
// //  * - 域 C: 3个WiFi终端 + 1个AP -- sw3
// //  * - 交换机 sw1 与 sw2 直接连接，sw2 与 sw3 直接连接，sw3 与 sw1 直接连接（三角互连）
// //  * - 单 OpenFlow 控制器管理 sw1 和 sw2 和 sw3
// //  * - 为所有 AP 分配 IP 并注册到控制器
// //  * -
// //  * 构建：确保 ns-3 已构建并启用 ofswitch13 模块。
// //  */

// // #include <ns3/core-module.h>
// // #include <ns3/network-module.h>
// // #include <ns3/csma-module.h>
// // #include <ns3/internet-module.h>
// // #include <ns3/ofswitch13-module.h>
// // #include <ns3/internet-apps-module.h>
// // #include "ns3/point-to-point-module.h"
// // #include "ns3/wifi-module.h"
// // #include "ns3/mobility-module.h"
// // #include "ns3/applications-module.h"
// // #include <ns3/internet-apps-module.h>
// // #include "ns3/bridge-helper.h"
// // #include "ns3/aodv-module.h"
// // #include "ns3/olsr-module.h"
// // #include "ns3/netanim-module.h"
// // #include "ns3/flow-monitor.h"
// // #include "ns3/flow-monitor-helper.h"
// // #include "ns3/flow-monitor-module.h"
// // #include "ns3/ipv4-flow-classifier.h"  
// // #include "ns3/ipv4-address.h"
// // #include "ns3/vector.h"
// // #include <cstdint>
// // #include <string>
// // #include <map>
// // #include <sstream>
// // #include <fstream>
// // #include <cmath>
// // #include "ns3/ptr.h"
// // #include "ns3/object.h"
// // using namespace ns3;


// // // 全局FlowMonitorHelper
// // FlowMonitorHelper flowmonHelper;
// // // 性能指标存储
// // std::map<std::string, double> currentPerformanceMetrics;
// // std::map<std::string, double> previousPerformanceMetrics;
// // // 添加全局变量存储历史性能数据
// // std::map<uint32_t, FlowMonitor::FlowStats> previousFlowStats;
// // double previousTotalThroughput = 0.0;
// // double previousTotalLossRate = 0.0;
// // double previousAverageDelay = 0.0;

// // // 距离阈值配置（可调整）
// // const double DISTANCE_THRESHOLD = 25.0; // 平均节点距离阈值（米）：小于该值切换到MULTI，大于切换到ADHOC

// // // 网络状态结构体（简化：仅保留节点平均距离作为核心判断依据）
// // struct NetworkState {
// //     double averageNodeDistance; // 三维空间中节点间平均距离
// //     int nodeDensityLevel;        // 基于距离的密度等级：0-低密度（距离>阈值），1-高密度（距离<=阈值）
// // };

// // // 全局变量用于存储当前状态（避免lambda表达式的问题）
// // NetworkState g_currentState;
// // std::string g_currentMode;

// // // Q-Learning 算法（简化状态和动作逻辑）
// // class NetworkModeQLearning {
// // public:
// //     NetworkModeQLearning(double alpha, double gamma, double epsilon)
// //         : alpha(alpha), gamma(gamma), epsilon(epsilon) {
// //         // 初始化 Q 表：2种状态（低/高密度）× 3个动作
// //         for (int i = 0; i < 2; ++i) {
// //             qTable[i] = std::vector<double>(3, 0.0); // 动作0:MULTI_TO_ADHOC, 1:ADHOC_TO_MULTI, 2:KEEP_MODE
// //         }
// //     }

// //     // 将网络状态转换为状态ID（简化为0/1）
// //     int StateToId(const NetworkState& state) {
// //         return state.nodeDensityLevel; // 0=低密度，1=高密度
// //     }

// //     // 选择动作（基于节点距离阈值决策）
// //     int ChooseAction(const NetworkState& state) {
// //         int stateId = StateToId(state);
// //         int action;

// //         // 基于距离阈值的启发式决策（优先）
// //         if (state.averageNodeDistance <= DISTANCE_THRESHOLD) {
// //             // 平均距离小于阈值：适合MULTI模式（高密度场景）
// //             if (g_currentMode != "MULTI") {
// //                 action = 1; // 切换到MULTI
// //                 std::cout << "[全局Q学习] 时间: " << Simulator::Now().GetSeconds() 
// //                           << "秒 - 状态ID: " << stateId << " - 距离触发: 平均距离" << state.averageNodeDistance 
// //                           << "<=" << DISTANCE_THRESHOLD << "，切换到MULTI模式" << std::endl;
// //             } else {
// //                 action = 2; // 已在MULTI，保持
// //                 std::cout << "[全局Q学习] 时间: " << Simulator::Now().GetSeconds() 
// //                           << "秒 - 状态ID: " << stateId << " - 距离触发: 平均距离" << state.averageNodeDistance 
// //                           << "<=" << DISTANCE_THRESHOLD << "，保持MULTI模式" << std::endl;
// //             }
// //         } else {
// //             // 平均距离大于阈值：适合ADHOC模式（低密度场景）
// //             if (g_currentMode != "ADHOC") {
// //                 action = 0; // 切换到ADHOC
// //                 std::cout << "[全局Q学习] 时间: " << Simulator::Now().GetSeconds() 
// //                           << "秒 - 状态ID: " << stateId << " - 距离触发: 平均距离" << state.averageNodeDistance 
// //                           << ">" << DISTANCE_THRESHOLD << "，切换到ADHOC模式" << std::endl;
// //             } else {
// //                 action = 2; // 已在ADHOC，保持
// //                 std::cout << "[全局Q学习] 时间: " << Simulator::Now().GetSeconds() 
// //                           << "秒 - 状态ID: " << stateId << " - 距离触发: 平均距离" << state.averageNodeDistance 
// //                           << ">" << DISTANCE_THRESHOLD << "，保持ADHOC模式" << std::endl;
// //             }
// //         }

// //         // 保留epsilon探索机制（10%概率随机选择）
// //         if ((double)rand() / RAND_MAX < epsilon) {
// //             int randomAction = rand() % 3;
// //             std::cout << "[全局Q学习] 时间: " << Simulator::Now().GetSeconds() 
// //                       << "秒 - 探索机制触发: 随机选择动作 " << randomAction << " (原建议动作: " << action << ")" << std::endl;
// //             action = randomAction;
// //         }

// //         // 避免无效切换（当前模式已符合目标模式时强制保持）
// //         if ((g_currentMode == "ADHOC" && action == 0) || (g_currentMode == "MULTI" && action == 1)) {
// //             action = 2;
// //             std::cout << "[全局Q学习] 时间: " << Simulator::Now().GetSeconds() 
// //                       << "秒 - 调整: 已在目标模式，改为保持模式 " << action << std::endl;
// //         }

// //         return action;
// //     }

// //     // 更新 Q 表（保留原有逻辑，基于FlowMonitor性能奖励）
// //     void Update(const NetworkState& state, int action, const NetworkState& newState, double reward) {
// //         int stateId = StateToId(state);
// //         int newStateId = StateToId(newState);
    
// //         double qPredict = qTable[stateId][action];
// //         double qTarget = reward + gamma * *std::max_element(qTable[newStateId].begin(), qTable[newStateId].end());
// //         qTable[stateId][action] += alpha * (qTarget - qPredict);  // Q 表更新公式
    
// //         std::cout << "[全局Q学习] 更新 - 时间: " << Simulator::Now().GetSeconds() << "秒" << std::endl;
// //         std::cout << "  状态: " << stateId << "(" << state.averageNodeDistance << "m) -> 动作: " << action 
// //                   << " -> 新状态: " << newStateId << "(" << newState.averageNodeDistance << "m)" << std::endl;
// //         std::cout << "  奖励: " << reward << " | 预测Q值: " << qPredict << " | 目标Q值: " << qTarget << std::endl;
// //         std::cout << "  更新后Q[" << stateId << "][" << action << "] = " << qTable[stateId][action] << std::endl;
// //     }

// //     // 打印 Q 表
// //     void PrintQTable() {
// //         std::cout << "[全局Q学习] 当前Q表 (状态0=低密度，状态1=高密度):" << std::endl;
// //         for (const auto& entry : qTable) {
// //             std::cout << "  状态 " << entry.first << ": ";
// //             std::cout << "动作0(ADHOC)=" << entry.second[0] << ", ";
// //             std::cout << "动作1(MULTI)=" << entry.second[1] << ", ";
// //             std::cout << "动作2(保持)=" << entry.second[2] << std::endl;
// //         }
// //         std::cout << std::endl;
// //     }

// // private:
// //     std::map<int, std::vector<double>> qTable;  // 状态到动作值的映射（2个状态×3个动作）
// //     double alpha, gamma, epsilon;  // 学习率、折扣因子、探索率
// // };

// // // 全局Q-Learning实例
// // NetworkModeQLearning global_rl(0.1, 0.9, 0.1);

// // // 函数声明
// // void GlobalPeriodicModeSwitch(NodeContainer staNodes, NodeContainer apNodes, 
// //                             NetDeviceContainer staWifiDevs, NetDeviceContainer adhocDevs);
// // void EvaluateAndLearn(NodeContainer staNodes, NodeContainer apNodes, 
// //                     NetDeviceContainer staWifiDevs, NetDeviceContainer adhocDevs,
// //                     int action);
// // NetworkState EvaluateNetworkState(NodeContainer staNodes);
// // double CalculateRewardBasedOnPerformance(std::map<FlowId, FlowMonitor::FlowStats> flowStats, 
// //                                        std::map<FlowId, FlowMonitor::FlowStats> prevFlowStats);
// // void ExecuteGlobalSwitch(int action, NodeContainer staNodes, NetDeviceContainer staWifiDevs, NetDeviceContainer adhocDevs);
// // void DisableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev);  
// // void EnableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev);   
// // double Calculate3dDistance(const Vector& pos1, const Vector& pos2); // 三维距离计算

// // // 检测当前网络模式（保留原有逻辑）
// // std::string DetectCurrentNetworkMode(NodeContainer staNodes, NetDeviceContainer staWifiDevs, NetDeviceContainer adhocDevs) {
// //     int multiCount = 0;  // 多中心模式计数
// //     int adhocCount = 0;  // 无中心模式计数
    
// //     for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
// //         Ptr<Node> node = staNodes.Get(i);
// //         Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        
// //         // 检查WiFi设备状态（MULTI模式）
// //         Ptr<NetDevice> wifiDev = staWifiDevs.Get(i);
// //         uint32_t wifiIdx = ipv4->GetInterfaceForDevice(wifiDev);
// //         if (wifiIdx != uint32_t(-1) && ipv4->IsUp(wifiIdx)) {
// //             multiCount++;
// //         }
        
// //         // 检查Adhoc设备状态（ADHOC模式）
// //         Ptr<NetDevice> adhocDev = adhocDevs.Get(i);
// //         uint32_t adhocIdx = ipv4->GetInterfaceForDevice(adhocDev);
// //         if (adhocIdx != uint32_t(-1) && ipv4->IsUp(adhocIdx)) {
// //             adhocCount++;
// //         }
// //     }
    
// //     std::cout << "  [模式检测] MULTI设备: " << multiCount << ", ADHOC设备: " << adhocCount << std::endl;
    
// //     if (multiCount > adhocCount) {
// //         return "MULTI";
// //     } else if (adhocCount > multiCount) {
// //         return "ADHOC";
// //     } else {
// //         return "MIXED";
// //     }
// // }

// // // 三维距离计算函数
// // double Calculate3dDistance(const Vector& pos1, const Vector& pos2) {
// //     double dx = pos1.x - pos2.x;
// //     double dy = pos1.y - pos2.y;
// //     double dz = pos1.z - pos2.z; // 新增z轴距离计算
// //     return sqrt(dx*dx + dy*dy + dz*dz);
// // }

// // // 评估整个网络状态（简化：仅计算三维节点平均距离和密度等级）
// // NetworkState EvaluateNetworkState(NodeContainer staNodes) {
// //     NetworkState state;
// //     double totalDistance = 0.0;
// //     int pairCount = 0;
// //     uint32_t nodeCount = staNodes.GetN();
    
// //     if (nodeCount < 2) {
// //         // 节点数不足2个，默认低密度
// //         state.averageNodeDistance = DISTANCE_THRESHOLD + 10.0;
// //         state.nodeDensityLevel = 0;
// //         std::cout << "[全局Q学习] 网络状态评估: 节点数不足2个，默认低密度" << std::endl;
// //         return state;
// //     }
    
// //     // 计算所有节点对之间的三维平均距离
// //     for (uint32_t i = 0; i < nodeCount; ++i) {
// //         Vector pos1 = staNodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
// //         for (uint32_t j = i + 1; j < nodeCount; ++j) {
// //             Vector pos2 = staNodes.Get(j)->GetObject<MobilityModel>()->GetPosition();
// //             double distance = Calculate3dDistance(pos1, pos2);
// //             totalDistance += distance;
// //             pairCount++;
// //         }
// //     }
    
// //     // 计算平均距离
// //     state.averageNodeDistance = totalDistance / pairCount;
    
// //     // 基于距离阈值判断密度等级
// //     state.nodeDensityLevel = (state.averageNodeDistance <= DISTANCE_THRESHOLD) ? 1 : 0;
    
// //     // 输出详细信息
// //     std::cout << "[全局Q学习] 网络状态评估:" << std::endl;
// //     std::cout << "  三维平均节点距离: " << state.averageNodeDistance << " 米" << std::endl;
// //     std::cout << "  密度等级: " << state.nodeDensityLevel << " (" 
// //               << (state.nodeDensityLevel == 1 ? "高密度" : "低密度") << ")" << std::endl;
// //     std::cout << "  距离阈值: " << DISTANCE_THRESHOLD << " 米" << std::endl;
    
// //     return state;
// // }

// // // 基于FlowMonitor性能计算奖励（保留原有逻辑）
// // double CalculateRewardBasedOnPerformance(std::map<FlowId, FlowMonitor::FlowStats> flowStats, 
// //                                        std::map<FlowId, FlowMonitor::FlowStats> prevFlowStats) {
// //     // 计算当前网络性能指标
// //     double totalThroughput = 0.0;
// //     double totalLossRate = 0.0;
// //     double averageDelay = 0.0;
// //     int flowCount = 0;
    
// //     for (auto const& flow : flowStats) {
// //         double throughput = 0.0;
// //         double lossRate = 0.0;
// //         double delay = 0.0;
        
// //         if (flow.second.timeLastRxPacket.GetSeconds() > flow.second.timeFirstTxPacket.GetSeconds()) {
// //             throughput = flow.second.rxBytes * 8.0 /
// //                 (flow.second.timeLastRxPacket.GetSeconds() - flow.second.timeFirstTxPacket.GetSeconds());
// //         }
        
// //         if (flow.second.txPackets > 0) {
// //             lossRate = (flow.second.txPackets - flow.second.rxPackets) / (double)flow.second.txPackets;
// //         }
        
// //         if (flow.second.rxPackets > 0) {
// //             delay = flow.second.delaySum.GetSeconds() / flow.second.rxPackets;
// //         }
        
// //         totalThroughput += throughput;
// //         totalLossRate += lossRate;
// //         averageDelay += delay;
// //         flowCount++;
// //     }
    
// //     if (flowCount > 0) {
// //         totalThroughput /= flowCount;
// //         totalLossRate /= flowCount;
// //         averageDelay /= flowCount;
// //     }
    
// //     // 输出当前性能指标
// //     std::cout << "[奖励计算] 当前性能指标:" << std::endl;
// //     std::cout << "  平均吞吐量: " << totalThroughput / 1000 << " Kbps" << std::endl;
// //     std::cout << "  平均丢包率: " << totalLossRate * 100 << "%" << std::endl;
// //     std::cout << "  平均延迟: " << averageDelay * 1000 << " ms" << std::endl;
    
// //     // 计算性能改进值
// //     double throughputImprovement = totalThroughput - previousTotalThroughput;
// //     double lossRateImprovement = previousTotalLossRate - totalLossRate; // 丢包率降低是改进
// //     double delayImprovement = previousAverageDelay - averageDelay;     // 延迟降低是改进
    
// //     // 更新历史值
// //     previousTotalThroughput = totalThroughput;
// //     previousTotalLossRate = totalLossRate;
// //     previousAverageDelay = averageDelay;
    
// //     // 计算加权奖励值（权重不变）
// //     double reward = 0.0;
// //     reward += throughputImprovement * 0.3;      // 吞吐量权重 30%
// //     reward += lossRateImprovement * 0.5;        // 丢包率权重 50%
// //     reward += delayImprovement * 0.2;           // 延迟权重 20%
    
// //     // 输出奖励构成
// //     std::cout << "[奖励计算] 性能改进:" << std::endl;
// //     std::cout << "  吞吐量改进: " << throughputImprovement / 1000 << " Kbps → 贡献: " << throughputImprovement * 0.3 << std::endl;
// //     std::cout << "  丢包率改进: " << lossRateImprovement * 100 << "百分点 → 贡献: " << lossRateImprovement * 0.5 << std::endl;
// //     std::cout << "  延迟改进: " << delayImprovement * 1000 << " ms → 贡献: " << delayImprovement * 0.2 << std::endl;
// //     std::cout << "[奖励计算] 总奖励: " << reward << std::endl;
    
// //     return reward;
// // }

// // // 执行全局模式切换（保留原有逻辑）
// // void ExecuteGlobalSwitch(int action, NodeContainer staNodes, NetDeviceContainer staWifiDevs, NetDeviceContainer adhocDevs) {
// //     std::string actionNames[] = {"MULTI_TO_ADHOC", "ADHOC_TO_MULTI", "KEEP_MODE"};
// //     std::cout << "[全局Q学习] 执行全局切换: " << actionNames[action] << std::endl;
    
// //     if (action == 0) { // MULTI_TO_ADHOC (多中心切换到无中心)
// //         std::cout << "[全局Q学习] 切换所有节点到ADHOC模式 (低密度环境，平均距离>阈值)" << std::endl;
// //         for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
// //             DisableDeviceLogical(staNodes.Get(i), staWifiDevs.Get(i));
// //             EnableDeviceLogical(staNodes.Get(i), adhocDevs.Get(i));
// //         }
// //     } 
// //     else if (action == 1) { // ADHOC_TO_MULTI (无中心切换到多中心)
// //         std::cout << "[全局Q学习] 切换所有节点到MULTI模式 (高密度环境，平均距离<=阈值)" << std::endl;
// //         for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
// //             EnableDeviceLogical(staNodes.Get(i), staWifiDevs.Get(i));
// //             DisableDeviceLogical(staNodes.Get(i), adhocDevs.Get(i));
// //         }
// //     }
// //     else {
// //         std::cout << "[全局Q学习] 保持当前全局模式不变" << std::endl;
// //     }
// // }

// // // 评估切换结果并进行学习（保留原有逻辑，简化状态参数）
// // void EvaluateAndLearn(NodeContainer staNodes, NodeContainer apNodes, 
// //                     NetDeviceContainer staWifiDevs, NetDeviceContainer adhocDevs,
// //                     int action)
// // {
// //     std::cout << "[全局Q学习] === 评估切换结果并学习 ===" << std::endl;
    
// //     // 获取当前网络性能数据
// //     Ptr<FlowMonitor> monitor = flowmonHelper.GetMonitor();
// //     monitor->CheckForLostPackets();
// //     std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
// //     // 计算基于实际性能的奖励
// //     double reward = CalculateRewardBasedOnPerformance(stats, previousFlowStats);
    
// //     // 保存当前统计数据供下次使用
// //     previousFlowStats = stats;
    
// //     // 评估新状态（仅基于节点距离）
// //     NetworkState newState = EvaluateNetworkState(staNodes);
    
// //     // 更新Q表
// //     global_rl.Update(g_currentState, action, newState, reward);
    
// //     // 定期打印Q表（每5秒）
// //     if (fmod(Simulator::Now().GetSeconds(), 5.0) < 1.0) {
// //         global_rl.PrintQTable();
// //     }
    
// //     std::cout << "[全局Q学习] ====================================" << std::endl;
// // }

// // // 全局周期模式切换（简化状态评估，聚焦距离感知）
// // void GlobalPeriodicModeSwitch(NodeContainer staNodes, NodeContainer apNodes, 
// //                             NetDeviceContainer staWifiDevs, NetDeviceContainer adhocDevs)
// // {
// //     std::cout << "[全局Q学习] === 全局周期模式切换决策 ===" << std::endl;
    
// //     // 检测当前网络模式
// //     g_currentMode = DetectCurrentNetworkMode(staNodes, staWifiDevs, adhocDevs);
// //     std::cout << "[全局Q学习] 当前网络模式: " << g_currentMode << std::endl;
    
// //     // 评估当前网络状态（仅基于三维节点距离）
// //     g_currentState = EvaluateNetworkState(staNodes);
    
// //     // 选择动作（基于距离阈值+Q学习）
// //     int action = global_rl.ChooseAction(g_currentState);
    
// //     // 输出最终决策
// //     std::cout << "[全局Q学习] 最终决策: " << (action == 0 ? "切换到ADHOC模式" : 
// //                                            action == 1 ? "切换到MULTI模式" : "保持当前模式") << std::endl;
    
// //     // 执行模式切换
// //     ExecuteGlobalSwitch(action, staNodes, staWifiDevs, adhocDevs);
    
// //     // 等待一段时间让网络稳定，然后评估结果并更新Q表
// //     Simulator::Schedule(Seconds(0.5), &EvaluateAndLearn, staNodes, apNodes, 
// //                        staWifiDevs, adhocDevs, action);
    
// //     // 周期性调用（每4秒进行一次更新）
// //     Simulator::Schedule(Seconds(4.0), &GlobalPeriodicModeSwitch, staNodes, apNodes, staWifiDevs, adhocDevs);
// // }

// // // ---------------------------------------------------------
// // // 函数：禁用 AdHoc 接口
// // // ---------------------------------------------------------
// // // 逻辑 down
// // // ---------------------------------------------------------
// // // 函数：逻辑上下线设备
// // // enable = true 表示开启，false 表示关闭
// // // ---------------------------------------------------------

// // void DisableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
// // {
// //     Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
// //     uint32_t idx = ipv4->GetInterfaceForDevice(dev);
// //     if (idx != uint32_t(-1))
// //         ipv4->SetDown(idx);
// // }
// // void EnableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
// // {
// //     Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
// //     uint32_t idx = ipv4->GetInterfaceForDevice(dev);
// //     if (idx != uint32_t(-1))
// //         ipv4->SetUp(idx);
// // }


// // std::map<uint32_t, double> lastRxBytes;    // 上一次采样时接收字节数
// // std::map<uint32_t, double> lastPacketRtt;  // 上一次平均 RTT

// // void MonitorFlow(Ptr<FlowMonitor> monitor, FlowMonitorHelper* flowHelper, double interval, std::ofstream* fout)
// // {
// //     monitor->CheckForLostPackets();
// //     Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
// //     std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

// //     double now = Simulator::Now().GetSeconds();
// //     *fout << now;

// //     // 按端口号 port0-3 对应顺序输出链路 1-4
// //     std::vector<uint16_t> ports = { 9, 10, 11, 12 }; // 对应 port0, port1, port2, port3

// //     for (auto port : ports)
// //     {
// //         // 找到对应端口的 FlowId
// //         FlowId fid = 0;
// //         for (auto const& flow : stats)
// //         {
// //             Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
// //             if (t.destinationPort == port)
// //             {
// //                 fid = flow.first;
// //                 break;
// //             }
// //         }

// //         double throughput = 0.0;
// //         double lossRate = 0.0;
// //         double avgRtt = 0.0;
// //         double jitter = 0.0;

// //         if (fid != 0 && stats.count(fid))
// //         {
// //             FlowMonitor::FlowStats flowStats = stats[fid];



// //             // 瞬时吞吐量（Kbps）
// //             double rxBytesDelta = flowStats.rxBytes;
// //             if (lastRxBytes.count(fid))
// //             {
// //                 rxBytesDelta -= lastRxBytes[fid];
// //             }
// //             throughput = rxBytesDelta * 8.0 / (interval * 1024.0); // Kbps
// //             lastRxBytes[fid] = flowStats.rxBytes;

// //             // 丢包率
// //             if (flowStats.txPackets > 0)
// //             {
// //                 lossRate = 100.0 * (flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets;
// //             }

// //             // 平均 RTT
// //             if (flowStats.rxPackets > 0)
// //             {
// //                 avgRtt = flowStats.delaySum.GetSeconds() / flowStats.rxPackets * 1000.0; // ms

// //                 double packetRtt = avgRtt;
// //                 if (lastPacketRtt.count(fid))
// //                 {
// //                     jitter = std::abs(packetRtt - lastPacketRtt[fid]);
// //                 }
// //                 lastPacketRtt[fid] = packetRtt;
// //             }
// //         }

// //         *fout << "," << throughput
// //             << "," << lossRate
// //             << "," << avgRtt
// //             << "," << jitter;
// //     }

// //     *fout << std::endl;

// //     Simulator::Schedule(Seconds(interval), &MonitorFlow, monitor, flowHelper, interval, fout);
// // }

// // //静态容器保存所有应用实例
// // static ApplicationContainer apps;

// // void SendPacketByNodeId(Ptr<Node> sender, Ipv4Address receiverAddr, uint16_t port,
// //     std::string dataRate = "600kbps", uint32_t packetSize = 1024,
// //     double start = 1.0, double stop = 9.0, Ptr<NetDevice> device = nullptr)
// // {
// //     // 创建 OnOffHelper，用于发送 UDP 包
// //     OnOffHelper onOffHelper("ns3::UdpSocketFactory", Address());
// //     onOffHelper.SetAttribute("DataRate", StringValue(dataRate));
// //     onOffHelper.SetAttribute("PacketSize", UintegerValue(packetSize));
// //     onOffHelper.SetAttribute("StartTime", TimeValue(Seconds(start)));
// //     onOffHelper.SetAttribute("StopTime", TimeValue(Seconds(stop)));

// //     // 目标地址和端口
// //     InetSocketAddress dst(receiverAddr, port);
// //     onOffHelper.SetAttribute("Remote", AddressValue(dst));

// //     if (device == nullptr) {
// //         std::cerr << "Error: Device is nullptr!" << std::endl;
// //         return;
// //     }

// //     // 如果指定了设备接口，设置使用该接口
// //     if (device != nullptr)
// //     {
// //         // 显式指定使用的接口
// //         Ptr<Ipv4> ipv4 = sender->GetObject<Ipv4>();
// //         uint32_t interfaceIndex = ipv4->GetInterfaceForDevice(device);

// //         // 获取该接口的本地地址
// //         Ipv4Address localAddr = ipv4->GetAddress(interfaceIndex, 0).GetLocal();

// //         // 设置 OnOffHelper 的 Local 属性为这个接口的地址
// //         onOffHelper.SetAttribute("Local", AddressValue(InetSocketAddress(localAddr, 0)));
// //     }

// //     // 安装应用程序到发送节点
// //     apps.Add(onOffHelper.Install(sender));
// // }

// // // 计算节点到目标的距离
// // double CalculateDistanceToTarget(Ptr<Node> node, Ptr<Node> targetNode)
// // {
// //     Vector nodePos = node->GetObject<MobilityModel>()->GetPosition();  // 获取节点位置
// //     Vector targetPos = targetNode->GetObject<MobilityModel>()->GetPosition();  // 获取目标位置

// //     return CalculateDistance(nodePos, targetPos);  // 使用 CalculateDistance 计算距离
// // }

// // // 域内数据流函数
// // void IntraDomainFlows(NodeContainer& domainNodes, Ipv4InterfaceContainer& interfaces, uint16_t basePort, double simTime)
// // {
// //     int port = basePort;

// //     for (size_t i = 0; i < domainNodes.GetN(); i++) {
// //         for (size_t j = 0; j < domainNodes.GetN(); j++) {
// //             if (i != j) {
// //                 Ipv4Address dstAddr = interfaces.GetAddress(j);

// //                 // 获取源节点的设备接口
// //                 Ptr<Node> sender = domainNodes.Get(i);
// //                 Ptr<Ipv4> ipv4 = sender->GetObject<Ipv4>();
// //                 uint32_t interfaceIndex = ipv4->GetInterfaceForDevice(sender->GetDevice(1));  // 获取第二个设备接口
// //                 Ptr<NetDevice> device = sender->GetDevice(interfaceIndex);

// //                 // 调用 SendPacketByNodeId 函数并传入设备接口
               
// //                 SendPacketByNodeId(sender, dstAddr, port, "600kbps", 1024, 0.0, simTime-1, device);

// //                 port++;  // 每条链路端口递增
// //             }
// //         }
// //     }
// // }

// // // 域外数据流函数
// // void InterDomainFlows(NodeContainer& localNodes, Ipv4InterfaceContainer& remoteInterfaces, uint16_t basePort, double simTime)
// // {
// //     int port = basePort;
// //     int count = 0;

// //     for (size_t i = 0; i < localNodes.GetN(); i++) {
// //         for (size_t j = 0; j < remoteInterfaces.GetN(); j++) {
// //             if (count >= 6) return;  // 限制 x 条链路

// //             Ipv4Address dstAddr = remoteInterfaces.GetAddress(j);

// //             // 获取源节点的设备接口
// //             Ptr<Node> sender = localNodes.Get(i);
// //             Ptr<Ipv4> ipv4 = sender->GetObject<Ipv4>();
// //             uint32_t interfaceIndex = ipv4->GetInterfaceForDevice(sender->GetDevice(2));  // 获取第三个设备接口
// //             Ptr<NetDevice> device = sender->GetDevice(interfaceIndex);

// //             // 调用 SendPacketByNodeId 函数并传入设备接口
            
// //             SendPacketByNodeId(sender, dstAddr, port, "600kbps", 1024, 0.0, simTime-1, device);

// //             port++;
// //             count++;
// //         }
// //     }
// // }

// // // —— 全局变量 —— //
// // NodeContainer StaC;                       // 域C节点
// // Ipv4InterfaceContainer adhocIfC;          // 域C的adhoc接口
// // Ipv4InterfaceContainer ifOtherDomain;     // 域外接口

// // // 定义周期性监控函数
// // void MonitorNodeDistance(  Ptr<Node> ApC, double range, double simTime, double interval)
// // {
// //     uint32_t totalNodes = StaC.GetN();
// //     uint32_t nodesInRange = 0;

// //     for (size_t i = 0; i < totalNodes; i++) {
// //         Ptr<Node> node = StaC.Get(i);
// //         double distanceToAp = CalculateDistanceToTarget(node, ApC);

// //         if (distanceToAp <= range) {
// //             nodesInRange++;
// //         }
// //     }

// //     if (nodesInRange == totalNodes) {
// //         // 所有节点都进入范围，启动域内数据流
// //         IntraDomainFlows(StaC, adhocIfC, 8000, simTime);
// //     }
// //     else {
// //         // 有节点在域外，启动域外数据流
// //         InterDomainFlows(StaC, ifOtherDomain, 9000, simTime);
// //     }

// //     // 周期性调用自己
// //     Simulator::Schedule(Seconds(interval), &MonitorNodeDistance, ApC, range, simTime, interval);

// // }


// // int main(int argc, char* argv[])
// // {
// //     std::map<std::string, double> pingRttAvg;

// //     uint16_t simTime = 20;
// //     bool verbose = true;
// //     bool trace = false;

// //     CommandLine cmd;
// //     cmd.AddValue("simTime", "simulate time ", simTime); // 设置仿真时间
// //     cmd.AddValue("verbose", "enable verbose logs", verbose); //启用详细日志输出
// //     cmd.AddValue("trace", "enable trace /pcap", trace); //启用pcap文件
// //     cmd.Parse(argc, argv);

// //     if (verbose)
// //     {
// //         /* code */
// //     }

// //     // 启用校验和计算（ofswitch13 模块所需）
// //     GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

// //     //创建域内节点
// //     NodeContainer StaA;
// //     StaA.Create(4);
// //     NodeContainer ApA;
// //     ApA.Create(2);

// //     NodeContainer StaB;
// //     StaB.Create(2);
// //     NodeContainer ApB;
// //     ApB.Create(1);

// //     NodeContainer StaC;
// //     StaC.Create(3);
// //     NodeContainer ApC;
// //     ApC.Create(1);

// //     NodeContainer wifiStaNodes;
// //     wifiStaNodes.Add(StaA);
// //     wifiStaNodes.Add(StaB);
// //     wifiStaNodes.Add(StaC);

// //     //网络设备节点：3台交换机、1台控制器
// //     Ptr<Node> sw1 = CreateObject<Node>();
// //     Ptr<Node> sw2 = CreateObject<Node>();
// //     Ptr<Node> sw3 = CreateObject<Node>();
// //     Ptr<Node> controllerNode = CreateObject<Node>();

// //     //有线链路配置
// //     CsmaHelper csma;
// //     csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
// //     csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));


// //     NetDeviceContainer apCsmaDevsA, apCsmaDevsB, apCsmaDevsC; //AP设备有线接口
// //     NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports; //交换机端口设备

// //     // 连接域 A 主机到 sw1
// //      // 遍历每个 AP 都连接交换机
// //     for (uint32_t i = 0; i < ApA.GetN(); ++i)
// //     {
// //         NodeContainer pair(ApA.Get(i), sw1); // sw1 是 A 域的交换机节点
// //         NetDeviceContainer link = csma.Install(pair);
// //         apCsmaDevsA.Add(link.Get(0));        // AP 侧
// //         sw1Devsports.Add(link.Get(1));   // 交换机侧
// //     }
// //     // 连接域 B 主机到 sw2
// //     {
// //         NodeContainer pair(ApB.Get(0), sw2);
// //         NetDeviceContainer link = csma.Install(pair);
// //         apCsmaDevsB.Add(link.Get(0));
// //         sw2Devsports.Add(link.Get(1));
// //     }

// //     // 连接域 C 主机到 sw3
// //     {
// //         NodeContainer pair(ApC.Get(0), sw3);
// //         NetDeviceContainer link = csma.Install(pair);
// //         apCsmaDevsC.Add(link.Get(0));
// //         sw3Devsports.Add(link.Get(1));
// //     }

// //     // --------------------------
// //     // 交换机之间三角连接配置
// //     // --------------------------
// //     CsmaHelper csmaSwitch;// 交换机互连链路配置
// //     csmaSwitch.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
// //     csmaSwitch.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

// //     // sw1 与 sw2 连接
// //     {
// //         NodeContainer pair(sw1, sw2);
// //         NetDeviceContainer link = csmaSwitch.Install(pair);
// //         sw1Devsports.Add(link.Get(0));
// //         sw2Devsports.Add(link.Get(1));
// //     }

// //     // sw2 与 sw3 连接（新增）
// //     {
// //         NodeContainer pair(sw2, sw3);
// //         NetDeviceContainer link = csmaSwitch.Install(pair);
// //         sw2Devsports.Add(link.Get(0));
// //         sw3Devsports.Add(link.Get(1));
// //     }

// //     // sw3 与 sw1 连接（新增）
// //     {
// //         NodeContainer pair(sw3, sw1);
// //         NetDeviceContainer link = csmaSwitch.Install(pair);
// //         sw3Devsports.Add(link.Get(0));
// //         sw1Devsports.Add(link.Get(1));
// //     }


// //     // wifi配置部分
// //     YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
// //     WifiHelper wifi;
// //     wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");
// //     WifiMacHelper mac; //逻辑可复用

// //     //A域
// //     Ptr<YansWifiChannel> channelA = channel.Create();
// //     YansWifiPhyHelper phyA;
// //     phyA.SetChannel(channelA);

// //     Ssid ssidA = Ssid("A");

// //     mac.SetType("ns3::StaWifiMac",
// //         "Ssid", SsidValue(ssidA),
// //         "ActiveProbing", BooleanValue(false));
// //     NetDeviceContainer staWifiDevsA = wifi.Install(phyA, mac, StaA);

// //     mac.SetType("ns3::ApWifiMac",
// //         "Ssid", SsidValue(ssidA));
// //     NetDeviceContainer apWifiDevsA = wifi.Install(phyA, mac, ApA);

// //     //创建ap-ap backbone信道
// //     Ptr<YansWifiChannel> backboneChannel = channel.Create(); //独立信道
// //     YansWifiPhyHelper phyBackbone;
// //     phyBackbone.SetChannel(backboneChannel);

// //     WifiMacHelper backboneMac;
// //     backboneMac.SetType("ns3::AdhocWifiMac");
// //     NetDeviceContainer apBackboneDevices = wifi.Install(phyBackbone, backboneMac, ApA);

// //     //B域
// //     Ptr<YansWifiChannel> channelB = channel.Create();
// //     YansWifiPhyHelper phyB;
// //     phyB.SetChannel(channelB);

// //     Ssid ssidB = Ssid("B");

// //     mac.SetType("ns3::StaWifiMac",
// //         "Ssid", SsidValue(ssidB),
// //         "ActiveProbing", BooleanValue(false));
// //     NetDeviceContainer staWifiDevsB = wifi.Install(phyB, mac, StaB);

// //     mac.SetType("ns3::ApWifiMac",
// //         "Ssid", SsidValue(ssidB));
// //     NetDeviceContainer apWifiDevsB = wifi.Install(phyB, mac, ApB);

// //     //C域
// //     Ptr<YansWifiChannel> channelC = channel.Create();
// //     YansWifiPhyHelper phyC;
// //     phyC.SetChannel(channelC);
   
// //     Ssid ssidC = Ssid("C");

// //     mac.SetType("ns3::StaWifiMac",
// //         "Ssid", SsidValue(ssidC),
// //         "ActiveProbing", BooleanValue(false));
// //     NetDeviceContainer staWifiDevsC = wifi.Install(phyC, mac, StaC);

// //     mac.SetType("ns3::ApWifiMac",
// //         "Ssid", SsidValue(ssidC));
// //     NetDeviceContainer apWifiDevsC = wifi.Install(phyC, mac, ApC);

// //     mac.SetType("ns3::AdhocWifiMac");
// //     NetDeviceContainer adhocDevsC = wifi.Install(phyC, mac, StaC);


// //     // 在这里添加AP应用创建代码：
// // // 为域A的AP节点创建应用
// // for (uint32_t i = 0; i < ApA.GetN(); ++i) {
// //     Ptr<ApProtocolInfoApp> apAppA = CreateObject<ApProtocolInfoApp>();
// //     ApA.Get(i)->AddApplication(apAppA);
// //     apAppA->SetStartTime(Seconds(0.0));
// //     apAppA->SetStopTime(Seconds(simTime - 1.0));
// // }

// // // 为域B的AP节点创建应用
// // Ptr<ApProtocolInfoApp> apAppB = CreateObject<ApProtocolInfoApp>();
// // ApB.Get(0)->AddApplication(apAppB);
// // apAppB->SetStartTime(Seconds(0.0));
// // apAppB->SetStopTime(Seconds(simTime - 1.0));

// // // 为域C的AP节点创建应用
// // Ptr<ApProtocolInfoApp> apAppC = CreateObject<ApProtocolInfoApp>();
// // ApC.Get(0)->AddApplication(apAppC);
// // apAppC->SetStartTime(Seconds(0.0));
// // apAppC->SetStopTime(Seconds(simTime - 1.0));

// //     //合并所有sta设备
// //     NetDeviceContainer allStaDevices;
// //     allStaDevices.Add(staWifiDevsA);
// //     allStaDevices.Add(staWifiDevsB);
// //     allStaDevices.Add(staWifiDevsC);

// //     //合并所有ap设备
// //     NetDeviceContainer allApDevices;
// //     allApDevices.Add(apWifiDevsA);
// //     allApDevices.Add(apWifiDevsB);
// //     allApDevices.Add(apWifiDevsC);


// //     //节点位置配置
// //     MobilityHelper mobility;
// //     mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

// //     // 控制器位置
// //     Ptr<ListPositionAllocator> posController = CreateObject<ListPositionAllocator>();
// //     posController->Add(Vector(0, 0, 0)); // 单个节点直接 Add
// //     mobility.SetPositionAllocator(posController);
// //     mobility.Install(controllerNode);

// //     // 交换机
// //     Ptr<ListPositionAllocator> swPos = CreateObject<ListPositionAllocator>();
// //     swPos->Add(Vector(-100, 150, 0)); // sw1
// //     swPos->Add(Vector(100, 150, 0)); // sw2
// //     swPos->Add(Vector(0, 150, 0)); // sw3
// //     mobility.SetPositionAllocator(swPos);
// //     mobility.Install(sw1);
// //     mobility.Install(sw2);
// //     mobility.Install(sw3);

// //     // 域A节点
// //     Ptr<ListPositionAllocator> posA = CreateObject<ListPositionAllocator>();
// //     posA->Add(Vector(-150, 200, 0));  
// //     posA->Add(Vector(-120, 200, 0));
// //     posA->Add(Vector(-155, 230, 0)); 
// //     posA->Add(Vector(-150, 230, 0));
// //     posA->Add(Vector(-165, 210, 0));
// //     posA->Add(Vector(-123, 220, 0));
// //     mobility.SetPositionAllocator(posA);
// //     mobility.Install(ApA);
// //     mobility.Install(StaA);

// //     // 域B节点
// //     Ptr<ListPositionAllocator> posB = CreateObject<ListPositionAllocator>();
// //     posB->Add(Vector(120, 200, 0));  
// //     posB->Add(Vector(150, 230, 0));
// //     posB->Add(Vector(130, 230, 0));
// //     mobility.SetPositionAllocator(posB);
// //     mobility.Install(ApB);
// //     mobility.Install(StaB);

// //     // 域C节点
// //     MobilityHelper adhocMobility;
// //     adhocMobility.SetMobilityModel("ns3::WaypointMobilityModel");
// //     adhocMobility.Install(StaC);
// //     adhocMobility.Install(ApC);

// //     //AP位置固定
// //     Ptr<WaypointMobilityModel> apMob =
// //         ApC.Get(0)->GetObject<WaypointMobilityModel>();

// //     apMob->AddWaypoint(Waypoint(Seconds(0.0),
// //         Vector(0.0, 200.0, 0.0)));

// //     //STA向AP逐渐收敛
// //     for (uint32_t i = 0; i < StaC.GetN(); ++i)
// //     {
// //         Ptr<Node> node = StaC.Get(i);
// //         Ptr<WaypointMobilityModel> mob =
// //             node->GetObject<WaypointMobilityModel>();

// //         // 初始位置（t=0）
// //         Vector initPos;
// //         if (i == 0) initPos = Vector(20.0, 230.0, 0.0);
// //         if (i == 1) initPos = Vector(50.0, 210.0, 0.0);
// //         if (i == 2) initPos = Vector(-20.0, 230.0, 0.0);

// //         mob->AddWaypoint(Waypoint(Seconds(0.0), initPos));

// //         //3秒时节点静止
// //         mob->AddWaypoint(Waypoint(Seconds(3.0), initPos));

// //         // 3 秒开始向 AP 移动
// //         Vector apPos = ApC.Get(0)->GetObject<MobilityModel>()->GetPosition();
// //         mob->AddWaypoint(Waypoint(Seconds(20.0), apPos));
// //     }

  

// //     //将AP的WiFi接口与有线接口桥接（实现有线无线互通）
// //     BridgeHelper bridge;
// //     NetDeviceContainer  bridgeDevA,bridgeDevB,bridgeDevC;

// //     for (uint32_t i = 0;i < ApA.GetN();++i)
// //     {
      
// //         NetDeviceContainer bridgeDev = bridge.Install(ApA.Get(i),
// //             NetDeviceContainer(apWifiDevsA.Get(i), apCsmaDevsA.Get(i)));
// //         bridgeDevA.Add(bridgeDev);
// //     }

// //     bridgeDevB = bridge.Install(ApB.Get(0),
// //         NetDeviceContainer(apWifiDevsB.Get(0), apCsmaDevsB.Get(0)));

// //     bridgeDevC = bridge.Install(ApC.Get(0),
// //         NetDeviceContainer(apWifiDevsC.Get(0), apCsmaDevsC.Get(0))); //桥接AP的WiFi和有线设备


// //     // OpenFlow控制器与交换机配置
// //     Ptr<OFSwitch13InternalHelper> of13Helper =
// //         CreateObject<OFSwitch13InternalHelper>();

// //     //安装控制器应用到控制器节点
// //     of13Helper->InstallController(controllerNode);
// //     //安装交换机应用到交换机节点，并关联其端口设备
// //     of13Helper->InstallSwitch(sw1, sw1Devsports);
// //     of13Helper->InstallSwitch(sw2, sw2Devsports);
// //     of13Helper->InstallSwitch(sw3, sw3Devsports);
// //     //创建控制器与交换机之间的OpenFlow信道
// //     of13Helper->CreateOpenFlowChannels();

// //     //获取控制器应用实例（用于后续配置路由优先级）
// //     auto get = of13Helper->GetController();
// //     Ptr<OFSwitch13LearningController>
// //         controllerApp =
// //         DynamicCast<OFSwitch13LearningController>(get.Get(0));

// //     // --------------------------
// //     // 4. 网络栈配置部分
// //     // --------------------------
// //     // Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));
// //     // Config::SetDefault("ns3::Ipv4::IpForward", BooleanValue(true));

// //     // 为主机配置静态默认路由指向路由器
// //     Ipv4StaticRoutingHelper staticRoutingHelper;

// //     InternetStackHelper stack; //基础IP协议栈

// //     InternetStackHelper stack2;
// //     Ipv4ListRoutingHelper list;
// //     // Ipv4StaticRoutingHelper staticC;

// //     AodvHelper aodv;
// //     OlsrHelper olsr;
// //     list.Add(aodv, 10);
// //     list.Add(olsr, 10);
// //     list.Add(staticRoutingHelper, 100);
// //     stack2.SetRoutingHelper(list); // 对于 AdHoc 节点使用 AODV
// //     stack2.Install(wifiStaNodes);


// //     // 为AP节点安装协议栈（AP作为网关需要IP协议栈）
// //     stack.Install(ApA);
// //     stack.Install(ApB);
// //     stack.Install(ApC);

// //     // 为AP启用IP转发（AP作为网关需要转发功能）
// //     for (uint32_t i = 0; i < ApA.GetN(); ++i)
// //     {
// //         Ptr<Ipv4> ipv4 = ApA.Get(i)->GetObject<Ipv4>();
// //         ipv4->SetAttribute("IpForward", BooleanValue(true));
// //     }
// //     Ptr<Ipv4> ipv4ApB = ApB.Get(0)->GetObject<Ipv4>();
// //     ipv4ApB->SetAttribute("IpForward", BooleanValue(true));
// //     Ptr<Ipv4> ipv4ApC = ApC.Get(0)->GetObject<Ipv4>();
// //     ipv4ApC->SetAttribute("IpForward", BooleanValue(true));


// //     for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
// //     {
// //         Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
// //         ipv4->SetAttribute("IpForward", BooleanValue(true)); // 启用转发
// //     }

// //     // 分配IPv4地址
// //     Ipv4AddressHelper ipv4;

// //     Ipv4InterfaceContainer ifA,ifApA; // 域A主机和路由器接口
// //     ipv4.SetBase("10.1.1.0", "255.255.255.0");
// //     // 给 A域 设备和 routerDevsA 分配地址
// //     {
// //         NetDeviceContainer netA = NetDeviceContainer();
// //         // 先为主机分配地址（10.1.1.1-10.1.1.4）
// //         for (uint32_t i = 0; i < staWifiDevsA.GetN(); ++i)
// //             netA.Add(staWifiDevsA.Get(i));
// //         ifA = ipv4.Assign(netA);
// //         // 为AP分配地址（10.1.1.5-10.1.1.6）
// //         ifApA = ipv4.Assign(apCsmaDevsA);
// //     }

// //     Ipv4InterfaceContainer ifB,ifApB;
// //     ipv4.SetBase("10.2.1.0", "255.255.255.0");
// //     {
// //         NetDeviceContainer netB = NetDeviceContainer();
// //         // 先为主机分配地址（10.2.1.1-10.2.1.2）
// //         for (uint32_t i = 0; i < staWifiDevsB.GetN(); ++i)
// //             netB.Add(staWifiDevsB.Get(i));
// //         ifB = ipv4.Assign(netB);
// //         // 为AP分配地址（10.2.1.3）
// //         ifApB = ipv4.Assign(apCsmaDevsB);
// //     }

// //     Ipv4InterfaceContainer ifC, ifApC, adhocIfC;
// //     ipv4.SetBase("10.3.1.0", "255.255.255.0");
// //     {
// //         NetDeviceContainer netC = NetDeviceContainer();
// //         // 先为主机分配地址（10.3.1.1-10.3.1.3）
// //         for (uint32_t i = 0; i < staWifiDevsC.GetN(); ++i)
// //             netC.Add(staWifiDevsC.Get(i));
// //         // 添加Adhoc接口（10.3.1.4-10.3.1.6）
// //         for (uint32_t i = 0; i < adhocDevsC.GetN(); ++i)
// //             netC.Add(adhocDevsC.Get(i));         
// //         ifC = ipv4.Assign(netC);
// //         // 把 AdHoc 接口单独放到 adhocIfC
// //         for (uint32_t i = 0; i < adhocDevsC.GetN(); ++i) {
// //             uint32_t index = adhocDevsC.Get(i)->GetNode()->GetObject<Ipv4>()->GetInterfaceForDevice(adhocDevsC.Get(i));
// //             adhocIfC.Add(adhocDevsC.Get(i)->GetNode()->GetObject<Ipv4>(), index);
// //         }
// //         // 为AP分配地址（10.3.1.7）
// //         ifApC = ipv4.Assign(apCsmaDevsC);
// //     } // wifi 网络


// //     // 配置主机默认路由指向本域AP
// //     // 域A主机默认路由（指向AP的IP，这里用第一个AP作为主网关）
// //     Ipv4Address apAGateway = ifApA.GetAddress(0); // 10.1.1.5
// //     for (uint32_t i = 0; i < StaA.GetN(); ++i)
// //     {
// //         Ptr<Node> h = StaA.Get(i);
// //         Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
// //         Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
// //         staticRouting->SetDefaultRoute(apAGateway, 1);
// //     }

// //     // 域B主机默认路由（指向域B AP的IP：10.2.1.3）
// //     Ipv4Address apBGateway = ifApB.GetAddress(0);
// //     for (uint32_t i = 0; i < StaB.GetN(); ++i)
// //     {
// //         Ptr<Node> h = StaB.Get(i);
// //         Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
// //         Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
// //         staticRouting->SetDefaultRoute(apBGateway, 1);
// //     }

// //     // 域C主机默认路由（指向域C AP的IP：10.3.1.4）
// //     Ipv4Address apCGateway = ifApC.GetAddress(0);
// //     for (uint32_t i = 0; i < StaC.GetN(); ++i)
// //     {
// //         Ptr<Node> h = StaC.Get(i);
// //         Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
// //         Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
// //         staticRouting->SetDefaultRoute(apCGateway, 1);
// //     }



// //     //打印输出

// //     for (uint32_t i = 0; i < StaA.GetN(); ++i) {
// //         Ptr<Ipv4> ipv4h = StaA.Get(i)->GetObject<Ipv4>();
// //         Ptr<Ipv4StaticRouting> r = staticRoutingHelper.GetStaticRouting(ipv4h);
// //         std::cout << "STA A " << i << " default route: " << r->GetDefaultRoute() << std::endl;
// //     }

// //     for (uint32_t i = 0; i < StaC.GetN(); ++i) {
// //         Ptr<Ipv4> ipv4h = StaC.Get(i)->GetObject<Ipv4>();
// //         Ptr<Ipv4StaticRouting> r = staticRoutingHelper.GetStaticRouting(ipv4h);
// //         std::cout << "STA " << i << " default route: "
// //             << r->GetDefaultRoute() << std::endl;
// //     }

// //     std::cout << "sw1 ports: " << sw1Devsports.GetN() << std::endl;
// //     for (uint32_t i = 0; i < sw1Devsports.GetN(); ++i)
// //         std::cout << "Port " << i << ": " << sw1Devsports.Get(i)->GetAddress() << std::endl;

// //     std::cout << "sw2 ports: " << sw2Devsports.GetN() << std::endl;
// //         for (uint32_t i = 0; i < sw2Devsports.GetN(); ++i)
// //             std::cout << "Port " << i << ": " << sw2Devsports.Get(i)->GetAddress() << std::endl;
            
// //     std::cout << "sw3 ports: " << sw3Devsports.GetN() << std::endl;
// //         for (uint32_t i = 0; i < sw3Devsports.GetN(); ++i)
// //                 std::cout << "Port " << i << ": " << sw3Devsports.Get(i)->GetAddress() << std::endl;
                



// //     // 启用pcap追踪
// //     if (true)
// //     {
// //         // ---- C 域抓包 ----
// //         phyC.EnablePcap("C_adhoc", adhocDevsC);       // C 域 AdHoc 接口
// //         phyC.EnablePcap("C_sta", staWifiDevsC);        // C 域 STA 

// //         // ---- A/B 域抓包 ----
// //         phyA.EnablePcap("A_ap", apWifiDevsA);          // A 域 AP 
// //         phyB.EnablePcap("B_ap", apWifiDevsB);          // B 域 AP 

// //         // ---- OpenFlow / 交换机抓包 ----
// //         of13Helper->EnableOpenFlowPcap("openflow-interdomain");
// //         of13Helper->EnableDatapathStats("switch-stats");

// //          // 交换机端口抓包（包括互连端口）
// //         csmaSwitch.EnablePcap("sw1", sw1Devsports, true);
// //         csmaSwitch.EnablePcap("sw2", sw2Devsports, true);
// //         csmaSwitch.EnablePcap("sw3", sw3Devsports, true);

// //          // A/B/C 域 AP 的 CSMA 接口抓包
// //         csma.EnablePcap("A_domain_ap", apCsmaDevsA);
// //         csma.EnablePcap("B_domain_ap", apCsmaDevsB);
// //         csma.EnablePcap("C_domain_ap", apCsmaDevsC);
      
// //     }


// //     //应用层udp发送
// //     uint16_t port0 = 9;
// //     uint16_t port1 = 10;
// //     uint16_t port2 = 11;
// //     uint16_t port3 = 12;

// //     // Flow0: StaA[1] -> StaC[2]
// //    SendPacketByNodeId(StaA.Get(1), ifC.GetAddress(2), port0,
// //        "600kbps", 1024, 5.0, simTime - 1,staWifiDevsA.Get(1));

// //    // Flow1: StaA[3] -> StaB[1]
// //    SendPacketByNodeId(StaA.Get(3), ifB.GetAddress(1), port1,
// //        "600kbps", 1024, 5.0, simTime - 1,staWifiDevsA.Get(3));

// //    // Flow2: StaB[0] -> StaC[1]
// //    SendPacketByNodeId(StaB.Get(0), ifC.GetAddress(1), port2,
// //        "600kbps", 1024, 5.0, simTime - 1,staWifiDevsB.Get(0));

// //    // Flow3: StaC[0] -> StaC[2]
// //    SendPacketByNodeId(StaC.Get(0), ifC.GetAddress(5), port3,
// //        "600kbps", 1024, 5.0, simTime - 1, adhocDevsC.Get(0));
  
// //    double monitorInterval = 5.0;  // 每 5 秒检查一次
// //    Ipv4InterfaceContainer ifOtherDomain;
// //    ifOtherDomain.Add(ifA);
// //    ifOtherDomain.Add(ifB);
// //    // 第一次调用周期监控
// //    Simulator::Schedule(Seconds(5.0), &MonitorNodeDistance, ApC.Get(0), 30.0, simTime, monitorInterval);

    
// //     //adhoc接口开关（C域）
// //     for (uint32_t i = 0; i < StaC.GetN(); i++)
// //     {
// //         Ptr<Node> node = StaC.Get(i);
// //         Ptr<NetDevice> dev = adhocDevsC.Get(i); // 假设每个 STA 的 AdHoc 接口索引相同

// //         // 0秒时关闭adhoc接口
// //         Simulator::Schedule(Seconds(7.0), &DisableDeviceLogical, node, dev);

// //         // 第 7 秒开启
// //         Simulator::Schedule(Seconds(10.0), &EnableDeviceLogical, node, dev); //接口设置为3秒开启

// //        // Simulator::Schedule(Seconds(16.0), &DisableDeviceLogical, node, dev);
// //     }
    

// //     //调试信息输出：MAC地址和IP地址
// //     {
// //         std::cout << "ap mac and ip" << std::endl;
// //         // 输出域A的AP设备
// //         for (uint32_t i = 0; i < ApA.GetN(); ++i) {
// //             Ptr<NetDevice> dev = apWifiDevsA.Get(i);  // AP的WiFi接口
// //             Address addr = dev->GetAddress();
// //             std::cout << "  ApA[" << i << "] WiFi接口"
// //                       << " -> MAC Address: "
// //                       << Mac48Address::ConvertFrom(addr);
// //             // 输出IP地址
// //             Ptr<Ipv4> ipv4 = ApA.Get(i)->GetObject<Ipv4>();
// //             if (ipv4 && ipv4->GetNInterfaces() > 1) {
// //                 std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
// //             }
// //             std::cout << std::endl;
// //         }
    
// //         // 输出域B的AP设备
// //         Ptr<NetDevice> devB = apWifiDevsB.Get(0);
// //         Address addrB = devB->GetAddress();
// //         std::cout << "  ApB[0] WiFi接口"
// //                   << " -> MAC Address: "
// //                   << Mac48Address::ConvertFrom(addrB);
// //         // 输出IP地址
// //         Ptr<Ipv4> ipv4B = ApB.Get(0)->GetObject<Ipv4>();
// //         if (ipv4B && ipv4B->GetNInterfaces() > 1) {
// //             std::cout << " -> IP Address: " << ipv4B->GetAddress(1, 0).GetLocal();
// //         }
// //         std::cout << std::endl;
        
// //         // 输出域C的AP设备
// //         Ptr<NetDevice> devC = apWifiDevsC.Get(0);
// //         Address addrC = devC->GetAddress();
// //         std::cout << "  ApC[0] WiFi接口"
// //                   << " -> MAC Address: "
// //                   << Mac48Address::ConvertFrom(addrC);
// //         // 输出IP地址
// //         Ptr<Ipv4> ipv4C = ApC.Get(0)->GetObject<Ipv4>();
// //         if (ipv4C && ipv4C->GetNInterfaces() > 1) {
// //             std::cout << " -> IP Address: " << ipv4C->GetAddress(1, 0).GetLocal();
// //         }
// //         std::cout << std::endl;
    
// //         std::cout << "sta mac and ip" << std::endl;
// //         // 输出域A的STA设备
// //         for (uint32_t i = 0; i < StaA.GetN(); ++i) {
// //             Ptr<NetDevice> dev = staWifiDevsA.Get(i);
// //             Address addr = dev->GetAddress();
// //             std::cout << "  StaA[" << i << "] WiFi接口"
// //                       << " -> MAC Address: "
// //                       << Mac48Address::ConvertFrom(addr);
// //             // 输出IP地址
// //             Ptr<Ipv4> ipv4 = StaA.Get(i)->GetObject<Ipv4>();
// //             if (ipv4 && ipv4->GetNInterfaces() > 1) {
// //                 std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
// //             }
// //             std::cout << std::endl;
// //         }
        
// //         // 输出域B的STA设备
// //             for (uint32_t i = 0; i < StaB.GetN(); ++i) {
// //             Ptr<NetDevice> dev = staWifiDevsB.Get(i);
// //             Address addr = dev->GetAddress();
// //             std::cout << "  StaB[" << i << "] WiFi接口"
// //                           << " -> MAC Address: "
// //                           << Mac48Address::ConvertFrom(addr);
// //             // 输出IP地址
// //             Ptr<Ipv4> ipv4 = StaB.Get(i)->GetObject<Ipv4>();
// //             if (ipv4 && ipv4->GetNInterfaces() > 1) {
// //                 std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
// //             }
// //             std::cout << std::endl;
// //         }
        
// //         // 输出域C的STA设备
// //         for (uint32_t i = 0; i < StaC.GetN(); ++i) {
// //             Ptr<NetDevice> dev = staWifiDevsC.Get(i);
// //             Address addr = dev->GetAddress();
// //             std::cout << "  StaC[" << i << "] WiFi接口"
// //                       << " -> MAC Address: "
// //                       << Mac48Address::ConvertFrom(addr);
// //             // 输出IP地址
// //              Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
// //             if (ipv4 && ipv4->GetNInterfaces() > 1) {
// //                 std::cout << " -> IP Address: " << ipv4->GetAddress(1, 0).GetLocal();
// //             }
// //             std::cout << std::endl;
// //         }
    
// //         std::cout << "adhoc mac and ip" << std::endl;
// //         // 输出域C的Adhoc设备
// //         for (uint32_t i = 0; i < StaC.GetN(); ++i) {
// //             Ptr<NetDevice> dev = adhocDevsC.Get(i);
// //             Address addr = dev->GetAddress();
// //             std::cout << "  StaC[" << i << "] Adhoc接口"
// //                       << " -> MAC Address: "
// //                       << Mac48Address::ConvertFrom(addr);
// //             // 输出IP地址
// //             Ptr<Ipv4> ipv4 = StaC.Get(i)->GetObject<Ipv4>();
// //             if (ipv4 && ipv4->GetNInterfaces() > 2) {
// //                 std::cout << " -> IP Address: " << ipv4->GetAddress(2, 0).GetLocal();
// //             }
// //             std::cout << std::endl;
// //         }
// //     }

    
// //     //向控制器注册网关ARP信息（核心新增功能）
// //     if (controllerApp != nullptr) {
// //         // 域A网关（AP）ARP条目：IP -> MAC
// //         for (uint32_t i = 0; i < ApA.GetN(); ++i) {
// //             Ipv4Address apIp = ifApA.GetAddress(i);
// //             Mac48Address apMac = Mac48Address::ConvertFrom(apCsmaDevsA.Get(i)->GetAddress());
// //             controllerApp->AddArpEntry(apIp, apMac);
// //             std::cout <<"注册ARP条目：ApA[" << i << "] 网关 " << apIp << " -> " << apMac << std::endl;
// //         }
// //         // 域B网关（AP）ARP条目
// //         Ipv4Address apBIp = ifApB.GetAddress(0);
// //         Mac48Address apBMac = Mac48Address::ConvertFrom(apCsmaDevsB.Get(0)->GetAddress());
// //         controllerApp->AddArpEntry(apBIp, apBMac);
// //         std::cout << "注册ARP条目：ApB[0] 网关 "<< apBIp << " -> " << apBMac << std::endl;
// //         // 域C网关（AP）ARP条目
// //         Ipv4Address apCIp = ifApC.GetAddress(0);
// //         Mac48Address apCMac = Mac48Address::ConvertFrom(apCsmaDevsC.Get(0)->GetAddress());
// //         controllerApp->AddArpEntry(apCIp, apCMac);
// //         std::cout << "注册ARP条目：ApC[0] 网关 "<< apCIp << " -> "  << apCMac << std::endl;
// //     }

// //     // LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_DEBUG);
// //     //两秒时设置控制器路由优先级
// //     Simulator::Schedule(Seconds(2.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
// //     // Simulator::Schedule(Seconds(1.1), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
// //     // Simulator::Schedule(Seconds(1.3), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);

// //     FlowMonitorHelper flowmonHelper;

// //     // 添加获取AP和STA消息的调度函数
// // Ptr<OFSwitch13Device> sw1Device = sw1->GetObject<OFSwitch13Device>();
// // Ptr<OFSwitch13Device> sw2Device = sw2->GetObject<OFSwitch13Device>();
// // Ptr<OFSwitch13Device> sw3Device = sw3->GetObject<OFSwitch13Device>();

// // if (sw1Device) {
// //     Simulator::Schedule(Seconds(3.0), &OFSwitch13Device::GetApStaMessages, sw1Device);
// // }
// // if (sw2Device) {
// //     Simulator::Schedule(Seconds(3.0), &OFSwitch13Device::GetApStaMessages, sw2Device);
// // }
// // if (sw3Device) {
// //     Simulator::Schedule(Seconds(3.0), &OFSwitch13Device::GetApStaMessages, sw3Device);
// // }


// //     // 只监控 STA 节点
// //     NodeContainer monitorNodes;
// //     monitorNodes.Add(StaA);
// //     monitorNodes.Add(StaB);
// //     monitorNodes.Add(StaC);

// //     Ptr<FlowMonitor> monitor = flowmonHelper.Install(monitorNodes);

// //     // 打开输出文件
// //     std::ofstream fout("flow_stats.csv");
// //     fout << "Time";
// //     for (int i = 1; i <= 4; ++i) {  // 有4条链路
// //         fout << ",Throughput_" << i << "(Kbps),LossRate_" << i << "(%),AvgRTT_" << i << "(ms),Jitter_" << i << "(ms)";
// //     }
// //     fout << std::endl;
    
// //     // 每1秒采样一次
// //     Simulator::Schedule(Seconds(0.1), &MonitorFlow, monitor, &flowmonHelper, 1, &fout);

// //     //  // 只对域C进行Q-Learning
// //     // Simulator::Schedule(Seconds(8.0), &GlobalPeriodicModeSwitch, 
// //     //                    StaC, NodeContainer(ApC.Get(0)),
// //     //                    staWifiDevsC, adhocDevsC);

// //     Simulator::Stop(Seconds(simTime));
// //     Simulator::Run();



// //     // 在Simulator::Run()之后添加结果分析
// //    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
// //     std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

// //     std::cout << "\n========== 流量统计结果 ==========" << std::endl;
// //     for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
// //     {
// //         Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
// //         std::cout << "Flow (" << t.sourceAddress <<  " -> " << t.destinationAddress <<  ")" << std::endl;
// //         std::cout << "  Tx Bytes: " << i->second.txBytes << std::endl;
// //         std::cout << "  Rx Bytes: " << i->second.rxBytes << std::endl;
// //         std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
// //         std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
// //         std::cout << "  平均延迟: " << i->second.delaySum.GetSeconds() / i->second.rxPackets * 1000 << " ms" << std::endl;
// //         std::cout << "  丢包率: " << (double)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets * 100 << " %" << std::endl;
// //         std::cout << std::endl;
// //     }

// //     monitor->SerializeToXmlFile("flowmon-results.xml", true, true);
// //     fout.close();



// //   /*  PrintMyFlowStats(monitor, &flowmonHelper);*/

   

// //     Simulator::Destroy();

// //     return 0;
// // }


// /* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
// /*
//  * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块
//  *
//  * - 域 A: hostsA (2 主机) -- sw1
//  * - 域 B: hostsB (2 主机) -- sw2
//  * - 路由器节点连接 sw1 和 sw2（有两个子网的 IP）
//  * - 单 OpenFlow 控制器管理 sw1 和 sw2
//  *
//  * 构建：确保 ns-3 已构建并启用 ofswitch13 模块。
//  */

// #include <ns3/core-module.h>
// #include <ns3/network-module.h>
// #include <ns3/csma-module.h>
// #include <ns3/internet-module.h>
// #include <ns3/ofswitch13-module.h>
// #include <ns3/internet-apps-module.h>
// #include "ns3/point-to-point-module.h"
// #include "ns3/wifi-module.h"
// #include "ns3/mobility-module.h"
// #include "ns3/applications-module.h"
// #include <ns3/internet-apps-module.h>
// #include "ns3/bridge-helper.h"
// #include "ns3/aodv-module.h"
// #include "ns3/olsr-module.h"
// using namespace ns3;
// // ---------------------------------------------------------
// // 函数：禁用 AdHoc 接口
// // ---------------------------------------------------------
// // 逻辑 down
// // ---------------------------------------------------------
// // 函数：逻辑上下线设备
// // enable = true 表示开启，false 表示关闭
// // ---------------------------------------------------------

// void DisableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
// {
//     Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
//     uint32_t idx = ipv4->GetInterfaceForDevice(dev);
//     if (idx != uint32_t(-1))
//         ipv4->SetDown(idx);
// }
// void EnableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
// {
//     Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
//     uint32_t idx = ipv4->GetInterfaceForDevice(dev);
//     if (idx != uint32_t(-1))
//         ipv4->SetUp(idx);
// }

// // ---------------------------------------------------------
// // 包装函数：周期性执行 SendPosition（每3秒）
// // ---------------------------------------------------------
// void PeriodicSendPosition(Ptr<OFSwitch13Device> sw3Device, double interval)
// {
//     // 执行 SendPosition 函数
//     sw3Device->SendPosition();
    
//     // 打印日志（可选，便于调试）
//     std::cout << "[" << Simulator::Now().GetSeconds() << "s] 执行 SendPosition，下一次执行时间：" 
//               << Simulator::Now().GetSeconds() + interval << "s" << std::endl;
    
//     // 再次调度自身，实现周期性执行
//     Simulator::Schedule(Seconds(interval), &PeriodicSendPosition, sw3Device, interval);
// }
// int main(int argc, char *argv[])
// {
//     uint16_t simTime = 20;
//     bool verbose = true;
//     bool trace = false;

//     CommandLine cmd;
//     cmd.AddValue("simTime", "simulate time ", simTime); // 设置仿真时间
//     cmd.AddValue("verbose", "enable verbose logs", verbose);
//     cmd.AddValue("trace", "enable trace /pcap", trace);
//     cmd.Parse(argc, argv);

//     if (verbose)
//     {
//         /* code */
//     }

//     // 启用校验和计算（ofswitch13 模块所需）
//     GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

//     NodeContainer hostsA;
//     hostsA.Create(2);
//     NodeContainer hostsB;
//     hostsB.Create(2);

//     Ptr<Node> sw1 = CreateObject<Node>();
//     Ptr<Node> sw2 = CreateObject<Node>();
//     Ptr<Node> sw3 = CreateObject<Node>();
//     Ptr<Node> routerNode1 = CreateObject<Node>();
//     // Ptr<Node> routerNode2 = CreateObject<Node>();
//     Ptr<Node> controllerNode = CreateObject<Node>();

//     CsmaHelper csma;
//     csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
//     csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

//     NetDeviceContainer hostDevsA, hostDevsB, ApDevsC;
//     NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports;
//     NetDeviceContainer routerDevsA, routerDevsB, routerDevsC;
//     // 连接域 A 主机到 sw1
//     for (uint32_t i = 0; i < hostsA.GetN(); ++i)
//     {
//         NodeContainer pair(hostsA.Get(i), sw1);
//         NetDeviceContainer link = csma.Install(pair);
//         hostDevsA.Add(link.Get(0));
//         sw1Devsports.Add(link.Get(1));
//     }
//     // 连接域 B 主机到 sw2
//     for (uint32_t i = 0; i < hostsB.GetN(); ++i)
//     {
//         NodeContainer pair(hostsB.Get(i), sw2);
//         NetDeviceContainer link = csma.Install(pair);
//         hostDevsB.Add(link.Get(0));
//         sw2Devsports.Add(link.Get(1));
//     }

//     // 连接路由器到 sw1（域 A 网络）
//     {
//         NodeContainer pair(routerNode1, sw1);
//         NetDeviceContainer link = csma.Install(pair);
//         routerDevsA.Add(link.Get(0));
//         sw1Devsports.Add(link.Get(1));
//     }
//     // 连接路由器到 sw2（域 B 网络）
//     {
//         NodeContainer pair(routerNode1, sw2);
//         NetDeviceContainer link = csma.Install(pair);
//         routerDevsB.Add(link.Get(0));  // 路由器在 B 网络的接口
//         sw2Devsports.Add(link.Get(1)); // 将这个端口加入sw2
//     }
//     // 连接路由器到 sw3（域 C 网络）
//     {
//         NodeContainer pair(routerNode1, sw3);
//         NetDeviceContainer link = csma.Install(pair);
//         routerDevsC.Add(link.Get(0));  // 路由器在 C 网络的接口
//         sw3Devsports.Add(link.Get(1)); // 将这个端口添加到sw3
//     }
//     csma.EnablePcapAll("csma-trace", true);
//     // wifi配置部分
//     NodeContainer wifiStaNodes;
//     wifiStaNodes.Create(3);
//     NodeContainer wifiApNode;
//     wifiApNode.Create(1);

//     YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
//     YansWifiPhyHelper phy;
//     phy.SetChannel(channel.Create());
//     WifiHelper wifi;
//     wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");
//     WifiMacHelper mac;
//     Ssid ssid = Ssid("C");

//     mac.SetType("ns3::StaWifiMac",
//                 "Ssid", SsidValue(ssid));
//     NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

//     mac.SetType("ns3::ApWifiMac",
//                 "Ssid", SsidValue(ssid));
//     NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

//     mac.SetType("ns3::AdhocWifiMac");
//     NetDeviceContainer adhocDevices = wifi.Install(phy, mac, wifiStaNodes);

//     MobilityHelper mobility;
//     mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
//     mobility.Install(wifiApNode);
//     mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
//                               "Time", TimeValue(Seconds(1.0)),                                      // 每次移动的间隔时间
//                               "Speed", StringValue("ns3::UniformRandomVariable[Min=0.1|Max=0.5]")); // 移动速度
//     mobility.Install(wifiStaNodes);

//     // 创建并安装自定义应用
//     Ptr<ApProtocolInfoApp> apApp = CreateObject<ApProtocolInfoApp>();
//     wifiApNode.Get(0)->AddApplication(apApp);

//     // 设置应用的启动和停止时间
//     apApp->SetStartTime(Seconds(0.0));
//     apApp->SetStopTime(Seconds(simTime - 1.0));
    
//     // 连接域 C 主机到 sw3
//     {
//         NodeContainer pair(wifiApNode.Get(0), sw3);
//         NetDeviceContainer link = csma.Install(pair);
//         ApDevsC.Add(link.Get(0));
//         sw3Devsports.Add(link.Get(1));
//     }
//     BridgeHelper bridge;
//     NetDeviceContainer bridgeDev;
//     bridgeDev = bridge.Install(wifiApNode.Get(0),
//                                NetDeviceContainer(apDevice.Get(0), ApDevsC.Get(0)));

//     Ptr<OFSwitch13InternalHelper> of13Helper =
//         CreateObject<OFSwitch13InternalHelper>();

//     of13Helper->InstallController(controllerNode);
//     of13Helper->InstallSwitch(sw1, sw1Devsports);
//     of13Helper->InstallSwitch(sw2, sw2Devsports);
//     of13Helper->InstallSwitch(sw3, sw3Devsports);
//     of13Helper->CreateOpenFlowChannels();
//     auto get = of13Helper->GetController();

//     Ptr<OFSwitch13LearningController>
//         controllerApp =
//             DynamicCast<OFSwitch13LearningController>(get.Get(0));
//     //------------------------lcx 新增----------------------------------------
//     Ptr<OFSwitch13Device> sw3Device = sw3->GetObject<OFSwitch13Device>();
//     // --------------------------
//     // 4. 网络栈配置部分
//     // --------------------------
//     // Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));
//     // Config::SetDefault("ns3::Ipv4::IpForward", BooleanValue(true));

//     // 为主机配置静态默认路由指向路由器
//     Ipv4StaticRoutingHelper staticRoutingHelper;

//     InternetStackHelper stack;

//     InternetStackHelper stack2;
//     Ipv4ListRoutingHelper list;
//     // Ipv4StaticRoutingHelper staticC;

//     AodvHelper aodv;
//     OlsrHelper olsr;
//     list.Add(aodv, 10);
//     list.Add(olsr, 10);
//     list.Add(staticRoutingHelper, 100);
//     stack2.SetRoutingHelper(list); // 对于 AdHoc 节点使用 AODV
//     stack2.Install(wifiStaNodes);
//     stack2.Install(hostsA);
//     stack2.Install(hostsB);
//     stack.Install(routerNode1);

//     for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
//     {
//         Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
//         ipv4->SetAttribute("IpForward", BooleanValue(true)); // 启用转发
//     }
//     // 分配IPv4地址
//     Ipv4AddressHelper ipv4;

//     Ipv4InterfaceContainer ifA; // 域A主机和路由器接口
//     ipv4.SetBase("10.1.1.0", "255.255.255.0");
//     // 给 hostsA 设备和 routerDevsA 分配地址
//     {
//         NetDeviceContainer netA = NetDeviceContainer();
//         // 主机先分配
//         for (uint32_t i = 0; i < hostDevsA.GetN(); ++i)
//             netA.Add(hostDevsA.Get(i));
//         // 为A域配置路由器接口
//         for (uint32_t i = 0; i < routerDevsA.GetN(); ++i)
//             netA.Add(routerDevsA.Get(i));
//         ifA = ipv4.Assign(netA);
//     }

//     Ipv4InterfaceContainer ifB;
//     ipv4.SetBase("10.2.1.0", "255.255.255.0");
//     {
//         NetDeviceContainer netB = NetDeviceContainer();
//         for (uint32_t i = 0; i < hostDevsB.GetN(); ++i)
//             netB.Add(hostDevsB.Get(i));
//         for (uint32_t i = 0; i < routerDevsB.GetN(); ++i)
//             netB.Add(routerDevsB.Get(i));
//         ifB = ipv4.Assign(netB);
//     }

//     // wifi 网络配置
//     Ipv4InterfaceContainer ifC;
//     ipv4.SetBase("10.3.1.0", "255.255.255.0");
//     {
//         NetDeviceContainer netC = NetDeviceContainer();
//         // 先配置主机
//         for (uint32_t i = 0; i < staDevices.GetN(); ++i)
//             netC.Add(staDevices.Get(i));

//         // 为A域配置路由器接口
//         for (uint32_t i = 0; i < routerDevsC.GetN(); ++i)
//             netC.Add(routerDevsC.Get(i));
//         for (uint32_t i = 0; i < adhocDevices.GetN(); ++i)
//             netC.Add(adhocDevices.Get(i)); // AdHoc 接口
//         ifC = ipv4.Assign(netC);
//     } // wifi 网络

//     ///----------------------------------------///
//     // 路由器的 IP 地址配置
//     Ipv4Address routerA = ifA.GetAddress(hostDevsA.GetN()); // 路由器A的地址
//     Ipv4Address routerB = ifB.GetAddress(hostDevsB.GetN());
//     Ipv4Address routerC = ifC.GetAddress(staDevices.GetN());
//     // 为 C 网络的主机设置默认路由到路由器 C
//     for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
//     {
//         Ptr<Node> h = wifiStaNodes.Get(i);
//         Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
//         Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
//         // uint32_t staIfIndex = wifiStaNodes.Get(i)->GetObject<Ipv4>()->GetInterfaceForDevice(staDevices.Get(i));
//         staticRouting->SetDefaultRoute(routerC, 1);
//     }

//     // 为 A 网络的主机设置默认路由到路由器 A
//     for (uint32_t i = 0; i < hostsA.GetN(); ++i)
//     {
//         Ptr<Node> h = hostsA.Get(i);
//         Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
//         Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
//         staticRouting->SetDefaultRoute(routerA, 1);
//     }

//     // 为 B 网络的主机设置默认路由到路由器 B
//     for (uint32_t i = 0; i < hostsB.GetN(); ++i)
//     {
//         Ptr<Node> h = hostsB.Get(i);
//         Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
//         Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
//         staticRouting->SetDefaultRoute(routerB, 1);
//     }
//     phy.EnablePcap("adhocpcap", adhocDevices);
//     phy.EnablePcap("stapcap", staDevices);
//     phy.EnablePcap("appcap", apDevice);

//     // 启用pcap追踪
//     if (true)
//     {
//         of13Helper->EnableOpenFlowPcap("openflow-interdomain");
//         of13Helper->EnableDatapathStats("switch-stats");
//         csma.EnablePcap("sw1", sw1Devsports, true);
//         csma.EnablePcap("sw2", sw2Devsports, true);

//         csma.EnablePcap("hostA", hostDevsA);
//         csma.EnablePcap("hostB", hostDevsB);
//         // 开启 PCAP
//     }

//     for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
//     {
//         Ipv4StaticRoutingHelper staticRoutingHelper2;
//         Ptr<Ipv4> ipv42 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
//         Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);

//         s->AddHostRouteTo(ifC.GetAddress(0), 1);
//         s->AddHostRouteTo(ifC.GetAddress(1), 1);
//         s->AddHostRouteTo(ifC.GetAddress(2), 1);
//         s->AddHostRouteTo(ifC.GetAddress(3), 1);
//         s->AddHostRouteTo(ifC.GetAddress(i + 4), 0);
//     }
//     Ipv4StaticRoutingHelper staticRoutingHelper2;
//     Ptr<Ipv4> ipv42 = wifiStaNodes.Get(1)->GetObject<Ipv4>();
//     Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);

//     // s->AddHostRouteTo(ifC.GetAddress(3), 1);
//     //   Ping from Domain A hos1t 0 to Domain B host 0 (cross-domain ping)
//     //   Ipv4Address dst1 = ifB.GetAddress(1); // first host in domain C
//     //   V4PingHelper ping2(dst1);
//     //   ping2.SetAttribute("Verbose", BooleanValue(true));
//     //   ApplicationContainer pingApp2 = ping2.Install(wifiStaNodes.Get(1));

//     // Ping from Domain A host 0 to Domain B host 0 (cross-domain ping)
//     Ipv4Address dst = ifC.GetAddress(2); // first host in domain C
//     V4PingHelper ping(dst);
//     ping.SetAttribute("Verbose", BooleanValue(true));
//     ApplicationContainer pingApp = ping.Install(wifiStaNodes.Get(1));

//     pingApp.Start(Seconds(1.0));
//     pingApp.Stop(Seconds(simTime - 1));
//     // kai or guan bi adhoc

//     //Simulator::Schedule(Seconds(7.0),&OFSwitch13LearningController::CDL,controllerApp);
    
//     /*for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
//     {
//         Ptr<Node> node = wifiStaNodes.Get(i);
//         Ptr<NetDevice> dev = adhocDevices.Get(i); // 假设每个 STA 的 AdHoc 接口索引相同

//         // 关闭
//         Simulator::Schedule(Seconds(0.0), &DisableDeviceLogical, node, dev);

//         // 第 7 秒开启
//         Simulator::Schedule(Seconds(7.0), &EnableDeviceLogical, node, dev);
//     }*/
//     {
//         std::cout << "ap mac" << std::endl;
//         for (uint32_t j = 0; j < apDevice.GetN(); ++j)
//         {
//             Ptr<NetDevice> dev = apDevice.Get(j);
//             Address addr = dev->GetAddress();

//             std::cout << "  Device " << j
//                       << " -> MAC Address: "
//                       << Mac48Address::ConvertFrom(addr)
//                       << std::endl;
//         }
//         std::cout << "sta mac" << std::endl;
//         for (uint32_t j = 0; j < staDevices.GetN(); ++j)
//         {
//             Ptr<NetDevice> dev = staDevices.Get(j);
//             Address addr = dev->GetAddress();

//             std::cout << "  Device " << j
//                       << " -> MAC Address: "
//                       << Mac48Address::ConvertFrom(addr)
//                       << std::endl;
//         }
//         //
//         std::cout << "adhoc mac" << std::endl;
//         for (uint32_t j = 0; j < adhocDevices.GetN(); ++j)
//         {
//             Ptr<NetDevice> dev = adhocDevices.Get(j);
//             Address addr = dev->GetAddress();

//             std::cout << "  Device " << j
//                       << " -> MAC Address: "
//                       << Mac48Address::ConvertFrom(addr)
//                       << std::endl;
//         }
//         for (uint32_t j = 0; j < routerDevsC.GetN(); ++j)
//         {
//             Ptr<NetDevice> dev = routerDevsC.Get(j);
//             Address addr = dev->GetAddress();

//             std::cout << "  Device " << j
//                       << " -> MAC Address: "
//                       << Mac48Address::ConvertFrom(addr)
//                       << std::endl;
//         }
//     }
//     // Schedule函数使用格式:
//     //Simulator::Schedule(时间间隔, 待执行函数, 函数参数1, 函数参数2, ...);
//     // LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_DEBUG);
//     Simulator::Schedule(Seconds(2.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
//     // Simulator::Schedule(Seconds(3.0),&OFSwitch13Device::GetApStaMessages,sw3Device);
//     if (sw3Device)
//     {
//         double interval = 3.0;  // 执行间隔（秒）
//         // 首次调度：0秒后执行第一次，之后每3秒执行一次
//         Simulator::Schedule(Seconds(0.0), &PeriodicSendPosition, sw3Device, interval);
//     }
//      // 2. 直接调度周期性决策函数（替代原StartQLearningDecisionProcess）
//     // 立即执行一次，然后8秒后开始每5秒执行
//     // 立即执行一次
//     controllerApp->PeriodicDecisionMaking(); 
//     // 3秒后再执行一次
//     Simulator::Schedule(Seconds(3.5), &OFSwitch13LearningController::PeriodicDecisionMaking, controllerApp); //Simulator::Schedule(Seconds(2.0),&OFSwitch13LearningController::SetPriorityToAll,controllerApp);
    
//     // Simulator::Schedule(Seconds(1.1), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
//     // Simulator::Schedule(Seconds(1.3), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
//     Simulator::Stop(Seconds(simTime));
//     Simulator::Run();
//     Simulator::Destroy();

//     return 0;
// }

/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 单控制器，跨域 SDN 示例，使用 ns-3 + ofswitch13 模块
 *
 * - 域 A: hostsA (2 主机) -- sw1
 * - 域 B: hostsB (2 主机) -- sw2
 * - 路由器节点连接 sw1 和 sw2（有两个子网的 IP）
 * - 单 OpenFlow 控制器管理 sw1 和 sw2
 *
 * 构建：确保 ns-3 已构建并启用 ofswitch13 模块。
 */

#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/internet-apps-module.h>
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include <ns3/internet-apps-module.h>
#include "ns3/bridge-helper.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
using namespace ns3;
// ---------------------------------------------------------
// 函数：禁用 AdHoc 接口
// ---------------------------------------------------------
// 逻辑 down
// ---------------------------------------------------------
// 函数：逻辑上下线设备
// enable = true 表示开启，false 表示关闭
// ---------------------------------------------------------

void DisableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    uint32_t idx = ipv4->GetInterfaceForDevice(dev);
    if (idx != uint32_t(-1))
        ipv4->SetDown(idx);
}
void EnableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    uint32_t idx = ipv4->GetInterfaceForDevice(dev);
    if (idx != uint32_t(-1))
        ipv4->SetUp(idx);
}
int main(int argc, char *argv[])
{
    uint16_t simTime = 10;
    bool verbose = true;
    bool trace = false;

    CommandLine cmd;
    cmd.AddValue("simTime", "simulate time ", simTime); // 设置仿真时间
    cmd.AddValue("verbose", "enable verbose logs", verbose);
    cmd.AddValue("trace", "enable trace /pcap", trace);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        /* code */
    }

    // 启用校验和计算（ofswitch13 模块所需）
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    NodeContainer hostsA;
    hostsA.Create(2);
    NodeContainer hostsB;
    hostsB.Create(2);

    Ptr<Node> sw1 = CreateObject<Node>();
    Ptr<Node> sw2 = CreateObject<Node>();
    Ptr<Node> sw3 = CreateObject<Node>();
    Ptr<Node> routerNode1 = CreateObject<Node>();
    // Ptr<Node> routerNode2 = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer hostDevsA, hostDevsB, ApDevsC;
    NetDeviceContainer sw1Devsports, sw2Devsports, sw3Devsports;
    NetDeviceContainer routerDevsA, routerDevsB, routerDevsC;
    // 连接域 A 主机到 sw1
    for (uint32_t i = 0; i < hostsA.GetN(); ++i)
    {
        NodeContainer pair(hostsA.Get(i), sw1);
        NetDeviceContainer link = csma.Install(pair);
        hostDevsA.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }
    // 连接域 B 主机到 sw2
    for (uint32_t i = 0; i < hostsB.GetN(); ++i)
    {
        NodeContainer pair(hostsB.Get(i), sw2);
        NetDeviceContainer link = csma.Install(pair);
        hostDevsB.Add(link.Get(0));
        sw2Devsports.Add(link.Get(1));
    }

    // 连接路由器到 sw1（域 A 网络）
    {
        NodeContainer pair(routerNode1, sw1);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsA.Add(link.Get(0));
        sw1Devsports.Add(link.Get(1));
    }
    // 连接路由器到 sw2（域 B 网络）
    {
        NodeContainer pair(routerNode1, sw2);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsB.Add(link.Get(0));  // 路由器在 B 网络的接口
        sw2Devsports.Add(link.Get(1)); // 将这个端口加入sw2
    }
    // 连接路由器到 sw3（域 C 网络）
    {
        NodeContainer pair(routerNode1, sw3);
        NetDeviceContainer link = csma.Install(pair);
        routerDevsC.Add(link.Get(0));  // 路由器在 C 网络的接口
        sw3Devsports.Add(link.Get(1)); // 将这个端口添加到sw3
    }
    csma.EnablePcapAll("csma-trace", true);
    // wifi配置部分
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(3);
    NodeContainer wifiApNode;
    wifiApNode.Create(1);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager");
    WifiMacHelper mac;
    Ssid ssid = Ssid("C");

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer adhocDevices = wifi.Install(phy, mac, wifiStaNodes);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);
    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Time", TimeValue(Seconds(1.0)),                                      // 每次移动的间隔时间
                              "Speed", StringValue("ns3::UniformRandomVariable[Min=0.1|Max=0.5]")); // 移动速度
    mobility.Install(wifiStaNodes);

    // 连接域 C 主机到 sw3
    {
        NodeContainer pair(wifiApNode.Get(0), sw3);
        NetDeviceContainer link = csma.Install(pair);
        ApDevsC.Add(link.Get(0));
        sw3Devsports.Add(link.Get(1));
    }
    BridgeHelper bridge;
    NetDeviceContainer bridgeDev;
    bridgeDev = bridge.Install(wifiApNode.Get(0),
                               NetDeviceContainer(apDevice.Get(0), ApDevsC.Get(0)));

    Ptr<OFSwitch13InternalHelper> of13Helper =
        CreateObject<OFSwitch13InternalHelper>();

    of13Helper->InstallController(controllerNode);
    of13Helper->InstallSwitch(sw1, sw1Devsports);
    of13Helper->InstallSwitch(sw2, sw2Devsports);
    of13Helper->InstallSwitch(sw3, sw3Devsports);
    of13Helper->CreateOpenFlowChannels();
    auto get = of13Helper->GetController();

    Ptr<OFSwitch13LearningController>
        controllerApp =
            DynamicCast<OFSwitch13LearningController>(get.Get(0));

    // --------------------------
    // 4. 网络栈配置部分
    // --------------------------
    // Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));
    // Config::SetDefault("ns3::Ipv4::IpForward", BooleanValue(true));

    // 为主机配置静态默认路由指向路由器
    Ipv4StaticRoutingHelper staticRoutingHelper;

    InternetStackHelper stack;

    InternetStackHelper stack2;
    Ipv4ListRoutingHelper list;
    // Ipv4StaticRoutingHelper staticC;

    AodvHelper aodv;
    OlsrHelper olsr;
    list.Add(aodv, 10);
    list.Add(olsr, 10);
    list.Add(staticRoutingHelper, 100);
    stack2.SetRoutingHelper(list); // 对于 AdHoc 节点使用 AODV
    stack2.Install(wifiStaNodes);
    stack2.Install(hostsA);
    stack2.Install(hostsB);
    stack.Install(routerNode1);

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetAttribute("IpForward", BooleanValue(true)); // 启用转发
    }
    // 分配IPv4地址
    Ipv4AddressHelper ipv4;

    Ipv4InterfaceContainer ifA; // 域A主机和路由器接口
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    // 给 hostsA 设备和 routerDevsA 分配地址
    {
        NetDeviceContainer netA = NetDeviceContainer();
        // 主机先分配
        for (uint32_t i = 0; i < hostDevsA.GetN(); ++i)
            netA.Add(hostDevsA.Get(i));
        // 为A域配置路由器接口
        for (uint32_t i = 0; i < routerDevsA.GetN(); ++i)
            netA.Add(routerDevsA.Get(i));
        ifA = ipv4.Assign(netA);
    }

    Ipv4InterfaceContainer ifB;
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    {
        NetDeviceContainer netB = NetDeviceContainer();
        for (uint32_t i = 0; i < hostDevsB.GetN(); ++i)
            netB.Add(hostDevsB.Get(i));
        for (uint32_t i = 0; i < routerDevsB.GetN(); ++i)
            netB.Add(routerDevsB.Get(i));
        ifB = ipv4.Assign(netB);
    }

    // wifi 网络配置
    Ipv4InterfaceContainer ifC;
    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    {
        NetDeviceContainer netC = NetDeviceContainer();
        // 先配置主机
        for (uint32_t i = 0; i < staDevices.GetN(); ++i)
            netC.Add(staDevices.Get(i));

        // 为A域配置路由器接口
        for (uint32_t i = 0; i < routerDevsC.GetN(); ++i)
            netC.Add(routerDevsC.Get(i));
        for (uint32_t i = 0; i < adhocDevices.GetN(); ++i)
            netC.Add(adhocDevices.Get(i)); // AdHoc 接口
        ifC = ipv4.Assign(netC);
    } // wifi 网络

    ///----------------------------------------///
    // 路由器的 IP 地址配置
    Ipv4Address routerA = ifA.GetAddress(hostDevsA.GetN()); // 路由器A的地址
    Ipv4Address routerB = ifB.GetAddress(hostDevsB.GetN());
    Ipv4Address routerC = ifC.GetAddress(staDevices.GetN());
    // 为 C 网络的主机设置默认路由到路由器 C
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
    {
        Ptr<Node> h = wifiStaNodes.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        // uint32_t staIfIndex = wifiStaNodes.Get(i)->GetObject<Ipv4>()->GetInterfaceForDevice(staDevices.Get(i));
        staticRouting->SetDefaultRoute(routerC, 1);
    }

    // 为 A 网络的主机设置默认路由到路由器 A
    for (uint32_t i = 0; i < hostsA.GetN(); ++i)
    {
        Ptr<Node> h = hostsA.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerA, 1);
    }

    // 为 B 网络的主机设置默认路由到路由器 B
    for (uint32_t i = 0; i < hostsB.GetN(); ++i)
    {
        Ptr<Node> h = hostsB.Get(i);
        Ptr<Ipv4> ipv4h = h->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4h);
        staticRouting->SetDefaultRoute(routerB, 1);
    }
    phy.EnablePcap("adhocpcap", adhocDevices);
    phy.EnablePcap("stapcap", staDevices);
    phy.EnablePcap("appcap", apDevice);

    // 启用pcap追踪
    if (true)
    {
        of13Helper->EnableOpenFlowPcap("openflow-interdomain");
        of13Helper->EnableDatapathStats("switch-stats");
        csma.EnablePcap("sw1", sw1Devsports, true);
        csma.EnablePcap("sw2", sw2Devsports, true);

        csma.EnablePcap("hostA", hostDevsA);
        csma.EnablePcap("hostB", hostDevsB);
        // 开启 PCAP
    }

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
    {
        Ipv4StaticRoutingHelper staticRoutingHelper2;
        Ptr<Ipv4> ipv42 = wifiStaNodes.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);

        s->AddHostRouteTo(ifC.GetAddress(0), 1);
        s->AddHostRouteTo(ifC.GetAddress(1), 1);
        s->AddHostRouteTo(ifC.GetAddress(2), 1);
        s->AddHostRouteTo(ifC.GetAddress(3), 1);
        s->AddHostRouteTo(ifC.GetAddress(i + 4), 0);
    }
    Ipv4StaticRoutingHelper staticRoutingHelper2;
    Ptr<Ipv4> ipv42 = wifiStaNodes.Get(1)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> s = staticRoutingHelper2.GetStaticRouting(ipv42);

    // s->AddHostRouteTo(ifC.GetAddress(3), 1);
    //   Ping from Domain A hos1t 0 to Domain B host 0 (cross-domain ping)
    //   Ipv4Address dst1 = ifB.GetAddress(1); // first host in domain C
    //   V4PingHelper ping2(dst1);
    //   ping2.SetAttribute("Verbose", BooleanValue(true));
    //   ApplicationContainer pingApp2 = ping2.Install(wifiStaNodes.Get(1));

    // Ping from Domain A host 0 to Domain B host 0 (cross-domain ping)
    Ipv4Address dst = ifC.GetAddress(2); // first host in domain C
    V4PingHelper ping(dst);
    ping.SetAttribute("Verbose", BooleanValue(true));
    ApplicationContainer pingApp = ping.Install(wifiStaNodes.Get(1));

    pingApp.Start(Seconds(1.0));
    pingApp.Stop(Seconds(simTime - 1));
    // guan bi adhoc

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
    {
        Ptr<Node> node = wifiStaNodes.Get(i);
        Ptr<NetDevice> dev = adhocDevices.Get(i); // 假设每个 STA 的 AdHoc 接口索引相同

        // 关闭
        Simulator::Schedule(Seconds(0.0), &DisableDeviceLogical, node, dev);

        // 第 7 秒开启
        Simulator::Schedule(Seconds(7.0), &EnableDeviceLogical, node, dev);
    }
    {
        std::cout << "ap mac" << std::endl;
        for (uint32_t j = 0; j < apDevice.GetN(); ++j)
        {
            Ptr<NetDevice> dev = apDevice.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        std::cout << "sta mac" << std::endl;
        for (uint32_t j = 0; j < staDevices.GetN(); ++j)
        {
            Ptr<NetDevice> dev = staDevices.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        //
        std::cout << "adhoc mac" << std::endl;
        for (uint32_t j = 0; j < adhocDevices.GetN(); ++j)
        {
            Ptr<NetDevice> dev = adhocDevices.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
        for (uint32_t j = 0; j < routerDevsC.GetN(); ++j)
        {
            Ptr<NetDevice> dev = routerDevsC.Get(j);
            Address addr = dev->GetAddress();

            std::cout << "  Device " << j
                      << " -> MAC Address: "
                      << Mac48Address::ConvertFrom(addr)
                      << std::endl;
        }
    }
    // LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_DEBUG);
    Simulator::Schedule(Seconds(2.0), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.1), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    // Simulator::Schedule(Seconds(1.3), &OFSwitch13LearningController::SetRoutingPriority, controllerApp);
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}