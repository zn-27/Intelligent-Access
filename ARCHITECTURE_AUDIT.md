# 架构审计报告 — BlindConnectApp + train4-new

**项目**: 基于协同智能感知的多模态无人自组织网络临机入网与灵巧重构  
**仿真平台**: ns-3.34 + ofswitch13  
**审计日期**: 2026-07-11  
**审计范围**: `scratch/train4-new.cc`, `contrib/mymodule/model/blind-connect-app.{cc,h}`  
**审计人员**: Claude Code (自动审计)

---

## 审计摘要

| 严重级别 | 数量 | 说明 |
|---------|------|------|
| **P0** | 7 | 会导致实验结论错误，必须立即修复 |
| **P1** | 9 | 系统架构与论文设计不一致 |
| **P2** | 7 | 统计指标定义错误或误导 |
| **P3** | 6 | 代码结构和可维护性问题 |

---

# P0 — 会导致实验结论错误

---

## P0-1: AP Beacon 网关地址硬编码为域 B 地址

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 790  
**函数**: `ReceiveStaBeacon`

### 现有行为
```cpp
info.gateway = Ipv4Address("10.2.1.1");  // 硬编码为域B网关
```
无论收到 SSID="A"、"B" 还是 "C" 的 AP Beacon，`gateway` 字段始终被设为 `10.2.1.1`（域 B 的 AP VND 地址）。

### 为什么有问题
1. 域 A 的 AP Beacon 被标记为网关 `10.2.1.1`，这将导致：
   - 域 A 入网时默认路由指向错误的网关地址
   - 如果该 gateway 地址被用于 IP 请求目标或路由配置，数据包将发送到错误的目的地
2. **实验结论错误**: 域 A 和域 C 的 AP 入网看似"成功"，但实际使用的网关信息完全错误

### 建议修改
```cpp
// 根据 SSID 推导正确的网关地址
std::string ssidStr(beaconSsid.PeekString());
if (ssidStr == "A")      info.gateway = Ipv4Address("10.1.1.1");
else if (ssidStr == "B") info.gateway = Ipv4Address("10.2.1.1");
else if (ssidStr == "C") info.gateway = Ipv4Address("10.3.1.1");
// 更好的做法：从 Beacon 的 BSSID 或控制器已知映射中查询
```

---

## P0-2: 硬编码 IP 地址绕过动态 IP_REQUEST/IP_OFFER 协议

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 1754-1842  
**函数**: `ConfigureAdhocIpByDomain`, `ConfigureStaIpByDomain`

### 现有行为
```cpp
// ConfigureStaIpByDomain: 硬编码 10.X.1.100
case 1: ip = Ipv4Address("10.1.1.100"); gw = Ipv4Address("10.1.1.1"); break;

// ConfigureAdhocIpByDomain: 硬编码 10.100.X.100
case 1: ip = Ipv4Address("10.100.1.100"); gw = Ipv4Address("10.100.1.1"); break;
```

调用路径：
1. `HandleStaIpRead` (STA 收到 IP_OFFER) → 配置 STA IP 后 → 调用 `ConfigureAdhocIpByDomain` 硬编码 AdHoc 地址
2. `HandleAdhocIpMessage` (终端收到 AdHoc IP_OFFER) → 配置 AdHoc IP 后 → 调用 `ConfigureStaIpByDomain` 硬编码 STA 地址

### 为什么有问题
1. **论文声称"动态入网地址分配"，但实际每一对 (STA, AdHoc) 地址中总有一个是硬编码的**
2. 硬编码地址 `10.1.1.100` / `10.100.1.100` 可能与 IP_OFFER 动态分配的地址冲突
3. **实验结论错误**: IP_REQUEST→IP_OFFER 协议实际上只配置了主导接口，非主导接口从未经过动态分配流程
4. 终端在到达新域之前就已经"提前知道"目标域地址，这不符合真实入网场景

### 建议修改
- 移除非主导接口的硬编码配置
- 非主导接口应通过独立的 IP_REQUEST/IP_OFFER 流程获取地址，或使用 link-local 地址
- 论文明确区分: "主导接口动态分配的地址" 与 "非主导接口的 link-local 占位地址"

---

## P0-3: AdHoc Beacon 源 IP 默认值硬编码

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 572  
**函数**: `ReceiveAdhocBeacon`

### 现有行为
```cpp
Ipv4Address srcIp = Ipv4Address("10.1.1.1");  // 默认值，若解析失败则使用此值
```
当 AdHoc 伪信标中 `IP:` 字段解析失败时，`srcIp` 默认为域 A 的网关地址。

### 为什么有问题
1. 若域 C 的 GATEWAY 伪信标 IP 字段格式有误，会将域 C 的候选网络标记为域 A 地址，导致路由配置完全错误
2. 默认值应无法路由或不应参与决策，而非静默使用域 A 地址

