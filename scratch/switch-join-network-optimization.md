# 切换入网优化建议

## 1. 架构层面

### 1.1 引入终端状态机

当前 `ExecuteSwitch` 通过延迟 3s、PHY 忙时 5ms 重试等方式管理状态，缺乏显式状态机。

```
建议状态:
IDLE → SCANNING → EVALUATING → SWITCHING → WAIT_ASSOC → REQUESTING_IP → CONNECTED
                                  │                              │
                                  └──────── TIMEOUT ─────────────┘
```

每个状态有明确的进入条件、超时时间和失败回退路径，替代当前的硬编码延迟和重试。

### 1.2 分离信道扫描与网络评估

当前 `EvaluateAndSwitch` 同时做了排序、评分、切换决策、状态清理。建议拆分：

- `CollectCandidates()` — 纯数据收集
- `RankNetworks()` — 评分排序
- `DecideSwitch()` — 决策 (含防抖)
- `ExecuteSwitch()` — 执行 (不含重试逻辑)

---

## 2. 健壮性

### 2.1 IP 请求超时重传

当前 `RequestAdhocIp()` 和 `RequestStaIp()` 只发一次，无超时重试。

```
建议: 指数退避重传
- 第1次: 立即发送
- 第2次: 1s 后重发
- 第3次: 2s 后重发
- 第4次: 4s 后重发 → 放弃，标记失败
```

### 2.2 AP 关联状态确认

当前切换到 AP 后硬等 3s 就请求 IP，但 Association 可能失败。

```
建议: 监听 StaWifiMac::StateChange trace
- 当状态变为 ASSOCIATED 时触发 IP 请求
- 设置 5s 超时，超时后回退到扫描状态
```

### 2.3 IP_RELEASE 确认 + 租约

当前 IP_RELEASE 是 fire-and-forget，如果丢包 IP 永久泄漏。

```
建议:
- GATEWAY/AP_SERVER 收到 IP_RELEASE 后回复 ACK
- 引入 IP 租约时间 (如 30s)，到期未续约自动回收
- 伪信标/数据包中捎带续约信息
```

### 2.4 PHY 状态等待改为事件驱动

当前多处轮询 PHY 状态 (`IsStateTx/IsStateRx/IsStateSwitching/IsStateSleep/IsStateOff`)，5ms 重试。

```
建议: 监听 WifiPhy::StateChange trace
- 当状态变为 IDLE 时回调触发挂起的操作
```

---

## 3. 安全增强

### 3.1 密钥派生

当前直接截取 Chebyshev 共享密钥的前 16 字节作为会话密钥。

```
建议: 使用 HKDF (HMAC-based Key Derivation Function)
  session_key = HKDF-Extract(salt, shared_secret)
  hmac_key    = HKDF-Expand(session_key, "hmac", 32)
  可扩展为: encryption_key, hmac_key 分离派生
```

### 3.2 消息防重放

当前消息无序列号或时间戳，可能被重放攻击。

```
建议:
- IBSS_BEACON: 本身含 TS 字段，接收方检查 TS 单调递增
- IP_REQUEST / IP_OFFER: 添加 32位 sequence number
- 接收方维护 (src_mac, seq) 滑动窗口
```

### 3.3 前向安全性

当前 Chebyshev 密钥协商不提供前向安全性（静态密钥对）。在无中心场景中可接受，但如果安全需求更高：

```
建议: 定期轮换密钥 (如每 5 分钟重新协商)
- GATEWAY 伪信标中新增 KEY_VERSION 字段
- 终端检测版本变化后触发重新协商
```

### 3.4 防拒绝服务

当前 GATEWAY 收到任何 IP_REQUEST 都会分配 IP，可被耗尽 IP 池。

```
建议:
- 对未完成密钥协商的请求限速 (每秒最多 3 个)
- IP 池耗尽后返回 IP_DENY 而非静默丢弃
- 对新 MAC 的分配加入 proof-of-work (轻量挑战)
```

---

## 4. 协议优化

