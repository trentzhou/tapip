#include "lib.h"
#include "netif.h"
#include "ether.h"
#include <string.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>


#define DEVICE_NAME_LEN 16
#define ETH_P_ALL 0x0003

struct physical_eth_dev
{
    int fd;
    unsigned int ip;
    unsigned int mask;
    char device_name[DEVICE_NAME_LEN];
};

int physical_eth_dev_xmit(struct netdev* d, struct pkbuf* b)
{
    struct physical_eth_dev* priv = (struct physical_eth_dev*)d->priv;
    int l = write(priv->fd, b->pk_data, b->pk_len);
    if ( l != b->pk_len) {
        dbg("write not complete");
        d->net_stats.tx_errors++;
    } else {
        d->net_stats.tx_packets++;
        d->net_stats.tx_bytes += l;
    }
    return l;
}

int physical_eth_dev_init(struct netdev* d)
{
    struct physical_eth_dev* priv = (struct physical_eth_dev*)d->priv;
    priv->fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (priv->fd < 0)
    {
        perror("socket");
        return -1;
    }

    // find name
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, priv->device_name, sizeof(ifr.ifr_name));
    if (ioctl(priv->fd, SIOCGIFINDEX, &ifr) < 0)
    {
        perror("ioctl");
        return -1;
    }
    int dev_id = ifr.ifr_ifindex;

    // get MTU
    if (ioctl(priv->fd, SIOCGIFMTU, &ifr) < 0)
    {
        perror("ioctl MTU");
        return -1;
    }
    d->net_mtu = ifr.ifr_mtu;
    
    // get MAC
    if (ioctl(priv->fd, SIOCGIFHWADDR, &ifr) < 0)
    {
        perror("ioctl MAC");
        return -1;
    }
    hwacpy(d->net_hwaddr, ifr.ifr_hwaddr.sa_data);

    // bind the socket to device
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = PF_PACKET;
    sll.sll_ifindex = dev_id;
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(priv->fd, (struct sockaddr*)&sll, sizeof(sll)) < 0)
    {
        perror("bind");
        return -1;
    }

    d->net_ipaddr = priv->ip;
    d->net_mask = priv->mask;
    return 0;
}

void physical_eth_dev_exit(struct netdev* d)
{
    struct physical_eth_dev* priv = (struct physical_eth_dev*)d->priv;
    close(priv->fd);
    free(priv);
    d->priv = 0;
}

extern void* physical_eth_poll(void* x);
static struct netdev* peth;
struct netdev* physical_eth_init(const char* device, char* ipstr, int maskbits)
{
    static struct netdev_ops peth_ops = {
        .init = physical_eth_dev_init,
        .xmit = physical_eth_dev_xmit,
        .exit = physical_eth_dev_exit,
    };

    struct physical_eth_dev* priv = (struct physical_eth_dev*)malloc(sizeof(struct physical_eth_dev));
    priv->fd = 0;
    strncpy(priv->device_name, device, DEVICE_NAME_LEN);
    str2ip(ipstr, &priv->ip);
    priv->mask = htonl(~((1<<(32-maskbits)) - 1));

    printf("Allocating peth device\n");
    peth = netdev_alloc("peth", &peth_ops, priv);
    // just start the rx thread now
    pthread_t tid;
    pthread_create(&tid, 0, physical_eth_poll, 0);
    return peth;
}

void physical_eth_exit(void)
{
    netdev_free(peth);
}

static int physical_eth_recv(struct pkbuf *pkb)
{
    struct physical_eth_dev* priv = (struct physical_eth_dev*)peth->priv;
	int l;
	l = read(priv->fd, pkb->pk_data, pkb->pk_len);
	if (l <= 0) {
		devdbg("read net dev");
		peth->net_stats.rx_errors++;
	} else {
		devdbg("read net dev size: %d\n", l);
		peth->net_stats.rx_packets++;
		peth->net_stats.rx_bytes += l;
		pkb->pk_len = l;
	}
	return l;
}

static void physical_eth_rx(void)
{
	struct pkbuf *pkb = alloc_netdev_pkb(peth);
	if (physical_eth_recv(pkb) > 0)
		net_in(peth, pkb);	/* pass to upper */
	else
		free_pkb(pkb);
}


void* physical_eth_poll(void* x)
{
	struct pollfd pfd = {};
    struct physical_eth_dev* priv = (struct physical_eth_dev*)peth->priv;
	int ret;
    x = x;
	while (1) {
		pfd.fd = priv->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		/* one event, infinite time */
		ret = poll(&pfd, 1, -1);
		if (ret <= 0)
			perrx("poll /dev/net/tun");
		/* get a packet and handle it */
		physical_eth_rx();
	}
    return 0;
}