### 建议修改
```cpp
Ipv4Address srcIp = Ipv4Address::GetAny();  // 解析失败则标记为无效
// 在后处理中过滤掉 srcIp 无效的候选
```

---

## P0-4: m_neighborRadar 无去重、无过期、直接 clear

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 646-662 (AdHoc push_back), 811 (AP push_back), 980 (clear)  
**函数**: `ReceiveAdhocBeacon`, `ReceiveStaBeacon`, `EvaluateAndSwitch`

### 现有行为
```cpp
// ReceiveAdhocBeacon: 每个 IBSS_BEACON 都 push_back
m_neighborRadar.push_back(info);

// ReceiveStaBeacon: 每个 AP Beacon 都 push_back
m_neighborRadar.push_back(info);

// EvaluateAndSwitch: 评估后直接清空
m_neighborRadar.clear();
```

### 为什么有问题
1. **同一 AP 每秒发送约 10 个 Beacon**，每个都 push_back，产生大量重复候选
2. **IBSS_BEACON 每 2 秒发送一次**，同样重复
3. 评估周期（约 1 秒）内可能积累几十条重复候选，排序和打分在重复数据上进行
4. **无过期机制**: 终端离开域 A 后，`m_neighborRadar` 中仍可能有域 A 的旧 Beacon 残留
5. **每次 Evaluate 后 clear**: 丢失了跨评估周期的信号稳定性信息
6. **实验结论不可靠**: 网络选择基于的是"最新采集窗口内最后一条 Beacon 的快照"，而非统计稳定的候选网络画像

### 建议修改
- 重构为 `CandidateNetworkTable`，唯一键: `(networkType, ssid/domainId, gateway, channel)`
- 每条候选记录维护: `lastSeen`, `sampleCount`, `rssiEwma`, `snrEwma`
- `CandidateTimeout`: lastSeen 超过 2~3 秒自动失效
- RSSI 使用 EWMA: `R_t = alpha * sample + (1-alpha) * R_(t-1)`

---

## P0-5: 流统计 avgDelay 实际是单向端到端时延，非 RTT

**文件**: `scratch/train4-new.cc`  
**行号**: 201-205  
**函数**: `MonitorFlow`

### 现有行为
```cpp
double deltaDelaySec = fs.delaySum.GetSeconds() - state.lastDelaySumSec;
if (deltaRx > 0)
    avgDelayMs = deltaDelaySec / deltaRx * 1000.0;
```
CSV 列头: `AvgDelay(ms)`  
控制台输出: `平均延迟` / `平均延迟`

### 评估
当前代码**已正确**标记为 "AvgDelay" 而非 "RTT"。`FlowMonitor::delaySum` 是单向端到端时延（从发送到接收）。代码无 RTT 误导。

**但需确认**: 论文正文中是否仍使用"RTT"一词描述此指标。若是，必须统一改为"端到端平均时延"。

### 判定
此问题在代码层面已修复。需要检查论文文稿。降级为提醒。

---

## P0-6: 流统计窗口 jitter 公式可能不正确

**文件**: `scratch/train4-new.cc`  
**行号**: 208-213  
**函数**: `MonitorFlow`

### 现有行为
```cpp
double deltaJitterSec = fs.jitterSum.GetSeconds() - state.lastJitterSumSec;
if (deltaRx > 1)
    avgJitterMs = deltaJitterSec / (deltaRx - 1) * 1000.0;
```

### 分析
FlowMonitor 的 `jitterSum` 定义: `jitterSum = Σ|D_i - D_{i-1}|`，其中 D_i 是第 i 个包的端到端时延。  
RFC 3550 定义的平均 jitter: `J_i = J_{i-1} + (|D_i - D_{i-1}| - J_{i-1}) / 16`

`jitterSum / (rxPackets - 1)` 计算的是 **平均绝对相邻时延差**，比 RFC 3550 的指数加权移动平均更简单但可接受。窗口化后 `deltaJitterSum / (deltaRx - 1)` 计算窗口内平均绝对相邻时延差。

**但需注意**: `jitterSum` 是一个累计值跨越所有包。当 `deltaRx == 1` 时，只有一个新包，相邻差数量为 `currentRx - 1 - (lastRx - 1) = deltaRx = 1` 个新差值… 等等。实际上：如果 lastRx=5, jitterSum 已经累加了 4 个差值的和。如果 currentRx=6, 只新增了 1 个差值，deltaJitterSum 就是这 1 个差值。所以 `deltaJitterSum / (deltaRx - 1)` 当 deltaRx=1 时分母为 0。

当前代码处理了此边界情况 (line 212-213):
```cpp
else if (deltaRx == 1 && fs.jitterSum.GetSeconds() > state.lastJitterSumSec)
    avgJitterMs = deltaJitterSec * 1000.0;
```

