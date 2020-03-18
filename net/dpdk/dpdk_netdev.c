#include "lib.h"
#include "netif.h"
#include "ether.h"
#include <arpa/inet.h>
/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>

#include <rte_version.h>
#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_udp.h>

#if RTE_VER_RELEASE < 18
	// for compatibility
	#define rte_ether_addr ether_addr
	#define rte_ether_hdr ether_hdr
	#define rte_ether_addr_copy ether_addr_copy
	#define rte_ipv4_hdr ipv4_hdr
	#define RTE_IPV4 IPv4
	#define rte_udp_hdr udp_hdr
	#define RTE_ETHER_TYPE_IPV4 ETHER_TYPE_IPv4
#endif

#if RTE_VER_YEAR < 18
	#define rte_eth_dev_count_avail rte_eth_dev_count
#endif

#define MAC_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC_ARG(x) (x)->addr_bytes[0], \
				   (x)->addr_bytes[1], \
				   (x)->addr_bytes[2], \
				   (x)->addr_bytes[3], \
				   (x)->addr_bytes[4], \
				   (x)->addr_bytes[5]
#define MAC_ARG_PTR(x)  &(x)->addr_bytes[0], \
						&(x)->addr_bytes[1], \
						&(x)->addr_bytes[2], \
						&(x)->addr_bytes[3], \
						&(x)->addr_bytes[4], \
						&(x)->addr_bytes[5]

struct ethernet_rx_t
{
	// for stats
	uint64_t pktcount;
	uint64_t bytes;
};

struct ethernet_tx_t
{
	struct rte_eth_dev_tx_buffer* tx_buffer;
    struct rte_ring* tx_ring;
	struct rte_ether_addr target_mac;
	uint32_t payload_length;
	// for stats
	uint64_t pktcount;	// total, including success and failure
	uint64_t bytes;		// sent bytes
	uint64_t dropped;	// failure
};

struct stat_reporter_t
{
	uint64_t last_pktcount;
	uint64_t last_bytes;
	uint64_t last_tick_us;
	uint32_t print_count;
};

enum operation_mode_t
{
	OPERATION_RX,
	OPERATION_TX,
};

struct ethernet_rw_t
{
	struct ethernet_rx_t rx;
	struct ethernet_tx_t tx;
	struct stat_reporter_t stat;
	enum operation_mode_t op;
	volatile int exit;	// signal handler sets it to 1
	int worker_lcore_id;
	int worker_port_id;
	int worker_socket_id;	// socket id for worker lcore, should be identical to port socket

	struct rte_mempool* mempool;
	struct rte_eth_dev_info dev_info;
	struct rte_ether_addr mac;
};

static uint64_t timestamp_us(void);
static void stat_show(struct stat_reporter_t* stat, uint64_t pktcount, uint64_t bytes, bool flush);
void signal_handler(int signum);
void create_udp_packet(struct rte_mbuf* m, 
					struct rte_ether_addr* dest_mac,
					uint32_t dest_ip,
					uint16_t dest_port,
					struct rte_ether_addr* src_mac,
					uint32_t src_ip,
					uint16_t src_port,
					uint16_t payload_length);
int ethernet_tx_loop(void* arg);
void ethernet_rw_init(struct ethernet_rw_t* rw);
bool string_to_mac(const char* s, struct rte_ether_addr* mac);
void* stat_thread(void* arg);

uint64_t timestamp_us(void)
{
    struct timeval tv; 
    gettimeofday(&tv,NULL);
    uint64_t time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;
    return time_in_micros;
}

