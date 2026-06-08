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
#include <iostream>
//
namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BlindConnectApp");
NS_OBJECT_ENSURE_REGISTERED(BlindConnectApp);

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
      m_currentChIdx(0),
      m_localHops(99),
      m_poolBase("0.0.0.0"),
      m_poolStart("0.0.0.0"),
      m_poolEnd("0.0.0.0"),
      m_poolMask("255.255.255.0"),
      m_assignedIp("0.0.0.0"),
      m_assignedMask("255.255.255.0"),
      m_assignedGw("0.0.0.0"),
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

    Ipv4Address ip = AllocateIp(mac);
    if (ip == Ipv4Address::GetAny()) return;

    std::ostringstream oss;
    oss << "TYPE:IP_OFFER;"
        << "MAC:" << mac << ";"
        << "IP:" << ip << ";"
        << "MASK:" << m_poolMask << ";"
        << "GW:" << m_poolBase;
    std::string resp = oss.str();
    Ptr<Packet> p = Create<Packet>((uint8_t*)resp.c_str(), resp.length());
    m_staDevice->Send(p, mac, 0x0800);

    std::cout << Simulator::Now().GetSeconds()
              << "s: [ApServer-Sniffer] 发送 IP_OFFER: " << ip << " -> " << mac << std::endl;

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
                        m_assignedIp = ip;
                        m_assignedMask = mask;
                        m_assignedGw = gw;
                        Simulator::Cancel(m_ipRetryEvent);
                        m_ipRetryCount = 0;
                        ConfigureIpOnInterface(GetSocketStaDev(), ip, mask, gw);
                        std::cout << Simulator::Now().GetSeconds() << "s: [IP-SWITCH] STA IP 配置完成 "
                                  << "IP=" << ip << " Mask=" << mask << " GW=" << gw << std::endl;
                        std::cout << "============================================\n" << std::endl;
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

    ScannedNodeInfo info;
    info.type = ScannedNodeInfo::TYPE_AP;
    info.ssid = beacon.GetSsid();
    info.snr = snr.signal;
    info.hopsToGw = 0;
    info.gateway = Ipv4Address("10.2.1.1");
    info.load = 0.0;
    info.minEnergy = 1.0;
    info.secure = false;
    info.nodes = 1;
    info.channelFreqMhz = channelFreqMhz;

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

    double wRssi = 0.50, wHops = 0.15, wLoad = 0.0, wEnergy = 0.25, wSec = 0.10;
    return wRssi * rssiNorm
           - wHops * hopsNorm
           - wLoad * load
           + wEnergy * energy
           + wSec * secBonus;
}