### 判定
代码逻辑基本正确。需确认 FlowMonitor 在 ns-3.34 中 `jitterSum` 字段确实存在且定义如上。可在 Phase 2 中验证。

---

## P0-7: SignalNoiseDbm 中 signal 字段语义确认

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 653-655, 786-788  
**函数**: `ReceiveAdhocBeacon`, `ReceiveStaBeacon`

### 现有行为
```cpp
info.rssiDbm = signalNoise.signal;       // 命名为 rssiDbm
info.noiseDbm = signalNoise.noise;       // 命名为 noiseDbm
info.snrDb = signalNoise.signal - signalNoise.noise;  // 正确计算 SNR
```

### 分析
`MonitorSnifferRx` 回调的 `SignalNoiseDbm` 结构在 ns-3.34 中包含:
- `signal`: 接收信号功率 (dBm)
- `noise`: 噪声功率 (dBm)

当前代码:
1. **已正确**将 `signalNoise.signal` 命名为 `rssiDbm`（而非 snr）
2. **已正确**将 `signalNoise.noise` 命名为 `noiseDbm`
3. **已正确**计算 `snrDb = signal - noise`

### 判定
RSSI/SNR 命名已在代码层面修复。需确认论文文稿中的命名一致性。降级为提醒。

---

# P1 — 系统架构与论文设计不一致

---

## P1-1: ExecuteSwitch 中关闭 broadcastSocket 违反"控制平面双活"

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 1068-1072  
**函数**: `ExecuteSwitch`

### 现有行为
```cpp
// 切换到AP数据模式时:
if (m_broadcastSocket) {
    Simulator::Cancel(m_beaconEvent);
    m_broadcastSocket->Close();
    m_broadcastSocket = nullptr;
}
```

### 为什么有问题
1. `m_broadcastSocket` 用于发送伪信标 (IBSS_BEACON) 和接收控制消息
2. 切换到 AP 模式时关闭此 socket，意味着终端无法再通过 AdHoc 接口发送控制消息
3. **违反论文设计**: "双接口控制平面双活" — AdHoc 接口的控制平面被禁用
4. 如果终端稍后需要回退到 AdHoc 模式，需要重新创建和绑定 socket

### 建议修改
- `m_broadcastSocket` 应在 ROLE_TERMINAL 启动时创建并保持存活
- 切换到 AP 数据面时:
  - 停止伪信标发送 (`Simulator::Cancel(m_beaconEvent)`)
  - **不关闭** socket
  - 保留 socket 以继续接收 AdHoc 域控制消息

---

## P1-2: ExecuteSwitch C域/AdHoc路径中关闭 staIpSocket

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 1134-1137, 1210-1213  
**函数**: `ExecuteSwitch`

### 现有行为
```cpp
// C域切换 (isDomainC == true) 和 A/B域 AdHoc 切换:
if (m_staIpSocket) {
    m_staIpSocket->Close();
    m_staIpSocket = nullptr;
}
```

### 为什么有问题
1. `m_staIpSocket` 是 STA 接口上用于 IP 地址协商的控制 socket
2. 切换到 AdHoc 数据面时关闭此 socket，STA 接口失去控制面通信能力
3. **违反控制平面双活**: STA 接口无法再接收 IP_OFFER

### 建议修改
- 保留 `m_staIpSocket`，仅停止重试定时器
- 数据面切换不应影响控制 socket

---

## P1-3: FallbackAndRescan 清除 SSID 和关闭控制 socket

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 1450-1484  
**函数**: `FallbackAndRescan`

### 现有行为
```cpp
// 清除 SSID:
if (staMac) staMac->SetSsid(Ssid(""));
if (adhocMac) adhocMac->SetSsid(Ssid(""));

// 关闭控制 socket:
if (m_broadcastSocket) {
    Simulator::Cancel(m_beaconEvent);
    m_broadcastSocket->Close();
    m_broadcastSocket = nullptr;
}
if (m_staIpSocket) {
    m_staIpSocket->Close();
    m_staIpSocket = nullptr;
}
```

### 为什么有问题
1. **SetSsid(Ssid(""))** 清除 AdHoc 和 STA 的 SSID，这相当于断开 Wi-Fi 关联和 IBSS 同步
2. 关闭所有控制 socket，终端完全失去网络连接
3. 虽然 FallbackAndRescan 是"重置后重新扫描"的语义，但这本质上是**硬重置**而非**软回退**
4. 论文设计应为"切换失败时退化到另一接口的数据面"，而非"断开所有连接重新开始"

### 建议修改
- FallbackAndRescan 应仅: 重置评估状态、清除候选表、将当前网络标记为"未知"、重新触发扫描
- **不应**清除 SSID（这会断开链路层关联）
- **不应**关闭控制 socket

---

## P1-4: DisableDeviceLogical 在静态 STA 上使用 Ipv4::SetDown

