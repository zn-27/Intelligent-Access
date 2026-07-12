#include "blind-connect-app.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/mgt-headers.h"
#include "ns3/ipv4-header.h"
#include "ns3/udp-header.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-phy.h"
#include "ns3/yans-wifi-phy.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <set>
#include <iomanip>
#include <iostream>
#include <fstream>
//
namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BlindConnectApp");
NS_OBJECT_ENSURE_REGISTERED(BlindConnectApp);

// 静态报文时间撮日志
std::map<std::string, std::vector<BlindConnectApp::LogEntry>> BlindConnectApp::s_messageLog;
std::vector<BlindConnectApp::ExperimentEvent> BlindConnectApp::s_experimentEvents;

void BlindConnectApp::LogMessage(const std::string& type, double timestamp, uint32_t domainId, const std::string& device) {
    s_messageLog[type].push_back({timestamp, domainId, device});
}

void BlindConnectApp::LogMessageOnce(const std::string& type, double timestamp, const std::string& dedupKey, uint32_t domainId, const std::string& device) {
    static std::map<std::string, std::set<std::string>> seen;
    if (seen[type].find(dedupKey) == seen[type].end()) {
        seen[type].insert(dedupKey);
        s_messageLog[type].push_back({timestamp, domainId, device});
    }
}

void BlindConnectApp::LogExperimentEvent(const std::string& event, uint32_t domainId, const std::string& iface,
                                         uint32_t txid, Ipv4Address ip, const std::string& mac,
                                         const std::string& step, const std::string& ssid,
                                         double snr, int hops, const std::string& note) {
    static std::set<std::string> seenFirstSeen;
    if (event == "FIRST_SEEN") {
        std::ostringstream firstSeenKey;
        firstSeenKey << domainId << "|" << iface << "|" << ssid;
        if (seenFirstSeen.find(firstSeenKey.str()) != seenFirstSeen.end()) {
            return;
        }
        seenFirstSeen.insert(firstSeenKey.str());
    }

    std::ostringstream ipText;
    ipText << ip;
    s_experimentEvents.push_back({Simulator::Now().GetSeconds(), domainId, iface, event, step,
                                  txid, ipText.str(), mac, ssid, snr, hops, note});
}

void BlindConnectApp::ExportExperimentEvents(const std::string& path) {
    std::ofstream out(path);
    out << "time,domain,iface,event,step,txid,ip,mac,ssid,snr,hops,note\n";
    for (const auto& e : s_experimentEvents) {
        out << std::fixed << std::setprecision(6) << e.ts << ","
            << e.domain << "," << e.iface << "," << e.event << ","
            << e.step << "," << e.txid << "," << e.ip << "," << e.mac << ","
            << e.ssid << "," << e.snr << "," << e.hops << "," << e.note << "\n";
    }
}

void BlindConnectApp::PrintMessageLog() {
    auto tsDom = [](const char* type, uint32_t dom, const std::string& dev) {
        auto it = s_messageLog.find(type);
        if (it == s_messageLog.end()) { std::cout << "-"; return; }
        bool first = true;
        for (const auto& e : it->second) {
            if (e.domain != dom) continue;
            if (!dev.empty() && e.device != dev) continue;
            if (!first) std::cout << "  ";
            first = false;
            std::cout << std::fixed << std::setprecision(3) << e.ts << "s";
        }
        if (first) std::cout << "-";
    };

    std::cout << "\n========== 报文时间撮汇总 ==========\n" << std::endl;

    // 第1次: 域A 有中心入网 (AP主导) — 只保留STA路径
    std::cout << "--- 第1次入网: 域A 有中心 (AP主导) ---" << std::endl;
    std::cout << "  AP Beacon   : "; tsDom("AP Beacon", 1, ""); std::cout << std::endl;
    std::cout << "  IP_REQUEST  : "; tsDom("IP_REQUEST", 1, "STA"); std::cout << std::endl;
    std::cout << "    (终端发起 IP 地址请求)" << std::endl;
    std::cout << "  IP_OFFER    : "; tsDom("IP_OFFER", 1, "STA"); std::cout << std::endl;
    std::cout << "    (AP_SERVER 回复 AP 网段 IP)" << std::endl;
    std::cout << "  AddArpEntry : "; tsDom("AddArpEntry", 1, "STA"); std::cout << std::endl;
    std::cout << "    (控制器下发 ARP 表项)" << std::endl;

    // 第2次: 域C 自组织入网 (Ad-Hoc主导) — 只保留ADHOC路径
    std::cout << "\n--- 第2次入网: 域C 自组织 (Ad-Hoc主导) ---" << std::endl;
    std::cout << "  IBSS_BEACON : "; tsDom("IBSS_BEACON", 3, ""); std::cout << std::endl;
    std::cout << "    (携带 HOPS/LOAD/ENERGY，网关下发伪信标)" << std::endl;
    std::cout << "  IP_REQUEST  : "; tsDom("IP_REQUEST", 3, "ADHOC"); std::cout << std::endl;
    std::cout << "    (终端发起 IP 地址请求)" << std::endl;
    std::cout << "  IP_OFFER    : "; tsDom("IP_OFFER", 3, "ADHOC"); std::cout << std::endl;
    std::cout << "    (网关回复 Ad-Hoc 网段 IP)" << std::endl;
    std::cout << "  IP_CONFIRM  : "; tsDom("IP_CONFIRM", 3, "ADHOC"); std::cout << std::endl;
    std::cout << "    (终端发送 IP 分配确认)" << std::endl;
    std::cout << "  AddArpEntry : "; tsDom("AddArpEntry", 3, "ADHOC"); std::cout << std::endl;
    std::cout << "    (向控制器下发 ARP 表项)" << std::endl;

    // 第3次: 域B 有中心入网 (AP主导) — 只保留STA路径
    std::cout << "\n--- 第3次入网: 域B 有中心 (AP主导) ---" << std::endl;
    std::cout << "  AP Beacon   : "; tsDom("AP Beacon", 2, ""); std::cout << std::endl;
    std::cout << "  IP_REQUEST  : "; tsDom("IP_REQUEST", 2, "STA"); std::cout << std::endl;
    std::cout << "    (终端发起 IP 地址请求)" << std::endl;
    std::cout << "  IP_OFFER    : "; tsDom("IP_OFFER", 2, "STA"); std::cout << std::endl;
    std::cout << "    (AP_SERVER 回复 AP 网段 IP)" << std::endl;
    std::cout << "  AddArpEntry : "; tsDom("AddArpEntry", 2, "STA"); std::cout << std::endl;
    std::cout << "    (控制器下发 ARP 表项)" << std::endl;

    std::cout << std::endl;
}

TypeId BlindConnectApp::GetTypeId() {
    static TypeId tid = TypeId("ns3::BlindConnectApp")
        .SetParent<Application>()
        .AddConstructor<BlindConnectApp>()
        .AddAttribute("DwellTime", "Time spent on each channel",
                      TimeValue(MilliSeconds(200)),
                      MakeTimeAccessor(&BlindConnectApp::m_dwellTime),
                      MakeTimeChecker())
        .AddAttribute("PoolBase", "IP pool network address",
                      Ipv4AddressValue(),
                      MakeIpv4AddressAccessor(&BlindConnectApp::m_poolBase),
                      MakeIpv4AddressChecker())
        .AddAttribute("PoolStart", "First assignable IP in pool",
                      Ipv4AddressValue(),
                      MakeIpv4AddressAccessor(&BlindConnectApp::m_poolStart),
                      MakeIpv4AddressChecker())
        .AddAttribute("PoolEnd", "Last assignable IP in pool",
                      Ipv4AddressValue(),
                      MakeIpv4AddressAccessor(&BlindConnectApp::m_poolEnd),
                      MakeIpv4AddressChecker())
        .AddAttribute("PoolMask", "IP pool subnet mask",
                      Ipv4MaskValue(),
                      MakeIpv4MaskAccessor(&BlindConnectApp::m_poolMask),
                      MakeIpv4MaskChecker());
    return tid;
}

BlindConnectApp::BlindConnectApp()
    : m_role(ROLE_TERMINAL),
      m_currentNetType(ScannedNodeInfo::TYPE_AP),
      m_adhocSsid(Ssid("Adhoc-Net")),
      m_currentSnr(-100.0),
      m_currentHops(99),
      m_socketStaDevice(nullptr),
      m_socketAdhocDevice(nullptr),
      m_ipRetryCount(0),
      m_adhocIpRetryCount(0),
      m_nextTxId(1),
      m_pendingStaTxId(0),
      m_pendingAdhocTxId(0),
      m_pendingStaDomainId(0),
      m_pendingAdhocDomainId(0),
      m_currentChIdx(0),
      m_initWaitApRounds(0),
      m_localHops(99),
      m_poolBase("0.0.0.0"),
      m_poolStart("0.0.0.0"),
      m_poolEnd("0.0.0.0"),
      m_poolMask("255.255.255.0"),
      m_assignedIp("0.0.0.0"),
      m_assignedMask("255.255.255.0"),
      m_assignedGw("0.0.0.0"),
      m_dataPlaneMode(DATA_PLANE_ADHOC),
      m_currentDomainId(0),
      m_cryptoEnabled(false),
      m_keyExchange(nullptr),
      m_privateKeyBytes(nullptr),
      m_privateKeyLen(0),
      m_publicKeyBytes(nullptr),
      m_publicKeyLen(0),
      m_sharedSecret(nullptr),
      m_sharedSecretLen(0)
{
    m_channels = {1, 6, 11};
    m_jitterVar = CreateObject<UniformRandomVariable>();
    m_jitterVar->SetAttribute("Min", DoubleValue(0.0));
    m_jitterVar->SetAttribute("Max", DoubleValue(0.1));
}

void BlindConnectApp::SetStaDevice(Ptr<WifiNetDevice> dev) { m_staDevice = dev; }
void BlindConnectApp::SetAdhocDevice(Ptr<WifiNetDevice> dev) { m_adhocDevice = dev; }
void BlindConnectApp::SetRole(AppRole role) { m_role = role; }
void BlindConnectApp::SetSocketStaDevice(Ptr<NetDevice> dev) { m_socketStaDevice = dev; }
void BlindConnectApp::SetSocketAdhocDevice(Ptr<NetDevice> dev) { m_socketAdhocDevice = dev; }
void BlindConnectApp::SetIpAllocatedCallback(IpAllocatedCallback cb) { m_ipAllocatedCallback = cb; }

void BlindConnectApp::StartApplication() {
    // --- ROLE_AP_SERVER: Raw Socket 监听 WiFi 设备（绕过 OFSwitch13 拦截）---
    if (m_role == ROLE_AP_SERVER && m_staDevice) {
        m_apServerSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_apServerSocket->SetAllowBroadcast(true);
        m_apServerSocket->BindToNetDevice(m_staDevice);   // 绑定原生 AP 设备
        m_apServerSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 67));
        m_apServerSocket->SetRecvPktInfo(true);
        m_apServerSocket->SetRecvCallback(MakeCallback(&BlindConnectApp::HandleApServerRead, this));
        // PHY层监听IP_REQUEST（绕过OFSwitch13数据面拦截）
        m_staDevice->GetPhy()->TraceConnectWithoutContext(
            "MonitorSnifferRx", MakeCallback(&BlindConnectApp::ReceiveApSniffer, this));
        if (m_poolStart != Ipv4Address("0.0.0.0")) {
            InitIpPool(m_poolBase, m_poolStart, m_poolEnd, m_poolMask);
        }
    }

    // --- ROLE_GATEWAY: 初始化 IP 池 + 加密 ---
    if (m_role == ROLE_GATEWAY && m_poolStart != Ipv4Address("0.0.0.0")) {
        InitIpPool(m_poolBase, m_poolStart, m_poolEnd, m_poolMask);
    }
    if (m_role == ROLE_GATEWAY || m_role == ROLE_BACKBONE) {
        InitCrypto();
        GenerateChebyshevKeypair();
    }
    if (m_role == ROLE_TERMINAL) {
        InitCrypto();
    }

    // --- ROLE_TERMINAL: STA 信标监听 + 跳频（STA 接口已在脚本中设为 0.0.0.0）---
    if (m_role == ROLE_TERMINAL && m_staDevice) {
        m_staDevice->GetPhy()->TraceConnectWithoutContext(
            "MonitorSnifferRx", MakeCallback(&BlindConnectApp::ReceiveStaBeacon, this));
        SwitchToNextChannel();
        m_evalEvent = Simulator::Schedule(Seconds(3.0), &BlindConnectApp::EvaluateAndSwitch, this);
    }

    // --- Adhoc 监听 (Gateway / Backbone) ---
    if (m_adhocDevice) {
        m_adhocDevice->GetPhy()->TraceConnectWithoutContext(
            "MonitorSnifferRx", MakeCallback(&BlindConnectApp::ReceiveAdhocBeacon, this));
    }

    // --- AP 网卡也监听 Adhoc 消息 (Gateway / Backbone) ---
    // 终端 Adhoc 设备与 GATEWAY Adhoc 设备可能在不同信道上，
    // 但终端 Adhoc 设备通常与 GATEWAY 的 AP 设备同信道 (ch11)，
    // 因此在 GATEWAY AP 网卡上也监听 IP_REQUEST
    if ((m_role == ROLE_GATEWAY || m_role == ROLE_BACKBONE) && m_staDevice) {
        m_staDevice->GetPhy()->TraceConnectWithoutContext(
            "MonitorSnifferRx", MakeCallback(&BlindConnectApp::ReceiveAdhocBeacon, this));
    }

    // --- 伪信标发送 (Gateway / Backbone) ---
    // GATEWAY: 用 Adhoc 设备发送伪信标 (与真实IBSS信标同信道，移动节点可接收)
    // BACKBONE: 优先用 STA 设备发送
    if (m_role == ROLE_GATEWAY || m_role == ROLE_BACKBONE) {
        Ptr<NetDevice> beaconDev;
        if (m_role == ROLE_GATEWAY) {
            beaconDev = GetSocketAdhocDev();
        } else {
            beaconDev = GetSocketStaDev();
            if (!beaconDev || !m_staDevice) {
                beaconDev = GetSocketAdhocDev();
            }
        }
        if (beaconDev) {
            m_broadcastSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
            m_broadcastSocket->SetAllowBroadcast(true);
            m_broadcastSocket->BindToNetDevice(beaconDev);
            m_localHops = (m_role == ROLE_GATEWAY) ? 0 : 99;
            SendPseudoBeacon();
        }
    }
}

