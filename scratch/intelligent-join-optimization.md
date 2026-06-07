# 智能入网优化方案

## 现状分析

### 当前入网决策的不足

当前 `BlindConnectApp::CalculateScore()` (blind-connect-app.cc:578-592) 使用**静态加权线性求和**：

```
Score = 0.35*RSSI - 0.15*Hops + 0.0*Load + 0.20*Energy + 0.30*Sec
```

存在的问题：

| 问题 | 影响 |
|------|------|
| 权重固定 | 高速移动场景和静止场景用同一套权重，RSSI 权重在快速信道变化下过低 |
| 无历史记忆 | 每次决策独立，上次切到某网络后立即断开也不影响下次评分 |
| 单向评估 | 只看网络广播的"平均值"，不知道自己实际能获得的吞吐 |
| 无应用感知 | 语音业务和文件传输的选网标准应该不同 |
| 缺乏预测 | 等 SNR 真的掉下去了才切，而不是提前准备 |

### 已有的 Q-learning 基础设施

本项目中已有三层 Q-learning，可从这些实现中复用模式和思路：

| 层级 | 实现 | 状态空间 | 决策 |
|------|------|---------|------|
| 路由层 | `smart-aodv-qlearning` | SNR×Hops (24 states) | 选下一跳路径 |
| 协议层 | `q-smart-hybrid` | Speed×NCR×PDR×SNRvar×Queue (3125 states) | OLSR/AODV 模式 |
| 控制器层 | `NetworkModeQLearning` | Distance×Var×Loss (8 states) | MULTI/ADHOC 域级切换 |

**但缺少终端侧的智能选网**——这是本文聚焦的优化点。

---

## 方案一：终端强化学习选网 (推荐落地)

### 1.1 整体思路

将 `CalculateScore()` 的静态加权替换为 **Q-learning 在线学习**，让每个终端根据自身实际的入网体验（切换后的真实吞吐、延迟、驻留时长）来学习"在某个状态下选择某个网络有多好"。

### 1.2 状态空间设计

不追求大而全，选 4 个终端可本地获取、与决策高度相关的维度：

```
State = { speedLevel, signalTrend, appDemand, currentNetworkType }
```

| 维度 | 离散化 | 含义 |
|------|--------|------|
| **speedLevel** (3 levels) | 低速 <3m/s, 中速 3-10m/s, 高速 >10m/s | 移动性影响网络驻留稳定性 |
| **signalTrend** (3 levels) | 上升、平稳、下降 | 当前网络 SNR 的变化趋势（用最近 3 次采样的线性拟合斜率） |
| **appDemand** (2 levels) | 低需求 (TCP bulk)、高需求 (UDP 实时流) | 不同业务对延迟和吞吐敏感度不同 |
| **currentNetworkType** (2 levels) | AP, Adhoc | 当前所在网络类型 |

共 3×3×2×2 = **36 个状态**，Q 表可控。

> 状态编码参考 `ofswitch13-learning-controller.h:1041-1052` 的 `StateToId()` 位编码方式。

### 1.3 动作空间

```cpp
enum Action {
    STAY_CURRENT = 0,   // 留在当前网络
    SWITCH_TO_BEST_AP = 1,   // 切到评分最高的 AP
    SWITCH_TO_BEST_ADHOC = 2 // 切到评分最高的 Adhoc
};
```

动作数 = 3，Q 表 36×3 = 108 个条目，内存开销极小。

### 1.4 奖励函数

**核心思路**: 奖励不是"网络好不好"，而是"切换这个决策带来的实际改善"。

```
Reward = throughputImprovement * 0.4
       + latencyReduction * 0.3
       + dwellTimeBonus * 0.2
       - switchingCost * 0.1
```

| 分量 | 计算方式 | 说明 |
|------|---------|------|
| **throughputImprovement** | `(newTp - oldTp) / max(newTp, oldTp, 1)` 映射到 [-1,1] | 切过去后吞吐是否真的变好 |
| **latencyReduction** | `(oldDelay - newDelay) / max(oldDelay, newDelay, 1ms)` 映射到 [-1,1] | 延迟是否下降 |
| **dwellTimeBonus** | `min(dwellSeconds, 30) / 30` 映射到 [0,1] | 能在该网络稳定驻留多久（防乒乓） |
| **switchingCost** | 固定 -0.15 | 每次切换有开销（断连、信令） |

