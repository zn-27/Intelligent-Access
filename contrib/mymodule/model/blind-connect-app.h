#ifndef BLIND_CONNECT_APP_H
#define BLIND_CONNECT_APP_H

#include "ns3/application.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-mac.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/socket.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4.h"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"
#include "ns3/ssid.h"
#include "crypto-utils.h"
#include "intelligent-access-algorithm.h"
#include "ns3/yans-wifi-channel.h"
#include <map>
#include <list>
#include <vector>
#include <functional>
#include <string>

namespace ns3 {

class BlindConnectApp : public Application
{
public:
  enum AppRole { ROLE_TERMINAL, ROLE_GATEWAY, ROLE_BACKBONE, ROLE_AP_SERVER };

  // 数据面模式：控制哪张网卡承载业务数据
  enum DataPlaneMode { DATA_PLANE_AP, DATA_PLANE_ADHOC };

  // 接口软状态：控制面常活，数据面可激活/静默
  struct InterfaceSoftState {
    Ipv4Address ip;
    Ipv4Address gw;
    Ipv4Mask mask;
    bool controlActive;  // 控制面常活（始终true）
    bool dataActive;     // 数据面活跃/静默

    InterfaceSoftState() : ip("0.0.0.0"), gw("0.0.0.0"), mask("255.255.255.0"),
                          controlActive(true), dataActive(false) {}
  };

  struct ScannedNodeInfo {
    enum NodeType { TYPE_AP, TYPE_ADHOC };
    NodeType type;
    Ssid ssid;
    double snr;            // dBm
    double noise;          // dBm
    uint32_t hopsToGw;
    double load;           // 0..1
    double minEnergy;      // 0..1
    uint32_t nodes;        // 域内节点数
    bool secure;
    uint32_t securityCapabilities;
    Ipv4Address gateway;
    uint16_t channelFreqMhz;
    Time observedAt{Seconds(0)};
  };

  /**
   * A domain describes capabilities, not a permanently selected operating
   * mode.  The SDN control plane may enable infrastructure, Ad-Hoc, or both
   * without changing the physical topology.
   */
  struct DomainProfile {
    uint32_t domainId;
    Ssid infrastructureSsid;
    Ssid adhocSsid;
    Ipv4Address infrastructureGateway;
    Ipv4Address adhocGateway;
    uint16_t infrastructureChannelFreqMhz;
    uint16_t adhocChannelFreqMhz;
    bool infrastructureAccessEnabled;

    DomainProfile()
      : domainId(0),
        infrastructureGateway("0.0.0.0"),
        adhocGateway("0.0.0.0"),
        infrastructureChannelFreqMhz(0),
        adhocChannelFreqMhz(0),
        infrastructureAccessEnabled(true) {}
  };

  static TypeId GetTypeId();

  BlindConnectApp();
  virtual ~BlindConnectApp() {}

  // SDN 路由回调：IP 分配成功后通知控制器注入 ARP
  typedef std::function<void(Mac48Address, Ipv4Address)> IpAllocatedCallback;
  void SetIpAllocatedCallback(IpAllocatedCallback cb);

  // 报文时间撮日志（仿真结束汇总打印）
  static void LogMessage(const std::string& type, double timestamp, uint32_t domainId = 0, const std::string& device = "");
  static void LogMessageOnce(const std::string& type, double timestamp, const std::string& dedupKey, uint32_t domainId = 0, const std::string& device = "");
  static void PrintMessageLog();
  static void LogExperimentEvent(const std::string& event, uint32_t domainId, const std::string& iface,
                                 uint32_t txid = 0, Ipv4Address ip = Ipv4Address("0.0.0.0"),
                                 const std::string& mac = "", const std::string& step = "",
                                 const std::string& ssid = "", double snr = 0.0,
                                 int hops = -1, const std::string& note = "");
  static void ExportExperimentEvents(const std::string& path);

  void SetStaDevice(Ptr<WifiNetDevice> dev);
  void SetAdhocDevice(Ptr<WifiNetDevice> dev);
  void SetRole(AppRole role);
  // 分离 socket 绑定设备：Switch 节点 IP 在 VND 上，但信标监控需要原生设备
  void SetSocketStaDevice(Ptr<NetDevice> dev);    // AP_SERVER socket 绑定到此设备（VND）
  void SetSocketAdhocDevice(Ptr<NetDevice> dev);  // GATEWAY socket 绑定到此设备（VND）

  // Adhoc 域 SSID（GATEWAY 伪信标携带，TERMINAL 切换使用）
  Ssid GetAdhocSsid() const { return m_adhocSsid; }
  void SetAdhocSsid(Ssid ssid) { m_adhocSsid = ssid; }