void BlindConnectApp::StopApplication() {
    Simulator::Cancel(m_hopEvent);
    Simulator::Cancel(m_beaconEvent);
    Simulator::Cancel(m_evalEvent);
    Simulator::Cancel(m_rescanEvent);
    Simulator::Cancel(m_ipRetryEvent);
    Simulator::Cancel(m_ipReqEvent);
    if (m_broadcastSocket) {
        m_broadcastSocket->Close();
        m_broadcastSocket = nullptr;
    }
    if (m_staIpSocket) {
        m_staIpSocket->Close();
        m_staIpSocket = nullptr;
    }
    if (m_apServerSocket) {
        m_apServerSocket->Close();
        m_apServerSocket = nullptr;
    }
    if (m_adhocIpSocket) {
        m_adhocIpSocket->Close();
        m_adhocIpSocket = nullptr;
    }
    // 清理加密资源
    if (m_privateKeyBytes) { delete[] m_privateKeyBytes; m_privateKeyBytes = nullptr; }
    if (m_publicKeyBytes)  { delete[] m_publicKeyBytes;  m_publicKeyBytes = nullptr; }
    if (m_sharedSecret)    { delete[] m_sharedSecret;    m_sharedSecret = nullptr; }
    if (m_keyExchange)     { delete m_keyExchange;       m_keyExchange = nullptr; }
    m_cryptoEnabled = false;
}

void BlindConnectApp::ScheduleEvaluate() {
    m_evalEvent = Simulator::Schedule(Seconds(1.0), &BlindConnectApp::EvaluateAndSwitch, this);
}


uint32_t BlindConnectApp::NextTxId() {
    return m_nextTxId++;
}

bool BlindConnectApp::IsExpectedStaOffer(const std::string& payload) const {
    std::string tx = ExtractField(payload, "TXID");
    if (tx.empty()) return true;
    return m_pendingStaTxId != 0 && std::stoul(tx) == m_pendingStaTxId &&
           m_pendingStaDomainId == m_currentDomainId;
}

bool BlindConnectApp::IsExpectedAdhocOffer(const std::string& payload) const {
    std::string tx = ExtractField(payload, "TXID");
    if (tx.empty()) return true;
    return m_pendingAdhocTxId != 0 && std::stoul(tx) == m_pendingAdhocTxId &&
           m_pendingAdhocDomainId == m_currentDomainId;
}

// ---------- IP 池 ----------
void BlindConnectApp::InitIpPool(Ipv4Address netAddr, Ipv4Address start, Ipv4Address end, Ipv4Mask mask) {
    m_poolBase = netAddr;
    m_poolStart = start;
    m_poolEnd = end;
    m_poolMask = mask;
    m_availableAddresses.clear();
    uint32_t startInt = start.Get();
    uint32_t endInt = end.Get();
    for (uint32_t addr = startInt; addr <= endInt; addr++) {
        m_availableAddresses.push_back(Ipv4Address(addr));
    }
    NS_LOG_INFO("IP pool: " << start << " - " << end);
}

Ipv4Address BlindConnectApp::AllocateIp(const Mac48Address& mac) {
    auto it = m_macToIp.find(mac);
    if (it != m_macToIp.end()) return it->second;
    if (m_availableAddresses.empty()) {
        NS_LOG_WARN("IP pool exhausted");
        return Ipv4Address::GetAny();
    }
    Ipv4Address ip = m_availableAddresses.front();
    m_availableAddresses.pop_front();
    m_macToIp[mac] = ip;
    NS_LOG_INFO("Allocated " << ip << " to " << mac);
    return ip;
}

void BlindConnectApp::RegisterAdhocChannel(const std::string& ssid, Ptr<YansWifiChannel> channel) {
    m_adhocChannels[ssid] = channel;
    NS_LOG_INFO("Registered Adhoc channel: " << ssid);
}

void BlindConnectApp::ReleaseIp(const Mac48Address& mac) {
    auto it = m_macToIp.find(mac);
    if (it != m_macToIp.end()) {
        m_availableAddresses.push_back(it->second);
        NS_LOG_INFO("Released " << it->second << " from " << mac);
        m_macToIp.erase(it);
    }
}

// ---------- 伪信标发送 ----------
void BlindConnectApp::SendPseudoBeacon() {
    PurgeNeighborTable();

    if (m_role == ROLE_GATEWAY) {
        m_localHops = 0;
    } else {
        uint32_t minHops = 99;
        for (const auto& entry : m_neighborTable) {
            if (entry.second.hopsToGw < minHops) {
                minHops = entry.second.hopsToGw;
            }
        }
        m_localHops = (minHops < 99) ? minHops + 1 : 99;
    }

    uint32_t activeNodes = m_neighborTable.size() + 1;
    double totalLoad = 0.0, minEnergy = 1.0;
    double myLoad = 0.35, myEnergy = 1.0;
    for (const auto& entry : m_neighborTable) {
        totalLoad += entry.second.load;
        if (entry.second.energy < minEnergy) minEnergy = entry.second.energy;
    }
    double avgLoad = (totalLoad + myLoad) / activeNodes;
    double domainMinEnergy = std::min(minEnergy, myEnergy);

    Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
    if (!ipv4) return;
    Ptr<NetDevice> ipDev = GetSocketAdhocDev();  // 从Adhoc设备取IP(广告正确的网关地址)
    int32_t iface = ipv4->GetInterfaceForDevice(ipDev);
    Ipv4Address myIp("0.0.0.0");
    if (iface >= 0 && ipv4->GetNAddresses(iface) > 0) {
        myIp = ipv4->GetAddress(iface, 0).GetLocal();
    }

    std::ostringstream oss;
    oss << "TYPE:IBSS_BEACON;"
        << "SSID:" << m_adhocSsid << ";"
        << "MAC:" << m_adhocDevice->GetAddress() << ";"
        << "IP:" << myIp << ";"
        << "TS:" << Simulator::Now().GetSeconds() << ";"
        << "HOPS_TO_GW:" << m_localHops << ";"
        << "NODES:" << activeNodes << ";"
        << "AVG_LOAD:" << avgLoad << ";"
        << "MIN_ENERGY:" << domainMinEnergy << ";";

    if (m_cryptoEnabled && m_publicKeyBytes) {
        oss << "SEC:CHEBYSHEV;"
            << "MODULUS:" << GetModulusHex() << ";"
            << "PUBKEY:" << GetPublicKeyHex() << ";";
    } else {
        oss << "SEC:NONE;";
    }

    std::string payload = oss.str();
    // 去掉末尾分号，与接收端 msgBody 提取保持一致
    if (!payload.empty() && payload.back() == ';') {
        payload.pop_back();
    }

    // 对消息体计算 HMAC 签名
    if (m_cryptoEnabled && m_sharedSecret) {
        std::string hmacHex = SignMessage(payload);
        payload += ";HMAC:" + hmacHex + ";";
    }
    Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
    m_broadcastSocket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 9));

    Time nextTime = Seconds(2.0) + Seconds(m_jitterVar->GetValue());
    m_beaconEvent = Simulator::Schedule(nextTime, &BlindConnectApp::SendPseudoBeacon, this);
}

