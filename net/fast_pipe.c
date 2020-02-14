#include "fast_pipe.h"
#include <stdlib.h>
#include <string.h>

/// Init a circular buffer
void cb_init(struct circular_buffer_t* cb, uint32_t capacity)
{
    cb->capacity = capacity;
    cb->read_index = 0;
    cb->used = 0;
    cb->write_index = 0;
}

struct circular_buffer_t* cb_new(uint32_t capacity)
{
    struct circular_buffer_t* cb = malloc(sizeof(struct circular_buffer_t) + capacity);
    cb_init(cb, capacity);
    return cb;
}

void cb_delete(struct circular_buffer_t* cb)
{
    free(cb);
}

/**
 * Push data into a circular buffer.
 * This function just performs the memcpy. cb->write_index is not touched.
 * 
 * @param cb the circular buffer
 * @param write_index index to start the write
 * @param data data to write
 * @param len data length
 * 
 * @return new write index after the memcpy.
 */
uint32_t cb_push_data(struct circular_buffer_t* cb, uint32_t write_index, void* data, uint32_t len)
{
    uint32_t copylen = len;
    if (write_index + copylen > cb->capacity)
    {
        copylen = cb->capacity - write_index;
        memcpy(&cb->data[write_index], data, copylen);
        len -= copylen;
        memcpy(cb->data, (uint8_t*)data + copylen, len);
        return len;
    }
    else
    {
        memcpy(&cb->data[write_index], data, len);
        return write_index + len;
    }
}

/**
 * Fetch data from the circular buffer.
 * This function only does memcpy. cb->read_index is not touched.
 * 
 * @return the new read index
 */ 
uint32_t cb_shift_data(struct circular_buffer_t* cb, uint32_t read_index, void* buf, uint32_t buflen)
{
    uint32_t tail_length = cb->capacity - read_index;

    if (tail_length >= buflen)
    {
        memcpy(buf, &cb->data[read_index], buflen);
        return read_index + buflen;
    }
    else
    {
        memcpy(buf, &cb->data[read_index], tail_length);
        buflen -= tail_length;
        memcpy((uint8_t*)buf + tail_length, cb->data, buflen);
        return buflen;
    }
}

/// put data into the buffer
bool cb_put(struct circular_buffer_t* cb, void* data, uint32_t len)
{
    if (cb->used + len > cb->capacity)
        return false;
    cb->write_index = cb_push_data(cb, cb->write_index, data, len);
    __sync_fetch_and_add(&cb->used, len);
    return true;
}

/// read data from the buffer
bool cb_get(struct circular_buffer_t* cb, void* data, uint32_t len)
{
    if (cb->used < len)
        return false;
    cb->read_index = cb_shift_data(cb, cb->read_index, data, len);
    __sync_fetch_and_sub(&cb->used, len);
    return true;
}

bool cb_put_packet(struct circular_buffer_t* cb, void* data, uint32_t len)
{
    bool ok = true;
    uint32_t write_index;

    // quickly check without lock
    if (cb->capacity - cb->used < sizeof(uint32_t) + len)
        ok = false;
    else
    {
        // we have enough space.
        write_index = cb->write_index;
    }

    if (!ok)
    {
        // XXX: Sleep a while to release CPU
        usleep(25);
        return false;
    }

    // write data
    write_index = cb_push_data(cb, write_index, &len, sizeof(uint32_t));
    write_index = cb_push_data(cb, write_index, data, len);

    // update write index
    cb->write_index = write_index;
    __sync_fetch_and_add(&cb->used, sizeof(uint32_t) + len);

    return true;
}

bool cb_get_packet(struct circular_buffer_t* cb, void* buf, uint32_t buflen, uint32_t* pktlen)
{
    bool ok = true;
    uint32_t read_index;

    // quickly check without 
    if (cb->used < sizeof(uint32_t))
    {
        ok = false;
    }
    else
    {
        read_index = cb_shift_data(cb, cb->read_index, pktlen, sizeof(uint32_t));
        if (cb->used < *pktlen + sizeof(uint32_t) || *pktlen > buflen)
            ok = false;
    }

    if (!ok)
    {
        // XXX: Sleep a while to release CPU
        usleep(25);
        return false;
    }
    read_index = cb_shift_data(cb, read_index, buf, *pktlen);

    __sync_fetch_and_sub(&cb->used, *pktlen + sizeof(uint32_t));
    cb->read_index = read_index;
    return true;
}