### 4.1 二进制协议替换字符串协议

当前消息格式为 `KEY:VALUE;KEY:VALUE;...`，每帧约 200~400 字节，解析脆弱。

如果节点密度高、带宽紧张，可考虑紧凑二进制 TLV 格式：

```
| Type(1B) | Length(1B) | Value(Variable) |
```

预计压缩 40%~60%。

### 4.2 伪信标自适应发送间隔

当前固定 2s 间隔。在稳定网络中可拉长间隔节省带宽。

```
建议:
- 稳定状态 (跳数和邻居数变化 < 5%): 5s 间隔
- 过渡状态 (有新节点加入/离开): 1s 间隔
```

### 4.3 GATEWAY 冗余

当前每个 Adhoc 域只有一个 GATEWAY，单点故障。

```
建议: 多 GATEWAY 竞选
- 支持 ROLE_BACKBONE 节点升格为 GATEWAY
- 伪信标中携带 GATEWAY_PRIORITY
- 终端优先选择优先级高的 GATEWAY
```

---

## 5. 性能优化

### 5.1 智能扫描调度

当前信道扫描是盲目的 {1,6,11} 轮转，即使已知所有候选网络也继续扫。

```
建议:
- 已知 ≥2 个优质候选时，拉长扫描间隔到 10s
- 当前 SNR 持续 > 25dB 且稳定时，暂停跳频节省功耗
- SNR 下降趋势明显时提前触发密集扫描
```

### 5.2 切换防抖

当前只有阈值 0.02 防乒乓，但在信号边界区仍可能频繁切换。

```
建议:
- 加入最小驻留时间 (如 5s 内不允许再次切换)
- 连续两个评估周期都满足条件才触发切换
- 切换到 Adhoc 后，前 3s 评分结果加权打折避免立即切回
```

### 5.3 邻居信息增量更新

当前伪信标每次都发送完整信息。

```
建议:
- 稳定状态下发送增量更新 (仅变更的字段)
- 每 N 次增量后发一次全量快照做校验
```

---

## 6. 代码质量

### 6.1 消息解析统一化

当前 `ReceiveAdhocBeacon` 中手动 `find("KEY:")` + `substr` 解析每个字段，重复代码多。

```
建议: 实现通用解析器
  std::map<std::string, std::string> ParsePayload(const std::string& payload);
```

### 6.2 MAC 地址比较

`HandleAdhocIpMessage` 中通过 `payload` 字符串提取 MAC 再比较，应该用 `Mac48Address` 直接比较。

### 6.3 资源管理用 RAII

当前 `m_privateKeyBytes`、`m_publicKeyBytes`、`m_sharedSecret` 是裸指针手动 `new/delete`。

```
建议: 使用 std::vector<uint8_t> 或 std::unique_ptr<uint8_t[]>
```

### 6.4 加密初始化应检查返回值

`ComputeSharedSecretFromGateway` 中多处裸指针操作没有空检查就继续使用（`hex_to_bytes` 返回 nullptr 时后续 `mpz_import` 会崩溃）。

---

## 7. 优先级建议

| 优先级 | 条目 | 影响面 |
|--------|------|--------|
| **P0** | 2.1 IP 请求超时重传 | 终端可能永久无法入网 |
| **P0** | 2.2 AP 关联确认替代硬编码 3s | 切换到 AP 的可靠性 |
| **P0** | 6.4 空指针检查 | 崩溃风险 |
| **P1** | 2.3 IP 租约回收 | 长期运行 IP 泄漏 |
| **P1** | 3.2 防重放 | 安全基础 |
| **P1** | 4.3 GATEWAY 冗余 | 单点故障 |
| **P2** | 1.1 状态机 | 可维护性 |
| **P2** | 3.1 HKDF 密钥派生 | 安全增强 |
| **P2** | 5.2 切换防抖 | 边缘区域体验 |
| **P3** | 4.1 二进制协议 | 带宽优化 |
| **P3** | 5.1 智能扫描 | 功耗优化 |
