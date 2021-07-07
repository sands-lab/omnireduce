/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#include "DaietContext.hpp"
#include "daiet.hpp"
#include "utils.hpp"
#include "gloo/common/error.h"

namespace daiet {

    void* DaietMaster(void *ctx) {

        DaietContext* d_ctx_ptr = (DaietContext *) ctx;

        d_ctx_ptr->ret = master(d_ctx_ptr);

        return NULL;
    }

    DaietContext::DaietContext() :
            num_worker_threads (1), master_ready(0), data_ready(0), results(0), tensor_update_ptr(NULL), result_id(0), one_msec(1) {

        tid_counter.store(0);
        StartMaster();
    }

    DaietContext::~DaietContext() {

        StopMaster();
    }

    void DaietContext::set_num_worker_threads(uint32_t nt){
        num_worker_threads = nt;
    }

    void DaietContext::wait_master_ready() {
        boost::unique_lock<boost::mutex> lock(master_ready_mutex);

        while (master_ready!=num_worker_threads)
            master_ready_event.wait(lock);
    }

    void DaietContext::set_master_ready() {

        boost::unique_lock<boost::mutex> lock(master_ready_mutex);

        if ((++master_ready) == num_worker_threads)
            master_ready_event.notify_one();
    }

    void DaietContext::send_tensor(TensorUpdate* tuptr) {
        boost::unique_lock<boost::mutex> lock(data_ready_mutex);

        while (data_ready!=0)
            data_pop_event.wait(lock);

        tensor_update_ptr = tuptr;
        data_ready = num_worker_threads;
        data_push_event.notify_all();
    }

    bool DaietContext::receive_tensor(TensorUpdate& tu, uint16_t worker_id) {
        boost::unique_lock<boost::mutex> lock(data_ready_mutex);

        while (data_ready!=(uint32_t)(worker_id+1)) {
            if (data_push_event.wait_for(lock, one_msec) == boost::cv_status::timeout)
                return false;
        }

        tu = *tensor_update_ptr; // Copy

        if (data_ready != 1){
#ifdef OFFLOAD_BITMAP
            tu.block_count /= num_worker_threads;
            if (tu.block_count%num_worker_threads>worker_id)
                tu.block_count += 1;
            tu.count = tu.block_count * block_size;
#else
            tu.count /= num_worker_threads;
#endif
        } else {
            tu.count -= tu.start_idx;
        }

        tensor_update_ptr->start_idx += tu.count;

        if ((--data_ready) == 0)
            data_pop_event.notify_one();

        return true;
    }

    bool DaietContext::send_result(const int32_t rid) {
        boost::unique_lock<boost::mutex> lock(result_mutex);

        while (results == num_worker_threads) {
            if (result_pop_event.wait_for(lock, one_msec) == boost::cv_status::timeout)
                return false;
        }

        if ((++results)==num_worker_threads) {
            result_id = rid;
            result_push_event.notify_all();
        }

        return true;
    }

    void DaietContext::receive_result(const int32_t rid) {
        boost::unique_lock<boost::mutex> lock(result_mutex);

        while (results != num_worker_threads && result_id != rid)
            result_push_event.wait(lock);

        results = 0;
        result_id = 0;

        result_pop_event.notify_all();
    }

    void DaietContext::StartMaster() {

        /* Launch dpdk master thread */
        if (pthread_create(&masterThread, NULL, DaietMaster, this))
            GLOO_THROW("Error starting master dpdk thread");

        //Wait for EAL setup
        wait_master_ready();
    }

    void DaietContext::StopMaster() {

        force_quit = true;

        int join_ret = pthread_join(masterThread, NULL);
        if (join_ret)
            GLOO_THROW("Error joining master dpdk thread: returned ", join_ret);

        if (this->ret < 0)
            GLOO_THROW("Master dpdk thread returned ", this->ret);

    }

#ifdef OFFLOAD_BITMAP
    void DaietContext::AllReduce(gloo::float16* ptr, int count, uint8_t* bitmap_ptr, int block_count) {
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.type = FLOAT16;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        send_tensor(&tu);
        receive_result(tensor_id);
    }
#endif

    void DaietContext::AllReduce(gloo::float16* ptr, int count) {

        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.type = FLOAT16;

        send_tensor(&tu);
        receive_result(tensor_id);
    }

#ifdef OFFLOAD_BITMAP
    void DaietContext::AllReduce(float* ptr, int count, uint8_t* bitmap_ptr, int block_count) {
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.type = FLOAT32;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        send_tensor(&tu);
        receive_result(tensor_id);
    }
#endif

    void DaietContext::AllReduce(float* ptr, int count) {

        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.type = FLOAT32;

        send_tensor(&tu);
        receive_result(tensor_id);
    }

#ifdef OFFLOAD_BITMAP
    void DaietContext::AllReduce(int32_t* ptr, int count, uint8_t* bitmap_ptr, int block_count) {
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.type = INT32;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        send_tensor(&tu);
        receive_result(tensor_id);
    }
#endif

    void DaietContext::AllReduce(int32_t* ptr, int count) {

        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.type = INT32;

        send_tensor(&tu);
        receive_result(tensor_id);
    }

    bool DaietContext::try_daiet(gloo::float16* ptr, int count, int fn_) {
        if (fn_ == 1) { //sum

            AllReduce(ptr, count);

            return true;
        }

        return false;
    }

    bool DaietContext::try_daiet(float* ptr, int count, int fn_) {
        if (fn_ == 1) { //sum

            AllReduce(ptr, count);

            return true;
        }

        return false;
    }

    bool DaietContext::try_daiet(int32_t* ptr, int count, int fn_) {
        if (fn_ == 1) { //sum

            AllReduce(ptr, count);

            return true;
        }

        return false;
    }

    bool DaietContext::try_daiet(__attribute__((unused)) void* ptr, __attribute__((unused)) int count, __attribute__((unused)) int fn_) {

        return false;
    }
}