**文件**: `scratch/train4-new.cc`  
**行号**: 56-62, 778-782  
**函数**: `DisableDeviceLogical`, `main`

### 现有行为
```cpp
void DisableDeviceLogical(Ptr<Node> node, Ptr<NetDevice> dev) {
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    uint32_t idx = ipv4->GetInterfaceForDevice(dev);
    if (idx != uint32_t(-1))
        ipv4->SetDown(idx);  // 硬关闭 IPv4 接口
}

// main中：
if (DynamicCast<AdhocWifiMac>(wdev->GetMac())) {
    ipv4Dev->SetDown(ifIdx);  // 所有域统一：Ad-Hoc DOWN
}
```

### 为什么有问题
1. 对于**静态 STA**（非移动节点），目前的设计是"有中心模式，关闭 AdHoc 接口"
2. 但这意味着静态 STA 只能通过 AP 通信，无法在 AdHoc 模式切换时参与
3. 对于论文实验而言，`DisableDeviceLogical` 本身是正确的（静态 STA 的 AdHoc 接口不需要双活）
4. **但如果将此函数误用于移动节点**，将导致控制平面问题

### 判定
对静态 STA 使用 SetDown 是可接受的拓扑初始化。需确保此函数**不被移动节点的切换路径调用**。当前代码已正确排除了移动节点 (line 766)。降级为 P1 提醒。

---

## P1-5: ExecuteSwitch 在域变化时重置 IP 状态而非保留

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 1008-1035  
**函数**: `ExecuteSwitch`

### 现有行为
```cpp
if (targetDomainId > 0 && targetDomainId != m_currentDomainId) {
    // 释放旧域 IP, 重置为 0.0.0.0
    m_assignedIp = Ipv4Address("0.0.0.0");
    m_staState.ip = Ipv4Address("0.0.0.0");
    m_staState.gw = Ipv4Address("0.0.0.0");
    m_adhocState.ip = Ipv4Address("0.0.0.0");
    m_adhocState.gw = Ipv4Address("0.0.0.0");
    m_currentDomainId = targetDomainId;
}
```

### 为什么有问题
1. 按照论文设计，双接口地址应独立维护。离开域 A 进入域 C 时:
   - STA 接口的域 A 地址可能需要失效（因为是域 A 子网地址）
   - 但**不应同时清除两个接口的状态** — 只应清除与新域不兼容的地址
2. 对于跨域移动场景，AdHoc 接口可能在不同域使用不同子网 (10.100.1.0, 10.100.3.0)，重置是合理的。但 STA 接口在不同域也使用不同子网 (10.1.1.0, 10.3.1.0)
3. 当前的"全清除"策略在语义上等同于重新入网，而非协议设计中的"软切换"

### 建议修改
- 区分"域变化导致子网地址失效"和"接口控制面重置"
- 旧域地址在 IP_RELEASE 后清除
- 接口 UP 状态和 controlActive 始终保留
- 地址清除后立即分配 link-local placeholder，等待新域 IP_REQUEST

---

## P1-6: SetDataPlaneActive 实现不完整

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 1920-1962  
**函数**: `SetDataPlaneActive`

### 现有行为
```cpp
void BlindConnectApp::SetDataPlaneActive(DataPlaneMode mode) {
    m_dataPlaneMode = mode;
    if (mode == DATA_PLANE_AP) {
        m_staState.dataActive = true;
        m_adhocState.dataActive = false;
        SetDefaultRoute(m_staDevice, m_staState.gw);
        BindDataSocketsToActiveDevice(m_staDevice);
        // ...
    } else {
        m_adhocState.dataActive = true;
        m_staState.dataActive = false;
        SetDefaultRoute(m_adhocDevice, m_adhocState.gw);
        BindDataSocketsToActiveDevice(m_adhocDevice);
        // ...
    }
}
```

### 为什么有问题
按论文设计要求，`SetDataPlaneActive` 应统一负责:
1. ✅ 更新 activeDataPlane
2. ✅ 更新 staDataActive / adhocDataActive
3. ✅ 更新默认路由
4. ❌ 保证 STA 和 Ad-Hoc IPv4 interface 均 UP（依赖调用方单独调用 `EnsureBothInterfacesUp`）
5. ❌ 重绑定业务 socket（`BindDataSocketsToActiveDevice` 是空函数）
6. ❌ 更新业务发送门控（无实现）
7. ❌ 记录切换开始和切换完成时间（无实现）
8. ❌ 不关闭控制 socket（无显式保证）
9. ❌ 不清除非活动接口地址（无显式保证）
10. ❌ 不改变 controlActive（无显式保证）

