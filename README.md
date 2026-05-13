# Dataplane Router Implementation in C

## Project Description
Developed a high-performance dataplane router in C that handles IPv4 packet forwarding between subnets. The implementation features optimized routing lookups via a Trie-based Longest Prefix Match (LPM), dynamic ARP resolution with packet queuing, and full ICMP support for network diagnostics and error reporting.

## Key Features
*   **IPv4 Forwarding**: Handles checksum verification, TTL decrementing, and checksum recalculation for every forwarded packet.
*   **Longest Prefix Match (LPM)**: High-speed routing table lookups implemented via a Trie (Prefix Tree) data structure, achieving $O(W)$ time complexity where $W=32$ bits.
*   **Dynamic ARP**: 
    *   Custom queuing system to hold packets while waiting for Layer 2 address resolution.
    *   ARP Cache for efficient IP-to-MAC mapping storage.
    *   Automated generation of ARP Requests and processing of ARP Replies.
*   **ICMP Protocol**: Automatic generation of "Time Exceeded" and "Destination Unreachable" error messages, and support for ICMP Echo Requests (Ping) directed at the router.

## Project Structure
*   `router.c`: The core logic for packet processing and the main event loop.
*   `lib.c / lib.h`: Utility functions for interacting with the link layer.
*   `queue.c / queue.h`: Queue data structure implementation for packet buffering.
*   `protocols.h`: Definitions for Ethernet, IP, ARP, and ICMP headers.

## Usage

### Compilation
The project includes a Makefile for easy building. Run:
```bash
make
```

## Running the Topology
The router is designed to run within a Mininet simulated environment. To start the network topology, execute:

```bash
sudo python3 topo.py
```

### Manual Testing
Once the Mininet CLI is active, you can verify the router's functionality using:

*   **h1 ping h2**: Tests end-to-end routing and dynamic ARP.
*   **h1 traceroute h2**: Tests TTL expiration and ICMP error generation.
*   **ping <router_interface_ip>**: Verifies the ICMP Echo responder.
