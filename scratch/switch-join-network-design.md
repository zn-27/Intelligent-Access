# 切换入网设计与实现

## 1. 总体架构

系统为**无线融合 SDN 三域架构**，Switch 与 AP 合并，每个节点同时具备 AP 基础设施模式和 Ad-Hoc 自组网能力，通过 BlindConnectApp 实现终端的动态网络发现、切换与入网。

```
         ┌──────────┐  ┌──────────┐  ┌──────────┐
         │ SW1(域A)  │  │ SW3(域C)  │  │ SW2(域B)  │
         │ AP+Adhoc  │  │ AP+Adhoc │  │ AP+Adhoc │
         └────┬──┬───┘  └────┬──┬───┘  └────┬──┬───┘
      Port1  │  Port2  Port1│  │Port2  Port1│  │Port2
     (AP VND)│(Backhaul)  (AP│  │(Backhaul) (AP│  │(Backhaul)
              │   VND      VND)  │  VND      VND)  │  VND
              │    │        │   │   │        │   │   │
              │    └────────┼───┼───┼────────┼───┘   │
              │     Adhoc骨干三角互连  │        │       │
              │             │   │   │        │       │
         STA A×4       STA C×3 (无中心)  STA B×2
    (AP模式,ch1)     (Adhoc,ch11)     (AP模式,ch6)
```

**三种组网模式：**

| 模式 | 域A | 域B | 域C |
|------|-----|-----|-----|
| 入网方式 | AP基础设施 (有中心) | AP基础设施 (有中心) | Ad-Hoc 无中心，Gateway 伪信标 |
| STA WiFi | StaWifiMac UP | StaWifiMac UP | StaWifiMac DOWN, AdhocWifiMac UP |
| Switch 角色 | ROLE_AP_SERVER | ROLE_AP_SERVER | ROLE_GATEWAY |

---

## 2. BlindConnectApp 角色模型

BlindConnectApp 有四种角色 (`blind-connect-app.h:27`)：

| 角色 | 职责 | 部署位置 |
|------|------|---------|
| `ROLE_TERMINAL` | 移动终端：信道扫描、网络评估、切换、IP请求 | 移动节点 |
| `ROLE_GATEWAY` | Adhoc 域网关：伪信标广播、IP池管理、IP分配 | sw3 (域C Adhoc) |
| `ROLE_BACKBONE` | Adhoc 中继节点：转发伪信标，中继IP请求 | (预留，当前未使用) |
| `ROLE_AP_SERVER` | AP 域服务器：监听 DHCP 请求、IP池分配 | sw1, sw2 (域A/B AP) |

---

## 3. 终端扫描与网络发现

### 3.1 双模感知

终端具备两张 WiFi 网卡：

| 网卡 | MAC 类型 | 用途 |
|------|----------|------|
| `m_staDevice` | StaWifiMac | AP 信标监听 + IP 请求 + 跳频扫描 |
| `m_adhocDevice` | AdhocWifiMac | Adhoc 伪信标监听 + IP 请求/接收 |

### 3.2 跳频信道扫描 (blind-connect-app.cc:558-575)

终端在信道 `{1, 6, 11}` 之间循环跳频，每信道驻留 **200ms** (`DwellTime`)。

```
SwitchToNextChannel()
  ├─ 检查 PHY 状态 (TX/RX/Switching 时延迟 5ms 重试)
  ├─ SetOperatingChannel(targetCh)
  ├─ m_currentChIdx = (m_currentChIdx + 1) % 3
  └─ 调度下次跳频: +DwellTime
```

### 3.3 AP 信标接收 (blind-connect-app.cc:502-555)

终端通过 `MonitorSnifferRx` trace 监听 STA 网卡的所有帧：
- 识别标准 802.11 Beacon 帧 (frameCtrl == 0x80)
- 解析 MgtBeaconHeader，提取 SSID、SNR、信道频率
- 解析 IE 元素：BSS Load (id=11)、RSN 安全能力 (id=48)
- 构造 `ScannedNodeInfo{TYPE_AP, ...}` 加入候选列表

### 3.4 Adhoc 伪信标接收 (blind-connect-app.cc:299-498)

GATEWAY 通过 AP 网卡广播**自定义 UDP 伪信标**（非标准 802.11 Beacon），终端在 Adhoc 监听到后解析：

