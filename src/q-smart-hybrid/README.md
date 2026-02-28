# Q-Smart-Hybrid Routing Protocol

## Overview

Q-Smart-Hybrid is a Q-Learning based hybrid routing protocol for Mobile Ad Hoc Networks (MANETs) that combines the advantages of proactive (OLSR-like) and reactive (AODV-like) routing approaches.

## Key Features

- **Adaptive Routing**: Dynamically switches between proactive and reactive modes based on network conditions
- **Q-Learning Decision Engine**: Uses reinforcement learning to optimize routing decisions
- **Soft Switching**: Gradual transition between modes to avoid protocol oscillation
- **Cross-Layer Design**: MAC layer feedback for link quality monitoring
- **Unified Routing Table**: Single routing table supporting multiple protocol sources

## Protocol Modes (Actions)

| Action | Mode | Hello Interval | TC Interval | Description |
|--------|------|----------------|-------------|-------------|
| A1 | Full Proactive | 1s | 3s | Complete OLSR behavior |
| A2 | Weak Proactive | 2s | 8s | Reduced OLSR frequency |
| A3 | Local Proactive | 5s | 30s | Minimal OLSR (1-hop only) |
| A4 | Pure Reactive | Stop | Stop | Pure Smart-AODV mode |

## Q-Learning State Space (5D)

1. **Node Speed**: Movement speed (0-5 m/s)
2. **Neighbor Change Rate**: Rate of neighbor topology changes
3. **PDR**: Current packet delivery ratio
4. **SNR Variance**: Link quality stability indicator
5. **Queue Length**: Interface queue utilization

## Reward Function

```
Reward = 0.5 × PDR - 0.3 × Delay - 0.2 × Overhead
```

## Usage

### Basic Example

```cpp
#include "ns3/q-smart-hybrid-helper.h"

// Create nodes
NodeContainer nodes;
nodes.Create (20);

// Install Internet stack with Q-Smart-Hybrid routing
InternetStackHelper internet;
QSmartHybridHelper qshHelper;
qshHelper.Set ("QlearningInterval", TimeValue (Seconds (5)));
internet.SetRoutingHelper (qshHelper);
internet.Install (nodes);

// Assign IP addresses and continue setup...
```

### Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| HelloInterval | 1s | HELLO message emission interval |
| ActiveRouteTimeout | 3s | Route validity period |
| MaxQueueLen | 64 | Maximum buffered packets |
| MaxQueueTime | 30s | Maximum buffering time |
| QlearningInterval | 5s | Q-Learning decision interval |

### Running Examples

```bash
# Basic example
./waf --run q-smart-hybrid-example

# With custom parameters
./waf --run "q-smart-hybrid-example --numNodes=30 --simTime=200 --nodeSpeed=3"

# Comparison with other protocols
./waf --run q-smart-hybrid-compare
```

## Files Structure

```
src/q-smart-hybrid/
├── model/
│   ├── q-smart-hybrid-routing-protocol.h/cc  # Main protocol class
│   ├── q-smart-hybrid-rtable.h/cc            # Unified routing table
│   ├── q-smart-hybrid-qlearning.h/cc         # Q-Learning engine
│   └── q-smart-hybrid-packet.h/cc            # Protocol packets
├── helper/
│   └── q-smart-hybrid-helper.h/cc            # Installation helper
├── examples/
│   ├── q-smart-hybrid-example.cc             # Basic example
│   └── q-smart-hybrid-compare.cc             # Performance comparison
└── wscript                                    # Build configuration
```

## Building

```bash
cd ns-3.34
./waf configure
./waf build
```

## Performance Metrics

The protocol optimizes for:
- **Packet Delivery Ratio (PDR)**: Primary metric (50% weight)
- **End-to-End Delay**: Secondary metric (30% weight)
- **Routing Overhead**: Control packet efficiency (20% weight)

## Cross-Layer Features

- **RSSI Monitoring**: PHY layer signal strength tracking
- **SNR Statistics**: Sliding window analysis for link stability
- **MAC Failure Detection**: Proactive route invalidation

## References

- RFC 3626: Optimized Link State Routing Protocol (OLSR)
- RFC 3561: Ad hoc On-Demand Distance Vector (AODV) Routing
- Sutton & Barto: Reinforcement Learning: An Introduction

## Authors

Q-Smart-Hybrid Implementation Team

## License

GNU General Public License v2