void BlindConnectApp::EvaluateAndSwitch() {
    // 防止频繁切换导致PHY状态断言崩溃 (最小切换间隔2秒)
    if (Simulator::Now() - m_lastSwitchTime < Seconds(2.0)) {
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

    ScannedNodeInfo bestNode = m_neighborRadar[0];
    double bestScore = CalculateScore(bestNode);

    bool currentAlive = false;
    for (const auto& n : m_neighborRadar) {
        if (n.type == m_currentNetType && n.ssid == m_currentSsid) {
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

    double threshold = 0.02;
    NS_LOG_UNCOND("当前网络评分:" << currentScore << " 最优候选评分:" << bestScore);

    // 同网络跳过：类型和SSID都相同不切换
    bool sameNetwork = (bestNode.type == m_currentNetType &&
                        bestNode.ssid == m_currentSsid);
    if (!sameNetwork && (bestScore > currentScore + threshold || m_currentHops == 99)) {
        std::string targetName;
        if (bestNode.type == ScannedNodeInfo::TYPE_AP) {
            targetName = "AP域 (" + std::string(bestNode.ssid.PeekString()) + ")";
        } else {
            targetName = "Adhoc域 (到网关" + std::to_string(bestNode.hopsToGw) + "跳)";
        }
        NS_LOG_UNCOND(">>> 切换至 " << targetName << " SNR:" << bestNode.snr << " Hops:" << bestNode.hopsToGw);
        ExecuteSwitch(bestNode);
    }

    m_neighborRadar.clear();
    ScheduleEvaluate();
}

void BlindConnectApp::ExecuteSwitch(const ScannedNodeInfo& bestNode) {
    m_lastSwitchTime = Simulator::Now();
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

    if (bestNode.type == ScannedNodeInfo::TYPE_AP) {
        // 停止跳频，锁定到 AP 信道
        Simulator::Cancel(m_hopEvent);
        // 不手动锁PHY信道——让StaWifiMac的扫描自己管理，否则EDCA发射会异常
        Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
        if (staMac) {
            staMac->SetSsid(bestNode.ssid);
        }

        // 断开 Adhoc 连接：清空 Adhoc SSID（PHY MonitorSnifferRx 继续监听）
        Ptr<AdhocWifiMac> adhocMac = DynamicCast<AdhocWifiMac>(m_adhocDevice->GetMac());
        if (adhocMac) {
            adhocMac->SetSsid(Ssid(""));
        }

        // 离开旧网络时释放 IP（Adhoc→AP 或 AP→AP）
        if (m_assignedIp != Ipv4Address("0.0.0.0")) {
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [ExecuteSwitch] 释放旧IP=" << m_assignedIp
                      << " (进入AP域)" << std::endl;
            if (m_currentNetType == ScannedNodeInfo::TYPE_ADHOC
                && m_broadcastSocket) {
                std::ostringstream oss;
                oss << "TYPE:IP_RELEASE;"
                    << "MAC:" << m_adhocDevice->GetAddress() << ";"
                    << "IP:" << m_assignedIp;
                std::string payload = oss.str();
                Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
                m_broadcastSocket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 9));
            }
            m_assignedIp = Ipv4Address("0.0.0.0");
        }
        // 停 Adhoc 广播 socket
        if (m_broadcastSocket) {
            Simulator::Cancel(m_beaconEvent);
            m_broadcastSocket->Close();
            m_broadcastSocket = nullptr;
        }

        m_currentNetType = ScannedNodeInfo::TYPE_AP;
        m_currentSsid = bestNode.ssid;
        m_currentHops = 0;
        m_currentSnr = bestNode.snr;

        // 等待 STA 关联完成后再请求 IP（延迟 3.0s，给足 Auth+Assoc 时间）
        Simulator::Schedule(Seconds(3.0), &BlindConnectApp::RequestStaIp, this);
        // 启动重试: 5s后若仍未获取则重试 (3s关联 + 2s等待响应)
        m_ipRetryCount = 0;
        Simulator::Cancel(m_ipRetryEvent);
        m_ipRetryEvent = Simulator::Schedule(Seconds(5.0), &BlindConnectApp::RetryStaIp, this);
        // 启动周期性重扫其他信道
        m_apChannelFreqMhz = bestNode.channelFreqMhz;
        m_apChannelNum = (bestNode.channelFreqMhz > 0)
            ? (bestNode.channelFreqMhz - 2407) / 5 : 1;
        ScheduleApRescan();

    } else {
        // 切换到 Adhoc 域，停止 AP 重扫，恢复跳频扫描
        Simulator::Cancel(m_rescanEvent);
        m_inApRescan = false;
        if (!m_hopEvent.IsRunning()) {
            SwitchToNextChannel();
        }

        // 断开 AP 连接：清空 STA SSID（PHY MonitorSnifferRx 继续监听）
        Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
        if (staMac) {
            staMac->SetSsid(Ssid(""));
        }

        // 切换 Adhoc PHY 到目标域的信道对象（方案A: 解决跨YansWifiChannel通信断裂）
        {
            std::string ssidKey = bestNode.ssid.PeekString();
            auto chanIt = m_adhocChannels.find(ssidKey);
            if (chanIt != m_adhocChannels.end()) {
                Ptr<WifiPhy> adhocPhy = m_adhocDevice->GetPhy();
                Ptr<YansWifiPhy> yansPhy = DynamicCast<YansWifiPhy>(adhocPhy);
                // 仅在信道对象确实不同时才切换（避免冗余 SetChannel 引发PHY状态异常）
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

        // SSID 已在 MAC Helper 创建时设为 "Adhoc-Net"，IBSS 信标始终发送
        // 运行时不再改动 SSID，避免触发 IBSS 重新同步导致数据帧阻塞
        Ptr<AdhocWifiMac> adhocMac = DynamicCast<AdhocWifiMac>(m_adhocDevice->GetMac());

        // 清理 STA 侧 IP socket
        if (m_staIpSocket) {
            m_staIpSocket->Close();
            m_staIpSocket = nullptr;
        }

        // 离开 AP 域时清除旧 IP（避免误判 alreadyHasIp）
        if (m_currentNetType != ScannedNodeInfo::TYPE_ADHOC
            && m_assignedIp != Ipv4Address("0.0.0.0")) {
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [ExecuteSwitch] 清除旧网络IP=" << m_assignedIp
                      << " (进入Adhoc域)" << std::endl;
            m_assignedIp = Ipv4Address("0.0.0.0");
        }

        bool alreadyHasIp = (m_assignedIp != Ipv4Address("0.0.0.0"));

        m_currentNetType = ScannedNodeInfo::TYPE_ADHOC;
        m_currentSsid = bestNode.ssid;
        m_currentHops = bestNode.hopsToGw;
        m_currentSnr = bestNode.snr;

        if (!m_broadcastSocket) {
            m_broadcastSocket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
            m_broadcastSocket->SetAllowBroadcast(true);
            m_broadcastSocket->BindToNetDevice(GetSocketAdhocDev());
            m_localHops = bestNode.hopsToGw;
            SendPseudoBeacon();
        }

        if (!alreadyHasIp) {
            m_ipRetryCount = 0;
            Simulator::Cancel(m_ipRetryEvent);
            // 延迟 500ms 发送首次 IP 请求，给 Adhoc MAC 充足时间建立IBSS并开始信标
            Simulator::Schedule(MilliSeconds(500), &BlindConnectApp::RequestAdhocIp, this);
            m_ipRetryEvent = Simulator::Schedule(Seconds(2.5), &BlindConnectApp::RetryAdhocIp, this);
        } else {
            // 已通过密钥协商路径获取IP，只更新路由
            std::cout << Simulator::Now().GetSeconds()
                      << "s: [ExecuteSwitch] Adhoc IP已分配(" << m_assignedIp
                      << "), 跳过重复请求" << std::endl;
            ConfigureIpOnInterface(GetSocketAdhocDev(), m_assignedIp, m_assignedMask, m_assignedGw);
        }
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
    if (!m_broadcastSocket || !m_adhocDevice) return;

    // 使用受限广播 255.255.255.255 (BindToNetDevice 配合此地址可正常路由)
    Ipv4Address dest = Ipv4Address("255.255.255.255");

    std::ostringstream oss;
    oss << "TYPE:IP_REQUEST;"
        << "MAC:" << m_adhocDevice->GetAddress();

    if (m_cryptoEnabled && m_publicKeyBytes) {
        oss << ";PUBKEY:" << GetPublicKeyHex();
    }

    std::string payload = oss.str();

    if (m_cryptoEnabled && m_sharedSecret) {
        std::string hmacHex = SignMessage(payload);
        payload += ";HMAC:" + hmacHex;
    }

    Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
    int sent = m_broadcastSocket->SendTo(p, 0, InetSocketAddress(dest, 9));
    std::cout << Simulator::Now().GetSeconds() << "s: [ReqAdhocIp] SendTo=" << sent
              << " dest=" << dest << " payload=" << payload << std::endl;

    NS_LOG_INFO("Sent Adhoc IP_REQUEST to " << dest);
}

void BlindConnectApp::RetryAdhocIp() {
    if (m_assignedIp != Ipv4Address("0.0.0.0")) return;
    if (m_ipRetryCount >= 3) {
        std::cout << Simulator::Now().GetSeconds()
                  << "s: [RetryAdhocIp] 已达最大重试次数(3), 回退重新扫描" << std::endl;
        FallbackAndRescan();
        return;
    }
    m_ipRetryCount++;
    std::cout << Simulator::Now().GetSeconds()
              << "s: [RetryAdhocIp] 第" << m_ipRetryCount << "次重试..." << std::endl;
    if (!m_broadcastSocket || !m_adhocDevice) return;
    std::ostringstream oss;
    oss << "TYPE:IP_REQUEST;"
        << "MAC:" << m_adhocDevice->GetAddress();
    if (m_cryptoEnabled && m_publicKeyBytes) {
        oss << ";PUBKEY:" << GetPublicKeyHex();
    }
    std::string payload = oss.str();
    if (m_cryptoEnabled && m_sharedSecret) {
        std::string hmacHex = SignMessage(payload);
        payload += ";HMAC:" + hmacHex;
    }
    Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
    m_broadcastSocket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 9));
    // 调度下一次重试
    m_ipRetryEvent = Simulator::Schedule(Seconds(2.0), &BlindConnectApp::RetryAdhocIp, this);
}

void BlindConnectApp::RequestStaIp() {
    if (!m_staDevice) return;
    Ptr<StaWifiMac> staMac = DynamicCast<StaWifiMac>(m_staDevice->GetMac());
    bool assoc = staMac ? staMac->IsAssociated() : false;
    std::cout << Simulator::Now().GetSeconds() << "s: [ReqStaIp] IsAssociated=" << assoc << std::endl;

    if (m_staIpSocket) { m_staIpSocket->Close(); }
    m_staIpSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    m_staIpSocket->SetAllowBroadcast(true);
    m_staIpSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 68));
    m_staIpSocket->SetRecvCallback(MakeCallback(&BlindConnectApp::HandleStaIpRead, this));

    std::ostringstream oss;
    oss << "TYPE:IP_REQUEST;"
        << "MAC:" << m_staDevice->GetAddress();
    std::string payload = oss.str();
    Ptr<Packet> p = Create<Packet>((uint8_t*)payload.c_str(), payload.length());
    int sent = m_staIpSocket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 67));
    std::cout << Simulator::Now().GetSeconds() << "s: [ReqStaIp] SendTo=" << sent << std::endl;
}

