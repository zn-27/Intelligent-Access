/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 融合架构交换机信息收集应用
 * 作用：在 AP 合并到 Switch 的融合架构中，独立收集 STA 信息、
 *       路由协议优先级、节点位置、流量统计，以及组网模式切换。
 *       直接持有 ApWifiMac 引用，无需通过通道发现。
 */
#ifndef SWITCH_PROTOCOL_INFO_APP_H
#define SWITCH_PROTOCOL_INFO_APP_H

#include "ap-protocol-info-app.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/mobility-model.h"
#include "ns3/ipv4-flow-classifier.h"

namespace ns3 {

// 数据结构 (Reply, stamessage, Position, FlowStatsRecord)
// 已在 ap-protocol-info-app.h 中定义，此处直接复用

class SwitchProtocolInfoApp : public Application
{
public:
  static TypeId GetTypeId(void);
  SwitchProtocolInfoApp();
  virtual ~SwitchProtocolInfoApp();

  void SetApMac(Ptr<ApWifiMac> apMac);
  void SetApPortNo(uint32_t portNo);
  uint32_t GetApPortNo() const;
  void SetFlowMonitor(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier);

  // STA 信息收集
  void CollectStaMassage();
  std::vector<stamessage>& GetStaMessages();

  // Adhoc STA 信息收集（模式切换后调用）
  std::vector<stamessage>& GetAdhocStaMessages();

  // STA 路由协议优先级收集与修改
  std::vector<Reply> CollectStaProtocolReplies();
  void UpdateStaRoutingPriority(Mac48Address targetSta, int16_t newAodvPri,
                                int16_t newOlsrPri, int16_t newStaticPri);
  void UpdateStaRoutingPriority(int16_t newAodvPri, int16_t newOlsrPri,
                                int16_t newStaticPri);

  // 组网模式切换
  void ChangeZuWang(uint32_t op);

  // 节点位置收集
  std::vector<Position> SendNodePosition();

  // 流量统计
  std::vector<FlowStatsRecord> CollectFlowStats();

protected:
  virtual void StartApplication() override;
  virtual void StopApplication() override;

private:
  // 在 NodeList 中通过 MAC 地址查找 STA 节点
  Ptr<Node> FindStaNode(Mac48Address staMac);

  Ptr<ApWifiMac> m_apMac;
  uint32_t m_apPortNo;
  std::vector<stamessage> m_staMessages;
  std::vector<stamessage> m_adhocStaMessages;

  // FlowMonitor
  Ptr<FlowMonitor> m_monitor;
  Ptr<Ipv4FlowClassifier> m_classifier;
  double m_lastSampleTime;
  std::map<uint32_t, uint64_t> m_lastFlowRxBytes;
  std::map<uint32_t, uint32_t> m_lastFlowTxPackets;
  std::map<uint32_t, uint32_t> m_lastFlowRxPackets;
};

} // namespace ns3
#endif
