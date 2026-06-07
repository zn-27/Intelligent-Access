# 无 IP 切换方案 (IP 移动性 / 会话连续性)

## 问题诊断

当前切换到新网络时的 IP 处理 (`blind-connect-app.cc:671-771`)：

```
切换到 AP 域:
  1. 若之前在 Adhoc → 发送 IP_RELEASE → m_assignedIp = 0.0.0.0
  2. 关联 AP 后 → RequestStaIp() → 从新 IP 池获取 10.x.1.x
  3. old_IP ≠ new_IP → 所有 TCP/UDP 连接断开

切换到 Adhoc 域:
  1. m_staIpSocket.Close()
  2. m_assignedIp = 0.0.0.0
  3. RequestAdhocIp() → 从 GATEWAY 池获取 10.100.3.x
  4. old_IP ≠ new_IP → 所有连接断开
```

**根本问题**: IP 地址绑定到了"接入网络"，而不是绑定到"终端身份"。每次切网等于换了一个身份，上层连接无从恢复。

---

## 核心思路：SDN 原生 IP 移动性

在这个架构中，所有流量都经过 SDN Switch 转发，控制器掌握全局拓扑和 ARP 表。**利用 SDN 控制器的全局视野和流表编程能力**，实现终端跨网切换时 IP 保持不变。

```
传统 Mobile IP:  终端 ↔ HomeAgent ↔ ForeignAgent → 三角路由，额外隧道开销

SDN IP 移动性:   终端报告新位置 → 控制器更新流表 → 流量直接走新路径
                 无三角路由，无需额外隧道，天然的 SDN 优势
```

---

## 方案设计

### 1. 终端获取持久 IP

终端首次入网时分配一个**永久 IP**（Home Address），终生不变。

```
首次入网 (比如在域 A 的 AP 下):
  Terminal → IP_REQUEST (MAC: xx, FLAG: PERSISTENT)
  AP_SERVER/GATEWAY → IP_OFFER (IP: 10.1.1.100, PERSISTENT: true)
  Terminal: m_homeIp = 10.1.1.100   // 永久 IP，后续不再释放
```

**改动**: IP_OFFER 中加 `PERSISTENT` 标记。AP_SERVER/GATEWAY 将该 IP 从池中永久移除（不回收）。

### 2. 切换时不再释放 IP

```
ExecuteSwitch (切换到 Adhoc 域):
  - 删除: 发送 IP_RELEASE      ← 不再释放
  - 删除: m_assignedIp = 0.0.0.0
  + 保留: m_homeIp 不变
  + 新增: 通知控制器 "我的附着点变了"
```

`m_homeIp` 和 `m_assignedIp` 分离：
- `m_homeIp`: 终端永久身份，永不改变
- `m_currentGateway`: 当前附着点的网关 IP（用于配置默认路由）

### 3. SDN 控制器的移动性管理

这是方案的核心。控制器新增 **位置表** (`m_terminalLocation`):

```cpp
struct TerminalLocation {
    Ipv4Address homeIp;       // 终端永久 IP
    Mac48Address mac;          // 终端 MAC
    uint32_t switchId;         // 当前附着在哪个 Switch
    uint32_t switchPort;       // 当前附着在 Switch 的哪个 Port
    NetType netType;           // AP 还是 Adhoc
    Time lastUpdate;
};
std::map<Ipv4Address, TerminalLocation> m_terminalLocation;
```

#### 3.1 位置更新消息

终端切换网络后，发送轻量级位置更新：

```
TYPE:LOC_UPDATE;HOME_IP:10.1.1.100;MAC:xx:xx:xx:xx:xx:xx;ATTACH:GATEWAY_IP;NET:ADHOC
```

GATEWAY/AP_SERVER 收到后转发给控制器（或终端直接通过 OpenFlow 信道发送）。

#### 3.2 流表重编程

控制器收到位置更新后：

```
HandleLocationUpdate(homeIp, newSwitchId, newPort)
  │
  ├─ 1. 查找旧位置: oldSwitchId, oldPort
  ├─ 2. 在旧 Switch 删除指向 homeIp 的流表项
  ├─ 3. 在新 Switch 添加流表项:
  │     match: dst_ip = homeIp, actions: output = newPort
  ├─ 4. 对所有其他 Switch 更新流表:
  │     将 dst_ip = homeIp 的出端口从旧路径改为:
  │     output = port_to_newSwitch  (通过骨干网转发)
  └─ 5. 更新 ARP 表 (MAC 不变，但附着端口变了)
```