**伪信标格式：**
```
TYPE:IBSS_BEACON;SSID:Adhoc-C;MAC:xx:xx:xx:xx:xx:xx;IP:10.100.3.1;
TS:2.0;HOPS_TO_GW:0;NODES:4;AVG_LOAD:0.35;MIN_ENERGY:0.85;
SEC:CHEBYSHEV;MODULUS:abc...;PUBKEY:def...
```

终端解析后得到：网络类型 (`TYPE_ADHOC`)、到网关跳数、负载、能量、安全能力等。

---

## 4. 多属性网络评估与切换决策

### 4.1 评分函数 (blind-connect-app.cc:578-592)

```
Score = 0.35 × RSSI_norm  -  0.15 × Hops_norm  -  0.0 × Load  +  0.20 × Energy  +  0.30 × SecBonus
```

| 属性 | 归一化 | 权重 | 说明 |
|------|--------|------|------|
| RSSI | (snr+90)/60 映射到[0,1] | **0.35** | 信号强度 |
| Hops | min(hops,5)/5 | **0.15** | 到网关跳数（越小越好） |
| Load | 原始值 [0,1] | **0.0** | 负载（当前权重为0） |
| Energy | Adhoc取minEnergy, AP取1.0 | **0.20** | 域内最低能量 |
| Security | 有安全能力 +0.1 | **0.30** | 安全加分 |

### 4.2 切换决策流程 (blind-connect-app.cc:594-669)

```
EvaluateAndSwitch() [每秒调用一次]
  │
  ├─ 1. 收集 m_neighborRadar 候选节点
  ├─ 2. 按 Score 降序排列
  ├─ 3. 计算当前网络评分 currentScore
  ├─ 4. 判断 sameNetwork (类型+SSID相同不切换)
  └─ 5. 若 bestScore > currentScore + 0.02 或当前无网络:
       └─ ExecuteSwitch(bestNode)
```

**切换阈值**: 0.02（防止频繁乒乓切换）

---

## 5. 网络切换执行 (ExecuteSwitch)

### 5.1 切换到 AP 域 (blind-connect-app.cc:675-729)

```
ExecuteSwitch(AP节点)
  ├─ 停止跳频 → 锁定 AP 信道
  ├─ StaWifiMac.SetSsid(targetSsid)     ← 触发 802.11 关联
  ├─ AdhocWifiMac.SetSsid("")           ← 断开 Adhoc
  ├─ 若之前在 Adhoc: 发送 IP_RELEASE    ← 归还 IP
  ├─ 停止伪信标广播
  ├─ m_currentNetType = TYPE_AP
  ├─ 延迟 3s 调用 RequestStaIp()        ← 等待 Auth+Assoc 完成
  └─ 启动周期性重扫 ScheduleApRescan()   ← 每 5s 扫描其他信道
```

### 5.2 切换到 Adhoc 域 (blind-connect-app.cc:731-771)

```
ExecuteSwitch(Adhoc节点)
  ├─ 停止 AP 重扫
  ├─ 恢复跳频扫描
  ├─ StaWifiMac.SetSsid("")             ← 断开 AP
  ├─ AdhocWifiMac.SetSsid(targetSsid)   ← 加入 Adhoc 域
  ├─ 清理 STA IP socket
  ├─ m_currentNetType = TYPE_ADHOC
  ├─ 创建/复用广播 socket → SendPseudoBeacon()
  └─ RequestAdhocIp()                   ← 发起 IP 请求
```

### 5.3 AP 驻留期间周期性重扫 (blind-connect-app.cc:775-819)

当终端已在 AP 域时，每 **5s** 进行一次全信道扫描：
- 依次切换到 ch1→ch6→ch11，每个驻留 200ms
- 收集其他域的信标信息
- 扫描完成后切回原 AP 信道
- 为可能的切换提供候选数据

---

## 6. IP 分配协议

### 6.1 AP 域 IP 分配 (DHCP 风格)

```
Terminal (STA)                      AP_SERVER (Switch)
     │                                      │
     │  UDP:67, 广播                         │
     │  TYPE:IP_REQUEST;MAC:xx:xx:xx:xx:xx:xx │
     │─────────────────────────────────────→│
     │                                      │  AllocateIp(mac)
     │  UDP:68, 广播                         │  ← IP池 (10.x.1.100~200)
     │  TYPE:IP_OFFER;MAC:...;IP:...;MASK:...;GW:...
     │←─────────────────────────────────────│
     │                                      │  m_ipAllocatedCallback
     │  ConfigureIpOnInterface()            │  → SDN控制器ARP注入
     │  SetDefaultRouteVia(gw)              │
```