**关键**: 奖励在切换完成后 **N 秒才结算**（建议 5s），给网络稳定时间，避免用切换瞬间的瞬时值。

### 1.5 探索策略

参考 `NetworkModeQLearning` 的动态 epsilon (ofswitch13-learning-controller.cc:1144-1192)：

```
baseEpsilon = 0.15

if (当前网络 SNR 稳定 && 吞吐满意):
    epsilon = baseEpsilon * 0.4     // 稳定时少探索，减少不必要的切换
elif (SNR 持续下降 || 吞吐恶化):
    epsilon = baseEpsilon * 2.5     // 恶化时多探索，快速寻找替代网络
else:
    epsilon = baseEpsilon
```

### 1.6 与 Controller Q-learning 的协同

当前 `NetworkModeQLearning` 在控制器侧决定域级模式（MULTI/ADHOC），终端 Q-learning 在本地决定接入哪个网络。两者可以信息互通：

```
Controller → Terminal (通过 OpenFlow / 伪信标捎带):
  - 域级 Q-learning 的推荐模式
  - 当前域内各链路的聚合质量

Terminal → Controller (通过上行消息):
  - 终端本地的选网经验 Q 值 (可选，用于控制器聚合学习)
```

这形成**分层强化学习**：控制器做粗粒度的域模式决策，终端做细粒度的接入点选择。

### 1.7 实现改动量估计

| 文件 | 改动 |
|------|------|
| `blind-connect-app.h` | 新增 `TerminalQLearning` 成员类，状态/动作定义 |
| `blind-connect-app.cc` | `EvaluateAndSwitch()` 中调用 Q-learning 替换静态评分；新增 `ComputeReward()`、`UpdateQTable()`；`ExecuteSwitch()` 中记录切换前状态用于结算奖励 |
| 新增 `terminal-qlearning.h/.cc` | Q-learning 核心逻辑（可参考 `smart-aodv-qlearning` 的实现模式） |

改动集中在 BlindConnectApp 内部，不影响现有协议和消息格式。

---

## 方案二：情境感知多策略决策

### 2.1 思路

不是一套权重打天下，而是根据终端的**当前任务情境**动态切换评分策略。

```cpp
enum TerminalContext {
    CONTEXT_IDLE,         // 空闲，无活跃流 → 优先节能，选低负载网络
    CONTEXT_STREAMING,    // UDP 实时流 → 优先低延迟、低抖动，容忍一定丢包
    CONTEXT_BULK,         // TCP 批量传输 → 优先高吞吐、低丢包
    CONTEXT_EMERGENCY,    // 紧急通信 → 优先可达性，快速入网
};
```

不同情境下使用不同的评分权重矩阵：

| 权重 | IDLE | STREAMING | BULK | EMERGENCY |
|------|------|-----------|------|-----------|
| wRssi | 0.15 | 0.25 | 0.30 | 0.50 |
| wHops | 0.05 | 0.30 | 0.10 | 0.10 |
| wLoad | 0.40 | 0.10 | 0.25 | 0.0 |
| wEnergy | 0.30 | 0.05 | 0.05 | 0.0 |
| wSec | 0.10 | 0.30 | 0.30 | 0.40 |

**应用感知方式**: 终端本地检测 socket 的活跃流类型（UDP 端口 9 为流媒体，TCP 为批量），无需协议改动。

T此方案实现简单（仅修改 `CalculateScore`），但仍是启发式，不如方案一能自适应学习。

---

## 方案三：预测性切换

### 3.1 思路

不等 SNR 掉到阈值以下才切，而是**预测 SNR 的下降趋势并提前准备候选网络**。

### 3.2 实现

```
PredictiveHandover()
  │
  ├─ 1. 维护最近 N=10 次 SNR 采样的滑动窗口
  ├─ 2. 线性回归计算 SNR 衰减速率 (dB/s)
  ├─ 3. 估算到达"不可用阈值"(-80dBm) 的剩余时间 T_left
  └─ 4. 若 T_left < 3s:
       ├─ 提前开始密集扫描候选信道
       ├─ 对候选 Adhoc 域提前完成密钥协商 (预认证)
       └─ 当前 SNR < -75dBm 时执行无缝切换
```

### 3.3 预认证 (Make-Before-Break)