bool string_to_mac(const char* s, struct rte_ether_addr* mac)
{
	uint32_t a[6];
	int ret = sscanf(s, MAC_FMT, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
	if (ret == 6)
	{
		int i;
		for (i = 0; i < 6; i++)
			mac->addr_bytes[i] = (uint8_t)a[i];
		return true;
	}
	return false;
}

// stat utilities
void stat_show(struct stat_reporter_t* stat, uint64_t pktcount, uint64_t bytes, bool flush)
{
	if (stat->last_tick_us == 0)
	{
		stat->last_tick_us = timestamp_us();
		return;
	}
	// report every 1 million packets
	if (!flush && (pktcount - stat->last_pktcount < 1000000))
		return;
	uint64_t now = timestamp_us();
	uint64_t time_delta = now - stat->last_tick_us;
	uint64_t pktcount_delta = pktcount - stat->last_pktcount;
	uint64_t bytes_delta = bytes - stat->last_bytes;
	uint64_t pktrate = pktcount_delta * 1000000 / time_delta;
	uint64_t bps = bytes_delta * 8 * 1000000 / time_delta;

	stat->last_tick_us = now;
	stat->last_pktcount = pktcount;
	stat->last_bytes = bytes;
	
	if (stat->print_count % 10 == 0)
	{
		printf("\n%-10s %-10s %-10s %-10s\n-----------------------------------\n",
			"PACKETS", "TIME(us)", "RATE(pps)", "bit/s");
	}
	
	printf("%-10lu %-10lu %-10lu %-10lu\n", pktcount_delta, time_delta, pktrate, bps);
	stat->print_count++;
}


int ethernet_tx_loop(void* arg)
{
	printf("TX loop\n");

	struct ethernet_rw_t* rw = arg;
	struct ethernet_tx_t* tx = &rw->tx;
	
	while (!rw->exit)
	{
		// construct a few packets
		// send it
		struct rte_mbuf* m;
        if (rte_ring_dequeue(tx->tx_ring, (void**)&m) == 0)
        {
            uint16_t ret;
            ret = rte_eth_tx_buffer(rw->worker_port_id,
                            0,
                            rw->tx.tx_buffer,
                            m);

            tx->pktcount += ret;
            tx->bytes += ret * m->data_len;
        }
		else
        {
            // flush
            rte_eth_tx_buffer_flush(rw->worker_port_id, 0, rw->tx.tx_buffer);
			usleep(50);
        }
	}
	return 0;
}

void ethernet_rw_init(struct ethernet_rw_t* rw)
{
	const int NB_MBUFS = 8192;
	const int MEMPOOL_CACHE_SIZE = 256;
	const int MAX_PKT_BURST = 32;

	uint16_t nb_rxd = 1024;
	uint16_t nb_txd = 1024;

	struct rte_eth_conf port_conf = {
		.rxmode = {
			.split_hdr_size = 0,
		},
		.txmode = {
			.mq_mode = ETH_MQ_TX_NONE,
		},
	};

	int ret;

	// init mempool
	rw->mempool = rte_pktmbuf_pool_create("mbuf_pool", 
										NB_MBUFS, 
										MEMPOOL_CACHE_SIZE,
										0, 
										RTE_MBUF_DEFAULT_BUF_SIZE,
										rw->worker_socket_id);
	if (rw->mempool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

    // create the ring
    rw->tx.tx_ring = rte_ring_create("tx_ring", 512, rw->worker_socket_id, RING_F_SC_DEQ);

	// get dev info
	rte_eth_dev_info_get(rw->worker_port_id, &rw->dev_info);

	// dev configure
	port_conf.txmode.offloads = rw->dev_info.tx_offload_capa;
	port_conf.rxmode.offloads = rw->dev_info.default_rxconf.offloads;
	if (rw->dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
	{
		port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;	
	}

	printf("TX offloads %08lx, RX offloads %08lx\n", 
			port_conf.txmode.offloads,
			port_conf.rxmode.offloads);

	ret = rte_eth_dev_configure(rw->worker_port_id, 1, 1, &port_conf);
	if (ret < 0)
	rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%d\n", 
			ret,
			rw->worker_port_id);
	ret = rte_eth_dev_adjust_nb_rx_tx_desc(rw->worker_port_id, &nb_rxd, &nb_txd);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot adjust number of descriptors: err=%d, port=%d\n",
				ret, rw->worker_port_id);

	// get mac
	rte_eth_macaddr_get(rw->worker_port_id, &rw->mac);
	printf("Mac: " MAC_FMT "\n", MAC_ARG(&rw->mac));

	// enable promiscuous mode
	rte_eth_promiscuous_enable(rw->worker_port_id);

	// rx queue setup
	struct ethernet_rx_t* rx = &rw->rx;
	struct rte_eth_rxconf rxconf = rw->dev_info.default_rxconf;
	rxconf.offloads = 0;

	ret = rte_eth_rx_queue_setup(rw->worker_port_id,
								0,
								nb_rxd,
								rw->worker_socket_id,
								&rxconf,
								rw->mempool);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup failed: err=%d\n", ret);
	rx->pktcount = 0;
	struct ethernet_tx_t* tx = &rw->tx;

	struct rte_eth_txconf txconf = rw->dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;

	ret = rte_eth_tx_queue_setup(rw->worker_port_id, 
								0, 
								nb_txd, 
								rw->worker_socket_id, 
								&txconf);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, port=%d\n",
				ret, rw->worker_port_id);
	
	// init tx buffers
	tx->tx_buffer = rte_zmalloc_socket("tx_buffer", 
											RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST),
											0,
											rw->worker_socket_id);
	if (tx->tx_buffer == NULL)
		rte_exit(EXIT_FAILURE, "Failed to alloc tx buffer\n");
	rte_eth_tx_buffer_init(tx->tx_buffer, MAX_PKT_BURST);
	// stat for failure
	ret = rte_eth_tx_buffer_set_err_callback(tx->tx_buffer,
			rte_eth_tx_buffer_count_callback,
			&tx->dropped);
	if (ret < 0)
		rte_exit(EXIT_FAILURE,
				"Cannot set error callback for tx buffer on port\n");

	// start the device
	ret = rte_eth_dev_start(rw->worker_port_id);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%d\n", ret);
}

