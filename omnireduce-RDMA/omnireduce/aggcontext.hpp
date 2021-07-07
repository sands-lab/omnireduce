#pragma once
#include "omnireduce/common.hpp"

namespace omnireduce {
    class AggContext {
        public:
            static AggContext& getInstance() {
                static AggContext instance;
                return instance;
            }
            AggContext(AggContext const&) = delete;
            void operator=(AggContext const&) = delete;            
            uint32_t num_server_threads;
            int ret;
            int serverId;
            int tensor_size;
            TensorUpdateType typecode;
            uint32_t element_size;
            int *socks;
            void *comm_buf;
            struct ibv_context *ib_ctx;
            struct ibv_port_attr port_attr;
            struct ibv_pd *pd;
            struct ibv_cq **cq;
            struct ibv_qp **qp;
            struct ibv_cq *cq_address;
            struct ibv_qp **qp_address;
            struct ibv_mr *mr;
            uint32_t **srcs_;
            struct ibv_mr **mrs_;
            uint32_t **current_offset_thread;
            struct remote_con_data_t *remote_props_array;
            std::atomic_uint_fast32_t threadid;
            AggContext();
            ~AggContext();
            void init();
            void StartMaster();
            void StopMaster();
            int post_receive_address(uint32_t);
            int post_send_ready(uint32_t);
            void wait_master_ready();
            void set_master_ready();
            pthread_t aggmasterThread;
            boost::mutex master_ready_mutex;
            boost::condition_variable master_ready_event;
            uint32_t master_ready;
    };
}