  // 注册 Adhoc 域信道映射（SSID → YansWifiChannel），供 ExecuteSwitch 切换信道对象
  void RegisterAdhocChannel(const std::string& ssid, Ptr<YansWifiChannel> channel);
  void SetAdhocDiscoverySsid(Ssid ssid);
  void RegisterAdhocBeaconSource(Ptr<WifiNetDevice> source, uint16_t frequencyMhz);
  void RegisterDomainProfile(const DomainProfile& profile);
  void SetAccessAlgorithm(Ptr<IntelligentAccessAlgorithm> algorithm);

  // 状态查询（供外部日志回调使用）
  Ipv4Address GetAssignedIp() const { return m_assignedIp; }
  int GetCurrentNetType() const { return (int)m_currentNetType; }
  double GetCurrentSnr() const { return m_currentSnr; }

protected:
  virtual void StartApplication() override;
  virtual void StopApplication() override;

private:
  // --- 无线感知 ---
  void ReceiveStaBeacon(Ptr<const Packet> packet, uint16_t channelFreqMhz,
                        WifiTxVector txVector, MpduInfo aMpdu,
                        SignalNoiseDbm snr, uint16_t staId);
  void ReceiveAdhocBeacon(Ptr<const Packet> packet, uint16_t channelFreqMhz,
                          WifiTxVector txVector, MpduInfo aMpdu,
                          SignalNoiseDbm snr, uint16_t staId);
	  void ReceiveApSniffer(Ptr<const Packet> packet, uint16_t channelFreqMhz,
	                        WifiTxVector txVector, MpduInfo aMpdu,
	                        SignalNoiseDbm snr, uint16_t staId);

  void SwitchToNextChannel();
  void EvaluateAndSwitch();
  void ScheduleEvaluate();
  void ExecuteSwitch(const ScannedNodeInfo& bestNode);
  bool ConfigureStaRadio(Ssid ssid, uint16_t frequencyMhz);
  bool ConfigureAdhocRadio(Ssid ssid, uint16_t frequencyMhz);
  bool ConfigureRadiosForDomain(uint32_t domainId);
  void ScheduleAdhocDiscoveryScan();
  void DoAdhocDiscoveryScan();
  void RestoreAdhocDomainChannel();
  void ScheduleApRescan();           // AP域驻留时周期性重扫其他信道
  void DoApRescanChannel();          // 执行一次重扫信道切换
  void DoApRescanRestore();          // 重扫完成后恢复AP信道
  double CalculateScore(const ScannedNodeInfo& node);
  IntelligentAccessAlgorithm::NetworkKey MakeNetworkKey(const ScannedNodeInfo& node) const;
  ScannedNodeInfo FromAccessCandidate(const IntelligentAccessAlgorithm::Candidate& candidate) const;

  void SendPseudoBeacon();
  void SendApKeyAdvertisement();
  void PurgeNeighborTable();
  void SetDefaultRouteVia(Ptr<Ipv4> ipv4, uint32_t iface, Ipv4Address gw);

  // --- IP 分配 (Gateway / AP Server) ---
  Ipv4Address AllocateIp(const Mac48Address& mac);
  void ReleaseIp(const Mac48Address& mac);
  void InitIpPool(Ipv4Address netAddr, Ipv4Address start, Ipv4Address end, Ipv4Mask mask);

  // --- IP 分配 (Terminal) ---
  void RequestAdhocIp();
  void RequestStaIp();
  void RetryAdhocIp();             // Adhoc IP请求重试
  void RetryStaIp();               // STA IP请求重试
  void FallbackAndRescan();        // IP分配失败后重置状态并重新扫描
  void HandleAdhocIpMessage(const std::string& payload);
  void HandleStaIpRead(Ptr<Socket> socket);
  void HandleApServerRead(Ptr<Socket> socket);
  void ConfigureIpOnInterface(Ptr<NetDevice> dev, Ipv4Address ip, Ipv4Mask mask, Ipv4Address gw);
  void ConfigureAdhocIpByDomain(uint32_t domainId);  // 仅保留兼容入口，不参与动态实验路径
  void ConfigureStaIpByDomain(uint32_t domainId);    // 仅保留兼容入口，不参与动态实验路径
  void ConfigureDualIpForDomain(uint32_t domainId); // 同时配置当前域的双网卡IP
  void SetDataPlaneActive(DataPlaneMode mode);      // 设置数据面活跃网卡
  void EnsureBothInterfacesUp();                    // 确保两张接口都UP
  void SetDefaultRoute(Ptr<NetDevice> dev, Ipv4Address gw);
  void BindDataSocketsToActiveDevice(Ptr<NetDevice> dev);
  void BeginHandover();
  void CommitHandover(DataPlaneMode mode);
  void AbortHandover(const std::string& reason);
  void FinishOldPathGrace();
  uint32_t GetDomainIdFromBestNode(const ScannedNodeInfo& node) const;

