#ifndef _SHM_ETH_H_
#define _SHM_ETH_H_

// A simulated ethernet interface using shared memory
#include "fast_pipe.h"

enum shmeth_side_t
{
    SHMETH_SIDE_A,
    SHMETH_SIDE_B,
};

typedef enum shmeth_side_t SHMETH_SIDE_T;

struct shmeth_t
{
    SHMETH_SIDE_T side;
    CB_T* cb_atob;
    CB_T* cb_btoa;
    int shm_id;
    void* shm_addr;
};

typedef struct shmeth_t SHMETH_T;

SHMETH_T* shmeth_open(const char* name, SHMETH_SIDE_T side);
void shmeth_close(SHMETH_T* shmeth);

void shmeth_get_mac(SHMETH_T* shmeth, SHMETH_SIDE_T side, uint8_t* buf);
bool shmeth_write_packet(SHMETH_T* shmeth, void* data, uint32_t len);
bool shmeth_read_packet(SHMETH_T* shmeth, void* buf, uint32_t buflen, uint32_t* pktlen);

#endif