void BlindConnectApp::PurgeNeighborTable() {
    Time now = Simulator::Now();
    for (auto it = m_neighborTable.begin(); it != m_neighborTable.end(); ) {
        if (now - it->second.lastSeen > Seconds(3.0)) {
            it = m_neighborTable.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------- AP Sniffer: PHY层监听 IP_REQUEST（绕过 OFSwitch13 拦截）----------
void BlindConnectApp::ReceiveApSniffer(Ptr<const Packet> packet, uint16_t channelFreqMhz,
                                        WifiTxVector txVector, MpduInfo aMpdu,
                                        SignalNoiseDbm snr, uint16_t staId) {
    if (m_role != ROLE_AP_SERVER) return;

    uint8_t buf[512] = {0};
    uint32_t size = std::min((uint32_t)packet->GetSize(), (uint32_t)511);
    packet->CopyData(buf, size);
    std::string payload((char*)buf, size);

    if (payload.find("TYPE:IP_REQUEST") == std::string::npos) return;

    std::cout << Simulator::Now().GetSeconds()
              << "s: [ApServer-Sniffer] 收到 IP_REQUEST, payload=" << payload << std::endl;

    size_t pos = payload.find("MAC:");
    if (pos == std::string::npos) return;
    size_t endPos = payload.find(";", pos);
    std::string macStr = payload.substr(pos + 4, endPos - pos - 4);
    Mac48Address mac = Mac48Address(macStr.c_str());
    std::string txid = ExtractField(payload, "TXID");

    Ipv4Address ip = AllocateIp(mac);
    if (ip == Ipv4Address::GetAny()) return;

    std::ostringstream oss;
    oss << "TYPE:IP_OFFER;"
        << "MAC:" << mac << ";"
        << "IP:" << ip << ";"
        << "MASK:" << m_poolMask << ";"
        << "GW:" << m_poolBase;
    if (!txid.empty()) {
        oss << ";TXID:" << txid;
    }
    std::string resp = oss.str();
    // 加2ms调度延迟，模拟AP端处理+传播时延，避免IP_OFFER与IP_REQUEST同时间戳
    Simulator::Schedule(MilliSeconds(2), &BlindConnectApp::SendDelayedStaIpOffer, this, resp, mac);

    std::cout << Simulator::Now().GetSeconds()
              << "s: [ApServer-Sniffer] 发送 IP_OFFER: " << ip << " -> " << mac
              << " (scheduled +2ms)" << std::endl;

    if (m_ipAllocatedCallback) {
        m_ipAllocatedCallback(mac, ip);
    }
}

// ---------- Adhoc 伪信标接收（含 IP 分配消息）----------
void BlindConnectApp::ReceiveAdhocBeacon(Ptr<const Packet> packet, uint16_t channelFreqMhz,
                                         WifiTxVector txVector, MpduInfo aMpdu,
                                         SignalNoiseDbm snr, uint16_t staId) {
    uint8_t buf[1024] = {0};
    uint32_t size = std::min((uint32_t)packet->GetSize(), (uint32_t)1023);
    packet->CopyData(buf, size);
    std::string payload((char*)buf, size);

    // --- IP 分配消息 (优先级高于普通信标) ---
    if (payload.find("TYPE:IP_REQUEST") != std::string::npos) {
        if (m_role == ROLE_GATEWAY) {
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [Gateway] 收到 IP_REQUEST, payload=" << payload << std::endl;
            size_t pos = payload.find("MAC:");
            if (pos == std::string::npos) return;
            size_t endPos = payload.find(";", pos);
            std::string macStr = payload.substr(pos + 4, endPos - pos - 4);
            Mac48Address mac = Mac48Address(macStr.c_str());
            std::string txid = ExtractField(payload, "TXID");

            // 提取终端公钥，计算共享密钥
            if (m_cryptoEnabled && m_sharedSecret == nullptr) {
                std::string termPubHex = ExtractField(payload, "PUBKEY");
                if (!termPubHex.empty() && m_privateKeyBytes) {
                    size_t termPubLen = 0;
                    unsigned char* termPubBytes =
                        ExtendedChebyshevKeyExchange::hex_to_bytes(termPubHex, &termPubLen);
                    if (termPubBytes && m_keyExchange) {
                        size_t secretLen = 0;
                        unsigned char* secret =
                            m_keyExchange->compute_shared_secret_bytes(
                                m_privateKeyBytes, m_privateKeyLen,
                                termPubBytes, termPubLen, &secretLen);
                        if (secret && secretLen >= 16) {
                            m_sharedSecret = new unsigned char[16];
                            memcpy(m_sharedSecret, secret, 16);
                            m_sharedSecretLen = 16;
                            std::cout << Simulator::Now().GetSeconds()
                                      << "s: 网关与终端完成Chebyshev密钥协商" << std::endl;
                        }
                        if (secret) ExtendedChebyshevKeyExchange::free_bytes(secret);
                    }
                    if (termPubBytes) ExtendedChebyshevKeyExchange::free_bytes(termPubBytes);
                }
            }

            Ipv4Address ip = AllocateIp(mac);
            if (ip == Ipv4Address::GetAny()) return;

            std::ostringstream oss;
            oss << "TYPE:IP_OFFER;"
                << "MAC:" << mac << ";"
                << "IP:" << ip << ";"
                << "MASK:" << m_poolMask << ";"
                << "GW:" << m_poolBase;
            if (!txid.empty()) {
                oss << ";TXID:" << txid;
            }

            std::string resp = oss.str();

            // HMAC 签名
            if (m_cryptoEnabled && m_sharedSecret) {
                std::string hmacHex = SignMessage(resp);
                resp += ";HMAC:" + hmacHex + ";";
            }

            Ptr<Packet> p = Create<Packet>((uint8_t*)resp.c_str(), resp.length());
            // 同时从 Adhoc 和 AP 设备发送 IP_OFFER（SendFrom/Send 绕过 OFSwitch13）
            Mac48Address bcast = Mac48Address::GetBroadcast();
            m_adhocDevice->SendFrom(p, m_adhocDevice->GetAddress(), bcast, 0x0800);
            if (m_staDevice) {
                Ptr<Packet> p2 = Create<Packet>((uint8_t*)resp.c_str(), resp.length());
                m_staDevice->Send(p2, bcast, 0x0800);
            }
            NS_LOG_INFO("Gateway sent IP_OFFER: " << ip << " to " << mac);
            // 通知 SDN 控制器注入 ARP
            if (m_ipAllocatedCallback) {
                m_ipAllocatedCallback(mac, ip);
            }
        }
        return;
    }

    if (payload.find("TYPE:IP_RELEASE") != std::string::npos) {
        if (m_role == ROLE_GATEWAY) {
            size_t pos = payload.find("MAC:");
            if (pos != std::string::npos) {
                size_t endPos = payload.find(";", pos);
                std::string macStr = payload.substr(pos + 4, endPos - pos - 4);
                ReleaseIp(Mac48Address(macStr.c_str()));
            }
        }
        return;
    }

    if (payload.find("TYPE:IP_OFFER") != std::string::npos) {
        if (m_role == ROLE_TERMINAL) {
            // 校验 MAC，只处理发给本节点的 OFFER
            size_t mp = payload.find("MAC:");
            if (mp != std::string::npos) {
                size_t mep = payload.find(";", mp);
                Mac48Address dm(payload.substr(mp + 4, mep - mp - 4).c_str());
                std::cout << Simulator::Now().GetSeconds()
                          << "s: [Terminal] 收到 IP_OFFER, for=" << dm
                          << " myAdhocMac=" << m_adhocDevice->GetAddress()
                          << " payload=" << payload << std::endl;
                if (dm == m_adhocDevice->GetAddress()) {
                    HandleAdhocIpMessage(payload);
                } else {
                    std::cout << Simulator::Now().GetSeconds()
                              << "s: [Terminal] IP_OFFER MAC不匹配, 忽略" << std::endl;
                }
            }
        }
        return;
    }

    if (payload.find("TYPE:IP_CONFIRM") != std::string::npos) {
        return;
    }

    // --- 普通 IBSS_BEACON ---
    if (payload.find("TYPE:IBSS_BEACON") == std::string::npos) return;

    Ipv4Address srcIp = Ipv4Address("10.1.1.1");
    uint32_t hops = 99;
    double load = 0.35, energy = 0.85;
    uint32_t nodes = 1;
    bool secure = false;

    size_t pos;
    if ((pos = payload.find("IP:")) != std::string::npos) {
        size_t endPos = payload.find(";", pos);
        std::string ipStr = payload.substr(pos + 3, endPos - pos - 3);
        srcIp = Ipv4Address(ipStr.c_str());
    }
    if ((pos = payload.find("HOPS_TO_GW:")) != std::string::npos)
        hops = std::stoi(payload.substr(pos + 11, 2));
    if ((pos = payload.find("AVG_LOAD:")) != std::string::npos)
        load = std::stod(payload.substr(pos + 9, 4));
    if ((pos = payload.find("MIN_ENERGY:")) != std::string::npos)
        energy = std::stod(payload.substr(pos + 11, 4));
    if ((pos = payload.find("NODES:")) != std::string::npos)
        nodes = std::stoi(payload.substr(pos + 6));
    if (payload.find("SEC:SAE") != std::string::npos ||
        payload.find("SEC:CHEBYSHEV") != std::string::npos)
        secure = true;

    Ssid adhocSsid = Ssid("Adhoc-Net");  // 默认值
    if ((pos = payload.find("SSID:")) != std::string::npos) {
        size_t endPos = payload.find(";", pos);
        adhocSsid = Ssid(payload.substr(pos + 5, endPos - pos - 5).c_str());
    }

    // --- 加密处理: 提取 MODULUS/PUBKEY，计算共享密钥 ---
    // 只与 Gateway (HOPS_TO_GW:0) 进行密钥交换
    // 先从 TYPE: 定位应用层消息体，避免在协议头中误匹配字段
    bool hasCheb = (payload.find("SEC:CHEBYSHEV") != std::string::npos);
    if (m_role == ROLE_TERMINAL && m_cryptoEnabled &&
        hasCheb &&
        hops == 0) {
        size_t appPos = payload.find("TYPE:");
        std::string appPayload = (appPos != std::string::npos) ? payload.substr(appPos) : payload;
        std::string modHex = ExtractField(appPayload, "MODULUS");
        std::string pubHex = ExtractField(appPayload, "PUBKEY");
        if (!modHex.empty() && !pubHex.empty() && m_sharedSecret == nullptr) {
            if (ComputeSharedSecretFromGateway(modHex, pubHex)) {
                std::cout << Simulator::Now().GetSeconds()
                          << "s: 终端与网关完成Chebyshev密钥协商" << std::endl;
                // 共享密钥已保存，后续无论何时切到 Adhoc 都能签名 IP 请求
                // 不在此处主动切换——交给 EvaluateAndSwitch 周期评估决定
            }
        }
        // 验证 HMAC (仅验证来自Gateway的beacon)
        if (m_sharedSecret) {
            std::string hmacHex = ExtractField(appPayload, "HMAC");
            if (!hmacHex.empty()) {
                size_t hmacPos = appPayload.find(";HMAC:");
                if (hmacPos != std::string::npos) {
                    std::string msgBody = appPayload.substr(0, hmacPos);
                    bool valid = VerifyMessage(msgBody, hmacHex);
                    if (!valid) {
                        std::cout << Simulator::Now().GetSeconds()
                                  << "s: [安全警告] 伪信标HMAC验证失败!" << std::endl;
                    }
                }
            }
        }
    }

    if (m_role == ROLE_TERMINAL) {
        std::cout << Simulator::Now().GetSeconds() << "s: 终端收到 Adhoc 伪信标, 源IP="
                  << srcIp << ", SNR=" << snr.signal << "dBm, Hops=" << hops << std::endl;
    }

    m_neighborTable[srcIp] = {Simulator::Now(), hops, load, energy};

    if (m_role == ROLE_TERMINAL) {
        ScannedNodeInfo info;
        info.type = ScannedNodeInfo::TYPE_ADHOC;
        info.ssid = adhocSsid;
        info.gateway = srcIp;
        info.snr = snr.signal;
        info.hopsToGw = hops;
        info.load = load;
        info.minEnergy = energy;
        info.nodes = nodes;
        info.secure = secure;
        info.channelFreqMhz = channelFreqMhz;
        m_neighborRadar.push_back(info);
        BlindConnectApp::LogExperimentEvent("FIRST_SEEN", GetDomainIdFromBestNode(info), "ADHOC",
                                            0, srcIp, "", "First Seen",
                                            std::string(adhocSsid.PeekString()), snr.signal,
                                            hops, "IBSS beacon detected");
    }
}

// ---------- AP Beacon 解析 (含伪信标: GATEWAY可通过AP网卡发伪信标) ----------
void BlindConnectApp::ReceiveStaBeacon(Ptr<const Packet> packet, uint16_t channelFreqMhz,
                                       WifiTxVector txVector, MpduInfo aMpdu,
                                       SignalNoiseDbm snr, uint16_t staId) {
    if (packet->GetSize() < 30) return;
    uint8_t frameCtrlByte = 0;
    packet->CopyData(&frameCtrlByte, 1);

    // 非 Beacon 帧: 检查是否为伪信标 (GATEWAY通过AP网卡发送)
    if (frameCtrlByte != 0x80) {
        uint8_t buf[1024] = {0};
        uint32_t size = std::min((uint32_t)packet->GetSize(), (uint32_t)1023);
        packet->CopyData(buf, size);
        std::string payload((char*)buf, size);

        // IP_OFFER 给 STA 设备：直接处理，不转发到 Adhoc
        if (payload.find("TYPE:IP_OFFER") != std::string::npos) {
            size_t mp = payload.find("MAC:");
            if (mp != std::string::npos && m_staDevice) {
                size_t mep = payload.find(";", mp);
                Mac48Address dm(payload.substr(mp + 4, mep - mp - 4).c_str());
                if (dm == m_staDevice->GetAddress()) {
                    if (!IsExpectedStaOffer(payload)) {
                        std::cout << Simulator::Now().GetSeconds()
                                  << "s: [Terminal-STA] IP_OFFER TXID过期, 忽略 payload=" << payload << std::endl;
                        BlindConnectApp::LogExperimentEvent("STALE_IP_OFFER", m_currentDomainId, "STA",
                                                     0, Ipv4Address("0.0.0.0"), "", "Stale Offer",
                                                     "", 0.0, -1, "STA offer txid mismatch");
                        return;
                    }
                    std::cout << Simulator::Now().GetSeconds()
                              << "s: [Terminal-STA] 收到STA IP_OFFER, ch="
                              << channelFreqMhz << "MHz, payload=" << payload << std::endl;
                    Ipv4Address ip, gw;
                    Ipv4Mask mask;
                    size_t pos;
                    if ((pos = payload.find("IP:")) != std::string::npos) {
                        size_t endPos = payload.find(";", pos);
                        ip = Ipv4Address(payload.substr(pos + 3, endPos - pos - 3).c_str());
                    }
                    if ((pos = payload.find("MASK:")) != std::string::npos) {
                        size_t endPos = payload.find(";", pos);
                        mask = Ipv4Mask(payload.substr(pos + 5, endPos - pos - 5).c_str());
                    }
                    if ((pos = payload.find("GW:")) != std::string::npos) {
                        size_t endPos = payload.find(";", pos);
                        gw = Ipv4Address(payload.substr(pos + 3, endPos - pos - 3).c_str());
                    }
                    if (ip != Ipv4Address::GetAny() && m_role == ROLE_TERMINAL) {
                        {
                            std::ostringstream ipKey; ipKey << ip << "_d" << (int)m_currentDomainId;
                            BlindConnectApp::LogMessageOnce("IP_OFFER", Simulator::Now().GetSeconds(), ipKey.str(), m_currentDomainId, "STA");
                            BlindConnectApp::LogExperimentEvent("IP_OFFER", m_currentDomainId, "STA",
                                                               m_pendingStaTxId, ip, "", "IP Offer",
                                                               std::string(m_currentSsid.PeekString()), m_currentSnr, m_currentHops);
                        }
                        m_pendingStaTxId = 0;
                        m_assignedIp = ip;
                        m_assignedMask = mask;
                        m_assignedGw = gw;
                        Simulator::Cancel(m_ipRetryEvent);
                        m_ipRetryCount = 0;
                        ConfigureIpOnInterface(GetSocketStaDev(), ip, mask, gw);
                        BlindConnectApp::LogExperimentEvent("CONFIGURED", m_currentDomainId, "STA", 0, ip,
                                                           "", "Configured", std::string(m_currentSsid.PeekString()),
                                                           m_currentSnr, m_currentHops, "STA interface configured");
                        m_staState.ip = ip;
                        m_staState.gw = gw;
                        m_staState.mask = mask;

                        // 备份接口只保持控制面UP，不再硬编码同步业务IP。
                        EnsureBothInterfacesUp();
                        // 通知控制器注册双网卡 ARP（只记一条，同步网卡不重复记）
                        if (m_ipAllocatedCallback) {
                            BlindConnectApp::LogMessage("AddArpEntry", Simulator::Now().GetSeconds(), m_currentDomainId, "STA");
                            BlindConnectApp::LogExperimentEvent("ADD_ARP_ENTRY", m_currentDomainId, "STA", 0, m_staState.ip,
                                                               "", "Sync", std::string(m_currentSsid.PeekString()),
                                                               m_currentSnr, m_currentHops, "Controller ARP update");
                            m_ipAllocatedCallback(Mac48Address::ConvertFrom(GetSocketStaDev()->GetAddress()), m_staState.ip);
                            if (m_adhocState.ip != Ipv4Address("0.0.0.0")) {
                                m_ipAllocatedCallback(Mac48Address::ConvertFrom(GetSocketAdhocDev()->GetAddress()), m_adhocState.ip);
                            }
                        }

                        std::string domain = "?";
                        if (ip.Get() >> 24 == 10) {
                            uint32_t secondOctet = (ip.Get() >> 16) & 0xFF;
                            if (secondOctet == 1 || secondOctet == 100) domain = "A";
                            else if (secondOctet == 2 || secondOctet == 101) domain = "B";
                            else if (secondOctet == 3 || secondOctet == 100) domain = "C";
                        }
                        std::cout << "\n╔══════════════════════════════════════════╗\n"
                                  << "║       终端 IP 配置成功 (节点"
                                  << GetNode()->GetId() << " → 域" << domain << ")      ║\n"
                                  << "╠══════════════════════════════════════════╣\n"
                                  << "║  时间  : " << Simulator::Now().GetSeconds() << " s\n"
                                  << "║  STA IP : " << ip << "\n"
                                  << "║  掩码  : " << mask << "\n"
                                  << "║  网关  : " << gw << "\n";
                        // 打印 Adhoc IP
                        Ptr<NetDevice> adhocDev = GetSocketAdhocDev();
                        Ptr<Ipv4> ipv4_ = GetNode()->GetObject<Ipv4>();
                        int32_t adhocIfIdx = ipv4_->GetInterfaceForDevice(adhocDev);
                        if (adhocIfIdx >= 0 && ipv4_->GetNAddresses(adhocIfIdx) > 0) {
                            Ipv4Address adhocIp = ipv4_->GetAddress(adhocIfIdx, 0).GetLocal();
                            std::cout << "║  AdHoc IP: " << adhocIp << "\n";
                        }
                        std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;
                    }
                    return;
                }
            }
        }

        // IBSS_BEACON / Adhoc IP 消息转发到 Adhoc 处理
        if (payload.find("TYPE:IBSS_BEACON") != std::string::npos ||
            payload.find("TYPE:IP_OFFER") != std::string::npos) {
            ReceiveAdhocBeacon(packet, channelFreqMhz, txVector, aMpdu, snr, staId);
        }
        return;
    }

    Ptr<Packet> p = packet->Copy();
    WifiMacHeader hdr;
    if (!p->RemoveHeader(hdr) || !hdr.IsBeacon()) return;
    MgtBeaconHeader beacon;
    if (!p->RemoveHeader(beacon)) return;

    // SSID=C 的 AP Beacon 是 SDN 内部用,域 C 对终端是自组织域,过滤掉避免误入 AP 模式
    Ssid beaconSsid = beacon.GetSsid();
    if (std::string(beaconSsid.PeekString()) == "C") return;

    ScannedNodeInfo info;
    info.type = ScannedNodeInfo::TYPE_AP;
    info.ssid = beaconSsid;
    info.snr = snr.signal;
    info.hopsToGw = 0;
    info.gateway = Ipv4Address("0.0.0.0");
    info.load = 0.0;
    info.minEnergy = 1.0;
    info.secure = false;
    info.nodes = 1;
    info.channelFreqMhz = channelFreqMhz;
    std::string beaconSsidStr(beaconSsid.PeekString());
    if (beaconSsidStr == "A") {
        info.gateway = Ipv4Address("10.1.1.1");
    } else if (beaconSsidStr == "B") {
        info.gateway = Ipv4Address("10.2.1.1");
    } else if (beaconSsidStr == "C") {
        info.gateway = Ipv4Address("10.3.1.1");
    }

    uint8_t buffer[256];
    uint32_t size = p->CopyData(buffer, 256);
    for (uint32_t i = 0; i + 2 <= size; ) {
        uint8_t id = buffer[i];
        uint8_t len = buffer[i+1];
        if (i + 2 + len > size) break;
        if (id == 11 && len >= 5) {
            info.load = buffer[i+4] / 255.0;
        } else if (id == 48) {
            info.secure = true;
        }
        i += (2 + len);
    }

    m_neighborRadar.push_back(info);
    BlindConnectApp::LogExperimentEvent("FIRST_SEEN", GetDomainIdFromBestNode(info), "STA",
                                        0, info.gateway, "", "First Seen",
                                        std::string(beaconSsid.PeekString()), snr.signal,
                                        0, "AP beacon detected");
}

// ---------- 跳频 ----------
void BlindConnectApp::SwitchToNextChannel() {
    if (!m_staDevice) return;
    Ptr<WifiPhy> phy = m_staDevice->GetPhy();
    if (phy->IsStateTx() || phy->IsStateRx() || phy->IsStateSwitching() ||
        phy->IsStateSleep() || phy->IsStateOff()) {
        m_hopEvent = Simulator::Schedule(MilliSeconds(5), &BlindConnectApp::SwitchToNextChannel, this);
        return;
    }

    uint8_t targetCh = m_channels[m_currentChIdx];
    phy->SetOperatingChannel(targetCh, 2407 + targetCh * 5, 20);

    m_currentChIdx++;
    if (m_currentChIdx >= m_channels.size()) {
        m_currentChIdx = 0;
    }
    m_hopEvent = Simulator::Schedule(m_dwellTime, &BlindConnectApp::SwitchToNextChannel, this);
}

// ---------- 多属性打分 ----------
double BlindConnectApp::CalculateScore(const ScannedNodeInfo& node) {
    double rssi = std::max(-90.0, std::min(-30.0, node.snr));
    double rssiNorm = (rssi + 90.0) / 60.0;
    double hopsNorm = std::min(node.hopsToGw, 5u) / 5.0;
    double load = node.load;
    double energy = node.minEnergy;
    double secBonus = node.secure ? 0.1 : 0.0;

    double wRssi = 0.60, wHops = 0.05, wLoad = 0.0, wEnergy = 0.25, wSec = 0.0;
    return wRssi * rssiNorm
           - wHops * hopsNorm
           - wLoad * load
           + wEnergy * energy
           + wSec * secBonus;
}

void BlindConnectApp::EvaluateAndSwitch() {
    // 最小停留间隔 8s:保证在每个域有充分驻留时间完成 IP 协商和业务
    if (Simulator::Now() - m_lastSwitchTime < Seconds(8.0)) {
        m_neighborRadar.clear();
        ScheduleEvaluate();
        return;
    }

    if (m_neighborRadar.empty()) {
        if (m_currentHops < 99) {
            m_currentSnr -= 5.0;
            if (m_currentSnr < -100.0) m_currentSnr = -100.0;
        }
        ScheduleEvaluate();
        return;
    }

    NS_LOG_UNCOND("----- 候选域列表 (t=" << Simulator::Now().GetSeconds() << ") -----");
    for (const auto& n : m_neighborRadar) {
        std::string type = (n.type == ScannedNodeInfo::TYPE_AP) ? "AP" : "Adhoc";
        std::string ssid = n.ssid.PeekString();
        NS_LOG_UNCOND(type << " SSID:" << ssid
                      << " SNR:" << n.snr << "dBm"
                      << " Hops:" << n.hopsToGw
                      << " Load:" << n.load
                      << " Energy:" << n.minEnergy
                      << " Secure:" << n.secure
                      << " Score:" << CalculateScore(n));
    }

    std::sort(m_neighborRadar.begin(), m_neighborRadar.end(),
              [this](const ScannedNodeInfo& a, const ScannedNodeInfo& b) {
                  return CalculateScore(a) > CalculateScore(b);
              });

    // 首次入网(m_currentHops==99)优先选择 AP 域:基础设施可用时不主动入 Adhoc
    // 若 radar 中暂无 AP 候选,最多等待 5 轮(给跳频扫描更多机会接收 AP Beacon)
    ScannedNodeInfo bestNode = m_neighborRadar[0];
    if (m_currentHops == 99) {
        bool foundAp = false;
        for (const auto& n : m_neighborRadar) {
            if (n.type == ScannedNodeInfo::TYPE_AP) {
                bestNode = n;
                foundAp = true;
                NS_LOG_UNCOND("首次入网优先选择 AP 域: " << n.ssid.PeekString()
                              << " SNR:" << n.snr);
                break;
            }
        }
        if (!foundAp && m_initWaitApRounds < 5) {
            m_initWaitApRounds++;
            NS_LOG_UNCOND("首次入网未发现 AP,等待第 " << m_initWaitApRounds
                          << "/5 轮后再决定 (避免误入 Adhoc)");
            m_neighborRadar.clear();
            ScheduleEvaluate();
            return;
        }
    }
    double bestScore = CalculateScore(bestNode);

    bool currentAlive = false;
    for (const auto& n : m_neighborRadar) {
        // 正常匹配：类型和SSID都相同
        if (n.type == m_currentNetType && n.ssid == m_currentSsid) {
            currentAlive = true;
            m_currentSnr = n.snr;
            break;
        }
        // C域特殊匹配：当前在C域AP模式，扫描到C域伪Beacon
        if (std::string(m_currentSsid.PeekString()) == "C" &&
            n.type == ScannedNodeInfo::TYPE_ADHOC &&
            std::string(n.ssid.PeekString()) == "Adhoc-C") {
            currentAlive = true;
            m_currentSnr = n.snr;
            break;
        }
    }
    if (!currentAlive && m_currentHops < 99) {
        m_currentSnr -= 5.0;
        if (m_currentSnr < -100.0) m_currentSnr = -100.0;
    }

    double currentScore = -1e9;
    if (m_currentHops < 99) {
        ScannedNodeInfo currentInfo;
        currentInfo.type = m_currentNetType;
        currentInfo.snr = m_currentSnr;
        currentInfo.hopsToGw = m_currentHops;
        currentInfo.load = 0.0;  // 与候选节点一致（AP beacon不含load信息）
        currentInfo.minEnergy = 1.0;
        currentInfo.secure = false;
        currentScore = CalculateScore(currentInfo);
    }

    double threshold = 0.0;
    NS_LOG_UNCOND("当前网络评分:" << currentScore << " 最优候选评分:" << bestScore);

    // 同网络跳过：类型和SSID都相同不切换
    bool sameNetwork = (bestNode.type == m_currentNetType &&
                        bestNode.ssid == m_currentSsid);
    // 首次入网(hops==99)或已拿到 IP 才允许评估切换
    // 避免:已切换但 IP 还在协商中时再次抢跑切换,引发抖动 + 重复请求
    bool firstJoin = (m_currentHops == 99);
    bool ipReady = (m_assignedIp != Ipv4Address("0.0.0.0"));
    if (!sameNetwork && (firstJoin || ipReady) &&
        (bestScore > currentScore + threshold || firstJoin)) {
        std::string targetName;
        if (bestNode.type == ScannedNodeInfo::TYPE_AP) {
            targetName = "AP域 (" + std::string(bestNode.ssid.PeekString()) + ")";
        } else {
            targetName = "Adhoc域 (到网关" + std::to_string(bestNode.hopsToGw) + "跳)";
        }
        NS_LOG_UNCOND(">>> 切换至 " << targetName << " SNR:" << bestNode.snr << " Hops:" << bestNode.hopsToGw);
        std::ostringstream note;
        note << "score=" << bestScore << ";type="
             << ((bestNode.type == ScannedNodeInfo::TYPE_AP) ? "AP" : "ADHOC");
        BlindConnectApp::LogExperimentEvent("SELECTED", GetDomainIdFromBestNode(bestNode),
                                            (bestNode.type == ScannedNodeInfo::TYPE_AP) ? "STA" : "ADHOC",
                                            0, bestNode.gateway, "", "Selected",
                                            std::string(bestNode.ssid.PeekString()), bestNode.snr,
                                            bestNode.hopsToGw, note.str());
        ExecuteSwitch(bestNode);
    }

    m_neighborRadar.clear();
    ScheduleEvaluate();
}

void BlindConnectApp::ExecuteSwitch(const ScannedNodeInfo& bestNode) {
    m_lastSwitchTime = Simulator::Now();
    Simulator::Cancel(m_ipReqEvent);
    Simulator::Cancel(m_ipRetryEvent);
    Simulator::Cancel(m_adhocIpReqEvent);
    Simulator::Cancel(m_adhocIpRetryEvent);
    Ptr<Node> node = GetNode();
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

    const char* oldType = (m_currentNetType == ScannedNodeInfo::TYPE_AP) ? "AP" : "Adhoc";
    const char* newType = (bestNode.type == ScannedNodeInfo::TYPE_AP) ? "AP" : "Adhoc";
    std::string newSsid(bestNode.ssid.PeekString());

    std::cout << "\n============================================" << std::endl;
    std::cout << Simulator::Now().GetSeconds() << "s: [SWITCH] " << oldType
              << " -> " << newType << " (SSID=" << newSsid
              << ", SNR=" << bestNode.snr << "dBm, Hops=" << bestNode.hopsToGw << ")"
              << std::endl;
    std::cout << "           旧IP=" << m_assignedIp << std::endl;
    std::string switchNote = std::string(oldType) + "->" + newType;
    BlindConnectApp::LogExperimentEvent("SWITCH_START", m_currentDomainId, oldType,
                                        0, m_assignedIp, "", "Switch Start",
                                        std::string(m_currentSsid.PeekString()), m_currentSnr,
                                        m_currentHops, switchNote);

    // 1. 确定目标域ID，域变了则释放旧域IP，重置状态等待动态IP分配
    uint32_t targetDomainId = GetDomainIdFromBestNode(bestNode);
    if (targetDomainId > 0 && targetDomainId != m_currentDomainId) {
        // 离开旧域时释放Ad-Hoc IP（如果是从Ad-Hoc域离开）
        if (m_currentNetType == ScannedNodeInfo::TYPE_ADHOC
            && m_broadcastSocket
            && m_assignedIp != Ipv4Address("0.0.0.0")) {
            std::ostringstream oss;
            oss << "TYPE:IP_RELEASE;"
                << "MAC:" << m_adhocDevice->GetAddress() << ";"
                << "IP:" << m_assignedIp;
            std::string payload = oss.str();
            Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
            m_broadcastSocket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 9));
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [ExecuteSwitch] 释放旧域IP=" << m_assignedIp << std::endl;
        }
        // 域变了，重置IP状态（不再硬编码，等待动态IP_REQUEST/IP_OFFER分配）
        m_assignedIp = Ipv4Address("0.0.0.0");
        m_staState.ip = Ipv4Address("0.0.0.0");
        m_staState.gw = Ipv4Address("0.0.0.0");
        m_adhocState.ip = Ipv4Address("0.0.0.0");
        m_adhocState.gw = Ipv4Address("0.0.0.0");
        m_currentDomainId = targetDomainId;
        m_pendingStaTxId = 0;
        m_pendingAdhocTxId = 0;
        m_pendingStaDomainId = 0;
        m_pendingAdhocDomainId = 0;
        // 记录触发切换的信标
        BlindConnectApp::LogMessage(
            (bestNode.type == ScannedNodeInfo::TYPE_AP) ? "AP Beacon" : "IBSS_BEACON",
            Simulator::Now().GetSeconds(), m_currentDomainId);
        EnsureBothInterfacesUp();
    }

    // 2. 判断是否C域（Ad-Hoc决定入网）
    std::string adhocSsidStr(bestNode.ssid.PeekString());
    bool isDomainC = (adhocSsidStr == "Adhoc-C");

    if (bestNode.type == ScannedNodeInfo::TYPE_AP) {
        // --- 切换到AP数据模式 ---
        Simulator::Cancel(m_hopEvent);
        Simulator::Cancel(m_rescanEvent);
        m_inApRescan = false;

        // STA网卡连接目标AP
        Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
        if (staMac) {
            staMac->SetSsid(bestNode.ssid);
        }
        // 锁定 STA PHY 到目标 AP 信道 (确保链路层关联)
        {
            std::string ssidStr(bestNode.ssid.PeekString());
            uint8_t chNum = 1; uint16_t chFreq = 2412;
            if (ssidStr == "B")      { chNum = 6;  chFreq = 2437; }
            else if (ssidStr == "C") { chNum = 11; chFreq = 2462; }
            Ptr<WifiPhy> staPhy = m_staDevice->GetPhy();
            if (staPhy) staPhy->SetOperatingChannel(chNum, chFreq, 20);
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [ExecuteSwitch] STA PHY锁定 ch" << (int)chNum << std::endl;
        }

        // 切换数据面到AP
        SetDataPlaneActive(DATA_PLANE_AP);

        // 停Ad-Hoc广播socket（AP域不需要发伪信标）
        if (m_broadcastSocket) {
            Simulator::Cancel(m_beaconEvent);
            m_broadcastSocket->Close();
            m_broadcastSocket = nullptr;
        }

        m_currentNetType = ScannedNodeInfo::TYPE_AP;
        m_currentSsid = bestNode.ssid;
        m_currentHops = 0;
        m_currentSnr = bestNode.snr;

        // AP域IP请求（动态分配，等待STA关联完成后发送IP_REQUEST）
        m_ipReqEvent = Simulator::Schedule(Seconds(3.0), &BlindConnectApp::RequestStaIp, this);
        m_ipRetryCount = 0;
        m_ipRetryEvent = Simulator::Schedule(Seconds(5.0), &BlindConnectApp::RetryStaIp, this);
        BlindConnectApp::LogExperimentEvent("SWITCH_READY", m_currentDomainId, "STA", 0, m_assignedIp,
                                            "", "Switch Ready", std::string(m_currentSsid.PeekString()),
                                            m_currentSnr, m_currentHops, "STA control path ready");
        m_adhocIpRetryCount = 0;

        m_apChannelFreqMhz = bestNode.channelFreqMhz;
        m_apChannelNum = (bestNode.channelFreqMhz > 0)
            ? (bestNode.channelFreqMhz - 2407) / 5 : 1;
        ScheduleApRescan();

    } else if (isDomainC) {
        // --- C域切换：Ad-Hoc决定，STA网卡连接C域AP ---
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [ExecuteSwitch] C域切换: Ad-Hoc决定入网" << std::endl;

        Simulator::Cancel(m_hopEvent);
        Simulator::Cancel(m_rescanEvent);
        m_inApRescan = false;

        // STA网卡连接C域AP
        Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
        if (staMac) {
            staMac->SetSsid(Ssid("C"));
            Ptr<WifiPhy> staPhy = m_staDevice->GetPhy();
            if (staPhy) {
                staPhy->SetOperatingChannel(11, 2462, 20);
            }
        }

        // 切换数据面到AP（C域走STA承载业务）
        SetDataPlaneActive(DATA_PLANE_AP);

        m_apChannelFreqMhz = 2462;
        m_apChannelNum = 11;
        ScheduleApRescan();

        m_currentNetType = ScannedNodeInfo::TYPE_AP;
        m_currentSsid = Ssid("C");
        m_currentHops = 0;
        m_currentSnr = bestNode.snr;

        // C域IP请求（动态分配，STA关联C-AP后由GATEWAY响应IP_OFFER）
        m_ipReqEvent = Simulator::Schedule(Seconds(3.0), &BlindConnectApp::RequestStaIp, this);
        m_ipRetryCount = 0;
        m_ipRetryEvent = Simulator::Schedule(Seconds(5.0), &BlindConnectApp::RetryStaIp, this);
        BlindConnectApp::LogExperimentEvent("SWITCH_READY", m_currentDomainId, "STA", 0, m_assignedIp,
                                            "", "Switch Ready", std::string(m_currentSsid.PeekString()),
                                            m_currentSnr, m_currentHops, "STA control path ready");
        m_adhocIpRetryCount = 0;

    } else {
        // --- A/B域：切换到Ad-Hoc数据模式 ---
        Simulator::Cancel(m_rescanEvent);
        m_inApRescan = false;
        if (!m_hopEvent.IsRunning()) {
            SwitchToNextChannel();
        }

        // 切换Adhoc PHY到目标域信道
        {
            std::string ssidKey = bestNode.ssid.PeekString();
            auto chanIt = m_adhocChannels.find(ssidKey);
            if (chanIt != m_adhocChannels.end()) {
                Ptr<WifiPhy> adhocPhy = m_adhocDevice->GetPhy();
                Ptr<YansWifiPhy> yansPhy = DynamicCast<YansWifiPhy>(adhocPhy);
                if (yansPhy && yansPhy->GetChannel() != chanIt->second) {
                    if (adhocPhy->IsStateTx() || adhocPhy->IsStateRx() ||
                        adhocPhy->IsStateSwitching() || adhocPhy->IsStateSleep() ||
                        adhocPhy->IsStateOff()) {
                        Simulator::Schedule(MilliSeconds(5), &BlindConnectApp::ExecuteSwitch,
                                            this, bestNode);
                        return;
                    }
                    yansPhy->SetChannel(chanIt->second);
                    uint8_t chNum = (bestNode.channelFreqMhz > 0)
                        ? (bestNode.channelFreqMhz - 2407) / 5
                        : 11;
                    adhocPhy->SetOperatingChannel(chNum, bestNode.channelFreqMhz, 20);
                    std::cout << Simulator::Now().GetSeconds()
                              << "s: [ExecuteSwitch] Adhoc信道对象已切换到 " << ssidKey
                              << " ch=" << (int)chNum << std::endl;
                }
            }
        }

        // 清理STA侧IP socket
        if (m_staIpSocket) {
            m_staIpSocket->Close();
            m_staIpSocket = nullptr;
        }

        // 切换数据面到Ad-Hoc
        SetDataPlaneActive(DATA_PLANE_ADHOC);

        m_currentNetType = ScannedNodeInfo::TYPE_ADHOC;
        m_currentSsid = bestNode.ssid;
        m_currentHops = bestNode.hopsToGw + 1;
        m_currentSnr = bestNode.snr;

        // 确保Adhoc接口有临时IP
        {
            Ptr<NetDevice> adhocDev = GetSocketAdhocDev();
            int32_t adhocIf = ipv4->GetInterfaceForDevice(adhocDev);
            if (adhocIf >= 0 && ipv4->GetNAddresses(adhocIf) == 0) {
                ipv4->AddAddress(adhocIf, Ipv4InterfaceAddress(
                    Ipv4Address("169.254.0.1"), Ipv4Mask("255.255.0.0")));
                ipv4->SetUp(adhocIf);
            }
        }

        // 启动伪信标广播
        if (!m_broadcastSocket) {
            m_broadcastSocket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
            m_broadcastSocket->SetAllowBroadcast(true);
            m_broadcastSocket->BindToNetDevice(GetSocketAdhocDev());
            m_localHops = bestNode.hopsToGw;
            SendPseudoBeacon();
        }

        // Ad-Hoc IP请求（动态分配，由GATEWAY响应IP_OFFER）
        m_adhocIpRetryCount = 0;
        m_adhocIpReqEvent = Simulator::Schedule(MilliSeconds(500), &BlindConnectApp::RequestAdhocIp, this);
        m_adhocIpRetryEvent = Simulator::Schedule(Seconds(2.5), &BlindConnectApp::RetryAdhocIp, this);
        BlindConnectApp::LogExperimentEvent("SWITCH_READY", m_currentDomainId, "ADHOC", 0, m_assignedIp,
                                            "", "Switch Ready", std::string(m_currentSsid.PeekString()),
                                            m_currentSnr, m_currentHops, "Adhoc control path ready");
    }
}

// ---------- AP 域周期性信道重扫 ----------
void BlindConnectApp::ScheduleApRescan() {
    m_inApRescan = true;
    m_rescanChCount = 0;
    // 每 5 秒触发一次重扫（避开 IP 请求等关键操作）
    m_rescanEvent = Simulator::Schedule(Seconds(5.0), &BlindConnectApp::DoApRescanChannel, this);
}

void BlindConnectApp::DoApRescanChannel() {
    if (!m_staDevice || m_currentNetType != ScannedNodeInfo::TYPE_AP) {
        m_inApRescan = false;
        return;
    }
    Ptr<WifiPhy> staPhy = m_staDevice->GetPhy();
    if (staPhy->IsStateTx() || staPhy->IsStateRx() || staPhy->IsStateSwitching() ||
        staPhy->IsStateSleep() || staPhy->IsStateOff()) {
        m_rescanEvent = Simulator::Schedule(MilliSeconds(50), &BlindConnectApp::DoApRescanChannel, this);
        return;
    }
    // 切换到下一个扫描信道
    m_currentChIdx = (m_currentChIdx + 1) % m_channels.size();
    uint8_t targetCh = m_channels[m_currentChIdx];
    uint16_t targetFreq = 2407 + targetCh * 5;
    staPhy->SetOperatingChannel(targetCh, targetFreq, 20);
    m_rescanChCount++;
    // 每个信道驻留 200ms，然后切回或继续
    if (m_rescanChCount < m_channels.size()) {
        m_rescanEvent = Simulator::Schedule(MilliSeconds(200), &BlindConnectApp::DoApRescanChannel, this);
    } else {
        // 重扫完成：延迟 50ms 后恢复 AP 信道（让 PHY 稳定）
        m_inApRescan = false;
        Simulator::Schedule(MilliSeconds(50), &BlindConnectApp::DoApRescanRestore, this);
        ScheduleApRescan();  // 周期性
    }
}

void BlindConnectApp::DoApRescanRestore() {
    if (!m_staDevice) return;
    Ptr<WifiPhy> phy = m_staDevice->GetPhy();
    if (phy->IsStateTx() || phy->IsStateRx() || phy->IsStateSwitching() ||
        phy->IsStateSleep() || phy->IsStateOff()) {
        Simulator::Schedule(MilliSeconds(50), &BlindConnectApp::DoApRescanRestore, this);
        return;
    }
    phy->SetOperatingChannel(m_apChannelNum, m_apChannelFreqMhz, 20);
}

void BlindConnectApp::SetDefaultRouteVia(Ptr<Ipv4> ipv4, uint32_t iface, Ipv4Address gw) {
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    Ptr<Ipv4StaticRouting> staticRouting = nullptr;
    Ptr<Ipv4ListRouting> listRp = DynamicCast<Ipv4ListRouting>(rp);
    if (listRp) {
        for (uint32_t i = 0; i < listRp->GetNRoutingProtocols(); ++i) {
            int16_t prio;
            staticRouting = DynamicCast<Ipv4StaticRouting>(listRp->GetRoutingProtocol(i, prio));
            if (staticRouting) break;
        }
    } else {
        staticRouting = DynamicCast<Ipv4StaticRouting>(rp);
    }

    if (staticRouting) {
        for (uint32_t i = 0; i < staticRouting->GetNRoutes(); ++i) {
            if (staticRouting->GetRoute(i).IsDefault()) {
                staticRouting->RemoveRoute(i);
                break;
            }
        }
        staticRouting->SetDefaultRoute(gw, iface);
    }
}

// ========== Terminal IP 请求 ==========

void BlindConnectApp::RequestAdhocIp() {
    if (!m_adhocDevice) return;
    if (m_adhocState.ip != Ipv4Address("0.0.0.0")) return;

    if (m_adhocIpRetryCount == 0 || m_pendingAdhocTxId == 0 || m_pendingAdhocDomainId != m_currentDomainId) {
        m_pendingAdhocTxId = NextTxId();
        m_pendingAdhocDomainId = m_currentDomainId;
    }

    std::ostringstream oss;
    oss << "TYPE:IP_REQUEST;"
        << "MAC:" << m_adhocDevice->GetAddress()
        << ";TXID:" << m_pendingAdhocTxId;

    if (m_cryptoEnabled && m_publicKeyBytes) {
        oss << ";PUBKEY:" << GetPublicKeyHex();
    }

    std::string payload = oss.str();

    if (m_cryptoEnabled && m_sharedSecret) {
        std::string hmacHex = SignMessage(payload);
        payload += ";HMAC:" + hmacHex;
    }

    // socket: BindToNetDevice 绑定 AdHoc 设备
    if (!m_adhocIpSocket) {
        m_adhocIpSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_adhocIpSocket->SetAllowBroadcast(true);
        m_adhocIpSocket->BindToNetDevice(m_adhocDevice);
        m_adhocIpSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 69));
    }

    // device 直发,与 STA 路径对称,绕过任何 EDCA 不可达风险;接收方走 PHY MonitorSnifferRx
    Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
    bool ok = m_adhocDevice->Send(p, Mac48Address::GetBroadcast(), 0x0800);
    if (m_adhocIpRetryCount == 0) {
        BlindConnectApp::LogMessage("IP_REQUEST", Simulator::Now().GetSeconds(), m_currentDomainId, "ADHOC");
        BlindConnectApp::LogExperimentEvent("IP_REQUEST", m_currentDomainId, "ADHOC",
                                           m_pendingAdhocTxId, Ipv4Address("0.0.0.0"), "",
                                           "IP Request", std::string(m_currentSsid.PeekString()),
                                           m_currentSnr, m_currentHops);
    }
    std::cout << Simulator::Now().GetSeconds() << "s: [ReqAdhocIp] device->Send ok=" << ok
              << " payload=" << payload << std::endl;
}

