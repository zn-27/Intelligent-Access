/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * 融合架构交换机信息收集应用
 * 独立实现，直接通过 m_apMac 收集 STA 信息，不依赖 ApProtocolInfoApp
 */
#include "switch-protocol-info-app.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SwitchProtocolInfoApp");
NS_OBJECT_ENSURE_REGISTERED(SwitchProtocolInfoApp);

TypeId
SwitchProtocolInfoApp::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::SwitchProtocolInfoApp")
    .SetParent<Application>()
    .AddConstructor<SwitchProtocolInfoApp>()
    .SetGroupName("ApProtocolInfo");
  return tid;
}

SwitchProtocolInfoApp::SwitchProtocolInfoApp()
  : m_apMac(nullptr),
    m_apPortNo(0),
    m_adhocMac(nullptr),
    m_adhocPortNo(0),
    m_isAdhocMode(false),
    m_monitor(nullptr),
    m_classifier(nullptr),
    m_lastSampleTime(0.0)
{}

SwitchProtocolInfoApp::~SwitchProtocolInfoApp() {}

void SwitchProtocolInfoApp::StartApplication()
{
  if (m_isAdhocMode)
    {
      std::cout << "[SwitchApp] 初始 Ad-Hoc 模式启动 (port="
                << m_adhocPortNo << ")" << std::endl;
    }
  NS_LOG_INFO("SwitchProtocolInfoApp started on fused switch node");
}

void SwitchProtocolInfoApp::StopApplication()
{
  NS_LOG_INFO("SwitchProtocolInfoApp stopped");
}

void
SwitchProtocolInfoApp::SetApMac(Ptr<ApWifiMac> apMac)
{
  m_apMac = apMac;
}

void
SwitchProtocolInfoApp::SetApPortNo(uint32_t portNo)
{
  m_apPortNo = portNo;
}

uint32_t
SwitchProtocolInfoApp::GetApPortNo() const
{
  // Ad-Hoc 模式下返回 Ad-Hoc 端口号，否则返回 AP 端口号
  if (m_isAdhocMode)
    return m_adhocPortNo;
  return m_apPortNo;
}

void
SwitchProtocolInfoApp::SetFlowMonitor(Ptr<FlowMonitor> monitor,
                                       Ptr<Ipv4FlowClassifier> classifier)
{
  m_monitor = monitor;
  m_classifier = classifier;
}

// ============================================================
// 辅助函数：在 NodeList 中通过 MAC 地址查找 STA 节点
// ============================================================
Ptr<Node>
SwitchProtocolInfoApp::FindStaNode(Mac48Address staMac)
{
  for (uint32_t n = 0; n < NodeList::GetNNodes(); n++)
    {
      Ptr<Node> node = NodeList::GetNode(n);
      for (uint32_t j = 0; j < node->GetNDevices(); j++)
        {
          Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(node->GetDevice(j));
          if (wdev && wdev->GetMac()->GetAddress() == staMac)
            {
              return node;
            }
        }
    }
  return nullptr;
}

// ============================================================
// 收集 STA 的 MAC 和 IP 地址
// ============================================================
void
SwitchProtocolInfoApp::CollectStaMassage()
{
  NS_LOG_INFO("SwitchProtocolInfoApp::CollectStaMassage()");
  m_staMessages.clear();

  if (!m_apMac)
    {
      std::cout << "[SwitchApp] Error: m_apMac is null, cannot collect STA info"
                << std::endl;
      return;
    }

  const std::map<uint16_t, Mac48Address> &staMap = m_apMac->GetStaList();

  for (auto const &entry : staMap)
    {
      Mac48Address staMac = entry.second;
      Ptr<Node> staNode = FindStaNode(staMac);
      if (!staNode)
        {
          std::cout << "[SwitchApp] Cannot find STA node for MAC " << staMac
                    << std::endl;
          continue;
        }

      Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
      if (!ipv4) continue;

      Ipv4Address ip = ipv4->GetAddress(1, 0).GetLocal();

      stamessage sm;
      sm.mac_address = staMac.ConvertToU64();
      sm.ip_address  = ip.Get();
      m_staMessages.push_back(sm);

      NS_LOG_INFO("Collected STA MAC=" << staMac << " IP=" << ip);
    }

  NS_LOG_INFO("SwitchProtocolInfoApp collected " << m_staMessages.size()
               << " STA messages.");
}