void* stat_thread(void* arg)
{
	struct ethernet_rw_t* rw = (struct ethernet_rw_t*)arg;
	while (!rw->exit)
	{
		if (rw->op == OPERATION_RX)
		{
			struct ethernet_rx_t* rx = &rw->rx;
			stat_show(&rw->stat, rx->pktcount, rx->bytes, true);
		}
		else
		{
			struct ethernet_tx_t* tx = &rw->tx;
			stat_show(&rw->stat, tx->pktcount, tx->bytes, true);
		}
		sleep(1);
	}
	return 0;
}

int dpdk_dev_xmit(struct netdev* d, struct pkbuf* b)
{
    struct ethernet_rw_t* rw = d->priv;

    struct rte_mbuf* m = rte_pktmbuf_alloc(rw->mempool);
    memcpy(rte_pktmbuf_mtod(m, void*), b->pk_data, b->pk_len);
    m->pkt_len = b->pk_len;
    m->next = 0;
    m->data_len = b->pk_len;
    m->nb_segs = 1;

    // put it in the ring
    if (rte_ring_enqueue(rw->tx.tx_ring, m) == 0)
    {
        d->net_stats.tx_packets++;
        d->net_stats.tx_bytes += b->pk_len;
        return b->pk_len;
    }
    else
    {
        d->net_stats.tx_errors++;
        rte_pktmbuf_free(m);
        return 0;
    }
}

int dpdk_dev_init(struct netdev* d)
{
    // nothing
    return 0;
}

void dpdk_dev_exit(struct netdev* d)
{
    struct ethernet_rw_t* rw = d->priv;
    rw->exit = 1;
    int lcore_id;
    RTE_LCORE_FOREACH_SLAVE(lcore_id)
    {   
        printf("Waiting for lcore %d to shutdown\n", lcore_id);
        if (rte_eal_wait_lcore(lcore_id) < 0)
        {
            break;
        }
    }   

    // close
	rte_eth_dev_stop(rw->worker_port_id);
	rte_eth_dev_close(rw->worker_port_id);
}