  // 延迟发送辅助（确保时间戳顺序：REQUEST < OFFER < CONFIRM）
  void SendDelayedStaIpOffer(std::string resp, Mac48Address dstMac);
  void SendDelayedIpConfirm(std::string resp, Ipv4Address ip, uint32_t domId);
  uint32_t NextTxId();
  bool IsExpectedStaOffer(const std::string& payload) const;
  bool IsExpectedAdhocOffer(const std::string& payload) const;
  bool PrepareSecureApOffer(const std::string& payload, Mac48Address& mac,
                            Ipv4Address& ip, std::string& response);
  bool VerifyStaOfferSecurity(const std::string& payload) const;

  // --- 加密操作 ---
  void InitCrypto();                                           // 初始化加密子系统
  void GenerateChebyshevKeypair();                             // 生成 Chebyshev 密钥对
  bool ComputeSharedSecretFromGateway(const std::string& modulusHex, const std::string& pubKeyHex);  // 从网关公钥计算共享密钥
  std::string SignMessage(const std::string& msgBody);         // 对消息体计算 HMAC 签名
  bool VerifyMessage(const std::string& msgBody, const std::string& hmacHex);  // 验证 HMAC 签名
  bool DeriveStaCryptoContext(const std::string& ssid,
                              const std::string& modulusHex,
                              const std::string& apPublicKeyHex);
  bool ActivateStaCryptoContext(const std::string& ssid);
  bool DerivePeerSharedSecret(const std::string& peerPublicKeyHex,
                              std::vector<unsigned char>& secret) const;
  std::string SignMessageWithSecret(
      const std::string& msgBody,
      const std::vector<unsigned char>& secret) const;
  bool VerifyPayloadWithSecret(
      const std::string& payload,
      const std::vector<unsigned char>& secret) const;
  std::string GetPublicKeyHex() const;                         // 获取本节点公钥的 hex 串
  std::string GetModulusHex() const;                           // 获取模数的 hex 串
  std::string ExtractField(const std::string& payload, const std::string& key) const;  // 从 payload 提取字段值

  // --- 基本属性 ---
  AppRole m_role;
  ScannedNodeInfo::NodeType m_currentNetType;
  Ssid m_currentSsid;              // 当前AP的SSID (用于评估时精确匹配)
  Ssid m_adhocSsid;                // GATEWAY伪信标携带的Adhoc域SSID
  Ssid m_adhocDiscoverySsid;       // AP驻留期间由Ad-Hoc网卡周期扫描的网络
  std::map<std::string, Ptr<YansWifiChannel>> m_adhocChannels;  // SSID → Adhoc域信道对象
  std::map<Mac48Address, std::pair<Ptr<WifiNetDevice>, uint16_t>> m_adhocBeaconSources;
  std::map<uint32_t, DomainProfile> m_domainProfiles;
  Ptr<IntelligentAccessAlgorithm> m_accessAlgorithm;
  double m_currentSnr;
  int m_currentHops;
  bool m_useLegacyUdpBeacon;

  Ptr<WifiNetDevice> m_staDevice;
  Ptr<WifiNetDevice> m_adhocDevice;
  Ptr<NetDevice> m_socketStaDevice;    // AP_SERVER socket 绑定设备 (通常为 VND)
  Ptr<NetDevice> m_socketAdhocDevice;  // GATEWAY socket 绑定设备 (通常为 VND)

  Ptr<Socket> m_broadcastSocket;
  EventId m_beaconEvent;
  EventId m_apKeyAdvEvent;       // AP Chebyshev 公钥材料广播
  EventId m_hopEvent;
  EventId m_evalEvent;
  EventId m_rescanEvent;          // AP域周期性信道重扫
  EventId m_adhocScanEvent;       // Ad-Hoc网卡发现扫描
  EventId m_adhocRestoreEvent;    // 扫描后恢复当前域
  EventId m_ipRetryEvent;         // STA IP请求重试定时器
  EventId m_ipReqEvent;           // STA首次IP请求的延迟定时器
  EventId m_adhocIpRetryEvent;    // Adhoc IP请求重试定时器
  EventId m_adhocIpReqEvent;      // Adhoc首次IP请求的延迟定时器
  int m_ipRetryCount;             // STA IP请求重试次数
  int m_adhocIpRetryCount;        // Adhoc IP请求重试次数
  uint32_t m_nextTxId;
  uint32_t m_pendingStaTxId;
  uint32_t m_pendingAdhocTxId;
  uint32_t m_pendingStaDomainId;
  uint32_t m_pendingAdhocDomainId;
  Time m_dwellTime;
  Time m_lastSwitchTime;           // 上次切换时间，防止频繁切换