std::vector<stamessage>&
SwitchProtocolInfoApp::GetStaMessages()
{
  if (m_isAdhocMode)
    {
      CollectAdhocNeighbors();
      if (m_adhocStaMessages.empty())
        {
          std::cout << "[SwitchApp] m_adhocStaMessages is empty!" << std::endl;
        }
      return m_adhocStaMessages;
    }
  else
    {
      CollectStaMassage();
      if (m_staMessages.empty())
        {
          std::cout << "[SwitchApp] m_staMessages is empty!" << std::endl;
        }
      return m_staMessages;
    }
}

// ============================================================
// 收集 STA 路由协议优先级信息
// ============================================================
std::vector<Reply>
SwitchProtocolInfoApp::CollectStaProtocolReplies()
{
  std::vector<Reply> replies;

  if (!m_apMac) return replies;

  const std::map<uint16_t, Mac48Address> &staMap = m_apMac->GetStaList();

  for (auto const &entry : staMap)
    {
      uint16_t aid = entry.first;
      Mac48Address staMac = entry.second;
      Ptr<Node> staNode = FindStaNode(staMac);

      if (!staNode)
        {
          NS_LOG_WARN("Cannot find STA node for MAC " << staMac);
          continue;
        }

      Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
      if (!ipv4) continue;

      Ipv4Address staIp = Ipv4Address("0.0.0.0");
      uint32_t staIpRaw = 0;
      for (uint32_t ifc = 0; ifc < ipv4->GetNInterfaces(); ifc++)
        {
          for (uint32_t addrIdx = 0; addrIdx < ipv4->GetNAddresses(ifc); addrIdx++)
            {
              Ipv4InterfaceAddress ifAddr = ipv4->GetAddress(ifc, addrIdx);
              if (ifAddr.GetLocal() != Ipv4Address("127.0.0.1"))
                {
                  staIp = ifAddr.GetLocal();
                  staIpRaw = staIp.Get();
                  break;
                }
            }
        }

      Ptr<Ipv4ListRouting> listRouting =
          DynamicCast<Ipv4ListRouting>(ipv4->GetRoutingProtocol());
      if (!listRouting) continue;

      uint16_t pAodv = 0, pOlsr = 0, pStatic = 0;
      for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
        {
          int16_t pri = 0;
          Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, pri);
          std::string name = proto->GetInstanceTypeId().GetName();

          if (name.find("aodv") != std::string::npos)
            pAodv = pri;
          else if (name.find("olsr") != std::string::npos)
            pOlsr = pri;
          else if (name.find("Ipv4StaticRouting") != std::string::npos)
            pStatic = pri;
        }

      Reply reply;
      reply.vendor = 0x12345678;
      reply.subtype = 2001;
      reply.p1 = pAodv;
      reply.p2 = pOlsr;
      reply.p3 = pStatic;
      reply.mac_ad = staMac.ConvertToU64();
      reply.ip_ad  = staIpRaw;
      replies.push_back(reply);

      NS_LOG_INFO("STA (AID=" << aid << ", MAC=" << staMac
                   << ", IP=" << staIp
                   << ") AODV=" << pAodv
                   << ", OLSR=" << pOlsr
                   << ", Static=" << pStatic);
    }

  return replies;
}

// ============================================================
// 修改指定 STA 的路由协议优先级
// ============================================================
void
SwitchProtocolInfoApp::UpdateStaRoutingPriority(Mac48Address targetSta,
                                                  int16_t newAodvPri,
                                                  int16_t newOlsrPri,
                                                  int16_t newStaticPri)
{
  std::cout << "\n[SwitchApp] Updating routing priorities for STA: "
            << targetSta << std::endl;

  Ptr<Node> staNode = FindStaNode(targetSta);
  if (!staNode)
    {
      std::cout << "[SwitchApp] Error: Cannot find STA node for MAC "
                << targetSta << std::endl;
      return;
    }

  Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
  if (!ipv4) return;

  Ptr<Ipv4ListRouting> listRouting =
      DynamicCast<Ipv4ListRouting>(ipv4->GetRoutingProtocol());
  if (!listRouting) return;

  std::cout << "[Before] STA " << targetSta << " priorities:" << std::endl;
  for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
    {
      int16_t pri = 0;
      Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, pri);
      std::cout << "  " << proto->GetInstanceTypeId().GetName()
                << " => " << pri << std::endl;
    }

  listRouting->SetRoutingProtocolPriorityByType(aodv::RoutingProtocol::GetTypeId(), newAodvPri);
  listRouting->SetRoutingProtocolPriorityByType(olsr::RoutingProtocol::GetTypeId(), newOlsrPri);
  listRouting->SetRoutingProtocolPriorityByType(Ipv4StaticRouting::GetTypeId(), newStaticPri);

  std::cout << "[After] STA " << targetSta << " priorities:" << std::endl;
  for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
    {
      int16_t pri = 0;
      Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, pri);
      std::cout << "  " << proto->GetInstanceTypeId().GetName()
                << " => " << pri << std::endl;
    }
}

