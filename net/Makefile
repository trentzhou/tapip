C_FILES = $(wildcard *.c)
OBJS = $(subst .c,.o,$(C_FILES))
#OBJS	= net.o netdev.o veth.o loop.o pkb.o tap.o physical_eth.o
SUBDIR	= net

all:net_obj.o
net_obj.o:$(OBJS)
	@echo " [LD] $(SUBDIR)/$@"
	$(Q)$(LD) -r -o $@ $^

%.o:%.c
	@echo " [CC] $(SUBDIR)/$@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o
