# Train4 无线融合SDN实验总结

## 一、实验拓扑

```
                        ┌─────────────────────┐
                        │   SDN Controller    │
                        │   (OFSwitch13       │
                        │    Learning)        │
                        └──┬──────┬──────┬────┘
                           │      │      │
              ┌────────────┼──────┼──────┼────────────┐
              │            │      │      │            │
              ▼            ▼      ▼      ▼            ▼
         ┌─────────┐ ┌─────────┐ ┌─────────┐
         │   sw1   │ │   sw2   │ │   sw3   │
         │ (域A AP) │ │ (域B AP) │ │ (域C GW) │
         │ Port1:AP │ │ Port1:AP │ │ Port1:AP │
         │ Port2:BH │ │ Port2:BH │ │ Port2:BH │
         │ Port3:EM │ │ Port3:EM │ │ Port3:EM │
         └──┬───┬───┘ └──┬───┬───┘ └──┬───┬───┘
            │   │        │   │        │   │
    ┌───────┘   │   ┌────┘   │   ┌────┘   └──────────────┐
    │           │   │        │   │                        │
    ▼           ▼   ▼        ▼   ▼                        ▼
┌────────┐ ┌────────┐ ┌────────┐ ┌──────────┐
│域A STA │ │域B STA │ │域C STA │ │ 移动节点  │
│  ×4    │ │  ×2    │ │  ×3    │ │   ×1     │
│双模:   │ │双模:   │ │双模:   │ │双模:     │
│AP+Adhoc│ │AP+Adhoc│ │AP+Adhoc│ │AP+Adhoc  │
│有中心  │ │有中心  │ │无中心  │ │动态入网  │
└────────┘ └────────┘ └────────┘ └──────────┘
```

### 节点位置

```
Y轴
230 │    StaA[2,3]         StaC[0]   StaB[1]
    │
210 │              StaC[1]
    │
200 │ StaA[0,1] ←···········移动节点 (15m/s→)········→ StaB[0]
    │
150 │    sw1(-50,150)    sw3(0,150)    sw2(50,150)
    │
    └──────────────────────────────────────────────────→ X轴
       -250        -50     0     50        150
```

### 信道分配

| 用途 | 信道 | 频率 | 设备 |
|---|---|---|---|
| 域A AP (SSID="A") | ch1 | 2412 MHz | sw1 ApWifiMac → apVnd (Port 1) |
| 域B AP (SSID="B") | ch6 | 2437 MHz | sw2 ApWifiMac → apVnd (Port 1) |
| 域C AP (SSID="C") | ch11 | 2462 MHz | sw3 ApWifiMac → apVnd (Port 1) |
| 骨干 Adhoc | ch1 | 2412 MHz | sw1/2/3 backhaulVnd (Port 2) |
| 域A 域内 Adhoc | ch1 | 2412 MHz | sw1 emVnd (Port 3) + StaA Adhoc |
| 域B 域内 Adhoc | ch6 | 2437 MHz | sw2 emVnd (Port 3) + StaB Adhoc |
| 域C 域内 Adhoc | ch11 | 2462 MHz | sw3 emVnd (Port 3) + StaC Adhoc + 移动节点 |

---

## 二、融合架构设计

### Switch 三端口 VND 模型

```
           ┌─────────────────────────────────┐
           │        SDN Switch (swN)         │
           │                                 │
  Port 1 ──┤ apVnd ←→ ApWifiMac             │  ← AP基础设施模式
  Port 2 ──┤ backhaulVnd ←→ AdhocBackhaul   │  ← 跨域骨干互联
  Port 3 ──┤ emVnd ←→ AdhocEmergency        │  ← 域内自组网
           │                                 │
           │  OFSwitch13 Pipeline            │
           │  LearningController             │
           └─────────────────────────────────┘
```

- 每个 Port 是一个 VirtualNetDevice，通过 SendCallback 桥接到原生 WifiNetDevice
- 接收侧: WifiNetDevice 的 PromiscReceiveCallback 加 Ethernet 头后注入 VND
- 发送侧: VND::Send/SendFrom → SendCallback → WifiNetDevice::SendFrom

---

## 三、域C无中心模式 (核心创新点)

域C初始处于 Ad-Hoc 自组网模式，不依赖 AP 基础设施。

- STA 的 StaWifiMac **DOWN**，AdhocWifiMac **UP**
- STA 默认路由 → sw3 emVnd (10.100.3.1)
- sw3 运行 BlindConnectApp ROLE_GATEWAY，负责:
  - 周期性发送伪信标 (TYPE:IBSS_BEACON)
  - 响应 IP_REQUEST，从池 (10.100.3.10-50) 分配 IP
  - Chebyshev 密钥协商

---

## 四、当前流量配置

| 流 | 路径 | 目标IP | 端口 | 状态 |
|---|---|---|---|---|
| Flow 2 | StaA[0] → StaC[2] Adhoc | 10.100.3.4 | 13 | **100%丢包** |
| Flow 1 | StaA[1] → StaC[2] AP | 10.3.1.3 | 9 | 已注释 |
| Flow 3 | StaA[0] → StaB[0] | 10.2.1.1 | 10 | 已注释 |
| Flow 4 | StaB[1] → StaA[2] | 10.1.1.3 | 11 | 已注释 |
| Flow 5 | StaB[0] → StaC[0] | 10.3.1.1 | 12 | 已注释 |