void BlindConnectApp::RetryStaIp() {
    if (m_assignedIp != Ipv4Address("0.0.0.0")) return;
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
    m_ipRetryCount = 0;

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

    // 取消重试定时器
    Simulator::Cancel(m_ipRetryEvent);
    m_ipRetryCount = 0;

    ConfigureIpOnInterface(GetSocketAdhocDev(), ip, mask, gw);

    // 发送确认
    std::ostringstream oss;
    oss << "TYPE:IP_CONFIRM;"
        << "MAC:" << m_adhocDevice->GetAddress() << ";"
        << "IP:" << ip;
    std::string resp = oss.str();
    Ptr<Packet> p = Create<Packet>((uint8_t*)resp.c_str(), resp.length());
    m_broadcastSocket->SendTo(p, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), 9));

    std::cout << Simulator::Now().GetSeconds() << "s: [IP-SWITCH] Adhoc IP 配置完成 "
              << "IP=" << ip << " Mask=" << mask << " GW=" << gw
              << " (接口=" << GetSocketAdhocDev()->GetAddress() << ")" << std::endl;
    std::cout << "============================================\n" << std::endl;
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

        m_assignedIp = ip;
        m_assignedMask = mask;
        m_assignedGw = gw;

        // 取消重试定时器
        Simulator::Cancel(m_ipRetryEvent);
        m_ipRetryCount = 0;

        ConfigureIpOnInterface(GetSocketStaDev(), ip, mask, gw);

        std::cout << Simulator::Now().GetSeconds() << "s: [IP-SWITCH] STA IP 配置完成 "
                  << "IP=" << ip << " Mask=" << mask << " GW=" << gw
                  << " (接口=" << GetSocketStaDev()->GetAddress() << ")" << std::endl;
        std::cout << "============================================\n" << std::endl;

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

        Ipv4Address ip = AllocateIp(mac);
        if (ip == Ipv4Address::GetAny()) continue;

        std::ostringstream oss;
        oss << "TYPE:IP_OFFER;"
            << "MAC:" << mac << ";"
            << "IP:" << ip << ";"
            << "MASK:" << m_poolMask << ";"
            << "GW:" << m_poolBase;
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
    if (ifIndex < 0) return;

    while (ipv4->GetNAddresses(ifIndex) > 0) {
        ipv4->RemoveAddress(ifIndex, 0);
    }
    ipv4->AddAddress(ifIndex, Ipv4InterfaceAddress(ip, mask));
    ipv4->SetUp(ifIndex);

    if (gw != Ipv4Address("0.0.0.0")) {
        SetDefaultRouteVia(ipv4, ifIndex, gw);
    }
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

} // namespace ns3