**代码路径:**
- Terminal: `RequestStaIp()` → 创建 socket 绑定 StaDevice → UDP:67 发送 IP_REQUEST
- AP_SERVER: `HandleApServerRead()` → 解析 MAC → `AllocateIp()` → UDP:68 回复 IP_OFFER
- Terminal: `HandleStaIpRead()` → 解析 IP/MASK/GW → `ConfigureIpOnInterface()`

**IP 池管理 (blind-connect-app.cc:178-213):**
- 从 `PoolStart` 到 `PoolEnd` 顺序分配
- `m_macToIp` 记录 MAC→IP 绑定，重复请求返回相同 IP
- Terminal 离开时发送 `IP_RELEASE` 归还

### 6.2 Adhoc 域 IP 分配 (通过 UDP 广播)

```
Terminal                          GATEWAY (sw3)
     │                                      │
     │  UDP:9, 广播 (AdhocDevice)            │
     │  TYPE:IP_REQUEST;MAC:...;PUBKEY:...   │
     │─────────────────────────────────────→│
     │                                      │  AllocateIp(mac)
     │  UDP:9, 广播 (从VND发送)              │  ← IP池 (10.100.3.10~50)
     │  TYPE:IP_OFFER;MAC:...;IP:...;MASK:...;GW:...
     │←─────────────────────────────────────│
     │                                      │  m_ipAllocatedCallback
     │  HandleAdhocIpMessage()              │  → SDN控制器ARP注入
     │  验证HMAC → ConfigureIpOnInterface() │
     │                                      │
     │  UDP:9                                │
     │  TYPE:IP_CONFIRM;MAC:...;IP:...      │
     │─────────────────────────────────────→│
```

**关键设计：GATEWAY 的 socket 绑定分离**
- 伪信标 & IP_REQUEST 监听通过原生 Adhoc 设备 (`m_adhocDevice`)，绑定物理 PHY
- IP_OFFER 回复通过 VND (`m_socketAdhocDevice`)，IP 地址在 VND 上
- 这解决了 Switch 节点 IP 在 VND、但信标需通过原生设备发送的矛盾

---

## 7. 安全机制

### 7.1 Chebyshev 密钥协商 (crypto-utils.cc)

基于扩展 Chebyshev 多项式的半群性质：`T_a(T_b(x)) = T_{ab}(x) = T_b(T_a(x))`

**密钥交换流程：**

```
GATEWAY                                TERMINAL
  │                                       │
  │ 生成128位素数模数 p                    │
  │ 生成私钥 a                            │
  │ 计算公钥 A = T_a(2) mod p             │
  │                                       │
  │ ── IBSS_BEACON(MODULUS:p, PUBKEY:A) ─→│
  │                                       │ 收到p和A
  │                                       │ 用 p 初始化 KeyExchange
  │                                       │ 生成私钥 b
  │                                       │ 计算公钥 B = T_b(2) mod p
  │                                       │ 计算共享密钥 K = T_b(A) mod p
  │                                       │
  │ ←── IP_REQUEST(PUBKEY:B) ─────────────│
  │                                       │
  │ 计算共享密钥 K = T_a(B) mod p          │
  │   = T_a(T_b(2)) = T_b(T_a(2)) = K     │  (双方一致)
```

**实现细节:**
- 模数 p: 128 位随机素数 (`generate_128bit_prime()`)
- 私钥: 128 位随机数
- Chebyshev 计算: 二进制倍增法 O(log n)，维护 `(T_k, T_{k+1})` 对
- 共享密钥: 取前 16 字节作为会话密钥 (用于后续 HMAC)

### 7.2 HMAC 消息认证 (blind-connect-app.cc:1180-1201)

- 算法: **HMAC-SHA256**，取其前 64 位 (8 字节)
- 密钥: Chebyshev 协商的共享密钥
- 消息体: 从 `TYPE:` 到 `HMAC:` 之前的内容
- 签名格式: `;HMAC:<hex64>`
- 验证: `VerifyMessage()` 重新计算并与原值比对

**签名的消息类型:**
- IBSS_BEACON (伪信标)
- IP_OFFER (IP 分配响应)
- IP_REQUEST (IP 请求)

---

## 8. Gateway 伪信标协议 (blind-connect-app.cc:216-285)

### 8.1 发送周期

每 **2s** + 随机抖动 (0~100ms) 发送一次，防止同步碰撞。

### 8.2 发送设备