void BlindConnectApp::RetryAdhocIp() {
    if (m_adhocState.ip != Ipv4Address("0.0.0.0")) return;
    if (m_adhocIpRetryCount >= 3) {
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [RetryAdhocIp] 已达最大重试次数(3)" << std::endl;
        if (m_dataPlaneMode == DATA_PLANE_ADHOC) {
            // Adhoc是主数据面，失败则回退重新扫描
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [RetryAdhocIp] Adhoc数据面不可用, 回退重新扫描" << std::endl;
            FallbackAndRescan();
        } else {
            // AP数据面模式下Adhoc是备份，失败不影响主业务
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [RetryAdhocIp] AP模式下的Adhoc备份不可用, 放弃Adhoc请求" << std::endl;
        }
        return;
    }
    m_adhocIpRetryCount++;
    std::cout << Simulator::Now().GetSeconds()
              << "s: [RetryAdhocIp] 第" << m_adhocIpRetryCount << "次重试..." << std::endl;
    RequestAdhocIp();
    m_adhocIpRetryEvent = Simulator::Schedule(Seconds(2.0), &BlindConnectApp::RetryAdhocIp, this);
}

void BlindConnectApp::RequestStaIp() {
    if (!m_staDevice) return;
    if (m_staState.ip != Ipv4Address("0.0.0.0")) {
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [ReqStaIp] STA已有IP=" << m_staState.ip << ",跳过请求" << std::endl;
        return;
    }
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
    bool assoc = staMac ? staMac->IsAssociated() : false;
    std::cout << Simulator::Now().GetSeconds() << "s: [ReqStaIp] IsAssociated=" << assoc << std::endl;

    // 链路层必须先关联 AP 才能发起 IP 请求
    if (staMac && !staMac->IsAssociated()) {
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [ReqStaIp] STA未关联AP，1s后重试..." << std::endl;
        m_ipReqEvent = Simulator::Schedule(Seconds(1.0), &BlindConnectApp::RequestStaIp, this);
        return;
    }

    if (m_staIpSocket) { m_staIpSocket->Close(); }
    m_staIpSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    m_staIpSocket->SetAllowBroadcast(true);
    m_staIpSocket->BindToNetDevice(m_staDevice);
    m_staIpSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 68));
    m_staIpSocket->SetRecvCallback(MakeCallback(&BlindConnectApp::HandleStaIpRead, this));

    if (m_ipRetryCount == 0 || m_pendingStaTxId == 0 || m_pendingStaDomainId != m_currentDomainId) {
        m_pendingStaTxId = NextTxId();
        m_pendingStaDomainId = m_currentDomainId;
    }

    std::ostringstream oss;
    oss << "TYPE:IP_REQUEST;"
        << "MAC:" << m_staDevice->GetAddress()
        << ";TXID:" << m_pendingStaTxId;
    if (m_cryptoEnabled && m_publicKeyBytes) {
        oss << ";PUBKEY:" << GetPublicKeyHex();
    }
    std::string payload = oss.str();
    if (m_cryptoEnabled && m_sharedSecret) {
        std::string hmacHex = SignMessage(payload);
        payload += ";HMAC:" + hmacHex;
    }

    // device 层直发,绕过 StaWifiMac EDCA bug,接收方走 PHY MonitorSnifferRx
    Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
    bool ok = m_staDevice->Send(p, Mac48Address::GetBroadcast(), 0x0800);
    if (m_ipRetryCount == 0) {
        BlindConnectApp::LogMessage("IP_REQUEST", Simulator::Now().GetSeconds(), m_currentDomainId, "STA");
        BlindConnectApp::LogExperimentEvent("IP_REQUEST", m_currentDomainId, "STA",
                                           m_pendingStaTxId, Ipv4Address("0.0.0.0"), "",
                                           "IP Request", std::string(m_currentSsid.PeekString()),
                                           m_currentSnr, m_currentHops);
    }
    std::cout << Simulator::Now().GetSeconds() << "s: [ReqStaIp] device->Send ok=" << ok
              << " payload=" << payload << std::endl;
}

