# train4-new.cc 融合无线 SDN 仿真脚本架构文档

## 1. 总体架构概述

本脚本构建了一个**无线融合 SDN (Software-Defined Networking)** 仿真网络，核心特点是将 AP (接入点) 与 OpenFlow 交换机合并为统一节点，通过 VirtualNetDevice (VND) 将多种无线接口聚合为 OpenFlow 可控的数据端口，实现 SDN 控制器对无线网络的全局管理。

### 1.1 拓扑结构

```
                     SDN Controller (集中控制)
                           |
          +----------------+----------------+
          |                |                |
        sw1 (域A)        sw2 (域B)        sw3 (域C)
        AP+Switch        AP+Switch        AP+Switch
          |   \          /   \          /   |
          |    \--------/     \--------/    |      ← 骨干 AdHoc (Port 2)
          |     sw1←→sw2←→sw3 三角互连     |
          |                                    |
     +----+----+      +----+----+      +------+------+
     |   STA   |      |   STA   |      |    STA      |
     | ×4 (A)  |      | ×2 (B)  |      | ×3 (C)      |
     +---------+      +---------+      +-------------+
      ↕ StaWifi         ↕ StaWifi       ↕ StaWifi       ← Port 1 (基础设施)
      ↕ AdhocEm         ↕ AdhocEm       ↕ AdhocEm       ← Port 3 (紧急自组网)
```

### 1.2 核心设计理念

| 设计要点 | 实现方式 |
|---------|---------|
| AP+Switch 融合 | 每个交换机节点同时拥有 ApWifiMac 和 OpenFlow 数据面 |
| 无线接口可控 | VirtualNetDevice 将 WiFi 设备桥接为 OpenFlow 端口 |
| 三层网络平面 | Port 1(AP接入)、Port 2(骨干回传)、Port 3(紧急自组网) |
| 集中控制+智能决策 | Q-Learning 驱动的网络模式切换（基础设施/自组网）|
| 跨域通信 | 通过骨干 AdHoc 信道实现三交换机三角互连 |

---

## 2. 节点创建与角色分配 (Section 1)

```
域 A: 4 个 STA (StaA[0..3]) + 1 个 Switch/AP (sw1)
域 B: 2 个 STA (StaB[0..1]) + 1 个 Switch/AP (sw2)
域 C: 3 个 STA (StaC[0..2]) + 1 个 Switch/AP (sw3)
1 个集中控制器节点 (controllerNode)
```

**STA 双模特性**：每个 STA 同时拥有：
- **StaWifiMac** 接口 — 基础设施模式下连接本域 AP
- **AdhocWifiMac** 接口 — 紧急自组网模式下与同域节点直连

---

## 3. 各网络协议层实现

### 3.1 物理层 (PHY)

使用 **YANS (Yet Another Network Simulator)** WiFi 信道模型，按功能划分为多组独立物理信道：

| 信道组 | 用途 | 信道带宽 | 发射功率 | 天线配置 |
|--------|------|---------|---------|---------|
| `switchChannel` | Switch 间骨干互连 | 40 MHz | 33 dBm | 4×4 MIMO |
| `channelA/B/C` | 域内 AP-STA 通信 | 默认 | 33 dBm | 默认 |
| `domainChA/B/C` | 域内紧急 AdHoc | 默认 | 33 dBm | 默认 |

- WiFi 标准：**802.11n 2.4GHz**
- 速率控制算法：**MinstrelHtWifiManager**（自适应速率选择）
- MAC 队列：最大 2000 包，最大延迟 10 秒

**骨干信道增强**：Switch 间骨干使用 40MHz 带宽 + 4×4 MIMO + 10dB 增益天线，确保交换机间高速回传。

### 3.2 数据链路层 (MAC)

脚本使用了三种 WiFi MAC 模式：

#### (a) ApWifiMac — 交换机的 AP 接口
- 每个 Switch 运行一个 AP（SSID 分别为 "A"、"B"、"C"）
- 负责关联本域 STA，管理接入认证
- 收到 STA 数据包后通过 `ApWifiToVirtualDevForward` 转发到 VND

#### (b) StaWifiMac — STA 的基础设施接口
- 每个 STA 关联到本域 Switch 的 AP
- 默认网关指向 Switch 的 AP VND 地址
- 正常工作模式下使用此接口通信

#### (c) AdhocWifiMac — 自组网接口
- Switch 间骨干通信使用 Adhoc 模式（三角互连）
- 每个域有独立的紧急 Adhoc 信道（默认关闭）
- STA 的紧急 Adhoc 接口默认 Down，按需启用

### 3.3 网络层 (IP)

#### IP 地址规划