仅激活 Flow 2，其余被注释——说明仍在调试单条跨域流。

---

## 五、核心问题: 跨域流量 100% 丢包

### 问题定位

Flow 2 数据路径:

```
StaA[0] (10.1.1.1)
  │ 默认路由 → sw1 apVnd
  ▼
sw1 OF Pipeline
  │ 需要流表: dst=10.100.3.4 → Port 2 (backhaul)
  ▼
sw1 backhaulVnd → Adhoc骨干 → sw3 backhaulVnd
  │
  ▼
sw3 OF Pipeline
  │ 需要: ARP(10.100.3.4) → MAC
  │ 需要流表: dst=10.100.3.4 → Port 3 (emVnd)
  ▼
sw3 emVnd → Adhoc → StaC[2] (10.100.3.4)
  │
  ▼
❌ 丢包
```

### 根因分析

| # | 根因 | 详情 |
|---|---|---|
| **R1** | **ARP未注册** | 域C STA的Adhoc IP (10.100.3.2/3/4) 未注册到控制器ARP表。只注册了StaWifiMac IP (10.3.1.x) 但该接口为DOWN。`InjectAdhocNeighbors` 依赖邻居发现，域C STA不运行BlindConnectApp，不响应。 |
| **R2** | **流表缺失** | sw3没有 "dst=10.100.3.x → output Port 3" 的流表规则。LearningController依赖PacketIn学习，但ARP解析失败导致无法触发学习。 |
| **R3** | **数据面封装** | VND出站走WifiNetDevice::SendFrom，以太网帧被整个封入802.11载荷。接收侧WifiToVirtualDevForward再包一层以太网头，形成双重封装。OF Pipeline可能解析到错误的帧结构。 |

### flow_stats.csv 实测数据

```
时间    Throughput_1~4  LossRate_1~4  Throughput_5  LossRate_5
10.1s   0              0             0             100%
11.1s   0              0             0             100%
...     ...            ...           ...           ...
28.1s   0              0             0             100%
```

- Port 13 (Flow 2) 的 Throughput_5 始终为 0, LossRate_5 始终 100%
- 发送端发包 (txPackets > 0)，接收端收包为 0 (rxPackets = 0)

---

## 六、IP分配问题

### 问题A: 静态域C STA — 有IP但不可达

域C STA 拥有两个IP:
- `10.3.1.x` (StaWifiMac): 接口 DOWN，不可用
- `10.100.3.x` (AdhocWifiMac): 接口 UP，但 **ARP 未注册到控制器**

Flow 2 的目标地址 10.100.3.4，IP分配成功但通信不可达。

### 问题B: 移动节点 — AP路径有竞态，Adhoc路径来不及

| 路径 | 问题 |
|---|---|
| **AP路径** (→ sw1/sw2) | ① Terminal从跳频锁定到AP信道后，3秒可能不够完成Auth+Assoc<br>② IP_REQUEST在关联未完成时发出会被MAC层丢弃<br>③ AP_SERVER靠PHY Sniffer搜字符串接收，不可靠 |
| **Adhoc路径** (→ sw3) | ① GATEWAY伪信标间隔2s，Terminal第一次评估(t=2.5s)时还没收到，必然选AP<br>② 一旦进入AP模式，重扫周期5s，30s仿真内来不及切回Adhoc<br>③ Adhoc IP分配路径从未被触发 |

### 问题C: GATEWAY收发路径不对称

| 方向 | 发送设备 | 接收设备 | 问题 |
|---|---|---|---|
| GATEWAY → Terminal | emVnd (VND) | Terminal 原生Adhoc | VND::Send调用SendCallback可工作，但数据帧含以太网头 |
| Terminal → GATEWAY | Terminal 原生Adhoc | sw3 原生Adhoc (PHY Sniffer) | PHY Sniffer搜raw bytes不可靠 |

---

## 七、实验参数汇总

| 参数 | 值 |
|---|---|
| 仿真时长 | 30s |
| WiFi标准 | 802.11n 2.4GHz |
| 信道带宽 | 20 MHz / 骨干 40 MHz |
| TxPower | 33 dBm |
| 天线增益 | 10 dBi (Tx+Rx) |
| 天线数 | 4 (骨干) / 默认 (其他) |
| 速率控制 | MinstrelHtWifiManager |
| 流量类型 | UDP OnOff, 1 Mbps, 1024B, CBR |
| 流量启动 | t=10s |
| 路由协议 | AODV + 静态路由 (STA), 静态路由 (移动节点) |
| 控制器 | OFSwitch13LearningController |
| 加密 | Chebyshev 密钥交换 + HMAC-SHA256 |

---

## 八、修复优先级

| 优先级 | 措施 | 预期效果 |
|---|---|---|
| **P0** | 注册域C STA Adhoc IP (10.100.3.x) 到控制器ARP表 | 解决ARP解析失败 |
| **P0** | 确保sw3有 "Port2→Port3" 的流表规则 | 打通跨域转发 |
| **P1** | 修复VND↔Adhoc数据面的以太网双重封装 | 帧格式正确 |
| **P1** | GATEWAY伪信标/请求改用原生Adhoc设备而非VND | 消除VND中间层 |
| **P2** | 增大仿真时间至60s+，调小Adhoc伪信标间隔 | Adhoc路径可测试 |
| **P2** | Terminal首次评估延迟或增加Adhoc预扫描时间 | 给Adhoc被发现的机会 |
