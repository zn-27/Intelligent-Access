/*自定义ap应用类
作用：1.收集sta节点的mac和路由协议优先级信息。
     2.实现sta节点路由协议优先级的切换。
     3.在拓扑连接最开始时收集sta的mac、ip地址和ap连接交换机设备的端口号；
     4.修改组网切换模式。
     5.周期性收集目前域内sta的节点位置和ip地址。
ps： p1=aodv p2=olsr p3=static Routing
*/
#include "ap-protocol-info-app.h"
#include "ns3/log.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ApProtocolInfoApp");
NS_OBJECT_ENSURE_REGISTERED(ApProtocolInfoApp);

TypeId
ApProtocolInfoApp::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::ApProtocolInfoApp")
    .SetParent<Application>()
    .AddConstructor<ApProtocolInfoApp>()
    .SetGroupName("ApProtocolInfo");
  return tid;
}

ApProtocolInfoApp::ApProtocolInfoApp() {}
ApProtocolInfoApp::~ApProtocolInfoApp() {}

void StartApplication() 
{
   NS_LOG_INFO("Start STA info collection");
}
//-----------------------------------------
//获取sta的mac地址和ip地址
//-----------------------------------------
void
ApProtocolInfoApp::CollectStaMassage()
{
  NS_LOG_INFO("AP CollectStaMassage(): collecting STA {MAC, IP} info");

  Ptr<Node> apNode = GetNode();
  
  // 清空旧数据
  m_staMessages.clear();

  // 遍历 AP 上所有 WiFi 设备
  for (uint32_t i = 0; i < apNode->GetNDevices(); i++)
    {
      Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apNode->GetDevice(i));
      if (!wifiDev)
        continue;

      Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(wifiDev->GetMac());
      if (!apMac)
        continue;

      // 获取 AP 关联的所有 STA（aid -> MAC）
      const std::map<uint16_t, Mac48Address> &staMap = apMac->GetStaList();

      for (auto const &entry : staMap)
        {
          Mac48Address staMac = entry.second;

          // 查找对应 STA 节点
          Ptr<Node> staNode = nullptr;
          for (uint32_t n = 0; n < NodeList::GetNNodes(); n++)
            {
              Ptr<Node> node = NodeList::GetNode(n);
              for (uint32_t j = 0; j < node->GetNDevices(); j++)
                {
                  Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(node->GetDevice(j));
                  if (wdev && wdev->GetMac()->GetAddress() == staMac)
                    {
                      staNode = node;
                      break;
                    }
                }
              if (staNode)
                break;
            }

          if (!staNode)
            {
              std::cout<<"Cannot find STA node for MAC" <<std::endl;
              NS_LOG_WARN("Cannot find STA node for MAC " << staMac);
              continue;
            }

          // 获取 IPv4
          Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
          if (!ipv4)
            continue;

          Ipv4Address ip = ipv4->GetAddress(1, 0).GetLocal();

          // --------------------------
          //    正式构造并存储记录
          // --------------------------
          stamessage sm;
          sm.mac_address = staMac.ConvertToU64();
          sm.ip_address  = ip.Get();    // IPv4 转为 uint32_t

          m_staMessages.push_back(sm);

          NS_LOG_INFO("Collected STA MAC=" << staMac
                       << " IP=" << ip);
        }
    }

  NS_LOG_INFO("AP collected " << m_staMessages.size() << " STA messages.");
}
//--------------------------------------------------
/*获得sta messages 的函数*/
std::vector<stamessage>&
ApProtocolInfoApp::GetStaMessages()
{   
    CollectStaMassage();
    if(m_staMessages.empty()){
      std::cout<<"no no no no no!!!----------m_staMessages 是空的奥"<<std::endl;
    }
    return m_staMessages;
}
//---------------------------------------------------

//获取与ap相连的所有sta节点的信息。
//====================================================
  std::vector<Reply>