| 子网 | 地址范围 | 用途 | 连接设备 |
|------|---------|------|---------|
| 域 A 接入 | `10.1.1.0/24` | AP-STA 基础设施 | sw1 VND(Port1) + StaA[0..3] |
| 域 B 接入 | `10.2.1.0/24` | AP-STA 基础设施 | sw2 VND(Port1) + StaB[0..1] |
| 域 C 接入 | `10.3.1.0/24` | AP-STA 基础设施 | sw3 VND(Port1) + StaC[0..2] |
| 骨干回传 | `10.10.1.0/24` | Switch 间互连 | sw1/sw2/sw3 VND(Port2) |
| 域 A 紧急 | `10.100.1.0/24` | 域内紧急 Adhoc | sw1 VND(Port3) + StaA Adhoc |
| 域 B 紧急 | `10.100.2.0/24` | 域内紧急 Adhoc | sw2 VND(Port3) + StaB Adhoc |
| 域 C 紧急 | `10.100.3.0/24` | 域内紧急 Adhoc | sw3 VND(Port3) + StaC Adhoc |

#### 路由配置

**STA 路由**：
- 默认路由指向本域 Switch 的 AP VND 网关地址
- 例如 StaA[0] 的默认路由 → `10.1.1.x`（sw1 的 Port 1 VND 地址）

**STA 协议栈**（多路由策略）：
```
AodvHelper   (优先级 10)  — 按需距离矢量路由
OlsrHelper   (优先级 10)  — 优化链路状态路由
StaticRouting (优先级 100) — 静态默认路由（最高优先级）
```
静态路由优先级最高(100)，所以默认使用静态路由。当需要切换时，控制器可动态调整协议优先级。

### 3.4 传输层

使用 **UDP** 协议承载测试流量：
- OnOffApplication 生成 UDP 数据流
- 数据速率：1 Mbps
- 包大小：1024 字节
- 持续发送（OnTime=1, OffTime=0）

---

## 4. OpenFlow SDN 控制面

### 4.1 控制器与交换机建立

```
OFSwitch13InternalHelper  →  内部通道模式（非真实 TCP）
  ├── InstallController(controllerNode)  →  安装控制器
  ├── InstallSwitch(sw1, sw1Ports)       →  安装交换机1 (3端口)
  ├── InstallSwitch(sw2, sw2Ports)       →  安装交换机2 (3端口)
  ├── InstallSwitch(sw3, sw3Ports)       →  安装交换机3 (3端口)
  └── CreateOpenFlowChannels()           →  建立 OpenFlow 通道
```

### 4.2 VirtualNetDevice 桥接机制

这是本架构的核心创新点。WiFi 设备原生不支持 OpenFlow 的以太网帧格式，VND 充当协议转换桥梁：

```
                    Switch 节点内部架构

  WiFi 设备 (非 Ethernet)          VirtualNetDevice (Ethernet 兼容)
  ┌─────────────────┐            ┌──────────────────┐
  │ ApWifiMac       │──Promisc──→│ apVnd (Port 1)   │──┐
  │ (802.11 帧)     │  Receive   │ (Ethernet 帧)    │  │
  └─────────────────┘            └──────────────────┘  │
  ┌─────────────────┐            ┌──────────────────┐  │   OpenFlow
  │ AdhocWifiMac    │──Promisc──→│ backhaulVnd      │  │   交换机
  │ (骨干)          │  Receive   │ (Port 2)         │──┤   数据面
  └─────────────────┘            └──────────────────┘  │
  ┌─────────────────┐            ┌──────────────────┐  │
  │ AdhocWifiMac    │──Promisc──→│ emVnd            │  │
  │ (紧急)          │  Receive   │ (Port 3)         │──┘
  └─────────────────┘            └──────────────────┘
```

**数据流向（以 AP 接收为例）**：
1. STA 发送 WiFi 帧 → ApWifiMac 接收
2. `ApWifiToVirtualDevForward` 回调触发
3. 为原始 IP 包封装 Ethernet 头（源 MAC = STA MAC，目的 MAC = AP VND MAC）
4. 添加 Ethernet Trailer
5. 调用 `vdev->Receive()` 将以太网帧送入 OpenFlow 管道

**反向（交换机发送到 WiFi）**：
1. OpenFlow 管道决定从某端口输出
2. VND 的 `SetSendCallback` 绑定到 `WifiNetDevice::SendFrom`
3. 以太网帧被解封装，通过 WiFi 设备发送出去

### 4.3 交换机端口定义

每个交换机有 3 个 OpenFlow 数据端口：

| 端口号 | VND | 对应 WiFi 设备 | 功能 |
|--------|-----|---------------|------|
| Port 1 | `apVnd` | ApWifiMac | 域内 STA 接入 |
| Port 2 | `backhaulVnd` | AdhocWifiMac (骨干) | Switch 间回传 |
| Port 3 | `emVnd` | AdhocWifiMac (紧急) | 域内紧急自组网 |