  std::vector<uint8_t> m_channels;
  uint32_t m_currentChIdx;
  bool m_inApRescan;               // 是否正在进行AP域信道重扫
  bool m_inAdhocDiscoveryScan;     // Ad-Hoc网卡是否处于发现驻留
  uint32_t m_rescanChCount;        // 重扫已切换的信道计数
  uint16_t m_apChannelFreqMhz;     // 重扫前AP信道频率（用于恢复）
  uint8_t m_apChannelNum;          // 重扫前AP信道号
  uint32_t m_initWaitApRounds;     // 首次入网等待AP的轮数计数

  std::vector<ScannedNodeInfo> m_neighborRadar;

  struct NeighborEntry {
    Time lastSeen;
    uint32_t hopsToGw;
    double load;
    double energy;
  };
  std::map<Ipv4Address, NeighborEntry> m_neighborTable;
  uint32_t m_localHops;

  Ptr<UniformRandomVariable> m_jitterVar;

  // --- IP 池 (Gateway / AP Server) ---
  std::list<Ipv4Address> m_availableAddresses;
  std::map<Mac48Address, Ipv4Address> m_macToIp;
  Ipv4Address m_poolBase;
  Ipv4Address m_poolStart;
  Ipv4Address m_poolEnd;
  Ipv4Mask m_poolMask;

  // --- Terminal IP 请求 ---
  Ptr<Socket> m_staIpSocket;
  Ptr<Socket> m_adhocIpSocket;
  Ipv4Address m_assignedIp;
  Ipv4Mask m_assignedMask;
  Ipv4Address m_assignedGw;

  // --- 软休眠状态管理 ---
  DataPlaneMode m_dataPlaneMode;
  InterfaceSoftState m_staState;      // STA网卡状态
  InterfaceSoftState m_adhocState;    // Ad-Hoc网卡状态
  uint32_t m_currentDomainId;         // 当前所在域 (1=A, 2=B, 3=C)

  struct ActiveContextSnapshot {
    uint32_t domainId{0};
    ScannedNodeInfo::NodeType networkType{ScannedNodeInfo::TYPE_AP};
    Ssid ssid;
    int hops{99};
    double signalDbm{-100.0};
    Ipv4Address ip{"0.0.0.0"};
    Ipv4Mask mask{"255.255.255.0"};
    Ipv4Address gateway{"0.0.0.0"};
    DataPlaneMode dataPlane{DATA_PLANE_ADHOC};
  };
  bool m_handoverInProgress;
  ActiveContextSnapshot m_previousContext;
  EventId m_oldPathGraceEvent;
  Time m_oldPathGracePeriod;

  // --- AP Server ---
  Ptr<Socket> m_apServerSocket;
  std::map<Mac48Address, std::vector<unsigned char>> m_apPeerSecrets;

  // --- 加密相关 ---
  struct StaCryptoContext {
    std::string apPublicKeyHex;
    std::string terminalPublicKeyHex;
    std::vector<unsigned char> sharedSecret;
  };
  std::map<std::string, StaCryptoContext> m_staCryptoContexts;
  bool m_cryptoEnabled;                                        // 是否启用加密
  ExtendedChebyshevKeyExchange* m_keyExchange;                 // Chebyshev 密钥交换实例
  unsigned char* m_privateKeyBytes;                            // 本节点私钥
  size_t m_privateKeyLen;
  unsigned char* m_publicKeyBytes;                             // 本节点公钥
  size_t m_publicKeyLen;
  unsigned char* m_sharedSecret;                               // 与对端的共享密钥 (16字节，用于AES-128)
  size_t m_sharedSecretLen;
  std::string m_peerPubKeyHex;                                 // 对端公钥(hex)，缓存
  std::string m_modulusHex;                                    // 模数(hex)，缓存

  // --- SDN 回调 ---
  IpAllocatedCallback m_ipAllocatedCallback;                   // IP 分配成功时通知控制器

  // --- 报文时间撮日志 (static, 跨所有实例共享) ---
  struct LogEntry { double ts; uint32_t domain; std::string device; };
  static std::map<std::string, std::vector<LogEntry>> s_messageLog;
  struct ExperimentEvent {
    double ts;
    uint32_t domain;
    std::string iface;
    std::string event;
    std::string step;
    uint32_t txid;
    std::string ip;
    std::string mac;
    std::string ssid;
    double snr;
    int hops;
    std::string note;
  };
  static std::vector<ExperimentEvent> s_experimentEvents;

  // --- 内部辅助：获取 socket 绑定用的设备（优先 VND，退化原生）---
  Ptr<NetDevice> GetSocketStaDev() const {
    return (m_socketStaDevice) ? m_socketStaDevice : Ptr<NetDevice>(m_staDevice);
  }
  Ptr<NetDevice> GetSocketAdhocDev() const {
    return (m_socketAdhocDevice) ? m_socketAdhocDevice : Ptr<NetDevice>(m_adhocDevice);
  }
};

} // namespace ns3
#endif