ApProtocolInfoApp::CollectStaProtocolReplies()
{
  std::vector<Reply> replies;
  Ptr<Node> apNode = GetNode();

  // 遍历 AP 上所有 WiFi 设备
  for (uint32_t i = 0; i < apNode->GetNDevices(); i++)
    {
      Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apNode->GetDevice(i));
      if (!wifiDev)
        continue;

      Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(wifiDev->GetMac());
      if (!apMac)
        continue;

      // 获取 AP 的所有 STA
      const std::map<uint16_t, Mac48Address> &staMap = apMac->GetStaList();
      for (auto const &entry : staMap)
        {
          uint16_t aid = entry.first;
          Mac48Address staMac = entry.second;

          Ptr<Node> staNode = nullptr;

          // 在 NodeList 中寻找对应 STA 节点
          for (uint32_t n = 0; n < NodeList::GetNNodes(); n++)
            {
              Ptr<Node> node = NodeList::GetNode(n);
              for (uint32_t j = 0; j < node->GetNDevices(); j++)
                {
                  Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(node->GetDevice(j));
                  if (wdev && wdev->GetMac()->GetAddress() == staMac)
                    {
                      staNode = node;
                      break;
                    }
                }
              if (staNode)
                break;
            }

          if (!staNode)
            {
              NS_LOG_WARN("Cannot find STA node for MAC " << staMac);
              continue;
            }

          Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
          if (!ipv4)
            {
              std::cout << "STA node has no IPv4 stack!" << std::endl;
              continue;
            }

          // ******** 获取 STA 的 IPv4 地址 ********
          Ipv4Address staIp = Ipv4Address("0.0.0.0");
          uint32_t staIpRaw = 0;

          // 遍历所有接口，寻找非 127.0.0.1 地址
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
          if (!listRouting)
            {
              std::cout << "STA node does not use Ipv4ListRouting" << std::endl;
              continue;
            }

          uint16_t pAodv = 0, pOlsr = 0, pStatic = 0;

          // 遍历 STA 路由协议优先级
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

          // 构造 Reply
          Reply reply;
          reply.vendor = 0x12345678;
          reply.subtype = 2001;
          reply.p1 = pAodv;
          reply.p2 = pOlsr;
          reply.p3 = pStatic;
          reply.mac_ad = staMac.ConvertToU64();
          reply.ip_ad  = staIpRaw;     

          replies.push_back(reply);

          NS_LOG_INFO("Collected STA (AID=" << aid << ", MAC=" << staMac
                       << ", IP=" << staIp
                       << ") AODV=" << pAodv
                       << ", OLSR=" << pOlsr
                       << ", Static=" << pStatic);
        }
    }

  return replies;
}

