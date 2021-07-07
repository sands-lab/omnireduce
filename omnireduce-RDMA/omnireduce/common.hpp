/**
  * OmniReduce project
  * author: jiawei.fei@kaust.edu.sa
  */
#pragma once

#include <boost/thread.hpp>
#include <atomic>
#include <infiniband/verbs.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <byteswap.h>
#include <iostream>
#include "omnireduce/params.hpp"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

namespace omnireduce {

    struct remote_con_data_t
    {
        int remoteId;
	    uint64_t addr;   /* Buffer address */
	    uint32_t rkey;   /* Remote key */
	    uint32_t qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS+1]; /* QP number */
	    uint16_t lid;	/* LID of the IB port */
	    uint8_t gid[16]; /* gid */
    };

    struct cm_con_data_t
    {
    	int remoteId;
    	uint32_t num_peers;
    	uint64_t addr;   /* Buffer address */
    	uint32_t rkey;   /* Remote key */
    	uint32_t qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS+1]; /* QP number */
    	uint16_t lid;	/* LID of the IB port */
    	uint8_t gid[16]; /* gid */
    } __attribute__((packed));

    enum TensorUpdateType {
        NONE = 0, INT32 = 1, FLOAT32 = 2, FLOAT16 = 3
    };
    enum OpType {
        NOP = 0, ALLREDUCE = 1, BROADCAST = 2, ACK = 3
    };
    struct TensorUpdate {
        void* ptr;
        uint32_t count;
        uint32_t start_idx;
        int32_t id;
        uint32_t root;
        TensorUpdateType type;
        OpType op;
        uint8_t* bitmap_ptr;
        uint32_t block_count;
        int32_t devId;
        bool async;
        bool bitmap_async;
    }; 
    extern volatile bool force_quit;
}