void BlindConnectApp::RetryStaIp() {
    if (m_staState.ip != Ipv4Address("0.0.0.0")) return;
    if (m_ipRetryCount >= 3) {
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [RetryStaIp] 已达最大重试次数(3), 回退重新扫描" << std::endl;
        FallbackAndRescan();
        return;
    }
    m_ipRetryCount++;
    std::cout << Simulator::Now().GetSeconds()
              << "s: [RetryStaIp] 第" << m_ipRetryCount << "次重试..." << std::endl;
    RequestStaIp();
    m_ipRetryEvent = Simulator::Schedule(Seconds(2.0), &BlindConnectApp::RetryStaIp, this);
}

void BlindConnectApp::FallbackAndRescan() {
    std::cout << Simulator::Now().GetSeconds()
              << "s: [Fallback] IP分配失败, 重置网络状态并重新扫描评估" << std::endl;

    m_assignedIp = Ipv4Address("0.0.0.0");
    m_currentHops = 99;
    m_currentNetType = ScannedNodeInfo::TYPE_AP;
    m_currentSsid = Ssid("");

    Simulator::Cancel(m_ipRetryEvent);
    Simulator::Cancel(m_adhocIpRetryEvent);
    m_ipRetryCount = 0;
    m_adhocIpRetryCount = 0;
    m_pendingStaTxId = 0;
    m_pendingAdhocTxId = 0;
    m_pendingStaDomainId = 0;
    m_pendingAdhocDomainId = 0;

    if (m_broadcastSocket) {
        Simulator::Cancel(m_beaconEvent);
        m_broadcastSocket->Close();
        m_broadcastSocket = nullptr;
    }
    if (m_staIpSocket) {
        m_staIpSocket->Close();
        m_staIpSocket = nullptr;
    }

    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
    if (staMac) staMac->SetSsid(Ssid(""));

    Ptr<AdhocWifiMac> adhocMac = DynamicCast<AdhocWifiMac>(m_adhocDevice->GetMac());
    if (adhocMac) adhocMac->SetSsid(Ssid(""));

    if (!m_hopEvent.IsRunning()) {
        SwitchToNextChannel();
    }
    ScheduleEvaluate();
}