优先通过 STA 设备 (`GetSocketStaDev()`) 发送，避免与 Adhoc 数据面冲突。UDP 广播端口 9。

### 8.3 跳数计算

- GATEWAY 自身: `m_localHops = 0`
- 非 GATEWAY 节点: 遍历邻居表中最小 hop，`m_localHops = minHops + 1`

### 8.4 邻居表管理

- 结构: `map<Ipv4Address, {lastSeen, hopsToGw, load, energy}>`
- 清理: 超过 3s 未更新的邻居自动删除

### 8.5 信标内容字段

| 字段 | 含义 |
|------|------|
| SSID | Adhoc 域标识 |
| MAC | 节点 MAC 地址 |
| IP | Adhoc 接口 IP |
| HOPS_TO_GW | 到网关的跳数 |
| NODES | 域内活跃节点数 |
| AVG_LOAD | 域内平均负载 |
| MIN_ENERGY | 域内最低能量 |
| SEC | 安全能力 (NONE / CHEBYSHEV) |
| MODULUS / PUBKEY | Chebyshev 模数和公钥 (SEC:CHEBYSHEV时) |
| HMAC | 消息签名 |

---

## 9. SDN 控制器集成

### 9.1 ARP 注入回调

IP 分配成功后，通过 `m_ipAllocatedCallback` 通知 SDN 控制器：

```cpp
// train4-new.cc:857-858
app->SetIpAllocatedCallback([controllerApp](Mac48Address mac, Ipv4Address ip) {
    controllerApp->AddArpEntry(ip, mac);
});
```

控制器将动态分配的 IP→MAC 映射注入 ARP 表，使交换机能够正确转发流量。

### 9.2 域 C Adhoc 邻居发现注入

域 C 初始为无中心模式，`SwitchProtocolInfoApp` 通过 Adhoc 邻居发现收集 STA 的 MAC/IP，并在 `t=2s` 时注入控制器 ARP 表。

---

## 10. 完整入网时序 (移动节点视角)

```
t=1.5s  BlindConnectApp 启动 (ROLE_TERMINAL)
        ├─ InitCrypto()
        ├─ STA 网卡开始跳频扫描 ch1→6→11
        └─ Adhoc 网卡固定在 ch11 监听伪信标

t~2s    扫描到 sw3 的伪信标 (域C, GATEWAY, ch11)
        ├─ 收到 MODULUS + PUBKEY
        ├─ ComputeSharedSecretFromGateway() → Chebyshev 密钥协商
        └─ 密钥协商完成后自动 RequestAdhocIp()

t~2.1s  GATEWAY 回复 IP_OFFER
        ├─ Terminal 验证 HMAC
        ├─ ConfigureIpOnInterface(AdhocDev, 10.100.3.x)
        └─ Terminal 已加入域C Adhoc 网络

t~N s   终端移动到域A范围
        ├─ 重扫检测到 AP SSID="A" (ch1)
        ├─ EvaluateAndSwitch() → AP 评分更高
        ├─ ExecuteSwitch(AP)
        │   ├─ 锁定 ch1, StaWifiMac.Ssid="A"
        │   ├─ 发送 IP_RELEASE 到域C
        │   └─ 断开 Adhoc
        ├─ 3s 后 RequestStaIp()
        ├─ AP_SERVER 回复 IP_OFFER
        └─ ConfigureIpOnInterface(StaDev, 10.1.1.x)

t=N+5s  周期性重扫其他信道 → 持续评估是否有更好的网络
```

---

## 11. 关键设计决策总结

| 决策 | 原因 |
|------|------|
| 伪信标用 UDP 而非标准 Beacon | 可携带自定义字段 (跳数/负载/能量/密钥)，不受 802.11 IE 长度限制 |
| GATEWAY socket 绑定分离 (原生设备 vs VND) | Switch 节点 IP 在 VND 上，但 PHY 信标收发必须通过原生 WiFi 设备 |
| AP 域重扫保留跳频能力 | 确保已在 AP 域时仍能发现其他域（Adhoc 或其他 AP） |
| IP 请求等待 3s 延迟 (AP 模式) | 给足 802.11 Auth + Association 握手时间 |
| 非对称密钥交换 (Chebyshev) | 无需 PKI，适合无中心 Adhoc 场景；通过广播信道完成协商 |
| HMAC 取 SHA256 前 64 位 | 平衡安全性与协议开销，伪信标帧不宜过大 |
| 切换阈值 0.02 | 防止信号波动导致的频繁乒乓切换 |
