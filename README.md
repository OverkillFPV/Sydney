<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Sydney Router Firmware</h1>

A fork of the Meshtastic firmware optimized for Sydney's mesh

[![Based on Meshtastic](https://img.shields.io/badge/Based%20on-Meshtastic-blue)](https://meshtastic.org)
[![NSW Mesh](https://img.shields.io/badge/Part%20of-NSW%20Mesh-green)](https://wiki.nswmesh.au)

</div>

## Overview

This firmware fork is specifically designed to improve mesh network performance in high-density urban environments with difficult terrain like Sydney. It implements strategic packet management and routing optimizations to reduce network congestion and improve reliability in areas with many active nodes. Also includes the addition of Routers as Zero-Hop Rebroadcasters allowing for chains of Routers not normally available due to the hop limit.

### Key Features

#### Router Role Optimizations
- Only affects Router/Repeater/Router_Late modes
- Standard client behavior remains unchanged
- Different routing behaviors for Router vs Router_Late roles

#### Packet Management
- **Telemetry Control**:
  - Routers drop telemetry packets to reduce network load
  - Router_Late nodes retransmit with hop count limited to 1
  - 15-minute minimum interval between packets from same node
- **Position Packets**:
  - Normal routing behavior preserved
  - Zero-hop treatment disabled
  - 15-minute minimum interval enforced
- **Encrypted Packets**:
  - 5-minute minimum interval to prevent mesh flooding
  - Zero-hop treatment disabled
  - Addresses issues with encrypted nodes overwhelming the network
- **User/Info Packets**:
  - 15-minute minimum interval
- **Traceroute Management**:
  - Limited to 5 packets per 15-minute window
  - Reduces to 1 packet per 15 minutes after initial burst
  - Counter resets after 15 minutes of inactivity

#### Network Protection
- **Hop Count**: 
  - Prevents zero-hopping on first hop when hop limit is 7
  - Routers roles zero hop packets without decreasing hop count for most packets
  - Adjusts lower hop start upward with each zero hop to preserve hop count seen in node list
- **MQTT Integration**: 
  - Blocks forwarding of MQTT packets
  - Prevents routing loops between mesh and MQTT
- **Blocked Node List**: 
  - Supports blacklisting problematic nodes
  - Helps maintain network stability

### Why These Changes?

These modifications address specific challenges in the Sydney mesh network:
- Reduces channel congestion from frequent updates
- Prevents flooding from encrypted nodes
- Improves overall mesh reliability
- Increases max mesh range
- Enables better scaling in dense urban environments

### NSW Mesh Resources

- **[Wiki](https://wiki.nswmesh.au/)**: 
  - Comprehensive documentation
  - Setup guides
  - Best practices for node deployment
  - Network architecture information
- **[Network Map](https://map.nswmesh.au/map)**:
  - Real-time mesh visualization
  - Node locations and connections
  - Network coverage analysis
- **[Network Dashboard](https://dash.nswmesh.au/d/edqkge9mf7v28g/main-dashboard?orgId=1&refresh=1m)**:
  - Live network statistics
  - Performance monitoring
  - Network health indicators
  - Traffic analysis

### Installation

1. Flash this firmware to devices intended for router roles
2. Configure as Router, Router_Late, or Repeater based on deployment location
3. Position according to NSW Mesh deployment guidelines
4. Monitor performance via the Network Dashboard

For detailed setup instructions and best practices, visit our [wiki](https://wiki.nswmesh.au/).

## Contributing

This is a community-maintained fork focusing on urban mesh deployment optimization. Contributions and feedback are welcome through issues and pull requests.

## Original Project

Based on [Meshtastic](https://meshtastic.org). Original firmware repository at [GitHub](https://github.com/meshtastic/firmware).

