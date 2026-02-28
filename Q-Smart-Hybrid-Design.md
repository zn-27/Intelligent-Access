# Q-Smart-Hybrid 路由协议完整设计文档

**版本**: v1.0
**日期**: 2025-02-27
**作者**: Claude & 用户协作设计

---

## 目录

1. [设计概述](#一设计概述)
2. [核心架构](#二核心架构)
3. [已确认的设计决策](#三已确认的设计决策)
4. [详细设计](#四详细设计)
5. [设计决策记录](#五设计决策记录)
6. [仿真验证计划](#六仿真验证计划)

---

## 一、设计概述

### 1.1 背景

Q-Smart-Hybrid 是一个基于 Q-Learning 的混合路由协议，结合了 OLSR（主动路由）和 Smart-AODV（按需路由）的优势，通过人工智能动态调节协议参数以适应不同的网络环境。

### 1.2 核心创新点

| 创新点 | 描述 |
|--------|------|
| **软切换机制** | 通过调节 OLSR 发送频率实现平滑过渡，而非硬切换协议 |
| **统一路由表仲裁** | OLSR 和 Smart-AODV 共享同一个 RIB，自动选择最优路由 |
| **完全分布式** | 每个节点是独立的 Q-Learning Agent，无需全局同步 |
| **跨层修复** | MAC 层反馈机制解决路由黑洞问题 |
| **智能感知** | 利用物理层 RSSI/SNR 评估链路质量 |

### 1.3 适用场景

- 高动态移动自组织网络 (MANET)
- 节点移动速度范围: 0 ~ 5 m/s
- 网络密度: 中等到密集
- 对数据包投递率 (PDR) 和延迟都敏感的应用

---

## 二、核心架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Q-Smart-Hybrid 路由协议                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐              │
│  │   OLSR       │    │ Smart-AODV   │    │  Q-Learning  │              │
│  │  (可调频)    │◄──►│   (底层兜底)  │◄──►│   决策引擎    │              │
│  └──────────────┘    └──────────────┘    └──────────────┘              │
│         ▲                                       │                        │
│         │            渐变系数控制               │                        │
│         └───────────────────────────────────────┘                        │
│                           │                                              │
│                   ┌───────▼────────┐                                     │
│                   │  统一路由表     │                                     │
│                   │  (Unified RIB) │                                     │
│                   └────────────────┘                                     │
│                                                                         │
│  MAC 层跨层唤醒: RSSI(动态自适应) + 连续失败次数                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.1 数据流

```
应用层数据包
    │
    ▼
统一路由表查询
    │
    ├─ 有效路由存在 ──→ 加权评分选择路由 ──→ 发送
    │
    └─ 无有效路由 ──→ Smart-AODV RREQ ──→ 路由发现
        │
        ▼
    MAC 层发送
        │
        ├─ 成功 ──→ 更新路由统计
        │
        └─ 失败(连续3次或RSSI异常) ──→ 跨层唤醒 ──→ 强制失效 + 抢占式RREQ
```

---

## 三、已确认的设计决策

| 设计维度 | 确定方案 |
|----------|----------|
| **架构** | 完全分布式，每个节点独立 Q-Learning |
| **路由表** | 统一 RIB，支持 OLSR/AODV 混合条目，包含 HopCount |
| **动作空间** | 4 个离散动作 + 渐变系数平滑过渡 |
| **状态空间** | 5D：速度 + 邻居变化率 + PDR + SNR方差 + 队列长度 |
| **冷启动策略** | 保守启动，默认从 a₂ 开始 |
| **切换迟滞** | 阈值差机制 (Q_new - Q_curr > 0.1) |
| **奖励函数** | 加权归一化 (PDR 50% - 延迟 30% - 开销 20%) |
| **a₄ 模式** | 保留 MPR 选择，仅用于 RREQ 智能转发 |
| **MAC 唤醒** | RSSI 动态自适应 + 失败次数混合判断 |
| **路由选择** | 加权评分 (SNR 50% + 寿命 30% - 跳数 20%) |

---

## 四、详细设计

### 4.1 动作空间与渐变机制

#### 4.1.1 离散动作定义

```
┌─────────────────────────────────────────────────────────────────────┐
│  动作                      HelloInterval    TCInterval    MPR       │
├─────────────────────────────────────────────────────────────────────┤
│  a₁ (完全主动)              1s              3s          正常        │
│  a₂ (弱化主动)              2s              8s          正常        │
│  a₃ (局部主动)              5s              30s         仅1跳       │
│  a₄ (完全按需)              Stop            Stop        仅MPR转发   │
└─────────────────────────────────────────────────────────────────────┘
```

#### 4.1.2 渐变过渡机制

```
动作切换: a₁ → a₂

时间轴:  0s      1s      2s      3s      4s      5s
        │       │       │       │       │       │
Hello:  1.0s    1.2s    1.4s    1.6s    1.8s    2.0s
        │       │       │       │       │       │
系数:   (a₁)    90%     70%     50%     30%     (a₂)
        │       │       │       │       │       │
状态:   [基础→过渡────────────────────→目标]
```

#### 4.1.3 实现代码结构

```cpp
// 决策结构
struct QLearningDecision {
    Action  baseAction;        // 当前基础动作
    Action  targetAction;      // 目标动作
    float   transitionFactor;  // ∈ [0, 1] 渐变系数
};

// 获取实际 Hello 间隔
Time GetActualHelloInterval() {
    Time baseInterval = GetBaseInterval(m_baseAction);
    Time targetInterval = GetBaseInterval(m_targetAction);

    double actual = baseInterval.GetSeconds() * m_transitionFactor
                  + targetInterval.GetSeconds() * (1 - m_transitionFactor);

    return Seconds(actual);
}

// 更新过渡状态
void UpdateTransition() {
    if (m_baseAction != m_targetAction) {
        m_transitionFactor -= 0.1;  // 每秒衰减 10%
        if (m_transitionFactor <= 0) {
            m_baseAction = m_targetAction;
            m_transitionFactor = 1.0;
        }
    }
}
```

### 4.2 Q-Learning 核心参数

#### 4.2.1 状态空间 (5D)

```cpp
struct State {
    double nodeSpeed;           // 节点移动速度 (m/s)
    double neighborChangeRate;  // 邻居变化率 (neighbors/s)
    double currentPdr;          // 当前数据包投递率 [0,1]
    double snrVariance;         // SNR 方差 (稳定性指标)
    uint32_t queueLength;       // 接口队列长度
};
```

#### 4.2.2 奖励函数

```cpp
double CalculateReward(const PerformanceMetrics& metrics) {
    // 各指标独立归一化到 [0,1]
    double pdrScore = metrics.pdr;  // ∈ [0,1]
    double delayScore = metrics.avgDelay / 1.0;  // 假设最大延迟 1s
    double overheadScore = metrics.controlPackets / 10000.0;  // 归一化

    // 加权: PDR 正向，延迟和开销负向
    return 0.5 * pdrScore
         - 0.3 * delayScore
         - 0.2 * overheadScore;
}
```

#### 4.2.3 动作选择迟滞机制

```cpp
bool ShouldSwitchAction(double qNew, double qCurr) {
    // 阈值差: 只有当新动作明显更优时才切换
    return (qNew - Q_curr) > 0.1;
}
```

#### 4.2.4 冷启动策略

```cpp
Action GetInitialAction() {
    // 保守启动: 从 a₂ (弱化主动) 开始
    // 让模型向两端探索，而不是随机
    return A2_WEAK_PROACTIVE;
}

// 可选: 基于先验规则的冷启动
Action GetInitialActionWithHeuristic(double nodeSpeed) {
    if (nodeSpeed > 4.0) {
        return A4_PURE_REACTIVE;  // 高速直接用 AODV
    } else if (nodeSpeed < 1.0) {
        return A1_FULL_PROACTIVE;  // 低速用高频 OLSR
    } else {
        return A2_WEAK_PROACTIVE;  // 中等速度从 a₂ 开始
    }
}
```

### 4.3 统一路由表 (Unified RIB)

#### 4.3.1 路由表条目结构

```cpp
class UnifiedRoutingTableEntry {
public:
    // === 基础字段 ===
    Ipv4Address      m_destination;    // 目标地址
    Ipv4Address      m_nextHop;        // 下一跳
    uint16_t         m_hops;           // 跳数 (必需，用于环路检测)
    uint32_t         m_seqNo;          // 序列号
    Time             m_lifeTime;       // 过期时间（绝对时间）
    RouteFlags       m_flag;           // VALID/INVALID/IN_SEARCH

    // === Smart-AODV 增强字段 ===
    double           m_lastRssi;       // 最后 RSSI (dBm)
    double           m_minSnr;         // 路径最小 SNR (dB)
    Time             m_linkExpiryTime; // 预测链路过期时间

    // === Q-Smart-Hybrid 新增字段 ===
    enum ProtocolSource {
        PROACTIVE_OLSR,      // 来自 OLSR TC 消息
        REACTIVE_SAODV,      // 来自 Smart-AODV RREP
        HYBRID_LEARNED       // Q-Learning 学习到的优质路径
    } m_protocolSource;

    Time     m_routeDiscoveryTime;  // 路由发现时间
    uint32_t m_routeUsageCount;     // 使用次数
    double   m_qValue;              // 该路由的Q值

    // === 路由评分函数 ===
    double GetScore() const {
        double snrScore = m_minSnr / 30.0;  // 归一化，假设最大 30dB
        double lifeScore = GetRemainingLife().GetSeconds() / 30.0;
        double hopPenalty = m_hops / 10.0;

        return 0.5 * snrScore + 0.3 * lifeScore - 0.2 * hopPenalty;
    }
};
```

#### 4.3.2 路由选择策略

当存在多条到同一目的地的路由时：

```cpp
Ptr<UnifiedRoutingTableEntry> SelectBestRoute(
    std::vector<Ptr<UnifiedRoutingTableEntry>>& candidates
) {
    // 策略1: 加权评分
    return *std::max_element(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a->GetScore() < b->GetScore();
        });
}

// 策略2: 场景感知选择
Ptr<UnifiedRoutingTableEntry> SelectRouteByContext(
    std::vector<Ptr<UnifiedRoutingTableEntry>>& candidates,
    Action currentAction
) {
    if (currentAction == A4_PURE_REACTIVE) {
        // 纯 AODV 模式: 只使用 AODV 路由
        for (auto& route : candidates) {
            if (route->m_protocolSource == REACTIVE_SAODV) {
                return route;
            }
        }
    }
    // 其他模式: 使用评分最高
    return SelectBestRoute(candidates);
}
```

### 4.4 MAC 层跨层唤醒机制

#### 4.4.1 触发条件

```cpp
bool ShouldForceInvalidateRoute(const RouteEntry& route) {
    // 条件1: RSSI 动态自适应阈值
    double meanSnr = CalculateMeanSnr(route);  // 滑动窗口统计
    double stdSnr = CalculateStdSnr(route);
    double currentSnr = GetCurrentSnr(route);

    bool rssiTrigger = (currentSnr < meanSnr - 2 * stdSnr);

    // 条件2: 连续失败次数
    bool failureTrigger = (route.consecutiveTxFailures >= 3);

    // 条件3: 预测链路过期
    bool expiryTrigger = (Simulator::Now() > route.predictedExpiryTime);

    // 任一条件触发即强制失效
    return (rssiTrigger || failureTrigger || expiryTrigger);
}
```

#### 4.4.2 唤醒响应

```cpp
void OnMacLayerFailure(Ipv4Address nextHop) {
    // 1. 查找所有使用该下一跳的路由
    std::vector<RouteEntry> affectedRoutes;
    m_routingTable.GetRoutesByNextHop(nextHop, affectedRoutes);

    // 2. 强制失效
    for (auto& route : affectedRoutes) {
        if (ShouldForceInvalidateRoute(route)) {
            route.SetFlag(INVALID);
            // 3. 触发 Smart-AODV 抢占式 RREQ
            SendPreemptiveRreq(route.GetDestination());
        }
    }
}
```

### 4.5 软切换物理过程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          软切换时间轴                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  阶段1: 高频 OLSR (a₁)                                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ • Hello = 1s, TC = 3s                                            │    │
│  │ • 统一路由表充满 OLSR 条目                                        │    │
│  │ • 数据包查表秒发，0 延迟                                          │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                    │                                     │
│                                    ▼ Q-Learning 决策切换                  │
│                                                                         │
│  阶段2: 渐变过渡 (a₁ → a₂)                                               │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ • Hello 从 1s → 2s 线性变化 (约5秒)                              │    │
│  │ • 远端路由条目开始自然老化                                        │    │
│  │ • 局部(1-2跳)仍可用 OLSR                                          │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                    │                                     │
│                                    ▼                                     │
│  阶段3: 弱化 OLSR (a₂)                                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ • Hello = 2s, TC = 8s                                            │    │
│  │ • 远端路由老化完毕，形成"真空"                                    │    │
│  │ • AODV 开始接管远端通信                                           │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                    │                                     │
│                                    ▼                                     │
│  阶段4: 完全按需 (a₄)                                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ • OLSR 停止，仅保留 MPR 用于 RREQ 转发                           │    │
│  │ • 纯 Smart-AODV 模式                                              │    │
│  │ • 按需路由，开销最低                                              │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 五、设计决策记录

> 本节记录了设计过程中讨论的所有决策点、提供的选项和最终选择，作为未来修改方案的参考。

### 决策点 1: 动作切换的"震荡"问题

**问题描述**: Q-Learning 在动作边界可能导致频繁切换，引发协议震荡。

**提供选项**:
| 选项 | 描述 |
|------|------|
| A. 固定迟滞 | 动作切换后，强制保持 N 秒才能再次切换 |
| **B. 阈值迟滞** | 只有当 Q(a_new) - Q(a_curr) > δ 时才切换 |
| C. 无迟滞 | 信任 Q-Learning，允许即时切换 |

**用户选择**: **B (阈值迟滞)**
**阈值设置**: δ = 0.1

---

### 决策点 2: Q-Learning 初始策略（冷启动）

**问题描述**: 仿真开始时 Q 表为空，如何确定初始动作？

**提供选项**:
| 选项 | 描述 |
|------|------|
| A. 完全随机 | 纯 epsilon-greedy，从零学习 |
| B. 先验规则 | 根据节点速度直接映射初始动作 |
| **C. 保守启动** | 默认从 a₂ 开始，让模型向两端探索 |

**用户选择**: **C (保守启动)**

---

### 决策点 3: 奖励函数的量化与归一化

**问题描述**: PDR、延迟、开销量纲差异巨大，如何设计奖励函数？

**提供选项**:
| 选项 | 描述 |
|------|------|
| **A. 加权归一化** | 各指标独立归一化到 [0,1]，再加权 |
| B. 阈值分段 | 非线性奖励，达到阈值给大奖励 |
| C. 增量奖励 | 奖励"改进"而非"绝对值" |

**用户选择**: **A (加权归一化)**
**权重设置**: PDR 50% - 延迟 30% - 开销 20%

---

### 决策点 4: SNR 阈值的具体数值

**问题描述**: MAC 层唤醒需要 SNR 阈值，不同环境差异大。

**提供选项**:
| 选项 | 描述 |
|------|------|
| A. 固定绝对值 | 如 SNR < 10dB → 触发 |
| **B. 动态自适应** | 当前 SNR 低于 μ - 2σ 时触发 |
| C. 相对阈值 | SNR 下降超过 Δt 时触发 |

**用户选择**: **B (动态自适应)**

---

### 决策点 5: Q-Learning 状态空间定义

**问题描述**: 状态 $s$ 包含哪些特征？

**提供选项**: 建议从 2D 开始，避免维度灾难

**用户选择**: **5D 全包含**
- 节点移动速度
- 邻居变化率
- 当前 PDR
- 当前 SNR 方差
- 队列长度

---

### 决策点 6: 仿真验证的对照基线

**问题描述**: 需要与哪些协议对比验证优越性？

**提供选项**: OLSR + Smart-AODV + 静态混合（必需），传统 AODV（可选）

**用户选择**: **全部包含**
- OLSR
- Smart-AODV
- 静态混合
- 传统 AODV

---

### 决策点 7: 路由表是否需要 HopCount

**问题描述**: 统一路由表中是否保留 HopCount 字段？

**用户选择**: **需要保留**
**理由**:
- Smart-AODV 原本就有 (m_hops: uint16_t)
- 用于路径选择、环路检测
- OLSR 的 TC 消息也携带距离信息

---

### 决策点 8: 动作切换机制

**问题描述**: 离散动作跳变 vs 连续动作 vs 混合方案？

**提供选项**:
| 选项 | 描述 |
|------|------|
| **渐变系数** | 4个离散动作 + 线性插值平滑过渡 |
| 细化动作空间 | 增加到 7 个动作 |
| 保持原始设计 | 4 个离散动作直接跳变 |

**用户选择**: **渐变系数**
**过渡时间**: 约 5 秒完成完整过渡

---

### 决策点 9: a₄ 模式下的 OLSR 处理

**问题描述**: 完全按需模式下是否保留部分 OLSR 机制？

**用户选择**: **保留"最小化 OLSR"**
- 仅维持 MPR 选择机制
- 用于 RREQ 智能转发，不产生 TC 开销

---

### 决策点 10: MAC 层跨层唤醒触发条件

**问题描述**: 如何组合多种触发条件？

**用户选择**: **RSSI + 失败次数混合**
- RSSI 动态自适应阈值 (μ - 2σ)
- 连续 3 次发送失败
- 预测链路过期时间

---

### 决策点 11: 路由选择策略

**问题描述**: OLSR 和 AODV 路由同时存在时如何选择？

**提供选项**:
| 选项 | 描述 |
|------|------|
| **加权评分** | SNR 50% + 寿命 30% - 跳数 20% |
| 场景感知选择 | 根据当前动作决定优先级 |
| 最短路径优先 | 传统 AODV 方式 |

**用户选择**: **策略1 加权评分**

---

## 六、仿真验证计划

### 6.1 对照协议组

| 协议 | 验证目的 |
|------|----------|
| **Q-Smart-Hybrid** | 主协议 |
| **纯 OLSR** | 证明高速场景下按需模式更优 |
| **纯 Smart-AODV** | 证明混合模式优于单一协议 |
| **静态混合** | 证明 Q-Learning 自适应优于固定比例 |
| **传统 AODV** | 证明 Smart 特征（SNR/RSSI）的价值 |

### 6.2 性能指标

| 指标 | 说明 |
|------|------|
| **PDR** | 数据包投递率，首要指标 |
| **E2E Delay** | 端到端延迟 |
| **Routing Overhead** | 控制报文开销 |
| **Convergence Time** | 协议切换收敛时间 |

### 6.3 仿真场景

| 参数 | 范围 |
|------|------|
| 节点数量 | 20-50 |
| 仿真区域 | 1000m × 1000m |
| 节点速度 | 0-5 m/s (多档测试) |
| 仿真时间 | 300s |
| 数据流 | CBR, 4 packets/s |

---

## 附录 A: 术语表

| 术语 | 全称 | 说明 |
|------|------|------|
| OLSR | Optimized Link State Routing | 优化链路状态路由协议（主动式） |
| AODV | Ad Hoc On-Demand Distance Vector | 按需距离矢量路由协议 |
| Smart-AODV | - | 增强版 AODV，支持链路质量感知 |
| MPR | Multi-Point Relay | 多点中继，OLSR 的优化机制 |
| RIB | Routing Information Base | 路由信息库（路由表） |
| RSSI | Received Signal Strength Indicator | 接收信号强度指示 |
| SNR | Signal-to-Noise Ratio | 信噪比 |
| PDR | Packet Delivery Ratio | 数据包投递率 |
| RREQ | Route Request | 路由请求 |
| RREP | Route Reply | 路由应答 |
| TC | Topology Control | 拓扑控制消息 |

---

## 附录 B: 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
|------|------|----------|------|
| v1.0 | 2025-02-27 | 初始版本，完整设计文档 | Claude & 用户 |

---

*本文档由 Claude AI 辅助生成，基于与用户的协作讨论。*
