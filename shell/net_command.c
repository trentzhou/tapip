#include <stdio.h>
#include <signal.h>

#include "netif.h"
#include "ip.h"
#include "icmp.h"
#include "arp.h"
#include "lib.h"
#include "route.h"
#include "netcfg.h"
#include "sock.h"
#include "cbuf.h"

unsigned int net_debug = 0;


static void debug_usage(void)
{
	ferr(
		"Usage: debug [-c|-n] (dev|l2|arp|ip|icmp|udp|tcp|tcpstate)+\n"
		"     -c    clear non-blocking debug config\n"
		"     -n    open non-blocking debug \n\n"
		"EXAMPLES:\n"
		"  See IP packet flow in blocking model \n"
		"   # debug ip\n"
		"  See TCP packet flow and TCP state transmission in non-blocking model\n"
		"   # debug -n tcp tcpstate\n\n");
}

void signal_wait(int signum)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, signum);
	sigsuspend(&mask);
}

void netdebug(int argc, char **argv)
{
	int noblock = 0;
	int clear = 0;
	unsigned int debug = 0;

	do {
		argc--;
		if (strcmp(argv[argc], "-n") == 0)
			noblock = 1;
		else if (strcmp(argv[argc], "-c") == 0)
			clear = 1;
		else if (strcmp(argv[argc], "dev") == 0)
			debug |= NET_DEBUG_DEV;
		else if (strcmp(argv[argc], "l2") == 0)
			debug |= NET_DEBUG_L2;
		else if (strcmp(argv[argc], "arp") == 0)
			debug |= NET_DEBUG_ARP;
		else if (strcmp(argv[argc], "ip") == 0)
			debug |= NET_DEBUG_IP;
		else if (strcmp(argv[argc], "icmp") == 0)
			debug |= NET_DEBUG_ICMP;
		else if (strcmp(argv[argc], "udp") == 0)
			debug |= NET_DEBUG_UDP;
		else if (strcmp(argv[argc], "tcp") == 0)
			debug |= NET_DEBUG_TCP;
		else if (strcmp(argv[argc], "tcpstate") == 0)
			debug |= NET_DEBUG_TCPSTATE;
		else if (strcmp(argv[argc], "all") == 0)
			debug |= NET_DEBUG_ALL;
		else
			return debug_usage();
	} while (argc > 1);

	/* clear debug flags */
	if (clear) {
		if (debug)
			net_debug &= ~debug;
		else
			net_debug = 0;
		return;
	}

	net_debug |= debug;
	if (noblock)
		return;
	/* block mode */
	ferr("enter ^C to exit debug mode\n");
	/* waiting for interrupt signal */
	signal_wait(SIGQUIT);
	net_debug = 0;
	ferr("\nexit debug mode\n");
}

void arpcache(int argc, char **argv)
{
	arp_cache_traverse();
}

void route(int argc, char **argv)
{
	rt_traverse();
}

extern struct netdev* physical_eth_init(const char* device, const char* ipstr, int maskbits);
void attach_dev(int argc, char** argv)
{
	if (argc != 4)
	{
		printf("Usage: attach_dev [devname] [ip] [mask]");
		return;
	}
	char* devname = argv[1];
	char* ip = argv[2];
	char* netmask = argv[3];
	// init the device
	struct netdev* peth = physical_eth_init(devname, ip, atoi(netmask));
	// add route table
	rt_add(peth->net_ipaddr, 0xffffffff, 0, 0, RT_LOCALHOST, loop);
	rt_add(LOCALNET(peth), peth->net_mask, 0, 0, RT_NONE, peth);
}


struct netdev* shmeth_dev_create(const char* devname, const char* side, char* ipstr, int maskbits);

void attach_shmeth_dev(int argc, char** argv)
{
	if (argc != 5)
	{
		printf("Usage: attach_dev [devname] [side] [ip] [mask]");
		return;
	}
	char* devname = argv[1];
	char* side = argv[2];
	char* ip = argv[3];
	char* netmask = argv[4];
	// init the device
	struct netdev* dev = shmeth_dev_create(devname, side, ip, atoi(netmask));
	// add route table
	rt_add(dev->net_ipaddr, 0xffffffff, 0, 0, RT_LOCALHOST, loop);
	rt_add(LOCALNET(dev), dev->net_mask, 0, 0, RT_NONE, dev);
}

void ifinfo(struct netdev *dev)
{
	printf("%-10sHWaddr "MACFMT"\n"
		"          IPaddr "IPFMT"\n"
		"          mtu %d\n"
		"          RX packet:%lu bytes:%lu errors:%lu\n"
		"          TX packet:%lu bytes:%lu errors:%lu\n",
		dev->net_name,
		macfmt(dev->net_hwaddr),
		ipfmt(dev->net_ipaddr),
		dev->net_mtu,
		dev->net_stats.rx_packets,
		dev->net_stats.rx_bytes,
		dev->net_stats.rx_errors,
		dev->net_stats.tx_packets,
		dev->net_stats.tx_bytes,
		dev->net_stats.tx_errors);
}

extern struct list_head net_devices;
void ifconfig(int argc, char **argv)
{
	struct netdev *dev;
	list_for_each_entry(dev, &net_devices, net_list) {
		ifinfo(dev);
	}
}

void stat(int argc, char **argv)
{
	printf("[pkbuf memory information]\n"
		" alloced pkbs: %d\n"
		" free pkbs:    %d\n",
		alloc_pkbs, free_pkbs);
	printf("[sock memory information]\n"
		" alloced socks: %d\n"
		" free socks:    %d\n",
		alloc_socks, free_socks);
	printf("[cbuf memory information]\n"
		" alloced circular buffers: %d\n"
		" free circular buffers:    %d\n",
		alloc_cbufs, free_cbufs);
}
