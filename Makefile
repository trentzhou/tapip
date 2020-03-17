#### User configure  ###############
CONFIG_DEBUG = n
CONFIG_DEBUG_PKB = n
CONFIG_DEBUG_WAIT = n
CONFIG_DEBUG_SOCK = n
CONFIG_DEBUG_ARP_LOCK = n
CONFIG_DEBUG_ICMPEXCFRAGTIME = n
CONFIG_TOPLOGY = 2
#### End of User configure #########


# Use 'make V=1' to see the full commands
ifeq ("$(origin V)", "command line")
	Q =
else
	Q = @
endif
export Q

real_all: tapip
	echo "Building..."

CFLAGS=

ifeq ($(CONFIG_DPDK), 1)
	NET_STACK_OBJS += net/lib_dpdk.o

ifeq ($(RTE_SDK),)
	$(error "Please define RTE_SDK environment variable")
endif

	# Default target, detect a build directory, by looking for a path with a .config
	RTE_TARGET := build
	include $(RTE_SDK)/mk/rte.vars.mk
	include $(RTE_SDK)/mk/rte.app.mk
	LFLAGS += $(call linkerprefix, $(LDLIBS))
	CFLAGS += -DCONFIG_DPDK
endif
MAKEFLAGS += --no-print-directory

LD = ld
CC = gcc
CFLAGS += -Wall -I../include -fPIC
LFLAGS += -pthread 
export LD CC CFLAGS
CFLAGS += -g

ifeq ($(CONFIG_DEBUG), y)
	CFLAGS += -g
endif

ifeq ($(CONFIG_DEBUG), y)
	CFLAGS += -DDEBUG_PKB
endif

ifeq ($(CONFIG_DEBUG_SOCK), y)
	CFLAGS += -DSOCK_DEBUG
endif

ifeq ($(CONFIG_DEBUG_ICMPEXCFRAGTIME), y)
	CFLAGS += -DICMP_EXC_FRAGTIME_TEST
endif

ifeq ($(CONFIG_DEBUG_WAIT), y)
	CFLAGS += -DWAIT_DEBUG
endif

ifeq ($(CONFIG_DEBUG_ARP_LOCK), y)
	CFLAGS += -DDEBUG_ARPCACHE_LOCK
endif

ifeq ($(CONFIG_TOPLOGY), 1)
	CFLAGS += -DCONFIG_TOP1
else
	CFLAGS += -DCONFIG_TOP2
endif


NET_STACK_OBJS +=	shell/shell_obj.o	\
			net/net_obj.o		\
			arp/arp_obj.o		\
			ip/ip_obj.o		\
			socket/socket_obj.o	\
			udp/udp_obj.o		\
			tcp/tcp_obj.o		\
			app/app_obj.o		\
			lib/lib_obj.o



tapip:$(NET_STACK_OBJS)
	@echo " [BUILD] $@"
	$(Q)$(CC) $(LFLAGS) $^ -o $@ -lreadline

shell/shell_obj.o:shell/*.c
	@make -C shell/
net/net_obj.o:net/*.c
	@make -C net/
arp/arp_obj.o:arp/*.c
	@make -C arp/
ip/ip_obj.o:ip/*.c
	@make -C ip/
udp/udp_obj.o:udp/*.c
	@make -C udp/
tcp/tcp_obj.o:tcp/*.c
	@make -C tcp/
lib/lib_obj.o:lib/*.c
	@make -C lib/
socket/socket_obj.o:socket/*.c
	@make -C socket/
app/app_obj.o:app/*.c
	@make -C app/

test:cbuf
# test program for circul buffer
cbuf:lib/cbuf.c lib/lib.c
	@echo " [CC] $@"
	$(Q)$(CC) -DCBUF_TEST -Iinclude/ $^ -o $@

tag:
	ctags -R *

clean:
	find . -name *.o | xargs rm -f
	rm -f tapip cbuf
	rm -f _pre* _post* _clean

lines:
	@echo "code lines:"
	@wc -l `find . -name \*.[ch]` | sort -n

net/lib_dpdk.o: net/dpdk/dpdk_netdev.c
	$(Q)$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