static int dpdk_rx_thread(void* x)
{
    struct netdev* dev = x;
    struct ethernet_rw_t* dpdk = dev->priv;
	const uint16_t MAX_PKT_BURST = 32;
	struct ethernet_rx_t* rx = &dpdk->rx;
	struct rte_mbuf* pkts_burst[MAX_PKT_BURST];

	printf("RX loop\n");

	while (!dpdk->exit)
	{
		uint16_t nb_rx = rte_eth_rx_burst(dpdk->worker_port_id, 0, pkts_burst, MAX_PKT_BURST);
		if (nb_rx == 0)
		{
			usleep(50);
			continue;
		}
		
		rx->pktcount += nb_rx;

		uint16_t i;
		for (i = 0; i < nb_rx; i++)
		{
			struct rte_mbuf* m = pkts_burst[i];
			rx->bytes += m->data_len;

            struct pkbuf *pkb = alloc_netdev_pkb(dev);
            pkb->pk_len = m->data_len;
            memcpy(pkb->pk_data, rte_pktmbuf_mtod(m, void*), pkb->pk_len);

            dev->net_stats.rx_packets++;
            dev->net_stats.rx_bytes += pkb->pk_len;

			rte_pktmbuf_free(m);
            net_in(dev, pkb);
		}
	}

    return 0;
}

struct netdev* dpdk_dev_create(char* coremask, char* ipstr, int maskbits)
{
    struct netdev* dev;
    static struct netdev_ops dpdk_ops = {
        .init = dpdk_dev_init,
        .xmit = dpdk_dev_xmit,
        .exit = dpdk_dev_exit,
    };
    struct ethernet_rw_t* rw = malloc(sizeof(struct ethernet_rw_t));
    memset(rw, 0, sizeof(*rw));

	int ret;
    int argc = 3;
    char* argv[] = {
        "dpdk", "-c", coremask
    };

	printf("Going to run rte_eal_init\n");
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	printf("Finished rte_eal_init\n");

	// check lcore mask
    int lcore_count = rte_lcore_count();
	if (lcore_count != 3)
	{
		rte_exit(EXIT_FAILURE, "Expected 3 lcores, but %d provided\n", lcore_count);
	}
	// check port count
	// this program just handles one single port
	uint16_t ethdev_count = rte_eth_dev_count_avail();
	if (ethdev_count != 1)
	{
		rte_exit(EXIT_FAILURE,
				"We need exactly 1 ethdev but %d provided\n",
				ethdev_count);
	}
	rw->worker_lcore_id = rte_lcore_id();
	rw->worker_port_id = rte_eth_find_next(0);
	// check socket id
	int lcore_socket_id = rte_socket_id();
	int port_socket_id = rte_eth_dev_socket_id(rw->worker_port_id);
	if (port_socket_id != -1 && lcore_socket_id != port_socket_id)
	{
		rte_exit(EXIT_FAILURE,
				"lcore_socket_id %d != port_socket_id %d\n",
				lcore_socket_id, port_socket_id);
	}
	rw->worker_socket_id = lcore_socket_id;

	ethernet_rw_init(rw);

    printf("Allocating dpdk device\n");
    dev = netdev_alloc("dpdk", &dpdk_ops, rw);
    dev->net_mtu = 1500;
    memcpy(dev->net_hwaddr, rw->mac.addr_bytes, 6);

    str2ip(ipstr, &dev->net_ipaddr);
    dev->net_mask = htonl(~((1<<(32-maskbits)) - 1));
    dev->priv = rw;
    
    int lcore_id = rte_get_next_lcore(-1, 1, 0);
    rte_eal_remote_launch(ethernet_tx_loop, rw, lcore_id);
    lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
    rte_eal_remote_launch(dpdk_rx_thread, dev, lcore_id);
    
    return dev;
}