**关键**: 流表更新后，发往 `homeIp` 的包自动走新路径，终端侧无需任何改变——IP 不变、socket 不变、连接不断。

### 4. 入站流量路由

```
场景: StaA[0] (10.1.1.1, 域A) 正在向 MobileTerminal (10.1.1.100) 发送 UDP 流

切换前 (Mobile 在域 A AP 下):
  StaA[0] → sw1(Port1) → [流表匹配: dst=10.1.1.100] → sw1(Port1, AP VND)
  → 无线 → MobileTerminal

Mobile 切换到域 C Adhoc:
  Controller 收到 LOC_UPDATE
  → 更新 sw1 流表: dst=10.1.1.100 → output=Port2(骨干网)
  → 更新 sw3 流表: dst=10.1.1.100 → output=Port3(Adhoc VND)

切换后:
  StaA[0] → sw1(Port1) → [流表: dst=10.1.1.100 → Port2]
  → 骨干 Adhoc → sw3 → [流表: dst=10.1.1.100 → Port3]
  → 域C Adhoc → MobileTerminal

MobileTerminal 的 socket: 依然绑定 10.1.1.100，sendto/recvfrom 无感知
```

### 5. 出站流量路由

终端出站包到达当前 Switch 的 VND 后，由流表决定转发：

```
MobileTerminal 在域 C → 发包 dst=10.1.1.1

  sw3(Port3, Adhoc VND) 收到包
  → [流表: dst=10.1.1.1 → Port2(骨干网) → sw1 → Port1(AP VND)]
  → 到达 StaA[0]

终端唯一需要更新的: 默认路由指向当前附着点的网关 IP
  ConfigureIpOnInterface 中:
    ipv4->AddAddress(ifIndex, m_homeIp, mask)    // IP 不变
    SetDefaultRouteVia(ipv4, ifIndex, newGateway) // 仅改网关
```

---

## 实现改动

### 改动文件清单

| 文件 | 改动 |
|------|------|
| `blind-connect-app.h` | 新增 `m_homeIp`、`m_locationCallback`；新增 `LOC_UPDATE` 消息发送方法 |
| `blind-connect-app.cc` | `ExecuteSwitch()` 移除 IP_RELEASE；切换后调用 `SendLocationUpdate()`；`HandleAdhocIpMessage`/`HandleStaIpRead` 区分首次分配/切换场景 |
| `ofswitch13-learning-controller.h` | 新增 `TerminalLocation` 结构体、`m_terminalLocation` 表、`HandleLocationUpdate()` |
| `ofswitch13-learning-controller.cc` | 实现位置更新与流表重编程逻辑 |
| 消息协议 | 新增 `TYPE:LOC_UPDATE`、IP_OFFER 增加 `PERSISTENT` 字段 |

### 消息扩展

**IP_OFFER 增加 PERSISTENT 标记:**
```
TYPE:IP_OFFER;MAC:xx;IP:10.1.1.100;MASK:255.255.255.0;GW:10.1.1.1;PERSISTENT:1
```

**新增位置更新消息:**
```
TYPE:LOC_UPDATE;HOME_IP:10.1.1.100;MAC:xx;SWITCH:3;PORT:3;NET:ADHOC;TS:15.2
```

### 控制器流表操作伪代码

```cpp
void HandleLocationUpdate(Ipv4Address homeIp, uint32_t newSwId, uint32_t newPort) {
    auto it = m_terminalLocation.find(homeIp);
    if (it != m_terminalLocation.end()) {
        // 清除旧路径
        Ptr<OFSwitch13Device> oldSw = GetSwitch(it->second.switchId);
        RemoveFlowEntry(oldSw, homeIp);
    }

    // 记录新位置
    m_terminalLocation[homeIp] = {newSwId, newPort, Simulator::Now()};

    // 新 Switch: 添加本地交付流表
    Ptr<OFSwitch13Device> newSw = GetSwitch(newSwId);
    AddFlowEntry(newSw, /*match=*/homeIp, /*output=*/newPort, /*priority=*/HIGH);

    // 其他 Switch: 更新 dst=homeIp 的转发路径
    for (auto& sw : GetAllSwitches()) {
        if (sw->GetId() == newSwId) continue;
        uint32_t outPort = GetPortToSwitch(sw->GetId(), newSwId); // 骨干网端口
        UpdateFlowEntry(sw, /*match=*/homeIp, /*output=*/outPort);
    }

    // 更新 ARP: MAC 不变，但确保附着端口可达
    UpdateArpReachability(homeIp, newSwId, newPort);
}
```