// --- Terminal: 处理 Adhoc IP_OFFER ---
void BlindConnectApp::HandleAdhocIpMessage(const std::string& payload) {
    std::cout << Simulator::Now().GetSeconds()
              << "s: [HandleAdhocIp] 处理 IP_OFFER, payload=" << payload << std::endl;
    Ipv4Address ip, gw;
    Ipv4Mask mask;
    size_t pos;
    if ((pos = payload.find("IP:")) != std::string::npos) {
        size_t endPos = payload.find(";", pos);
        ip = Ipv4Address(payload.substr(pos + 3, endPos - pos - 3).c_str());
    }
    if ((pos = payload.find("MASK:")) != std::string::npos) {
        size_t endPos = payload.find(";", pos);
        mask = Ipv4Mask(payload.substr(pos + 5, endPos - pos - 5).c_str());
    }
    if ((pos = payload.find("GW:")) != std::string::npos) {
        size_t endPos = payload.find(";", pos);
        gw = Ipv4Address(payload.substr(pos + 3, endPos - pos - 3).c_str());
    }
    if (ip == Ipv4Address::GetAny()) return;
    if (!IsExpectedAdhocOffer(payload)) {
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [HandleAdhocIp] IP_OFFER TXID过期, 忽略" << std::endl;
        BlindConnectApp::LogExperimentEvent("STALE_IP_OFFER", m_currentDomainId, "ADHOC",
                                       0, Ipv4Address("0.0.0.0"), "", "Stale Offer",
                                       "", 0.0, -1, "Adhoc offer txid mismatch");
        return;
    }

    {
        std::ostringstream ipKey; ipKey << ip << "_d" << (int)m_currentDomainId;
        BlindConnectApp::LogMessageOnce("IP_OFFER", Simulator::Now().GetSeconds(), ipKey.str(), m_currentDomainId, "ADHOC");
        BlindConnectApp::LogExperimentEvent("IP_OFFER", m_currentDomainId, "ADHOC",
                                           m_pendingAdhocTxId, ip, "", "IP Offer",
                                           std::string(m_currentSsid.PeekString()), m_currentSnr, m_currentHops);
    }
    m_pendingAdhocTxId = 0;

    // 验证 HMAC
    if (m_cryptoEnabled && m_sharedSecret) {
        size_t appPos = payload.find("TYPE:");
        std::string appPayload = (appPos != std::string::npos) ? payload.substr(appPos) : payload;
        std::string hmacHex = ExtractField(appPayload, "HMAC");
        if (!hmacHex.empty()) {
            size_t hmacPos = appPayload.find(";HMAC:");
            if (hmacPos != std::string::npos) {
                std::string msgBody = appPayload.substr(0, hmacPos);
                if (!VerifyMessage(msgBody, hmacHex)) {
                    std::cout << Simulator::Now().GetSeconds()
                              << "s: [安全警告] IP_OFFER HMAC验证失败!" << std::endl;
                    return;
                }
            }
        }
    }

    m_assignedIp = ip;
    m_assignedMask = mask;
    m_assignedGw = gw;

    // 更新Ad-Hoc软状态
    m_adhocState.ip = ip;
    m_adhocState.gw = gw;
    m_adhocState.mask = mask;

    // 取消Adhoc重试定时器
    Simulator::Cancel(m_adhocIpRetryEvent);
    m_adhocIpRetryCount = 0;

    ConfigureIpOnInterface(GetSocketAdhocDev(), ip, mask, gw);
    BlindConnectApp::LogExperimentEvent("CONFIGURED", m_currentDomainId, "ADHOC", 0, ip,
                                        "", "Configured", std::string(m_currentSsid.PeekString()),
                                        m_currentSnr, m_currentHops, "Adhoc interface configured");

    // 备份接口只保持控制面UP，不再硬编码同步业务IP。
    EnsureBothInterfacesUp();
    // 通知控制器注册双网卡 ARP（只记一条主导网卡的 AddArpEntry）
    if (m_ipAllocatedCallback) {
        std::ostringstream arpKey; arpKey << ip << "_d" << (int)m_currentDomainId;
        BlindConnectApp::LogMessageOnce("AddArpEntry", Simulator::Now().GetSeconds(), arpKey.str(), m_currentDomainId, "ADHOC");
        BlindConnectApp::LogExperimentEvent("ADD_ARP_ENTRY", m_currentDomainId, "ADHOC", 0, m_adhocState.ip,
                                           "", "Sync", std::string(m_currentSsid.PeekString()),
                                           m_currentSnr, m_currentHops, "Controller ARP update");
        m_ipAllocatedCallback(Mac48Address::ConvertFrom(GetSocketAdhocDev()->GetAddress()), m_adhocState.ip);
        if (m_staState.ip != Ipv4Address("0.0.0.0")) {
            m_ipAllocatedCallback(Mac48Address::ConvertFrom(GetSocketStaDev()->GetAddress()), m_staState.ip);
        }
    }

    // 发送确认（加2ms延迟，确保IP_CONFIRM时间戳在IP_OFFER之后）
    std::ostringstream oss;
    oss << "TYPE:IP_CONFIRM;"
        << "MAC:" << m_adhocDevice->GetAddress() << ";"
        << "IP:" << ip;
    std::string resp = oss.str();
    // 加2ms调度延迟，确保IP_CONFIRM时间戳在IP_OFFER之后
    Simulator::Schedule(MilliSeconds(2), &BlindConnectApp::SendDelayedIpConfirm, this,
                        resp, ip, m_currentDomainId);

    std::string domain = "?";
    if (ip.Get() >> 24 == 10) {
        uint32_t thirdOctet = (ip.Get() >> 8) & 0xFF;
        if (thirdOctet == 1) domain = "A";
        else if (thirdOctet == 2) domain = "B";
        else if (thirdOctet == 3) domain = "C";
    }
    std::cout << "\n╔══════════════════════════════════════════╗\n"
              << "║       终端双网卡配置成功 (节点"
              << GetNode()->GetId() << " → 域" << domain << ")      ║\n"
              << "╠══════════════════════════════════════════╣\n"
              << "║  时间  : " << Simulator::Now().GetSeconds() << " s\n"
              << "║  Adhoc IP: " << ip << "\n"
              << "║  掩码  : " << mask << "\n"
              << "║  网关  : " << gw << "\n";
    // 打印 STA IP
    Ptr<NetDevice> staDev = GetSocketStaDev();
    Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
    int32_t staIfIdx = ipv4->GetInterfaceForDevice(staDev);
    if (staIfIdx >= 0 && ipv4->GetNAddresses(staIfIdx) > 0) {
        Ipv4Address staIp = ipv4->GetAddress(staIfIdx, 0).GetLocal();
        std::cout << "║  STA IP : " << staIp << "\n";
    }
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;
}

