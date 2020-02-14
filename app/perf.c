#include "lib.h"
#include "netif.h"
#include "ip.h"
#include "udp.h"
#include "route.h"
#include "socket.h"
#include "sock.h"
#include "../net/stat.h"
#include <sys/time.h>

#define F_BIND		1
#define F_CONNECT	2
#define F_DEBUG		16

#define BUF_SIZE 1500

#define debug(fmt, args...) \
do {\
	if (flags & F_DEBUG)\
		dbg("(perf): "fmt, ##args);\
} while (0)

static unsigned int flags;
static struct socket *sock;
static struct socket *csock;
static struct sock_addr skaddr;
static unsigned int packet_length = BUF_SIZE;
volatile static int interrupt;

static void close_socket(void)
{
	struct socket *tmp;
	if (sock) {
		tmp = sock;
		sock = NULL;
		_close(tmp);
	}
	if (csock) {
		tmp = csock;
		csock = NULL;
		_close(tmp);
	}
}

static int create_socket(void)
{
	sock = _socket(AF_INET, SOCK_DGRAM, 0);
	if (!sock) {
		debug("_socket error");
		return -1;
	}
	return 0;
}

static void sigint(int num)
{
	interrupt = 1;
	close_socket();
}

static void usage(void)
{
	printf(
		"perf - Simple UDP performance test\n\n"
		"Usage: snc [OPTIONS] [addr:port]\n"
		"OPTIONS:\n"
		"      -d             enable debugging on the socket\n"
		"      -b addr:port   listen model: bind addr:port\n"
		"      -c addr:port   connect model: connect addr:port\n"
        "      -l length      UDP packet length\n"
		"      -h             display help information\n\n"
		"EXAMPLES:\n"
		"   Listen on local port 1234 with UDP:\n"
		"       # perf -b 0.0.0.0:1234\n"
		"   Open a UDP connection to port 12345 of 10.0.0.2\n"
		"       # perf -c 10.0.0.2:12345\n"
	);
}

static void send_packet(void)
{
	char buf[BUF_SIZE];
	int len;

	while (!interrupt) {
		/*
		 * FIXME: I have set SIGINT norestart,
		 *  Why cannot read return from interrupt at right!
		 */
        len = packet_length;
		
        if (_send(sock, buf, len, &skaddr) != len) {
            debug("send error");
            break;
        }
	}
}

static void on_rx_packet(void* x, struct pkbuf* pkb)
{
	uint64_t * count = x;
	++*count;
}

static void recv_udp_packet(void)
{
	uint64_t pktcount = 0;
	struct stat_reporter_t stat;
	stat_init(&stat);

	// set the callback
	_sock_set_rx_callback(sock, on_rx_packet, &pktcount);
	while (!interrupt)
	{
		sleep(1);
		stat_show(&stat, pktcount, true);
	}

}

static void recv_packet(void)
{
	if (_bind(sock, &skaddr) < 0) {
			debug("_bind error");
			return;
	}
	debug("bind " IPFMT ":%d", ipfmt(sock->sk->sk_saddr),
					_ntohs(sock->sk->sk_sport));

	recv_udp_packet();
}

static int parse_args(int argc, char **argv)
{
	int c, err = 0;
	/* reinitialize getopt() */
	optind = 0;
	opterr = 0;
	while ((c = getopt(argc, argv, "b:c:l:du?h")) != -1) {
		switch (c) {
		case 'd':
			flags |= F_DEBUG;
			break;
		case 'b':
			err = parse_ip_port(optarg, &skaddr.src_addr,
						&skaddr.src_port);
			flags |= F_BIND;
			break;
		case 'c':
			err = parse_ip_port(optarg, &skaddr.dst_addr,
						&skaddr.dst_port);
			flags |= F_CONNECT;
			break;
        case 'l':
            packet_length = atoi(optarg);
            if (packet_length > BUF_SIZE)
                packet_length = BUF_SIZE;
            break;
		case 'h':
		case '?':
		default:
			return -1;
		}
		if (err < 0) {
			printf("%s:address format is error\n", optarg);
			return -2;
		}
	}

	if ((flags & (F_BIND|F_CONNECT)) == (F_BIND|F_CONNECT))
		return -1;
	if ((flags & (F_BIND|F_CONNECT)) == 0)
		return -1;
	argc -= optind;
	argv += optind;
	if (argc > 0)
		return -1;
	
	return 0;
}

static void init_options(void)
{
	memset(&skaddr, 0x0, sizeof(skaddr));
	csock = NULL;
	sock = NULL;
	interrupt = 0;
}

static int init_signal(void)
{
	struct sigaction act = { };	/* zero */
	act.sa_flags = 0;		/* No restart */
	act.sa_handler = sigint;
	if (sigaction(SIGINT, &act, NULL) < 0)
		return -1;
	return 0;
}

void perf(int argc, char **argv)
{
	int err;
	/* init arguments */
	init_options();
	/* parse arguments */
	if ((err = parse_args(argc, argv)) < 0) {
		if (err == -1)
			usage();
		return;
	}
	/* signal install */
	if (init_signal() < 0)
		goto out;
	/* init socket */
	if (create_socket() < 0)
		goto out;
	/* receive reply */
	if (flags & F_BIND)
		recv_packet();
	else
		send_packet();
out:	/* close and out */
	close_socket();
}