### 建议修改
```cpp
void BlindConnectApp::SetDataPlaneActive(DataPlaneMode mode) {
    Time switchStart = Simulator::Now();

    // 1. 确保双接口 UP
    EnsureBothInterfacesUp();

    // 2. 更新数据面模式
    m_dataPlaneMode = mode;
    m_staState.dataActive = (mode == DATA_PLANE_AP);
    m_adhocState.dataActive = (mode == DATA_PLANE_ADHOC);

    // 3. 控制面始终活跃 (断言)
    NS_ASSERT(m_staState.controlActive && m_adhocState.controlActive);

    // 4. 更新默认路由
    // 5. 重绑定业务 socket
    // 6. 更新发送门控
    // ...

    // 记录切换时间
    m_lastSwitchStartTime = switchStart;
    m_lastSwitchCompleteTime = Simulator::Now();
}
```

---

## P1-7: HandleStaIpRead 中收到 IP_OFFER 后立即 Close socket

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 1687-1688  
**函数**: `HandleStaIpRead`

### 现有行为
```cpp
m_staIpSocket->Close();
m_staIpSocket = nullptr;
```

### 为什么有问题
1. 接收到一次 IP_OFFER 后关闭 socket，后续无法再接收该域的 IP_OFFER
2. 如果终端在同一域内需要重新请求 IP（如租约过期），需要重新创建 socket
3. 控制 socket 应保持打开以持续监听

### 建议修改
- 保留 socket 打开
- 用 `m_staState.ip` 是否为 `0.0.0.0` 来判断是否需要处理 IP_OFFER

---

## P1-8: 控制器仅使用 AddArpEntry 不足以表达双接口终端

**文件**: `scratch/train4-new.cc`  
**行号**: 895-912  
**函数**: `main` (ARP 注册部分)

### 现有行为
控制器通过 `AddArpEntry(ip, mac)` 注册 IP-MAC 映射。移动节点每次只注册一条映射。

### 为什么有问题
1. 双接口终端有两个 MAC 地址和（可能）两个 IP 地址
2. 仅 ARP 注入无法区分 AP 路径和 AdHoc 路径去往同一终端
3. 控制器需要知道终端当前 dataPlaneMode 以正确转发下行数据

### 建议修改
- 利用 `ADHOC_EXT_STAINFO` 消息扩展为: `{ip, mac, ofPort, domainId, interfaceType, dataPlaneMode, lastUpdate}`
- 控制器至少维护 `map<Ipv4Address, HostAttachmentInfo>`

---

## P1-9: VirtualNetDevice 转发函数存在潜在 MAC 地址问题

**文件**: `scratch/train4-new.cc`  
**行号**: 75-121  
**函数**: `WifiToVirtualDevForward`, `ApWifiToVirtualDevForward`

### 现有行为
```cpp
// WifiToVirtualDevForward (AdHoc 路径):
Mac48Address srcMac = Mac48Address::ConvertFrom(dev->GetAddress());

// ApWifiToVirtualDevForward (AP 路径):
Mac48Address srcMac = Mac48Address::ConvertFrom(source);  // STA的MAC
```

### 问题分析
1. **AdHoc 路径** (line 89): `dev->GetAddress()` 返回的是底层 WifiNetDevice 的 MAC 地址。如果此 dev 是 Switch 的 AdHoc 设备，则 source MAC 是 Switch 的 MAC，而非原始发送终端的 MAC
2. **AP 路径** (line 111): 使用 `source` 参数（STA MAC），这个是正确的
3. 两个函数的 destination MAC 都使用 `vdev->GetAddress()` — 这是 VND 的 MAC（即 Switch 的 MAC），但目标应该是下一跳或最终目的地的 MAC
4. **EthernetHeader 可能被重复添加**: 如果 WifiNetDevice 的回调已经被调用且 packet 已包含 EthernetHeader...

### 建议修改
- 增加断言验证 `EthernetHeader` 是否已存在
- 统一为 `InjectWifiPacketToVnd(...)` 函数
- 通过 `SourceMacPolicy` 参数明确源 MAC 策略
- 添加调试日志验证四条关键路径:
  - 单域 AP → VND
  - 单域 AdHoc → VND
  - 跨域 AP → backbone → AP
  - 跨域 AdHoc → backbone → AP

---

# P2 — 统计指标定义错误

---

## P2-1: CalculateScore 的 wLoad=0 和 wSec=0

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 843  
**函数**: `CalculateScore`

### 现有行为
```cpp
double wRssi = 0.60, wHops = 0.05, wLoad = 0.0, wEnergy = 0.25, wSec = 0.0;
```
Load 和安全性的权重为 0，实际**不参与决策**。

### 为什么有问题
1. 不能称为"协同智能感知多属性网络选择算法"
2. Load 信息在 IBSS_BEACON 中携带但从未使用
3. SEC:CHEBYSHEV 验证完成但安全属性权重为 0

### 建议修改
- 设计可配置的权重向量
- 至少 wLoad > 0, wSec > 0 以体现多属性决策

