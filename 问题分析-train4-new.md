# train4-new.cc 入网/切换/IP分配 问题分析

## 仿真环境

- 场景: 单控制器跨域 SDN (域A/B/C)，移动节点从左侧水平穿越
- 分析文件: `scratch/train4-new.cc`, `contrib/mymodule/model/blind-connect-app.cc`
- 仿真时间: 30s / 60s

---

## 问题 1: 网关IP填写错误 (致命)

**位置:** `blind-connect-app.cc:358` (ReceiveApSniffer), `blind-connect-app.cc:424` (ReceiveAdhocBeacon)

IP_OFFER 中的 GW 字段填入的是 `m_poolBase`（网段地址），而非实际网关IP：

```
实际输出:  GW:10.1.1.0    ← 错误，这是网段地址
正确值:    GW:10.1.1.5    ← AP VND 的 IP

实际输出:  GW:10.2.1.0    ← 错误
正确值:    GW:10.2.1.3

实际输出:  GW:10.100.3.0  ← 错误
正确值:    GW:10.100.3.1
```

**影响:** `ConfigureIpOnInterface` 将默认路由指向 `10.1.1.0`，这是一个无效的下一跳地址。控制器 ARP 表中没有此地址的条目，ARP 解析失败导致所有数据包无法发送。

**根因:** `m_poolBase` 是设计上的 IP 池网络地址（如 `10.1.1.0`），不代表任何接口。网关应该是 AP VND 或 Adhoc VND 上配置的实际 IP。

---

## 问题 2: AP Server 的 PHY 嗅探器从未收到 IP_REQUEST

**位置:** `blind-connect-app.cc:328-368` (ReceiveApSniffer)

三次独立运行中，均未出现 `[ApServer-Sniffer] 收到 IP_REQUEST` 输出。

**调用链:**
```
终端: m_staDevice->Send(p, broadcast, 0x0800)  ← 绕过MAC层，直发PHY
  ↓
AP:   WifiPhy::MonitorSnifferRx → ReceiveApSniffer  ← 从未触发
```

**可能原因:**
1. 终端在未关联状态下 (`IsAssociated=0`) 发送 IP_REQUEST，此时 StaWifiMac 处于扫描跳频中，PHY 可能在 ch6/ch11 上发送，而 AP 在 ch1 监听
2. AP 端 PHY 处于 TX 状态（每 102ms 发送 Beacon）时错过接收窗口
3. 终端即使关联后 (`IsAssociated=1`, t=15.5s) 发送，AP 端仍无反应，不排除 ns-3 中 `WifiNetDevice::Send()` 直接发出的帧在接收端的 MonitorSnifferRx 路径存在限制

**影响:** IP 分配的主路径失效。终端偶尔能通过其他路径获得 IP（如 HandleStaIpRead socket 路径），但极不稳定。

---

## 问题 3: STA 关联延迟 + IP 请求时机过早

**位置:** `blind-connect-app.cc:919-923`, `scratch/train4-new.cc:393`

仿真输出清晰显示了时序错配：

```
t=8.5s   ExecuteSwitch → AP 域 A, staMac->SetSsid("A")
t=11.5s  RequestStaIp (IsAssociated=0, +3s)  → 发送时 PHY 在扫描跳频
t=13.5s  RetryStaIp  (IsAssociated=0, +5s)  → 仍未关联
t=13.56s [AssocTrace] sw1 AID=5 STA=00:..:1c  → 此时才关联成功
t=15.5s  RetryStaIp  (IsAssociated=1, +7s)  → 已关联但 AP 仍收不到
```

**问题:**
- `RequestStaIp` 延迟仅 3 秒，但 StaWifiMac 完成扫描+认证+关联需要 ~5 秒
- 前两次 IP 请求在 `IsAssociated=0` 时发出，PHY 处于扫描跳频状态，发送信道不确定
- 由于问题 2，即使 t=15.5s 关联后发送，AP 也收不到

**建议:** 将 `RequestStaIp` 延迟从 3s 改为等待关联确认后再触发，或在关联回调中触发。

---

## 问题 4: 移动节点在仿真时间内无法抵达任何域

**位置:** `scratch/train4-new.cc:765-776`

```
起始位置: (-250, 200)
速度:     3 m/s
60s 终点:  (-70, 200)

距 sw1(域A at -50,150):  最近 54m  (t=60s)
距 sw3(域C at 0,150):    最近 86m  (t=60s)
距 sw2(域B at 50,150):   最近 130m (t=60s)
```

**影响:** 尽管信号强度（-56~-61 dBm）允许通信，但移动节点始终处于域A边缘，从未穿过域C或域B。**跨域切换逻辑完全未被测试。** 当前仿真等价于"远距离静态节点缓慢靠近域A"的场景。

**建议:** 将起始位置改为 `(-120, 200)` 或将速度提高到 `10+ m/s`，确保节点在 60s 内穿越全部三个域。

---

## 问题 5: 其他问题

### 5.1 输出与实际值不一致

`scratch/train4-new.cc:775`:
```cpp
cvmm->SetVelocity(Vector(3.0, 0.0, 0.0));   // 实际: 3 m/s
std::cout << "速度: (8, 0, 0)" << std::endl; // 打印: 8 m/s
```

### 5.2 HMAC 签名对 AP_SERVER 无效

终端在 IP_REQUEST 中携带 HMAC 签名 (`blind-connect-app.cc:1177-1179`)，但只有 ROLE_GATEWAY 执行 Chebyshev 密钥协商 (`blind-connect-app.cc:116-118`)。ROLE_AP_SERVER (sw1/sw2) 不做密钥交换，无法验证 HMAC。签名仅增加了 ~50 字节负载。

### 5.3 跨域流 A→C 在当前版本全部丢包

前次 `flow_stats.csv` 记录显示 `StaA[0] → StaC[2]-Adhoc (port 13)` 有 100Kbps 吞吐量。当前代码运行三次，均显示:

```
Flow (10.1.1.1 -> 10.100.3.4)
  Tx Bytes: 2439588  Rx Bytes: 0
  Tx Packets: 2319  Rx Packets: 0
  丢包率: 100 %
```

说明 backhaul 路由或 OF 流表在最近的代码变更中有退化。

---

## 总结优先级

| 优先级 | 问题 | 影响 |
|--------|------|------|
| P0 | 网关IP填成网段地址 | 移动节点即使拿到IP也无法通信 |
| P0 | AP PHY嗅探器收不到IP_REQUEST | IP分配主路径失效 |
| P1 | STA关联延迟 vs IP请求时机 | 前两次IP请求必然失败 |
| P1 | 移动节点无法抵达任何域 | 跨域切换逻辑零覆盖 |
| P2 | 跨域流当前全部丢包 | 静态STA间通信也失败 |
| P2 | 输出信息与实际值不一致 | 调试误导 |
| P3 | HMAC对AP_SERVER无意义 | 轻微性能浪费 |