// ============================================================
// 修改全部 STA 的路由协议优先级（重载）
// ============================================================
void
SwitchProtocolInfoApp::UpdateStaRoutingPriority(int16_t newAodvPri,
                                                  int16_t newOlsrPri,
                                                  int16_t newStaticPri)
{
  std::cout << "\n[SwitchApp] ====== Updating Routing Priorities for All STAs ======"
            << std::endl;

  if (!m_apMac) return;

  const std::map<uint16_t, Mac48Address> &staMap = m_apMac->GetStaList();
  for (auto const &entry : staMap)
    {
      Mac48Address staMac = entry.second;
      Ptr<Node> staNode = FindStaNode(staMac);
      if (!staNode) continue;

      Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
      if (!ipv4) continue;

      Ptr<Ipv4ListRouting> listRouting =
          DynamicCast<Ipv4ListRouting>(ipv4->GetRoutingProtocol());
      if (!listRouting) continue;

      listRouting->SetRoutingProtocolPriorityByType(aodv::RoutingProtocol::GetTypeId(), newAodvPri);
      listRouting->SetRoutingProtocolPriorityByType(olsr::RoutingProtocol::GetTypeId(), newOlsrPri);
      listRouting->SetRoutingProtocolPriorityByType(Ipv4StaticRouting::GetTypeId(), newStaticPri);

      std::cout << "[SwitchApp] STA " << staMac << " priorities updated." << std::endl;
    }
}

// ============================================================
// 修改组网模式（启用/禁用 STA 的 Adhoc 接口）
// ============================================================
void
SwitchProtocolInfoApp::ChangeZuWang(uint32_t op)
{
  std::cout << "\n[SwitchApp] ====== Switch STA Network Mode, op="
            << op << " ======" << std::endl;

  // 更新模式标志
  if (op == 1)
    m_isAdhocMode = true;
  else if (op == 0)
    m_isAdhocMode = false;

  if (!m_apMac) return;

  if (op == 1)
    m_adhocStaMessages.clear();

  const std::map<uint16_t, Mac48Address> &staMap = m_apMac->GetStaList();

  for (auto const &entry : staMap)
    {
      Mac48Address staMac = entry.second;
      Ptr<Node> staNode = FindStaNode(staMac);

      if (!staNode)
        {
          std::cout << "[SwitchApp] Warning: Cannot find STA node for MAC "
                    << staMac << std::endl;
          continue;
        }

      Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
      if (!ipv4) continue;

      for (uint32_t d = 0; d < staNode->GetNDevices(); d++)
        {
          Ptr<WifiNetDevice> wdev =
              DynamicCast<WifiNetDevice>(staNode->GetDevice(d));
          if (!wdev) continue;

          uint32_t ifIdx = ipv4->GetInterfaceForDevice(wdev);
          if (ifIdx == uint32_t(-1)) continue;

          if (op == 1)
            {
              // 无中心模式：禁用 StaWifiMac，启用 AdhocWifiMac
              if (DynamicCast<StaWifiMac>(wdev->GetMac()))
                {
                  ipv4->SetDown(ifIdx);
                  std::cout << "[SwitchApp] STA " << staMac
                            << " StaWifiMac DOWN (ifIdx=" << ifIdx << ")" << std::endl;
                }
              else if (DynamicCast<AdhocWifiMac>(wdev->GetMac()))
                {
                  ipv4->SetUp(ifIdx);
                  std::cout << "[SwitchApp] STA " << staMac
                            << " AdhocWifiMac UP (ifIdx=" << ifIdx << ")" << std::endl;
                  // 收集 adhoc STA 信息（IP + MAC）
                  stamessage sm;
                  sm.mac_address = wdev->GetMac()->GetAddress().ConvertToU64();
                  if (ipv4->GetNAddresses(ifIdx) > 0)
                    sm.ip_address = ipv4->GetAddress(ifIdx, 0).GetLocal().Get();
                  else
                    sm.ip_address = 0;
                  m_adhocStaMessages.push_back(sm);
                }
            }
          else if (op == 0)
            {
              // 中心模式：启用 StaWifiMac，禁用 AdhocWifiMac
              if (DynamicCast<StaWifiMac>(wdev->GetMac()))
                {
                  ipv4->SetUp(ifIdx);
                  std::cout << "[SwitchApp] STA " << staMac
                            << " StaWifiMac UP (ifIdx=" << ifIdx << ")" << std::endl;
                }
              else if (DynamicCast<AdhocWifiMac>(wdev->GetMac()))
                {
                  ipv4->SetDown(ifIdx);
                  std::cout << "[SwitchApp] STA " << staMac
                            << " AdhocWifiMac DOWN (ifIdx=" << ifIdx << ")" << std::endl;
                }
            }
        }
      std::cout << "[SwitchApp] STA " << staMac
                << " switched to mode=" << (op == 1 ? "ADHOC" : "INFRA") << std::endl;
    }

  if (op == 0)
    m_adhocStaMessages.clear();
}