---

## P2-2: EvaluateAndSwitch 中首次入网优先 AP 的硬编码逻辑

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 891-912  
**函数**: `EvaluateAndSwitch`

### 现有行为
```cpp
if (m_currentHops == 99) {  // 首次入网
    // 优先选择 AP 域
    // 若 radar 中暂无 AP, 最多等待 5 轮
}
```

### 为什么有问题
1. 硬编码的"AP 优先"策略不应该是加权多属性算法的隐含行为
2. 如果论文声称"终端根据多属性评分自主选择网络"，则不应对 AP 有特殊偏好
3. 等待 5 轮的逻辑是合理的（避免因短暂未收到 AP Beacon 而误入 AdHoc），但应显式参数化

### 建议修改
- 将 `m_initWaitApRounds` 和 "AP 优先" 逻辑提取为 AccessSelectionStrategy 的参数
- 不应硬编码在核心评估循环中

---

## P2-3: PrintMessageLog 硬编码入网次数

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 44-95  
**函数**: `PrintMessageLog`

### 现有行为
```cpp
std::cout << "--- 第1次入网: 域A 有中心 (AP主导) ---" << std::endl;
std::cout << "--- 第2次入网: 域C 自组织 (Ad-Hoc主导) ---" << std::endl;
std::cout << "--- 第3次入网: 域B 有中心 (AP主导) ---" << std::endl;
```

### 为什么有问题
1. 硬编码假设始终是"域A→域C→域B"的入网序列
2. 如果终端在域A多次入网、或在域C反复切换，日志描述完全错误
3. 实验统计不应依赖硬编码标签

### 建议修改
- 由 ExperimentLogger 根据事件类型自动重建入网事务
- 不再依赖 PrintMessageLog 进行实验统计

---

## P2-4: EvaluateAndSwitch 的 threshold=0 导致无滞后保护

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 956  
**函数**: `EvaluateAndSwitch`

### 现有行为
```cpp
double threshold = 0.0;
if (bestScore > currentScore + threshold || firstJoin) {
    ExecuteSwitch(bestNode);
}
```

### 为什么有问题
1. threshold=0 意味着只要新候选比当前网络好一点点就切换
2. 无滞后保护将导致**乒乓切换**（在两个评分接近的网络之间反复横跳）
3. 论文实验应测量 PingPongCount，但当前 threshold=0 会人为放大乒乓效应

### 建议修改
- 设置非零 hysteresis，如 threshold=0.05
- hysteresis 应可通过 SelectionConfig 配置

---

## P2-5: m_lastSwitchTime 的 8 秒最小驻留硬编码

**文件**: `contrib/mymodule/model/blind-connect-app.cc`  
**行号**: 853  
**函数**: `EvaluateAndSwitch`

### 现有行为
```cpp
if (Simulator::Now() - m_lastSwitchTime < Seconds(8.0)) {
    m_neighborRadar.clear();
    ScheduleEvaluate();
    return;
}
```

### 为什么有问题
1. 8 秒最小驻留时间硬编码，不应作为算法参数
2. 论文实验需要测量 AverageResidenceTime，如果最小驻留被强制执行，测量结果被人为抬高
3. 应通过 ExperimentConfig 配置，且实验测量时应设为 0 或非常小的值

### 建议修改
- 将最小驻留时间提取为可配置参数
- 在需要测量自然驻留时间的实验中设为 0

---

## P2-6: flowmon-results.xml 保存的是全仿真累计值

**文件**: `scratch/train4-new.cc`  
**行号**: 1262  
**函数**: `main`

### 现有行为
```cpp
monitor->SerializeToXmlFile("flowmon-results.xml", true, true);
```
所有实验运行覆盖同一个 `flowmon-results.xml`。

### 为什么有问题
1. 多次运行实验互相覆盖结果
2. XML 中保存的是全仿真累计值，不是窗口统计
3. 无法按 run/seed 区分数据

### 建议修改
- 每个 run 保存到独立目录: `results/expXX/.../runNNN/flowmon-results.xml`
- CSV 窗口统计应作为主要输出格式

---

## P2-7: 缺少标准的 ServiceInterruptionTime 定义

**文件**: 无对应实现  
**函数**: 未定义

### 现有行为
当前代码没有任何"业务中断时间"的测量逻辑。

### 为什么有问题
论文实验二（软切换与硬切换对比）的核心指标 ServiceInterruptionTime 完全没有实现：
- 无业务包序列号和时间戳
- 无 lastOldPathRx 和 firstNewPathRx 记录
- SWITCH_START 到 SWITCH_COMPLETE 不等于业务中断时间

### 建议修改
- 业务发送端增加 sequence number + send timestamp
- 接收端记录 sequence + receive timestamp
- ServiceInterruptionTime = firstNewPathRxTime - lastOldPathRxTime

