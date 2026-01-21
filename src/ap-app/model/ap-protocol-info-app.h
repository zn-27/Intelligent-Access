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

private:
  //存储收集的sta信息
  std::vector<stamessage> m_staMessages;

protected:
  virtual void StartApplication() override {}
  virtual void StopApplication() override {}
};

} // namespace ns3

#endif
