#ifndef _FAST_PIPE_H_
#define _FAST_PIPE_H_

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

struct circular_buffer_t
{
    uint32_t capacity;
    uint32_t write_index;
    volatile uint32_t used;
    uint32_t read_index;

    uint8_t data[];
};

typedef struct circular_buffer_t CB_T;

#define CB_SIZE(capacity) (sizeof(struct circular_buffer_t) + capacity)

/// Init a circular buffer
void cb_init(struct circular_buffer_t* cb, uint32_t capacity);

/// Create a new circular buffer
struct circular_buffer_t* cb_new(uint32_t capacity);

/// delete a circular buffer which was created with cb_new
void cb_delete(struct circular_buffer_t* cb);

/// put data into the buffer
bool cb_put(struct circular_buffer_t* cb, void* data, uint32_t len);

/// read data from the buffer
bool cb_get(struct circular_buffer_t* cb, void* data, uint32_t len);

bool cb_put_packet(struct circular_buffer_t* cb, void* data, uint32_t len);
bool cb_get_packet(struct circular_buffer_t* cb, void* buf, uint32_t buflen, uint32_t* pktlen);

#endif