---

# P3 — 代码结构和可维护性

---

## P3-1: 所有实验逻辑集中于 train4-new.cc (1269 行)

**文件**: `scratch/train4-new.cc`

### 问题
单个文件包含: 拓扑构建、IP 分配、WiFi 配置、OpenFlow 配置、ARP 注册、UDP 流量、FlowMonitor、移动模型、位置配置等所有逻辑。

### 建议
按重构计划分文件:
```
scratch/multimodal-network/
  common/    — ExperimentConfig, TopologyBuilder, TrafficBuilder, ExperimentMetrics, ExperimentLogger
  access/    — CandidateNetwork, CandidateNetworkTable, AccessSelectionStrategy
  scenarios/ — exp01~exp05
```

### ns-3.34 scratch 编译限制
`scratch/` 目录下无 `wscript` 文件。ns-3.34 默认支持 `scratch/` 下的 `.cc` 文件直接编译，也支持 `scratch/subdir/` 子目录中的 `.cc` 文件。但子目录中的 `.cc` 文件需要使用 `../` 相对路径引用模块头文件。另一种方案是在 `src/multimodal-experiment/` 创建独立模块。

---

## P3-2: BlindConnectApp 单文件过大 (2174 行)

**文件**: `contrib/mymodule/model/blind-connect-app.cc`

### 问题
单个类包含: 信标监听、跳频扫描、网络选择、IP 分配、加密、数据面切换等多个职责。

### 建议
将 AccessSelectionStrategy 提取为独立类，BlindConnectApp 通过 Strategy 模式注入。加密逻辑也可提取为独立模块。

---

## P3-3: 缺少统一 ExperimentConfig

### 问题
仿真参数 (simTime, seed, run, speed, trafficLoad, algorithm 等) 散落在 main 函数中。无 CommandLine 支持配置 `--algorithm`, `--switchMode`, `--seed`, `--run` 等。

### 建议
```cpp
struct ExperimentConfig {
    uint32_t seed, run;
    double simTime, mobileSpeed;
    std::string algorithm, switchMode, outputDir;
    // 通过 CommandLine 解析
};
RngSeedManager::SetSeed(config.seed);
RngSeedManager::SetRun(config.run);
```

---

## P3-4: 缺少统一 ExperimentLogger

### 问题
事件记录依赖 `BlindConnectApp::LogMessage` 和 `PrintMessageLog` 的静态 map + 硬编码标签。无法生成结构化 CSV 事件日志。

### 建议
```cpp
class ExperimentLogger {
    void Log(EventType type, uint32_t nodeId, uint32_t domainId,
             const std::string& interfaceType, const std::string& detail);
    void FlushCsv(const std::string& filepath);
};
```
事件类型: NETWORK_FIRST_SEEN, CANDIDATE_SELECTED, SWITCH_START, IP_REQUEST, IP_OFFER, IP_CONFIRM, IP_CONFIGURED, CONTROLLER_SYNC, FIRST_DATA_TX, FIRST_DATA_RX, SWITCH_COMPLETE, MODE_DECISION

---

## P3-5: 缺少 FlowDescriptor 的实验注册

**文件**: `scratch/train4-new.cc`  
**行号**: 1211-1217

### 现有行为
```cpp
// 全部被注释掉:
/*
g_flowDescriptors.push_back({"Flow_A_to_C", ...});
*/
```

### 问题
虽然 FlowDescriptor 结构已定义，但实验场景中没有任何流被注册。MonitorFlow 在无注册流时输出占位数据。

### 建议
- 在 TrafficBuilder 中显式注册论文业务流
- 每个实验场景应定义需要监控的业务流集合

---

## P3-6: BlindConnectApp 缺少对 ScannedNodeInfo 中 nodes 和 secure 字段的有效使用

### 问题
`ScannedNodeInfo` 定义了 `nodes` (域内节点数) 和 `secure` (安全标记) 字段，但：
1. `nodes` 在 CalculateScore 中未使用
2. `secure` 在 CalculateScore 中加权为 0

这些字段在信标解析中被正确填充，但从未影响网络选择决策。

### 建议
在 WEIGHTED_MULTI_ATTRIBUTE 策略中正确加权这些属性。

---

# 附录 A: 全部硬编码值清单

