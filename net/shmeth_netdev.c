#include "lib.h"
#include "netif.h"
#include "ether.h"
#include "shm_eth.h"
#include <arpa/inet.h>

int shmeth_dev_xmit(struct netdev* d, struct pkbuf* b)
{
    SHMETH_T* shmeth = (SHMETH_T*)d->priv;
    bool ok = shmeth_write_packet(shmeth, b->pk_data, b->pk_len);
    int bytes = 0;
    
    if (!ok) {
        d->net_stats.tx_errors++;
    } else {
        d->net_stats.tx_packets++;
        d->net_stats.tx_bytes += b->pk_len;
        bytes = b->pk_len;
    }
    return bytes;
}

int shmeth_dev_init(struct netdev* d)
{
    // nothing
    return 0;
}

void shmeth_dev_exit(struct netdev* d)
{
    SHMETH_T* shmeth = (SHMETH_T*)d->priv;
    shmeth_close(shmeth);
}

static void* shmeth_rx_thread(void* x)
{
    struct netdev* dev = x;
    SHMETH_T* shmeth = dev->priv;

    while (1)
    {
        struct pkbuf *pkb = alloc_netdev_pkb(dev);
        while (1)
        {
            uint32_t pktlen;
            bool ok = shmeth_read_packet(shmeth, pkb->pk_data, pkb->pk_len, &pktlen);
            if (ok)
            {
                pkb->pk_len = pktlen;
                break;
            }
            else
            {
                usleep(25);
            }
        }

		dev->net_stats.rx_packets++;
		dev->net_stats.rx_bytes += pkb->pk_len;
        net_in(dev, pkb);
    }
    return 0;
}

struct netdev* shmeth_dev_create(const char* devname, const char* side, char* ipstr, int maskbits)
{
    struct netdev* dev;
    static struct netdev_ops peth_ops = {
        .init = shmeth_dev_init,
        .xmit = shmeth_dev_xmit,
        .exit = shmeth_dev_exit,
    };
    SHMETH_SIDE_T s = SHMETH_SIDE_A;

    if (!strcmp(side, "B") || !strcmp(side, "b"))
        s = SHMETH_SIDE_B;
    SHMETH_T* shmeth = shmeth_open(devname, s);

    printf("Allocating peth device\n");
    dev = netdev_alloc("shmeth", &peth_ops, shmeth);
    dev->net_mtu = 1500;
    shmeth_get_mac(shmeth, s, dev->net_hwaddr);
    str2ip(ipstr, &dev->net_ipaddr);
    dev->net_mask = htonl(~((1<<(32-maskbits)) - 1));
    dev->priv = shmeth;
    // just start the rx thread now
    pthread_t tid;
    pthread_create(&tid, 0, shmeth_rx_thread, dev);
    return dev;
}