// --- Terminal: 接收 STA IP_OFFER ---
void BlindConnectApp::HandleStaIpRead(Ptr<Socket> socket) {
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from))) {
        uint8_t buf[512] = {0};
        uint32_t size = std::min((uint32_t)packet->GetSize(), (uint32_t)511);
        packet->CopyData(buf, size);
        std::string payload((char*)buf, size);

        if (payload.find("TYPE:IP_OFFER") == std::string::npos) continue;

        Ipv4Address ip, gw;
        Ipv4Mask mask;
        size_t pos;
        if ((pos = payload.find("IP:")) != std::string::npos) {
            size_t endPos = payload.find(";", pos);
            ip = Ipv4Address(payload.substr(pos + 3, endPos - pos - 3).c_str());
        }
        if ((pos = payload.find("MASK:")) != std::string::npos) {
            size_t endPos = payload.find(";", pos);
            mask = Ipv4Mask(payload.substr(pos + 5, endPos - pos - 5).c_str());
        }
        if ((pos = payload.find("GW:")) != std::string::npos) {
            size_t endPos = payload.find(";", pos);
            gw = Ipv4Address(payload.substr(pos + 3, endPos - pos - 3).c_str());
        }
        if (ip == Ipv4Address::GetAny()) continue;
        if (!IsExpectedStaOffer(payload)) {
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [HandleStaIp] IP_OFFER TXID过期, 忽略" << std::endl;
            BlindConnectApp::LogExperimentEvent("STALE_IP_OFFER", m_currentDomainId, "STA",
                                                     0, Ipv4Address("0.0.0.0"), "", "Stale Offer",
                                                     "", 0.0, -1, "STA offer txid mismatch");
            continue;
        }

        {
            std::ostringstream ipKey; ipKey << ip << "_d" << (int)m_currentDomainId;
            BlindConnectApp::LogMessageOnce("IP_OFFER", Simulator::Now().GetSeconds(), ipKey.str(), m_currentDomainId, "STA");
            BlindConnectApp::LogExperimentEvent("IP_OFFER", m_currentDomainId, "STA",
                                               m_pendingStaTxId, ip, "", "IP Offer",
                                               std::string(m_currentSsid.PeekString()), m_currentSnr, m_currentHops);
        }
        m_pendingStaTxId = 0;

        m_assignedIp = ip;
        m_assignedMask = mask;
        m_assignedGw = gw;

        // 取消重试定时器
        Simulator::Cancel(m_ipRetryEvent);
        m_ipRetryCount = 0;

        ConfigureIpOnInterface(GetSocketStaDev(), ip, mask, gw);
        BlindConnectApp::LogExperimentEvent("CONFIGURED", m_currentDomainId, "STA", 0, ip,
                                            "", "Configured", std::string(m_currentSsid.PeekString()),
                                            m_currentSnr, m_currentHops, "STA interface configured");

        // 更新STA软状态
        m_staState.ip = ip;
        m_staState.gw = gw;
        m_staState.mask = mask;

        // 备份接口只保持控制面UP，不再硬编码同步业务IP。
        // AdHoc 网卡不关闭: 控制面常活，仅 dataSleep
        EnsureBothInterfacesUp();
        // 通知控制器注册双网卡 ARP（只记一条 STA 主导网卡的）
        if (m_ipAllocatedCallback) {
            BlindConnectApp::LogMessage("AddArpEntry", Simulator::Now().GetSeconds(), m_currentDomainId, "STA");
            BlindConnectApp::LogExperimentEvent("ADD_ARP_ENTRY", m_currentDomainId, "STA", 0, m_staState.ip,
                                                               "", "Sync", std::string(m_currentSsid.PeekString()),
                                                               m_currentSnr, m_currentHops, "Controller ARP update");
            m_ipAllocatedCallback(Mac48Address::ConvertFrom(GetSocketStaDev()->GetAddress()), m_staState.ip);
            if (m_adhocState.ip != Ipv4Address("0.0.0.0")) {
                m_ipAllocatedCallback(Mac48Address::ConvertFrom(GetSocketAdhocDev()->GetAddress()), m_adhocState.ip);
            }
        }

        {
            std::string domain = "?";
            if (ip.Get() >> 24 == 10) {
                uint32_t secondOctet = (ip.Get() >> 16) & 0xFF;
                if (secondOctet == 1 || secondOctet == 100) domain = "A";
                else if (secondOctet == 2 || secondOctet == 101) domain = "B";
                else if (secondOctet == 3 || secondOctet == 100) domain = "C";
            }
            std::cout << "\n╔══════════════════════════════════════════╗\n"
                      << "║       终端双网卡配置成功 (节点"
                      << GetNode()->GetId() << " → 域" << domain << ")      ║\n"
                      << "╠══════════════════════════════════════════╣\n"
                      << "║  时间  : " << Simulator::Now().GetSeconds() << " s\n"
                      << "║  STA IP : " << ip << "\n"
                      << "║  掩码  : " << mask << "\n"
                      << "║  网关  : " << gw << "\n";
            // 打印Adhoc IP
            Ptr<NetDevice> adhocDev = GetSocketAdhocDev();
            Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
            int32_t adhocIfIdx = ipv4->GetInterfaceForDevice(adhocDev);
            if (adhocIfIdx >= 0 && ipv4->GetNAddresses(adhocIfIdx) > 0) {
                Ipv4Address adhocIp = ipv4->GetAddress(adhocIfIdx, 0).GetLocal();
                std::cout << "║  Adhoc IP: " << adhocIp << "\n";
            }
            std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;
        }

        m_staIpSocket->Close();
        m_staIpSocket = nullptr;
    }
}

// --- AP Server: 接收 IP_REQUEST，分配 IP ---
void BlindConnectApp::HandleApServerRead(Ptr<Socket> socket) {
    std::cout << Simulator::Now().GetSeconds() << "s: [ApServer] HandleApServerRead 被调用!" << std::endl;
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from))) {
        uint8_t buf[512] = {0};
        uint32_t size = std::min((uint32_t)packet->GetSize(), (uint32_t)511);
        packet->CopyData(buf, size);
        std::string payload((char*)buf, size);

        if (payload.find("TYPE:IP_REQUEST") == std::string::npos) continue;

        size_t pos = payload.find("MAC:");
        if (pos == std::string::npos) continue;
        size_t endPos = payload.find(";", pos);
        std::string macStr = payload.substr(pos + 4, endPos - pos - 4);
        Mac48Address mac = Mac48Address(macStr.c_str());
        std::string txid = ExtractField(payload, "TXID");

        Ipv4Address ip = AllocateIp(mac);
        if (ip == Ipv4Address::GetAny()) continue;

        std::ostringstream oss;
        oss << "TYPE:IP_OFFER;"
            << "MAC:" << mac << ";"
            << "IP:" << ip << ";"
            << "MASK:" << m_poolMask << ";"
            << "GW:" << m_poolBase;
        if (!txid.empty()) {
            oss << ";TXID:" << txid;
        }
        std::string resp = oss.str();
        Ptr<Packet> p = Create<Packet>((uint8_t*)resp.c_str(), resp.length());

        socket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 68));
        NS_LOG_INFO("AP Server sent IP_OFFER: " << ip << " to " << mac);
        // 通知 SDN 控制器注入 ARP
        if (!!m_ipAllocatedCallback) {
            m_ipAllocatedCallback(mac, ip);
        }
    }
}

// --- 通用: 配置 IP + 默认路由 ---
void BlindConnectApp::ConfigureIpOnInterface(Ptr<NetDevice> dev, Ipv4Address ip, Ipv4Mask mask, Ipv4Address gw) {
    Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
    int32_t ifIndex = ipv4->GetInterfaceForDevice(dev);
    if (ifIndex < 0) {
        // InternetStackHelper::Install 不自动为 WiFi 设备创建 Ipv4 接口
        ifIndex = ipv4->AddInterface(dev);
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [ConfigureIp] 为设备创建新Ipv4接口 ifIndex=" << ifIndex << std::endl;
    }

    while (ipv4->GetNAddresses(ifIndex) > 0) {
        ipv4->RemoveAddress(ifIndex, 0);
    }
    ipv4->AddAddress(ifIndex, Ipv4InterfaceAddress(ip, mask));
    ipv4->SetUp(ifIndex);

    if (gw != Ipv4Address("0.0.0.0")) {
        SetDefaultRouteVia(ipv4, ifIndex, gw);
    }
}

// --- 硬编码: 根据域ID配置 Adhoc 网卡 (domainId: 1=A, 2=B, 3=C) ---
void BlindConnectApp::ConfigureAdhocIpByDomain(uint32_t domainId) {
    Ptr<NetDevice> adhocDev = GetSocketAdhocDev();
    if (!adhocDev) return;

    Ipv4Address ip, gw;
    Ipv4Mask mask("255.255.255.0");

    switch (domainId) {
        case 1:  // 域 A
            ip = Ipv4Address("10.100.1.100");
            gw = Ipv4Address("10.100.1.1");
            break;
        case 2:  // 域 B
            ip = Ipv4Address("10.100.2.100");
            gw = Ipv4Address("10.100.2.1");
            break;
        case 3:  // 域 C
            ip = Ipv4Address("10.100.3.100");
            gw = Ipv4Address("10.100.3.1");
            break;
        default:
            return;
    }

    std::cout << Simulator::Now().GetSeconds()
              << "s: [ConfigureAdhocByDomain] 域" << domainId
              << " Adhoc网卡 IP=" << ip << " GW=" << gw << std::endl;

    ConfigureIpOnInterface(adhocDev, ip, mask, gw);

    // 更新Ad-Hoc软状态
    m_adhocState.ip = ip;
    m_adhocState.gw = gw;
    m_adhocState.mask = mask;
    m_adhocState.controlActive = true;

    // 兼容旧逻辑：仅当Ad-Hoc数据面激活时更新全局状态
    if (m_dataPlaneMode == DATA_PLANE_ADHOC) {
        m_assignedIp = ip;
        m_assignedMask = mask;
        m_assignedGw = gw;
    }
}

// --- 硬编码: 根据域ID配置 STA 网卡 (domainId: 1=A, 2=B, 3=C) ---
void BlindConnectApp::ConfigureStaIpByDomain(uint32_t domainId) {
    Ptr<NetDevice> staDev = GetSocketStaDev();
    if (!staDev) return;

    Ipv4Address ip, gw;
    Ipv4Mask mask("255.255.255.0");

    switch (domainId) {
        case 1:  // 域 A
            ip = Ipv4Address("10.1.1.100");
            gw = Ipv4Address("10.1.1.1");
            break;
        case 2:  // 域 B
            ip = Ipv4Address("10.2.1.100");
            gw = Ipv4Address("10.2.1.1");
            break;
        case 3:  // 域 C
            ip = Ipv4Address("10.3.1.100");
            gw = Ipv4Address("10.3.1.1");
            break;
        default:
            return;
    }

    std::cout << Simulator::Now().GetSeconds()
              << "s: [ConfigureStaByDomain] 域" << domainId
              << " STA网卡 IP=" << ip << " GW=" << gw << std::endl;

    ConfigureIpOnInterface(staDev, ip, mask, gw);

    // 更新STA软状态
    m_staState.ip = ip;
    m_staState.gw = gw;
    m_staState.mask = mask;
    m_staState.controlActive = true;

    // 兼容旧逻辑：更新m_assignedIp等变量（C域切换时使用）
    if (m_dataPlaneMode == DATA_PLANE_AP) {
        m_assignedIp = ip;
        m_assignedMask = mask;
        m_assignedGw = gw;
    }
}

// --- 软休眠相关方法实现 ---

void BlindConnectApp::EnsureBothInterfacesUp()
{
    Ptr<Node> node = GetNode();
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

    Ptr<NetDevice> staDev = GetSocketStaDev();
    int32_t staIfx = ipv4->GetInterfaceForDevice(staDev);
    if (staIfx >= 0 && !ipv4->IsUp(staIfx)) {
        ipv4->SetUp(staIfx);
        std::cout << Simulator::Now().GetSeconds() << "s: [EnsureBothUp] STA接口UP" << std::endl;
    }

    Ptr<NetDevice> adhocDev = GetSocketAdhocDev();
    int32_t adhocIfx = ipv4->GetInterfaceForDevice(adhocDev);
    if (adhocIfx >= 0 && !ipv4->IsUp(adhocIfx)) {
        ipv4->SetUp(adhocIfx);
        std::cout << Simulator::Now().GetSeconds() << "s: [EnsureBothUp] AdHoc接口UP" << std::endl;
    }
}

void BlindConnectApp::ConfigureDualIpForDomain(uint32_t domainId)
{
    // 同时配置当前域的双网卡IP
    ConfigureStaIpByDomain(domainId);
    ConfigureAdhocIpByDomain(domainId);

    // 更新当前域ID
    m_currentDomainId = domainId;

    // 确保两张接口都UP
    EnsureBothInterfacesUp();

    // 通知SDN控制器双网卡IP→MAC映射，使其他节点可以路由到移动节点
    if (m_ipAllocatedCallback) {
        Ptr<NetDevice> staDev = GetSocketStaDev();
        if (staDev && m_staState.ip != Ipv4Address("0.0.0.0")) {
            m_ipAllocatedCallback(Mac48Address::ConvertFrom(staDev->GetAddress()),
                                  m_staState.ip);
        }
        Ptr<NetDevice> adhocDev = GetSocketAdhocDev();
        if (adhocDev && m_adhocState.ip != Ipv4Address("0.0.0.0")) {
            m_ipAllocatedCallback(Mac48Address::ConvertFrom(adhocDev->GetAddress()),
                                  m_adhocState.ip);
        }
    }

    std::cout << Simulator::Now().GetSeconds()
              << "s: [ConfigureDualIp] 域" << domainId
              << " 双网卡配置完成: STA=" << m_staState.ip
              << " AdHoc=" << m_adhocState.ip << std::endl;
}

