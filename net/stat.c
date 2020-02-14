#include "stat.h"
#include <stddef.h>
#include <sys/time.h>
#include <stdio.h>

uint64_t timestamp_us(void)
{
    struct timeval tv; 
    gettimeofday(&tv,NULL);
    uint64_t time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;
    return time_in_micros;
}

void stat_init(struct stat_reporter_t* stat)
{
    stat->last_pktcount = 0;
    stat->last_tick_us = 0;
    stat->print_count = 0;
}

// stat utilities
void stat_show(struct stat_reporter_t* stat, uint64_t pktcount, bool flush)
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
	uint64_t pktrate = pktcount_delta * 1000000 / time_delta;
	
	stat->last_tick_us = now;
	stat->last_pktcount = pktcount;
	
	if (stat->print_count % 10 == 0)
	{
		printf("\n%-10s %-10s %-10s\n-----------------------------------\n",
			"PACKETS", "TIME(us)", "RATE(pps)");
	}
	
	printf("%-10lu %-10lu %-10lu\n", pktcount_delta, time_delta, pktrate);
	stat->print_count++;
}