//==============================================================
//修改优先级的函数，用于修改指定sta节点的优先级.
//==============================================================
void
ApProtocolInfoApp::UpdateStaRoutingPriority(Mac48Address targetSta, int16_t newAodvPri, 
                                            int16_t newOlsrPri, int16_t newStaticPri)
{
  NS_LOG_FUNCTION(this << targetSta);
  std::cout << "\n[ApProtocolInfoApp] Updating routing priorities for STA: " << targetSta << std::endl;

  // 在 NodeList 中找到目标 STA
  Ptr<Node> staNode = nullptr;
  for (uint32_t n = 0; n < NodeList::GetNNodes(); n++)
    {
      Ptr<Node> node = NodeList::GetNode(n);
      for (uint32_t j = 0; j < node->GetNDevices(); j++)
        {
          Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(node->GetDevice(j));
          if (wdev && wdev->GetMac()->GetAddress() == targetSta)
            {
              staNode = node;
              break;
            }
        }
      if (staNode)
        break;
    }

  if (!staNode)
    {
      std::cout << "[ApProtocolInfoApp] Error: Cannot find STA node for MAC " << targetSta << std::endl;
      return;
    }

  Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
  if (!ipv4)
    {
      std::cout << "[ApProtocolInfoApp] Error: Target STA has no IPv4 stack!" << std::endl;
      return;
    }

  Ptr<Ipv4ListRouting> listRouting = DynamicCast<Ipv4ListRouting>(ipv4->GetRoutingProtocol());
  if (!listRouting)
    {
      std::cout << "[ApProtocolInfoApp] Error: Target STA does not use Ipv4ListRouting!" << std::endl;
      return;
    }

  // 打印修改前的优先级
  std::cout << "[Before] Routing priorities for STA " << targetSta << ":" << std::endl;
  for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
    {
      int16_t pri = 0;
      Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, pri);
      std::cout << "  Protocol: " << proto->GetInstanceTypeId().GetName()
                << ", Priority: " << pri << std::endl;
    }

  // 修改优先级
  listRouting->SetRoutingProtocolPriorityByType(aodv::RoutingProtocol::GetTypeId(), newAodvPri);
  listRouting->SetRoutingProtocolPriorityByType(olsr::RoutingProtocol::GetTypeId(), newOlsrPri);
  listRouting->SetRoutingProtocolPriorityByType(Ipv4StaticRouting::GetTypeId(), newStaticPri);
  
  // 打印修改后的结果
  std::cout << "[After] Routing priorities for STA " << targetSta << ":" << std::endl;
  for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
    {
      int16_t pri = 0;
      Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, pri);
      std::cout << "  Protocol: " << proto->GetInstanceTypeId().GetName()
                << ", Priority: " << pri << std::endl;
    }

  std::cout << "==============================================================================================" << std::endl;
}
//===========================================================================
//------可以一次性统一修改一个域内的sta路由协议优先级(函数重载)-------------------
//===========================================================================
void
ApProtocolInfoApp::UpdateStaRoutingPriority(int16_t newAodvPri, int16_t newOlsrPri, int16_t newStaticPri)
{
  NS_LOG_FUNCTION(this);
  Ptr<Node> apNode = GetNode();
  std::cout << "\n[ApProtocolInfoApp] ====== Updating Routing Priorities for All STAs in Domain ======" << std::endl;

  // 遍历 AP 上所有 WiFi 设备
  for (uint32_t i = 0; i < apNode->GetNDevices(); i++)
    {
      Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(apNode->GetDevice(i));
      if (!wifiDev)
        continue;

      Ptr<ApWifiMac> apMac = DynamicCast<ApWifiMac>(wifiDev->GetMac());
      if (!apMac)
        continue;

      const std::map<uint16_t, Mac48Address> &staMap = apMac->GetStaList();

      for (auto const &entry : staMap)
        {
          Mac48Address staMac = entry.second;

          // 在全局 NodeList 中查找对应 STA 节点
          Ptr<Node> staNode = nullptr;
          for (uint32_t n = 0; n < NodeList::GetNNodes(); n++)
            {
              Ptr<Node> node = NodeList::GetNode(n);
              for (uint32_t j = 0; j < node->GetNDevices(); j++)
                {
                  Ptr<WifiNetDevice> wdev = DynamicCast<WifiNetDevice>(node->GetDevice(j));
                  if (wdev && wdev->GetMac()->GetAddress() == staMac)
                    {
                      staNode = node;
                      break;
                    }
                }
              if (staNode)
                break;
            }

          if (!staNode)
            {
              std::cout << "[ApProtocolInfoApp] Warning: Cannot find STA node for MAC " << staMac << std::endl;
              continue;
            }

          Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
          if (!ipv4)
            {
              std::cout << "[ApProtocolInfoApp] Warning: STA " << staMac << " has no IPv4 stack!" << std::endl;
              continue;
            }

          Ptr<Ipv4ListRouting> listRouting = DynamicCast<Ipv4ListRouting>(ipv4->GetRoutingProtocol());
          if (!listRouting)
            {
              std::cout << "[ApProtocolInfoApp] Warning: STA " << staMac << " not using Ipv4ListRouting!" << std::endl;
              continue;
            }

          std::cout << "\n[Before] STA " << staMac << " Routing Priorities:" << std::endl;
          for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
            {
              int16_t pri = 0;
              Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, pri);
              std::cout << "  " << proto->GetInstanceTypeId().GetName() << " => " << pri << std::endl;
            }

          // 修改优先级
          bool okAodv = listRouting->SetRoutingProtocolPriorityByType(aodv::RoutingProtocol::GetTypeId(), newAodvPri);
          bool okOlsr = listRouting->SetRoutingProtocolPriorityByType(olsr::RoutingProtocol::GetTypeId(), newOlsrPri);
          bool okStatic = listRouting->SetRoutingProtocolPriorityByType(Ipv4StaticRouting::GetTypeId(), newStaticPri);

          std::cout << "\n[After] STA " << staMac << " Updated Priorities:" << std::endl;
          for (uint32_t k = 0; k < listRouting->GetNRoutingProtocols(); k++)
            {
              int16_t pri = 0;
              Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(k, pri);
              std::cout << "  " << proto->GetInstanceTypeId().GetName() << " => " << pri << std::endl;
            }

          std::cout << "  Update result: "
                    << "AODV(" << (okAodv ? "OK" : "NOT FOUND") << "), "
                    << "OLSR(" << (okOlsr ? "OK" : "NOT FOUND") << "), "
                    << "STATIC(" << (okStatic ? "OK" : "NOT FOUND") << ")" << std::endl;
          std::cout << "------------------------------------------------------------" << std::endl;
        }
    }

  std::cout << "[ApProtocolInfoApp] ====== Domain Routing Priorities Updated Successfully ======" << std::endl;
}
//===========================================================================
//--------------------------修改节点的组网模式----------------------------------
//===========================================================================
void
ApProtocolInfoApp::ChangeZuWang(uint32_t op)
{ 
    NS_LOG_FUNCTION(this<<op);

    Ptr<Node> apNode = GetNode();
    std::cout << "\n[ApProtocolInfoApp] ====== Switch STA Network Mode, op="
              << op << " ======" << std::endl;

    // 遍历 AP 上所有 WiFi 设备
    for (uint32_t i = 0; i < apNode->GetNDevices(); i++)
    {
        Ptr<WifiNetDevice> wifiDev =
            DynamicCast<WifiNetDevice>(apNode->GetDevice(i));
        if (!wifiDev)
            continue;

        Ptr<ApWifiMac> apMac =
            DynamicCast<ApWifiMac>(wifiDev->GetMac());
        if (!apMac)
            continue;

        // AP 当前已关联 STA 的 MAC 列表
        const std::map<uint16_t, Mac48Address> &staMap =
            apMac->GetStaList();

        for (auto const &entry : staMap)
        {
            Mac48Address staMac = entry.second;
            Ptr<Node> staNode = nullptr;

            // ---------- 在 NodeList 中查找 STA 节点 ----------
            for (uint32_t n = 0; n < NodeList::GetNNodes(); n++)
            {
                Ptr<Node> node = NodeList::GetNode(n);
                for (uint32_t d = 0; d < node->GetNDevices(); d++)
                {
                    Ptr<WifiNetDevice> wdev =
                        DynamicCast<WifiNetDevice>(node->GetDevice(d));
                    if (wdev && wdev->GetMac()->GetAddress() == staMac)
                    {
                        staNode = node;
                        break;
                    }
                }
                if (staNode)
                    break;
            }

            if (!staNode)
            {
                std::cout << "[ApProtocolInfoApp] Warning: Cannot find STA node for MAC "
                          << staMac << std::endl;
                continue;
            }

            Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
            if (!ipv4)
            {
                std::cout << "[ApProtocolInfoApp] Warning: STA "
                          << staMac << " has no IPv4 stack" << std::endl;
                continue;
            }
          if(op == 1)
          { 
            // ---------- 查找 STA 上的 AdHoc NetDevice ----------
            bool adhocFound = false;

            for (uint32_t d = 0; d < staNode->GetNDevices(); d++)
            {
                Ptr<WifiNetDevice> staWifiDev =
                    DynamicCast<WifiNetDevice>(staNode->GetDevice(d));
                if (!staWifiDev)
                    continue;

                Ptr<WifiMac> mac = staWifiDev->GetMac();

                // AdHoc 接口的典型判定方式
                if (DynamicCast<AdhocWifiMac>(mac))
                {
                    uint32_t ifIndex =
                        ipv4->GetInterfaceForDevice(staWifiDev);

                    if (ifIndex == uint32_t(-1))
                    {
                        std::cout <<  "[ApProtocolInfoApp] Warning: AdHoc device has no IPv4 interface on STA "
                                  << staMac << std::endl;
                        break;
                    }

                    ipv4->SetUp(ifIndex);
                    adhocFound = true;

                    std::cout << "[ApProtocolInfoApp] STA "
                              << staMac
                              << " AdHoc interface UP (ifIndex="
                              << ifIndex << ")" << std::endl;
                    break;
                }
            }

            if (!adhocFound)
            {
                std::cout << "[ApProtocolInfoApp] Warning: No AdHoc interface found on STA "
                          << staMac << std::endl;
            }
          }
            
        }
    }


}
std::vector<Position>
ApProtocolInfoApp::SendNodePosition()
{
   NS_LOG_FUNCTION(this);

    std::vector<Position> posVec;

    Ptr<Node> apNode = GetNode();
    if (!apNode)
    {
        std::cout << "[ApProtocolInfoApp] AP node is null" << std::endl;
        return posVec;
    }

    // 遍历 AP 上所有 WiFi 设备
    for (uint32_t i = 0; i < apNode->GetNDevices(); i++)
    {
        Ptr<WifiNetDevice> wifiDev =
            DynamicCast<WifiNetDevice>(apNode->GetDevice(i));
        if (!wifiDev)
            continue;

        Ptr<ApWifiMac> apMac =
            DynamicCast<ApWifiMac>(wifiDev->GetMac());
        if (!apMac)
            continue;

        // AP 当前已关联 STA 的 MAC 列表
        const std::map<uint16_t, Mac48Address> &staMap =
            apMac->GetStaList();

        for (auto const &entry : staMap)
        {
            Mac48Address staMac = entry.second;
            Ptr<Node> staNode = nullptr;

            // ---------- 在 NodeList 中查找 STA 节点 ----------
            for (uint32_t n = 0; n < NodeList::GetNNodes(); n++)
            {
                Ptr<Node> node = NodeList::GetNode(n);
                for (uint32_t d = 0; d < node->GetNDevices(); d++)
                {
                    Ptr<WifiNetDevice> wdev =
                        DynamicCast<WifiNetDevice>(node->GetDevice(d));
                    if (wdev && wdev->GetMac()->GetAddress() == staMac)
                    {
                        staNode = node;
                        break;
                    }
                }
                if (staNode)
                    break;
            }

            if (!staNode)
            {
                std::cout << "[ApProtocolInfoApp] Warning: Cannot find STA node for MAC "
                          << staMac << std::endl;
                continue;
            }

            // ================== 获取 IP ==================
            Ptr<Ipv4> ipv4 = staNode->GetObject<Ipv4>();
            if (!ipv4)
            {
                std::cout << "[ApProtocolInfoApp] Warning: STA "
                          << staMac << " has no IPv4 stack" << std::endl;
                continue;
            }

            // 默认使用接口 1（0 是 loopback）
            if (ipv4->GetNInterfaces() <= 1)
            {
                std::cout << "[ApProtocolInfoApp] Warning: STA "
                          << staMac << " has no valid IPv4 interface" << std::endl;
                continue;
            }

            Ipv4Address ip = ipv4->GetAddress(1, 0).GetLocal();

            // ================== 获取位置 ==================
            Ptr<MobilityModel> mobility =
                staNode->GetObject<MobilityModel>();

            if (!mobility)
            {
                std::cout << "[ApProtocolInfoApp] Warning: STA "
                          << staMac << " has no MobilityModel" << std::endl;
                continue;
            }

            Vector pos = mobility->GetPosition();

            // ================== 封装 Position ==================
            Position p;
            memset(&p, 0, sizeof(Position));

            p.ip_ad = ip.Get();   // network byte order
            p.x = static_cast<float>(pos.x);
            p.y = static_cast<float>(pos.y);
            p.z = static_cast<float>(pos.z);

            posVec.push_back(p);

            std::cout << "[ApProtocolInfoApp] STA "
                      << staMac
                      << " IP=" << ip
                      << " Pos=(" << p.x << ", "
                      << p.y << ", "
                      << p.z << ")"
                      << std::endl;
        }
    }

    return posVec;
}

} // namespace ns3
