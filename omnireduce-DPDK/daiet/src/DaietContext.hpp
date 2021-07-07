/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#pragma once

#define DAIET

#include <boost/thread.hpp>
#include <atomic>
#include "gloo/types.h"

namespace daiet {

    void *DaietMaster(void *ctx);

    enum TensorUpdateType {
        NONE = 0, INT32 = 1, FLOAT32 = 2, FLOAT16 = 3
    };

    struct TensorUpdate {
            void* ptr;
            int count;
            int start_idx;
            int32_t id;
            TensorUpdateType type;
#ifdef OFFLOAD_BITMAP
            uint8_t* bitmap_ptr;
            int block_count;
#endif
    };

    /* Singleton class*/
    class DaietContext {
        public:

            static DaietContext& getInstance() {
                // Guaranteed to be destroyed and instantiated on first use.
                static DaietContext instance;
                return instance;
            }

            DaietContext(DaietContext const&) = delete;
            void operator=(DaietContext const&) = delete;

            void wait_master_ready();
            void set_master_ready();
            void set_num_worker_threads(uint32_t);

            void receive_result(const int32_t);
            bool send_result(const int32_t);
            bool receive_tensor(TensorUpdate&, uint16_t);
            void send_tensor(TensorUpdate*);

            void StartMaster();
            void StopMaster();

#ifdef OFFLOAD_BITMAP
            void AllReduce(gloo::float16*, int, uint8_t*, int);
            void AllReduce(float*, int, uint8_t*, int);
            void AllReduce(int32_t*, int, uint8_t*, int);
            static const uint32_t block_size = 256;
#endif
            void AllReduce(gloo::float16*, int);
            void AllReduce(float*, int);
            void AllReduce(int32_t*, int);

            bool try_daiet(gloo::float16*, int, int);
            bool try_daiet(float*, int, int);
            bool try_daiet(int32_t*, int, int);
            bool try_daiet(void*, int, int);

            friend void *DaietMaster(void*);

        private:

            DaietContext();
            virtual ~DaietContext();

            pthread_t masterThread;
            int ret;

            std::atomic_uint_fast32_t tid_counter;
            boost::mutex master_ready_mutex, data_ready_mutex, result_mutex;
            boost::condition_variable master_ready_event, data_push_event, data_pop_event, result_push_event, result_pop_event;
            uint32_t num_worker_threads;

            // Shared
            uint32_t master_ready;
            uint32_t data_ready;
            uint32_t results;
            TensorUpdate* tensor_update_ptr;
            int32_t result_id;
            // ***

            boost::chrono::milliseconds one_msec;
    };
}