---

## 5. 流表的产生与管理

### 5.1 控制器学习机制

控制器 `OFSwitch13LearningController` 继承自 `OFSwitch13Controller`，实现了基于自学习的 MAC-端口映射：

#### 初始状态
- 流表为空，所有交换机的所有端口没有任何规则
- 交换机收到任何数据包都会触发 **packet-in** 消息上报控制器

#### 学习过程
```
Packet-In 到达控制器
    │
    ├── 记录: (源MAC, 交换机ID, 入端口号) → 学习源地址
    │
    ├── 查询: 目的MAC 是否已知？
    │     ├── 是 → 下发 Flow-Mod: 将包从对应端口转发
    │     │        同时安装正向流表项
    │     └── 否 → Flood: 从所有端口（除入端口）泛洪
    │
    └── 最终: 双向 MAC-端口映射学习完成
```

#### 流表项结构
每个学习到的流表项包含：
```
匹配字段: eth_src = XX:XX:XX:XX:XX:XX
动作:     OUTPUT:Port_N
优先级:   基于学习顺序
超时:     idle_timeout / hard_timeout
```

### 5.2 ARP 表预注册

为加速初始通信，脚本在控制器启动时手动注册了 ARP 表项：

```cpp
controllerApp->AddArpEntry(IP地址, MAC地址);
```

注册内容包括：
- 3 个域的 AP 网关 (sw1/sw2/sw3 的 Port 1 VND)
- 3 个 Switch 的骨干 VND (Port 2)

这样交换机间的跨域通信无需依赖 ARP 广播学习。

### 5.3 L2/L3 双层转发表

控制器维护两张转发表：

| 表类型 | 匹配字段 | 动作 |
|--------|---------|------|
| L2 转发表 | 以太网目的 MAC | OUTPUT 到对应端口 |
| L3 路由表 | IP 目的地址 | 先解析 MAC，再 OUTPUT |

对于跨域流量：
1. STA 发包到 AP 网关 (L2)
2. AP 交换机查表，发现目的 IP 在其他域
3. 通过骨干端口 (Port 2) 转发到目标交换机
4. 目标交换机查表，通过 AP 端口 (Port 1) 送到目标 STA

### 5.4 自定义 Experimenter 消息与流表交互

控制器通过 OpenFlow **Experimenter 消息**与交换机进行扩展通信：

| 消息类型 | 编号 | 功能 |
|---------|------|------|
| `ADHOC_EXT_STAINFO` | 5001 | 采集 STA 信息（MAC、IP、端口）|
| `ADHOC_EXT_NODE_STATUS_REPORT` | 8001 | 上报节点位置坐标 |
| `ADHOC_EXT_FLOW_STATUS_REPORT` | 8002 | 上报流量统计信息 |
| `ADHOC_EXT_CHANGELOGICAL` | 8003 | 切换组网模式（基础设施/自组网）|
| `ADHOC_EXT_PROTOCOL_SET` | — | 设置路由协议优先级 |
| `ADHOC_EXT_TEST` | — | 测试消息（路由优先级推送）|

---

## 6. SwitchProtocolInfoApp — 交换机智能应用

每个 Switch 节点运行一个 `SwitchProtocolInfoApp`，作为控制器与 STA 之间的桥梁。

### 6.1 主要功能

| 方法 | 功能 |
|------|------|
| `CollectStaMessage()` | 采集已关联 STA 的 MAC 和 IP 地址 |
| `CollectStaProtocolReplies()` | 采集各 STA 的路由协议优先级配置 |
| `UpdateStaRoutingPriority()` | 修改 STA 的路由协议优先级 |
| `ChangeZuWang(int mode)` | 切换组网模式（0=基础设施, 1=自组网）|
| `SendNodePosition()` | 采集 STA 位置坐标 |
| `CollectFlowStats()` | 采集流量统计（吞吐、延迟、抖动、丢包）|

### 6.2 组网模式切换流程

```
控制器决策 (Q-Learning)
    │
    ├── CDL() → 发送 Experimenter(CHANGELOGICAL) 到目标 Switch
    │
    └── Switch 收到消息
        │
        ├── Changelogical(1) → 切换到自组网模式
        │   ├── Enable STA 的 Adhoc 接口
        │   └── Disable STA 的 StaWifi 接口
        │
        └── Changelogical(0) → 切换回基础设施模式
            ├── Disable STA 的 Adhoc 接口
            └── Enable STA 的 StaWifi 接口
```

---

## 7. Q-Learning 智能决策系统

