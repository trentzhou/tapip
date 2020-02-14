#ifndef _XXX_STAT_H_
#define _XXX_STAT_H_

#include <stdint.h>
#include <stdbool.h>

struct stat_reporter_t
{
	uint64_t last_pktcount;
	uint64_t last_tick_us;
	uint32_t print_count;
};

uint64_t timestamp_us(void);

void stat_init(struct stat_reporter_t* stat);

void stat_show(struct stat_reporter_t* stat, uint64_t pktcount, bool flush);

#endif