std::vector<stamessage>&
SwitchProtocolInfoApp::GetAdhocStaMessages()
{
  return m_adhocStaMessages;
}

// ============================================================
// Ad-Hoc 邻居发现：Setter 方法
// ============================================================
void
SwitchProtocolInfoApp::SetAdhocMac(Ptr<AdhocWifiMac> adhocMac)
{
  m_adhocMac = adhocMac;
}

void
SwitchProtocolInfoApp::SetAdhocPortNo(uint32_t portNo)
{
  m_adhocPortNo = portNo;
}

void
SwitchProtocolInfoApp::SetAdhocChannelDevices(NetDeviceContainer staDevs)
{
  m_adhocStaDevs = staDevs;
}

void
SwitchProtocolInfoApp::SetInitialAdhocMode(bool isAdhoc)
{
  m_isAdhocMode = isAdhoc;
}

// ============================================================
// Ad-Hoc 邻居发现：收集域内 Ad-Hoc 邻居的 MAC/IP
// ============================================================
void
SwitchProtocolInfoApp::CollectAdhocNeighbors()
{
  std::cout << "[SwitchApp] CollectAdhocNeighbors()" << std::endl;
  m_adhocStaMessages.clear();

  for (uint32_t i = 0; i < m_adhocStaDevs.GetN(); ++i)
    {
      Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(m_adhocStaDevs.Get(i));
      if (!wdev) continue;

      Mac48Address staMac = Mac48Address::ConvertFrom(wdev->GetAddress());
      Ptr<Node> staNode = wdev->GetNode();
      if (!staNode) continue;

      Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
      if (!ipv4) continue;

      uint32_t ifIdx = ipv4->GetInterfaceForDevice(wdev);
      if (ifIdx == uint32_t(-1)) continue;

      if (ipv4->GetNAddresses(ifIdx) > 0)
        {
          Ipv4Address ip = ipv4->GetAddress(ifIdx, 0).GetLocal();
          stamessage sm;
          sm.mac_address = staMac.ConvertToU64();
          sm.ip_address  = ip.Get();
          m_adhocStaMessages.push_back(sm);
          std::cout << "[SwitchApp] Ad-Hoc neighbor: MAC=" << staMac
                    << " IP=" << ip << std::endl;
        }
    }

  std::cout << "[SwitchApp] Collected " << m_adhocStaMessages.size()
            << " Ad-Hoc neighbors." << std::endl;
}

// ============================================================
// 收集 STA 节点位置信息
// ============================================================
std::vector<Position>
SwitchProtocolInfoApp::SendNodePosition()
{
  std::vector<Position> posVec;

  if (!m_apMac) return posVec;

  if (m_monitor)
    {
      m_monitor->CheckForLostPackets();
    }

  const std::map<uint16_t, Mac48Address> &staMap = m_apMac->GetStaList();

  for (auto const &entry : staMap)
    {
      Mac48Address staMac = entry.second;
      Ptr<Node> staNode = FindStaNode(staMac);

      if (!staNode) continue;

      Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
      if (!ipv4 || ipv4->GetNInterfaces() <= 1) continue;

      Ipv4Address ip = ipv4->GetAddress(1, 0).GetLocal();

      Ptr<MobilityModel> mobility = staNode->GetObject<MobilityModel>();
      if (!mobility) continue;

      Vector pos = mobility->GetPosition();

      Position p;
      memset(&p, 0, sizeof(Position));
      p.ip_ad = ip.Get();
      p.x = static_cast<float>(pos.x);
      p.y = static_cast<float>(pos.y);
      p.z = static_cast<float>(pos.z);
      posVec.push_back(p);

      std::cout << "[SwitchApp] STA " << staMac
                << " IP=" << ip
                << " Pos=(" << p.x << ", " << p.y << ", " << p.z << ")"
                << std::endl;
    }

  return posVec;
}