当前切换是 hard handover（先断后连）。改进为：

```
当前在 AP 域 A，信号在衰减:

  T-3s: 检测 SNR 下降趋势
  T-2s: 扫描发现 Adhoc 域 C
  T-1s: 后台完成 Chebyshev 密钥协商，预先请求 IP
  T-0s: IP 已分配完毕，切换默认路由到 Adhoc 接口
  T+0s: 断开 AP 关联，发送 IP_RELEASE

  中断时间: ~0ms (路由切换级别) vs 当前 >3s (关联+DHCP)
```

这需要 BlindConnectApp 支持**双网卡同时持有 IP** 状态，并在切换时做路由层面的原子切换。

---

## 方案四：多臂老虎机冷启动

### 4.1 问题

终端首次启动时 Q 表全零，需要大量试错才能学到好策略。

### 4.2 思路：Upper Confidence Bound (UCB)

Q-learning 在做动作选择时，不只用 Q 值，还考虑**探索价值**：

```
UCB(s, a) = Q(s, a) + c * sqrt(ln(N) / n(s, a))
```

- `Q(s,a)`: 当前估计值
- `n(s,a)`: 该状态-动作对的被选择次数
- `N`: 该状态被访问的总次数
- `c`: 探索系数 (建议 1.0~2.0)

效果：**没试过的网络天然具有较高的 UCB 值**，终端会主动尝试陌生网络，加速冷启动收敛。

### 4.3 经验共享初始化

同一域内的终端可以共享 Q 表经验：

```
GATEWAY 伪信标中捎带:
  - 当前域内终端的平均 Q 值分布概要 (压缩后的少量字节)

新终端入网时:
  - 用自己的初始 Q 值与 GATEWAY 广播的群体经验做加权融合
  - Q_init = 0.3 * self + 0.7 * crowd  
```

---

## 方案五：端到端智能入网完整流程

结合以上方案，一个"智能终端"的入网决策流程如下：

```
智能终端入网流程 (每 1s 一个决策周期)
  │
  ├─ [感知层]
  │   ├─ 采集 SNR 滑动窗口 → 计算 signalTrend
  │   ├─ 获取 GPS/位置 → 计算 speedLevel
  │   ├─ 检测 socket 活跃流 → 判定 appDemand
  │   └─ 收集候选网络列表 (AP + Adhoc 伪信标)
  │
  ├─ [预测层]
  │   ├─ 线性回归 SNR 趋势 → 估算 T_left
  │   └─ T_left < 3s → 触发密集扫描 + 预认证
  │
  ├─ [决策层] ─ Q-learning ChooseAction(state)
  │   ├─ 构建当前状态 (speed, trend, demand, currentNet)
  │   ├─ 用 UCB 选择动作 (STAY / SWITCH_AP / SWITCH_ADHOC)
  │   └─ 记录 (state, action, timestamp) 用于后续奖励结算
  │
  ├─ [执行层]
  │   ├─ STAY: 继续监听
  │   ├─ SWITCH_AP: 预认证→锁信道→关联→请求IP
  │   └─ SWITCH_ADHOC: 密钥协商→请求IP→路由切换
  │
  └─ [学习层] ─ 5s 后结算奖励
      ├─ 采集实际吞吐/延迟/驻留时长
      ├─ 计算 Reward
      ├─ Q(s,a) += alpha*(reward + gamma*max(Q(s')) - Q(s,a))
      └─ 若切换失败: reward = -0.5 (强负反馈)
```

---

## 落地优先级

| 优先级 | 方案 | 理由 |
|--------|------|------|
| **P0** | 方案二: 情境感知多策略 | 改动最小（只改 `CalculateScore` 加一个 context 参数），立刻改善流媒体/批量传输的选网质量 |
| **P1** | 方案一: 终端 Q-learning | 核心智能化升级，复用现有 Q-learning 模式，36 状态 Q 表轻量可落地 |
| **P1** | 方案四: UCB 探索 + 经验共享 | 解决冷启动，可与方案一合并实现 |
| **P2** | 方案三: 预测性切换 + 预认证 | 需要双 IP 持有和路由原子切换，改动较大，但对移动场景体验提升显著 |
| **P2** | 方案一扩展: 分层 RL 协同 | 终端 Q-learning 与 Controller NetworkModeQLearning 信息互通 |
