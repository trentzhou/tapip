#include "shm_eth.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define SHM_SIZE (2*1024*1024)
#define SHM_MAGIC 0xd00ddaaa
#define CB_CAPACITY ((SHM_SIZE - sizeof(struct shmeth_internal_t))/2 - sizeof(CB_T))

struct shmeth_internal_t
{
    uint32_t magic;
    uint8_t mac_a[6];
    uint8_t mac_b[6];
};

static void gen_random_mac(uint8_t* mac)
{
    int i;
    for (i = 0; i < 6; i++)
    {
        mac[i] = (uint8_t)random();
    }
}

static uint32_t string_hash(const char* cp)
{
    uint32_t hash = 5381;
    while (*cp)
        hash = 33 * hash ^ (unsigned char) *cp++;
    return hash;
}


SHMETH_T* shmeth_open(const char* name, SHMETH_SIDE_T side)
{
    key_t shm_key = (key_t)string_hash(name);
    int shm_id = shmget(shm_key, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1)
    {
        printf("shmget failed: %d\n", errno);
        shm_id = shmget(shm_key, SHM_SIZE, 0666);
        if (shm_id != -1)
        {
            printf("Good. Crated.\n");
        }
        else
        {
            printf("Still failed: %d\n", errno);
            return NULL;
        }
    }
    void* shm_addr = shmat(shm_id, NULL, SHM_RND);
    
    
    struct shmeth_internal_t* shmint = shm_addr;

    uint8_t* cb_atob_addr = (uint8_t*)shm_addr + sizeof(struct shmeth_internal_t);
    uint8_t* cb_btoa_addr = cb_atob_addr + CB_SIZE(CB_CAPACITY);

    if (shmint->magic != SHM_MAGIC)
    {
        // not initialized yet.
        shmint->magic = SHM_MAGIC;
        gen_random_mac(shmint->mac_a);
        gen_random_mac(shmint->mac_b);

        cb_init((struct circular_buffer_t*)cb_atob_addr, CB_CAPACITY);
        cb_init((struct circular_buffer_t*)cb_btoa_addr, CB_CAPACITY);
    }

    SHMETH_T* shmeth = malloc(sizeof(SHMETH_T));
    shmeth->side = side;
    shmeth->cb_atob = (CB_T*)cb_atob_addr;
    shmeth->cb_btoa = (CB_T*)cb_btoa_addr;
    shmeth->shm_addr = shm_addr;
    shmeth->shm_id = shm_id;

    return shmeth;
}

void shmeth_close(SHMETH_T* shmeth)
{
    shmdt(shmeth->shm_addr);
    // mark the memory to be deleted
    shmctl(shmeth->shm_id, IPC_RMID, NULL);
    free(shmeth);
}

void shmeth_get_mac(SHMETH_T* shmeth, SHMETH_SIDE_T side, uint8_t* buf)
{
    struct shmeth_internal_t* shmint = shmeth->shm_addr;

    if (side == SHMETH_SIDE_A)
    {
        memcpy(buf, shmint->mac_a, 6);
    }
    else
    {
        memcpy(buf, shmint->mac_b, 6);
    }
    
}

bool shmeth_write_packet(SHMETH_T* shmeth, void* data, uint32_t len)
{
    CB_T* cb = shmeth->side == SHMETH_SIDE_A? shmeth->cb_atob: shmeth->cb_btoa;
    return cb_put_packet(cb, data, len);
}

bool shmeth_read_packet(SHMETH_T* shmeth, void* buf, uint32_t buflen, uint32_t* pktlen)
{
    CB_T* cb = shmeth->side == SHMETH_SIDE_B? shmeth->cb_atob: shmeth->cb_btoa;
    return cb_get_packet(cb, buf, buflen, pktlen);
}