// ============================================================
// 收集流量统计信息
// ============================================================
std::vector<FlowStatsRecord>
SwitchProtocolInfoApp::CollectFlowStats()
{
  std::vector<FlowStatsRecord> flowVec;

  if (!m_monitor || !m_classifier)
    {
      NS_LOG_WARN("FlowMonitor or Classifier not set!");
      return flowVec;
    }

  m_monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> stats = m_monitor->GetFlowStats();

  double currentTime = Simulator::Now().GetSeconds();
  double deltaTime = currentTime - m_lastSampleTime;
  if (deltaTime <= 0) deltaTime = 0.001;

  std::cout << "\n[SwitchApp] --- Periodic Flow Collection (Delta: "
            << deltaTime << "s) ---" << std::endl;

  for (auto const &flow : stats)
    {
      Ipv4FlowClassifier::FiveTuple t = m_classifier->FindFlow(flow.first);
      uint32_t flowId = flow.first;
      const FlowMonitor::FlowStats &fs = flow.second;

      uint64_t currentTotalRx = fs.rxBytes;
      uint64_t lastRx = m_lastFlowRxBytes.count(flowId) ? m_lastFlowRxBytes[flowId] : 0;
      uint64_t deltaRx = (currentTotalRx >= lastRx) ? (currentTotalRx - lastRx) : 0;
      uint32_t intervalThrKbps = static_cast<uint32_t>((deltaRx * 8.0) / (deltaTime * 1024.0));

      uint32_t lastTxP = m_lastFlowTxPackets.count(flowId) ? m_lastFlowTxPackets[flowId] : 0;
      uint32_t lastRxP = m_lastFlowRxPackets.count(flowId) ? m_lastFlowRxPackets[flowId] : 0;

      uint32_t deltaTxP = fs.txPackets - lastTxP;
      uint32_t deltaRxP = fs.rxPackets - lastRxP;

      uint16_t scaledLossRate = 0;
      if (deltaTxP > 0 && deltaTxP > deltaRxP)
        {
          double intervalLoss = static_cast<double>(deltaTxP - deltaRxP) / deltaTxP;
          scaledLossRate = static_cast<uint16_t>(intervalLoss * 10000);
        }

      uint32_t avgDelayMs = 0;
      uint32_t avgJitterMs = 0;
      if (fs.rxPackets > 0)
        {
          avgDelayMs = static_cast<uint32_t>(fs.delaySum.GetMilliSeconds() / fs.rxPackets);
        }
      if (fs.rxPackets > 1)
        {
          avgJitterMs = static_cast<uint32_t>(fs.jitterSum.GetMilliSeconds() / (fs.rxPackets - 1));
        }

      FlowStatsRecord record;
      record.throughputKbps = intervalThrKbps;
      record.delayMs        = avgDelayMs;
      record.jitterMs       = avgJitterMs;
      record.port           = static_cast<uint16_t>(t.destinationPort);
      record.lossRate       = scaledLossRate;
      flowVec.push_back(record);

      m_lastFlowRxBytes[flowId] = currentTotalRx;
      m_lastFlowTxPackets[flowId] = fs.txPackets;
      m_lastFlowRxPackets[flowId] = fs.rxPackets;

      std::cout << " [Flow ID " << flowId << "] " << record.port
                << "  Thr: " << record.throughputKbps << " Kbps"
                << " | Delay: " << record.delayMs << " ms"
                << " | Jitter: " << record.jitterMs << " ms"
                << " | Port: " << record.port
                << " | Loss: " << (record.lossRate / 100.0) << "%" << std::endl;
    }

  m_lastSampleTime = currentTime;
  return flowVec;
}

} // namespace ns3
