#!/usr/bin/python2

import os
import signal
import sys
from mininet.log import setLogLevel
from mininet.net import Mininet
from mininet.topo import Topo
from mininet.link import Link
from mininet.cli import CLI

import info

def signal_handler(signal, frame):
    sys.exit(0)

class FullTopo(Topo):
    def build(self, nr=2, nh=2):
        routers = []
        # Creare Routere
        for i in range(nr):
            routers.append(self.addHost(info.get("router_name", i)))

        # Conexiuni Router-to-Router (Full Mesh)
        for i in range(nr):
            for j in range(i + 1, nr):
                ifn = info.get("r2r_if_name", i, j)
                self.addLink(routers[i], routers[j], delay="1ms", intfName1=ifn,
                             intfName2=ifn)

        # Conexiuni Host-to-Router
        for i in range(nr):
            for j in range(nh):
                hidx = i * nh + j
                host = self.addHost(info.get("host_name", hidx))
                i1 = info.get("host_if_name", hidx)
                i2 = info.get("router_if_name", j)
                self.addLink(host, routers[i], delay="1ms", intfName1=i1, intfName2=i2)

class NetworkManager(object):
    def __init__(self, net, n_routers, n_hosts):
        self.net = net
        self.routers = []
        for i in range(n_routers):
            r = self.net.get(info.get("router_name", i))
            hosts = []
            for j in range(n_hosts):
                hidx = i * n_hosts + j
                h = self.net.get(info.get("host_name", hidx))
                hosts.append(h)
            self.routers.append((r, hosts))

    def setup_network(self):
        """Configurare IP-uri, MAC-uri si Rute implicite"""
        for i, (router, hosts) in enumerate(self.routers):
            for j, host in enumerate(hosts):
                hidx = i * len(hosts) + j
                
                # Configurare Interfete Host - Router
                router.setIP(info.get("router_ip", hidx), prefixLen=24, intf=info.get("router_if_name", j))
                host.setIP(info.get("host_ip", hidx), prefixLen=24, intf=info.get("host_if_name", hidx))
                
                # Configurare MAC-uri
                host.cmd("ifconfig {} hw ether {}".format(info.get("host_if_name", hidx), info.get("host_mac", hidx)))
                router.cmd("ifconfig {} hw ether {}".format(info.get("router_if_name", j), info.get("router_mac", hidx, i)))
                
                # Ruta default pentru host catre router
                host.cmd("ip route add default via {}".format(info.get("router_ip", hidx)))

        # Configurare legaturi intre routere
        nr = len(self.routers)
        for i in range(nr):
            for j in range(i + 1, nr):
                ri_if = info.get("r2r_if_name", i, j)
                ri_ip = info.get("r2r_ip1", i, j)
                rj_ip = info.get("r2r_ip2", j, i)
                
                self.routers[i][0].setIP(ri_ip, prefixLen=24, intf=ri_if)
                self.routers[j][0].setIP(rj_ip, prefixLen=24, intf=ri_if)

def main():
    # Parametrii preluati din modulul info
    nr_routers = info.N_ROUTERS
    nh_per_router = info.N_HOSTSEACH

    topo = FullTopo(nr=nr_routers, nh=nh_per_router)
    net = Mininet(topo, controller=None, link=Link)
    
    net.start()

    nm = NetworkManager(net, nr_routers, nh_per_router)
    nm.setup_network()

    signal.signal(signal.SIGINT, signal_handler)
    CLI(net)
    
    net.stop()

if __name__ == "__main__":
    setLogLevel("info")
    main()