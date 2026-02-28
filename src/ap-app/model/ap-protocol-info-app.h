#ifndef AP_PROTOCOL_INFO_APP_H
#define AP_PROTOCOL_INFO_APP_H

#include "ns3/application.h"
#include "ns3/wifi-net-device.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/node-list.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/mobility-model.h"

// 新增：FlowMonitor 相关头文件
#include "ns3/flow-monitor-module.h"

namespace ns3 {

// 自定义一个类似reply消息的结构体
struct Reply
{
  uint32_t vendor;
  uint32_t subtype;
  uint16_t p1;
  uint16_t p2;
  uint16_t p3;
  uint64_t mac_ad;
  uint32_t ip_ad;
};
struct stamessage
{
  uint64_t mac_address;        
  uint32_t ip_address;         
};
//自定义一个周期性发送节点信息的存储体
struct Position
{
  uint32_t ip_ad;
  float x;
  float y;
  float z;
};
//zi ding yi FLOW
struct FlowStatsRecord {
    uint32_t throughputKbps; // 吞吐量 (Kbps)
    uint32_t delayMs;        // 平均延迟 (毫秒)
    uint32_t jitterMs;       // 平均抖动 (毫秒)
    uint16_t port;           // 目的端口 (用于识别链路)
    uint16_t lossRate;       // 丢包率 (放大 10000 倍，如 555 代表 5.55%)
    uint32_t queueDropCnt; // 队列溢出丢包数
};

// 仅负责收集协议信息的应用
class ApProtocolInfoApp : public Application
{
public:
  static TypeId GetTypeId(void);
  ApProtocolInfoApp();
  virtual ~ApProtocolInfoApp();

  // 主函数：生成 reply 消息列表
  std::vector<Reply> CollectStaProtocolReplies();
  void UpdateStaRoutingPriority(Mac48Address targetSta, int16_t newAodvPri,
                                                      int16_t newOlsrPri, int16_t newStaticPri);
  // 一次性统一修改一个域的重载函数.
  void UpdateStaRoutingPriority(int16_t newAodvPri, int16_t newOlsrPri, int16_t newStaticPri);

  // 搜集sta的mac地址和ip地址信息
  void CollectStaMassage();

  // 这是一个修改组网模式的函数
  void ChangeZuWang(uint32_t op);
  
  //周期性发送节点位置和ip地址
  std::vector<Position> SendNodePosition();
  
  // 让外界访问 STA 信息的接口
  std::vector<stamessage>& GetStaMessages();


  // =======FLOW =======
    std::vector<FlowStatsRecord> CollectFlowStats();

    // 流量统计成员变量
    Ptr<FlowMonitor> m_monitor;
    Ptr<Ipv4FlowClassifier> m_classifier;
    double m_lastSampleTime;                    // 上次采样时间
    std::map<uint32_t, uint64_t> m_lastFlowRxBytes;      // 上次各流接收的总字节数
    
    // --- 新增以下两行 ---
    std::map<uint32_t, uint32_t> m_lastFlowTxPackets;    // 上次各流发送的总包数
    std::map<uint32_t, uint32_t> m_lastFlowRxPackets;    // 上次各流接收的总包数

private:
  //存储收集的sta信息
  std::vector<stamessage> m_staMessages;

protected:
  virtual void StartApplication() override {}
  virtual void StopApplication() override {}
};

} // namespace ns3

#endif