控制器内置了 **Q-Learning 算法**，用于根据网络状态自动决定组网模式：

### 7.1 状态空间
- 节点平均距离
- 链路质量指标（吞吐、丢包、延迟、抖动）

### 7.2 动作空间
| 动作值 | 含义 |
|--------|------|
| 0 (MULTI) | 保持多跳基础设施模式 |
| 1 (ADHOC) | 切换到 AdHoc 自组网模式 |

### 7.3 奖励函数
- 基于链路质量综合评分
- 引入切换惩罚（避免频繁模式切换）
- 距离阈值：25.0（超过此值倾向于自组网模式）

---

## 8. 事件调度时间线

```
T=0s      网络初始化，STA 开始关联 AP
            SwitchProtocolInfoApp 启动

T=0.1s    FlowMonitor 开始周期性采集

T=5s      各交换机采集 STA 信息
            → GetApStaMessages() → 上报控制器

T=7s      控制器设置路由优先级
            → SetRoutingPriority()

T=10s     UDP 流量应用启动
            Flow 1: StaA[1] → StaC[2] (跨域 A→C)

T=13s     控制器向所有交换机推送路由优先级
            → SetPriorityToAll() → SetRPtoAll()

T=30s     仿真结束，输出统计结果
```

---

## 9. 流量应用与监控

### 9.1 测试流量

当前启用 1 条跨域 UDP 流：

| 流编号 | 源 | 目的 | 端口 | 状态 |
|--------|-----|------|------|------|
| Flow 1 | StaA[1] (10.1.1.2) | StaC[2] (10.3.1.3) | 9 | 启用 |
| Flow 2 | StaA[0] → StaB[0] | 10.2.1.1 | 10 | 注释 |
| Flow 3 | StaB[1] → StaA[2] | 10.1.1.3 | 11 | 注释 |
| Flow 4 | StaB[0] → StaC[0] | 10.3.1.1 | 12 | 注释 |

### 9.2 FlowMonitor 监控

- 周期性采集（每秒）7 条流的统计信息
- 输出到 `flow_stats.csv`：吞吐量(Kbps)、丢包率(%)、平均 RTT(ms)、抖动(ms)
- 仿真结束后汇总打印并序列化为 `flowmon-results.xml`

### 9.3 PCAP 追踪

全量抓包覆盖：
- 各域 AP 和 STA 的 WiFi 帧
- Switch 间骨干信道
- 各域紧急 Adhoc 信道
- OpenFlow 控制通道

---

## 10. 数据包跨域转发完整流程

以 Flow 1 (StaA[1] → StaC[2]) 为例：

```
1. StaA[1] 构造 UDP 包
   源: 10.1.1.2  目的: 10.3.1.3:9

2. STA 默认路由 → 发往 sw1 的 AP 网关
   MAC 帧: STA_MAC → sw1_apVnd_MAC

3. sw1 ApWifiMac 接收 → ApWifiToVirtualDevForward
   → 封装 Ethernet 头 → 送入 OpenFlow 管道 (Port 1)

4. sw1 OpenFlow 管道查表
   目的 IP 10.3.1.3 不在本域 → 匹配跨域规则
   → 从 Port 2 (backhaulVnd) 输出

5. backhaulVnd SendCallback → sw1 的骨干 AdhocWifiMac
   → 通过无线骨干信道发送

6. sw3 的骨干 AdhocWifiMac 接收 → WifiToVirtualDevForward
   → 封装 Ethernet 头 → 送入 sw3 OpenFlow 管道 (Port 2)

7. sw3 OpenFlow 管道查表
   目的 IP 10.3.1.3 在本域 → 匹配域内规则
   → 从 Port 1 (apVnd) 输出

8. apVnd SendCallback → sw3 的 ApWifiMac
   → 通过 WiFi 发送给 StaC[2]

9. StaC[2] 接收，UDP 应用处理
```

---

## 11. 关键文件依赖关系

```
scratch/train4-new.cc                     ← 主仿真脚本
  ├── contrib/ofswitch13/
  │   ├── ofswitch13-learning-controller  ← SDN 控制器 (Q-Learning + 自学习)
  │   ├── ofswitch13-device               ← OpenFlow 交换机设备
  │   └── ofswitch13-module               ← OpenFlow 1.3 基础模块
  ├── src/ap-app/
  │   └── switch-protocol-info-app        ← 交换机智能应用 (STA 管理)
  └── ns-3 核心模块
      ├── wifi-module                     ← WiFi PHY/MAC
      ├── internet-module                 ← IP 协议栈
      ├── aodv/olsr-module               ← 路由协议
      ├── flow-monitor                    ← 流量监控
      └── virtual-net-device              ← 虚拟设备桥接
```