void BlindConnectApp::SetDefaultRoute(Ptr<NetDevice> dev, Ipv4Address gw)
{
    if (!dev || gw == Ipv4Address("0.0.0.0")) return;

    Ptr<Node> node = GetNode();
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    int32_t ifx = ipv4->GetInterfaceForDevice(dev);
    if (ifx < 0) return;

    SetDefaultRouteVia(ipv4, ifx, gw);

    std::cout << Simulator::Now().GetSeconds()
              << "s: [SetDefaultRoute] 默认路由 -> " << gw
              << " (iface=" << ifx << ")" << std::endl;
}

void BlindConnectApp::BindDataSocketsToActiveDevice(Ptr<NetDevice> dev)
{
    std::string iface = (dev == Ptr<NetDevice>(m_staDevice)) ? "STA" : "ADHOC";
    BlindConnectApp::LogExperimentEvent("DATA_SOCKET_BIND", m_currentDomainId, iface,
                                       0, m_assignedIp, "", "Socket Rebind",
                                       std::string(m_currentSsid.PeekString()), m_currentSnr, m_currentHops);
}

void BlindConnectApp::SetDataPlaneActive(DataPlaneMode mode)
{
    m_dataPlaneMode = mode;

    if (mode == DATA_PLANE_AP) {
        // AP数据面激活
        m_staState.dataActive = true;
        m_adhocState.dataActive = false;

        // 切换默认路由到STA
        SetDefaultRoute(m_staDevice, m_staState.gw);

        // 业务socket绑定到STA
        BindDataSocketsToActiveDevice(m_staDevice);

        if (m_staState.ip != Ipv4Address("0.0.0.0")) {
            m_assignedIp = m_staState.ip;
            m_assignedMask = m_staState.mask;
            m_assignedGw = m_staState.gw;
        }
        BlindConnectApp::LogExperimentEvent("DATA_PLANE_AP", m_currentDomainId, "STA", 0, m_assignedIp,
                                             "", "Data Plane Switch", std::string(m_currentSsid.PeekString()),
                                             m_currentSnr, m_currentHops, "STA data plane active");
        BlindConnectApp::LogExperimentEvent("ROUTE_UPDATED", m_currentDomainId, "STA", 0, m_assignedIp,
                                             "", "Route Updated", std::string(m_currentSsid.PeekString()),
                                             m_currentSnr, m_currentHops, "Default route via STA");

        std::cout << Simulator::Now().GetSeconds()
                  << "s: [SetDataPlaneActive] AP模式: STA数据激活, AdHoc数据静默" << std::endl;

    } else {
        // Ad-Hoc数据面激活
        m_adhocState.dataActive = true;
        m_staState.dataActive = false;

        // 切换默认路由到Ad-Hoc
        SetDefaultRoute(m_adhocDevice, m_adhocState.gw);

        // 业务socket绑定到Ad-Hoc
        BindDataSocketsToActiveDevice(m_adhocDevice);

        if (m_adhocState.ip != Ipv4Address("0.0.0.0")) {
            m_assignedIp = m_adhocState.ip;
            m_assignedMask = m_adhocState.mask;
            m_assignedGw = m_adhocState.gw;
        }
        BlindConnectApp::LogExperimentEvent("DATA_PLANE_ADHOC", m_currentDomainId, "ADHOC", 0, m_assignedIp,
                                                "", "Data Plane Switch", std::string(m_currentSsid.PeekString()),
                                                m_currentSnr, m_currentHops, "Adhoc data plane active");
        BlindConnectApp::LogExperimentEvent("ROUTE_UPDATED", m_currentDomainId, "ADHOC", 0, m_assignedIp,
                                                "", "Route Updated", std::string(m_currentSsid.PeekString()),
                                                m_currentSnr, m_currentHops, "Default route via Adhoc");

        std::cout << Simulator::Now().GetSeconds()
                  << "s: [SetDataPlaneActive] AdHoc模式: AdHoc数据激活, STA数据静默" << std::endl;
    }
}

uint32_t BlindConnectApp::GetDomainIdFromBestNode(const ScannedNodeInfo& node) const
{
    if (node.type == ScannedNodeInfo::TYPE_AP) {
        std::string ssidStr(node.ssid.PeekString());
        if (ssidStr == "A") return 1;
        else if (ssidStr == "B") return 2;
        else if (ssidStr == "C") return 3;
    } else {
        // Ad-Hoc类型，根据gateway IP判断域
        uint32_t secondOctet = (node.gateway.Get() >> 16) & 0xFF;
        if (secondOctet == 100) {
            uint32_t thirdOctet = (node.gateway.Get() >> 8) & 0xFF;
            if (thirdOctet == 1) return 1;
            else if (thirdOctet == 2) return 2;
            else if (thirdOctet == 3) return 3;
        }
    }
    return 0;
}

// ========== 加密相关方法实现 ==========

void BlindConnectApp::InitCrypto() {
    m_cryptoEnabled = true;
    NS_LOG_INFO("Crypto subsystem initialized for node " << GetNode()->GetId());
    std::cout << Simulator::Now().GetSeconds()
              << "s: 加密子系统初始化完成 (节点" << GetNode()->GetId() << ")" << std::endl;
}

void BlindConnectApp::GenerateChebyshevKeypair() {
    if (!m_cryptoEnabled) return;

    try {
        // 生成128位素数作为模数
        mpz_class prime = generate_128bit_prime();

        // 获取模数的 hex 表示
        size_t primeLen = (mpz_sizeinbase(prime.get_mpz_t(), 2) + 7) / 8;
        unsigned char* primeBytes = new unsigned char[primeLen];
        size_t count;
        mpz_export(primeBytes, &count, 1, 1, 0, 0, prime.get_mpz_t());
        m_modulusHex = ExtendedChebyshevKeyExchange::bytes_to_hex(primeBytes, count);
        delete[] primeBytes;

        // 创建密钥交换实例
        m_keyExchange = new ExtendedChebyshevKeyExchange(prime);

        // 生成私钥
        m_privateKeyBytes = m_keyExchange->generate_private_key_bytes(&m_privateKeyLen);

        // 使用基点 x=2 计算公钥
        mpz_class x = 2;
        size_t xLen = (mpz_sizeinbase(x.get_mpz_t(), 2) + 7) / 8;
        unsigned char* xBytes = new unsigned char[xLen];
        mpz_export(xBytes, &count, 1, 1, 0, 0, x.get_mpz_t());

        m_publicKeyBytes = m_keyExchange->compute_public_key_bytes(
            m_privateKeyBytes, m_privateKeyLen, xBytes, count, &m_publicKeyLen);
        delete[] xBytes;

        NS_LOG_INFO("Chebyshev keypair generated");
        std::cout << Simulator::Now().GetSeconds()
                  << "s: Chebyshev密钥对生成完成 (节点" << GetNode()->GetId() << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Chebyshev keypair generation failed: " << e.what() << std::endl;
        m_cryptoEnabled = false;
    }
}

bool BlindConnectApp::ComputeSharedSecretFromGateway(
    const std::string& modulusHex, const std::string& pubKeyHex) {
    if (!m_cryptoEnabled) return false;

    try {
        // 解析模数
        size_t modLen = 0;
        unsigned char* modBytes =
            ExtendedChebyshevKeyExchange::hex_to_bytes(modulusHex, &modLen);
        if (!modBytes) return false;

        // 根据模数创建密钥交换实例 (先清理旧的)
        if (m_keyExchange) { delete m_keyExchange; m_keyExchange = nullptr; }

        mpz_class prime;
        mpz_import(prime.get_mpz_t(), modLen, 1, 1, 0, 0, modBytes);
        m_keyExchange = new ExtendedChebyshevKeyExchange(prime);
        ExtendedChebyshevKeyExchange::free_bytes(modBytes);
        m_modulusHex = modulusHex;

        // 生成自己的密钥对
        if (m_privateKeyBytes) { delete[] m_privateKeyBytes; m_privateKeyBytes = nullptr; }
        if (m_publicKeyBytes)  { delete[] m_publicKeyBytes;  m_publicKeyBytes = nullptr; }

        m_privateKeyBytes = m_keyExchange->generate_private_key_bytes(&m_privateKeyLen);

        mpz_class x = 2;
        size_t xLen = (mpz_sizeinbase(x.get_mpz_t(), 2) + 7) / 8;
        unsigned char* xBytes = new unsigned char[xLen];
        size_t count;
        mpz_export(xBytes, &count, 1, 1, 0, 0, x.get_mpz_t());
        m_publicKeyBytes = m_keyExchange->compute_public_key_bytes(
            m_privateKeyBytes, m_privateKeyLen, xBytes, count, &m_publicKeyLen);
        delete[] xBytes;

        // 解析网关公钥并计算共享密钥
        size_t gwPubLen = 0;
        unsigned char* gwPubBytes =
            ExtendedChebyshevKeyExchange::hex_to_bytes(pubKeyHex, &gwPubLen);
        if (!gwPubBytes) return false;

        size_t secretLen = 0;
        unsigned char* secret = m_keyExchange->compute_shared_secret_bytes(
            m_privateKeyBytes, m_privateKeyLen,
            gwPubBytes, gwPubLen, &secretLen);

        ExtendedChebyshevKeyExchange::free_bytes(gwPubBytes);

        if (secret && secretLen >= 16) {
            if (m_sharedSecret) { delete[] m_sharedSecret; }
            m_sharedSecret = new unsigned char[16];
            memcpy(m_sharedSecret, secret, 16);
            m_sharedSecretLen = 16;
            ExtendedChebyshevKeyExchange::free_bytes(secret);
            m_peerPubKeyHex = pubKeyHex;
            return true;
        }
        if (secret) ExtendedChebyshevKeyExchange::free_bytes(secret);
        return false;
    } catch (const std::exception& e) {
        std::cerr << "ComputeSharedSecretFromGateway failed: " << e.what() << std::endl;
        return false;
    }
}

std::string BlindConnectApp::SignMessage(const std::string& msgBody) {
    if (!m_sharedSecret || m_sharedSecretLen == 0) return "";

    try {
        // 使用 HMAC-SHA256 前64位作为签名
        unsigned char* hmac = CryptoUtils::hmacSha256First64Bits(
            msgBody.c_str(), msgBody.length(),
            m_sharedSecret, m_sharedSecretLen);

        std::string hex = CryptoUtils::bytesToHex(hmac, 8);
        CryptoUtils::freeBytes(hmac);
        return hex;
    } catch (const std::exception& e) {
        std::cerr << "SignMessage failed: " << e.what() << std::endl;
        return "";
    }
}

bool BlindConnectApp::VerifyMessage(const std::string& msgBody, const std::string& hmacHex) {
    std::string expected = SignMessage(msgBody);
    return !expected.empty() && expected == hmacHex;
}

std::string BlindConnectApp::GetPublicKeyHex() const {
    if (!m_publicKeyBytes || m_publicKeyLen == 0) return "";
    return ExtendedChebyshevKeyExchange::bytes_to_hex(m_publicKeyBytes, m_publicKeyLen);
}

std::string BlindConnectApp::GetModulusHex() const {
    return m_modulusHex;
}

std::string BlindConnectApp::ExtractField(const std::string& payload, const std::string& key) const {
    std::string searchKey = key + ":";
    size_t pos = payload.find(searchKey);
    if (pos == std::string::npos) return "";

    pos += searchKey.length();
    size_t endPos = payload.find(";", pos);
    if (endPos == std::string::npos) {
        endPos = payload.length();  // 最后一个字段可能没有分号结尾
    }

    std::string value = payload.substr(pos, endPos - pos);
    // 去除末尾的 null 字节及其他非打印字符 (协议填充)
    while (!value.empty() && (value.back() == '\0' || !std::isprint(static_cast<unsigned char>(value.back())))) {
        value.pop_back();
    }
    return value;
}

// --- 延迟发送: AP_SERVER IP_OFFER（+2ms模拟处理+传播时延）---
void BlindConnectApp::SendDelayedStaIpOffer(std::string resp, Mac48Address dstMac) {
    Ptr<Packet> p = Create<Packet>((uint8_t*)resp.c_str(), resp.length());
    m_staDevice->Send(p, dstMac, 0x0800);
    std::cout << Simulator::Now().GetSeconds()
              << "s: [ApServer] 延迟发送 IP_OFFER -> " << dstMac << std::endl;
}

// --- 延迟发送: Terminal IP_CONFIRM（+2ms确保在IP_OFFER之后）---
void BlindConnectApp::SendDelayedIpConfirm(std::string resp, Ipv4Address ip, uint32_t domId) {
    Ptr<Packet> p = Create<Packet>((uint8_t*)resp.c_str(), resp.length());
    {
        std::ostringstream confKey; confKey << ip << "_d" << (int)domId;
        BlindConnectApp::LogMessageOnce("IP_CONFIRM", Simulator::Now().GetSeconds(), confKey.str(), domId, "ADHOC");
        BlindConnectApp::LogExperimentEvent("IP_CONFIRM", domId, "ADHOC", 0, ip, "",
                                           "IP Confirm", std::string(m_currentSsid.PeekString()),
                                           m_currentSnr, m_currentHops);
    }
    if (m_broadcastSocket) {
        m_broadcastSocket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 9));
    } else {
        m_adhocDevice->Send(p, Mac48Address::GetBroadcast(), 0x0800);
    }
    std::cout << Simulator::Now().GetSeconds()
              << "s: [Terminal] 延迟发送 IP_CONFIRM" << std::endl;
}

} // namespace ns3