| 文件 | 行号 | 硬编码值 | 用途 |
|------|------|---------|------|
| blind-connect-app.cc | 572 | `10.1.1.1` | AdHoc Beacon 源 IP 默认值 |
| blind-connect-app.cc | 790 | `10.2.1.1` | AP Beacon gateway 字段 |
| blind-connect-app.cc | 843 | `wRssi=0.60, wHops=0.05, wLoad=0.0, wEnergy=0.25, wSec=0.0` | 选网权重 |
| blind-connect-app.cc | 853 | `Seconds(8.0)` | 最小驻留时间 |
| blind-connect-app.cc | 956 | `threshold=0.0` | 切换阈值 (无滞后) |
| blind-connect-app.cc | 1764-1773 | `10.100.1.100` 等 | ConfigureAdhocIpByDomain |
| blind-connect-app.cc | 1809-1818 | `10.1.1.100` 等 | ConfigureStaIpByDomain |
| train4-new.cc | 176-177 | `Port 67, 68, 69` | IP 分配 socket 端口号 |
| train4-new.cc | 284-285 | `simTime=60` | 仿真时间 |
| train4-new.cc | 863 | `8.0 m/s` | 移动速度 |
| train4-new.cc | 1226 | `"flow_stats.csv"` | 输出文件固定名 |
| train4-new.cc | 1262 | `"flowmon-results.xml"` | 输出文件固定名 |

---

# 附录 B: SetDown / Close / SSID Clear 所有调用点

| 文件 | 行号 | 调用 | 资源类型 | 违规? |
|------|------|------|---------|------|
| train4-new.cc | 61 | `ipv4->SetDown(idx)` (DisableDeviceLogical) | 静态 STA IPv4 | ✅ 可接受 (静态 STA) |
| train4-new.cc | 780 | `ipv4Dev->SetDown(ifIdx)` | 静态 STA AdHoc IPv4 | ✅ 可接受 |
| blind-connect-app.cc | 252-267 | `StopApplication` 中 Close 所有 socket | 所有 socket | ✅ 可接受 (仿真结束) |
| blind-connect-app.cc | 1068-1072 | `m_broadcastSocket->Close()` | 控制 socket | ❌ 违规 |
| blind-connect-app.cc | 1134-1137 | `m_staIpSocket->Close()` | 控制 socket | ❌ 违规 |
| blind-connect-app.cc | 1210-1213 | `m_staIpSocket->Close()` | 控制 socket | ❌ 违规 |
| blind-connect-app.cc | 1466-1472 | `FallbackAndRescan` 中 Close | 控制 socket | ❌ 违规 |
| blind-connect-app.cc | 1475 | `staMac->SetSsid(Ssid(""))` | 链路层关联 | ❌ 违规 |
| blind-connect-app.cc | 1478 | `adhocMac->SetSsid(Ssid(""))` | 链路层同步 | ❌ 违规 |
| blind-connect-app.cc | 1687 | `HandleStaIpRead` 中 Close | 控制 socket | ⚠️ 部分违规 |

---

# 附录 C: ns-3.34 API 兼容性注意事项

1. **SignalNoiseDbm**: `MonitorSnifferRx` 回调的第 5 个参数类型。在 ns-3.34 中字段为 `signal` (dBm) 和 `noise` (dBm)。✅ 已正确使用。

2. **WifiPhy::SetOperatingChannel**: 在 ns-3.34 中签名为 `SetOperatingChannel(uint8_t channelNumber, uint16_t frequency, uint16_t channelWidth)`。✅ 已正确使用。

3. **YansWifiPhy::SetChannel**: ns-3.34 支持切换信道对象。✅ 已正确使用。

4. **FlowMonitor::FlowStats::jitterSum**: 在 ns-3.34 中为 `Time jitterSum`。需验证此字段在使用的 ns-3.34 版本中是否存在。

5. **StaWifiMac::IsAssociated()**: ns-3.34 支持。✅ 已正确使用。

6. **VirtualNetDevice::SetSupportsSendFrom**: ns-3.34 支持。✅ 已正确使用。

7. **WifiNetDevice::SendFrom**: ns-3.34 中签名为 `void SendFrom(Ptr<Packet>, Mac48Address, Mac48Address, uint16_t)`。✅ 已正确使用。

8. **Ipv4::AddInterface**: ns-3.34 支持 `int32_t AddInterface(Ptr<NetDevice>)`。✅ 已正确使用。

9. **Ipv4ListRouting**: ns-3.34 支持。✅ 已正确使用。

10. **OFSwitch13LearningController::AddArpEntry**: 需确认此方法在项目使用的 ofswitch13 版本中存在。✅ 已使用。

---

# 附录 D: 审计后的实施路线图

参见主文档 Phase 1-7 计划。本审计结论将作为 Phase 2-7 实施的输入。

**Phase 2 (修复科研指标)** 优先级:
1. P0-1: 修复 AP Beacon gateway 硬编码
2. P0-2: 移除非主导接口硬编码 IP
3. P0-3: 修复 AdHoc Beacon 默认 IP
4. P2-1~P2-5: 修复统计指标

**Phase 3 (收敛双接口软切换)** 优先级:
1. P1-1: 不关闭 broadcastSocket
2. P1-2: 不关闭 staIpSocket
3. P1-3: FallbackAndRescan 不清除 SSID
4. P1-6: SetDataPlaneActive 完整实现
