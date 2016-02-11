#!/usr/bin/python

from mininet.topo import Topo
from mininet.util import irange
from mininet.net import Mininet
from mininet.log import setLogLevel
from mininet.cli import CLI

class MyTopo( Topo ):

	def __init__( self ):

		Topo.__init__( self )

		sw = self.addSwitch('s0')

		for i in irange(0, 9):
			h = self.addHost('h%s' % i, ip='10.0.0.1%s/24' % i)
			self.addLink(h, sw)

		client = self.addHost('client', ip='10.0.0.200/24')
		self.addLink(client, sw)

def startTopo():
	topo = MyTopo()
        net = Mininet(topo=topo)
	net.start()
	
	for i in irange(0, 9):
		h = net.get('h%s' % i)
		h.cmdPrint('ifconfig h%s-eth0 mtu 1500' % i)
		h.cmdPrint('ethtool -K h%s-eth0 gso off' % i)
		h.cmdPrint('ethtool -K h%s-eth0 tso off' % i)
		h.cmdPrint('tc qdisc add dev h%s-eth0 root handle 1:0 htb default 1' % i)
		h.cmdPrint('tc class add dev h%s-eth0 parent 1:0 classid 1:1 htb rate 100Mbit' % i)
		h.cmdPrint('tc qdisc add dev h%s-eth0 parent 1:1 handle 10: netem delay 5ms limit 100' % i)
		h.cmdPrint('memcached -u mininet &')

	client = net.get('client')
	client.cmdPrint('ifconfig client-eth0 mtu 1500')
	client.cmdPrint('ethtool -K client-eth0 gso off')
	client.cmdPrint('ethtool -K client-eth0 tso off')
	client.cmdPrint('tc qdisc add dev client-eth0 root handle 1:0 htb default 1')
	client.cmdPrint('tc class add dev client-eth0 parent 1:0 classid 1:1 htb rate 100Mbit')
	client.cmdPrint('tc qdisc add dev client-eth0 parent 1:1 handle 10: netem delay 5ms limit 100')
	
	CLI( net )

if __name__ == '__main__':
        setLogLevel('info')
        startTopo()