---

## 切换时序对比

### 改造前 (IP 不连续)

```
t=10.0  终端在域A AP下，IP=10.1.1.100，正在接收 UDP 流
t=10.5  信号衰减，ExecuteSwitch(Adhoc)
t=10.5  发送 IP_RELEASE(10.1.1.100)
t=10.5  m_assignedIp = 0.0.0.0   ← 连接断开
t=10.6  RequestAdhocIp()
t=10.8  收到 IP_OFFER → 新 IP=10.100.3.15  ← IP 变了
t=10.8  ConfigureIpOnInterface(10.100.3.15)
        → 旧 socket 绑在 10.1.1.100 上，已失效
        → 应用层必须重建 socket，UDP 流丢失
```

### 改造后 (IP 保持)

```
t=10.0  终端在域A AP下，homeIp=10.1.1.100，接收 UDP 流
t=10.5  信号衰减，ExecuteSwitch(Adhoc)
t=10.5  SendLocationUpdate(homeIp=10.1.1.100, sw=3, port=3, ADHOC)
t=10.5  ConfigureIpOnInterface(AdhocDev, 10.1.1.100, 新mask, 新网关)
        → IP 不变，默认路由指向 Adhoc 网关
t=10.5  控制器收到 LOC_UPDATE
t=10.5  更新 sw1: dst=10.1.1.100 → Port2(骨干)
t=10.5  更新 sw3: dst=10.1.1.100 → Port3(Adhoc)
t=10.6  UDP 流恢复，包通过新路径到达
        → socket 未关闭，recvfrom 继续收到数据
        → 丢包仅发生在 t=10.5~10.6 之间的转发间隙
```

中断时间从**秒级（释放+重新分配+重建socket）**降到**百毫秒级（控制器流表更新延迟）**。

---

## 进阶：零中断软切换

上述方案仍然有流表更新窗口的丢包。进一步优化：

### 双向临时隧道 (Bicasting)

```
切换期间 (t=10.5 ~ 10.6):
  sw1 旧路径保留，同时新增隧道到 sw3
  → 发往 homeIp 的包同时发往旧 AP VND 和新 Adhoc VND
  → 终端任一接口收到即可

  sw3 新路径已生效
  → 终端从 Adhoc 发出上行包

t=10.6  终端完成 Adhoc 接口配置
  → 发送 LOC_CONFIRM
  → 控制器删除旧路径，只保留新路径
```

这需要控制器支持临时组播/双播流表，复杂度较高但中断时间可接近 **0ms**。

### 终端侧双接口同时就绪

```
切换期间终端状态:
  StaWifiMac: 仍关联在旧 AP，继续接收 (make-before-break)
  AdhocWifiMac: 已完成新域入网

  → 上行: 切换到 Adhoc 接口发送
  → 下行: 两张网卡同时可用，任一收到即交付

切换完成后:
  → 断开旧 AP 关联
```

这要求终端能短暂维护两张网卡同时有 IP 的状态，当前代码 `ConfigureIpOnInterface` 会先 `RemoveAddress(ifIndex, 0)` 再 `AddAddress`，需要改为支持多接口持有相同 homeIp。

---

## 落地优先级

| 步骤 | 内容 | 效果 |
|------|------|------|
| **Step 1** | IP 持久化 + LOC_UPDATE + 控制器流表重编程 | 切换后 IP 不变，秒级恢复 |
| **Step 2** | 双接口 make-before-break | 百毫秒级中断 |
| **Step 3** | 控制器 bicasting | 接近 0ms 中断 |

Step 1 是核心改动，约 200~300 行增量代码，即可实现 IP 不随切换变化，上层连接保持。
