#include "omnireduce/worker.hpp"
#include "omnireduce/context.hpp"
#ifdef USE_CUDA
#include "omnireduce/cuda_utils.hpp"
#endif

namespace omnireduce {
    thread_local static uint32_t num_worker_threads;
    thread_local static int32_t devId;
    thread_local static bool async;
    thread_local static bool bitmap_async;
    thread_local static uint32_t thread_id;
    thread_local static TensorUpdate tu;
    thread_local static uint32_t chunk_size;
    thread_local static uint32_t b_chunk_size;
    thread_local static uint32_t block_size;
    thread_local static uint32_t message_size;
    thread_local static uint32_t tensor_size;
    thread_local static uint32_t start_offset;
    thread_local static uint32_t num_slots_per_thread;
    thread_local static uint32_t num_blocks_per_thread;
    thread_local static uint32_t num_qps_per_aggregator_per_thread;
    thread_local static uint32_t num_aggregators;
    thread_local static uint32_t element_size;
    thread_local static uint32_t prepost_recv_num;
    thread_local static uint32_t buff_unit_size;
    thread_local static uint32_t typecode;
#ifdef USE_CUDA
    thread_local static cudaStream_t stream;
    thread_local static cudaEvent_t event;
    thread_local static cudaEvent_t *events;
    thread_local static bool *chunk_finished;
    thread_local static uint32_t *chunk_completed;
    thread_local static uint32_t chunk_num;
    thread_local static cudaStream_t b_stream;
    thread_local static cudaEvent_t *b_events;    
    thread_local static bool *b_chunk_finished;
    thread_local static uint32_t b_chunk_num;
#endif

    uint32_t find_next_nonzero_block(uint32_t next_offset)
    {
        uint32_t next_nonzero_offset = next_offset;
        uint32_t bid = (next_nonzero_offset/block_size)%num_blocks_per_thread;
        uint32_t max_index = omnireduce_par.getInfOffset(bid);
        while (next_nonzero_offset-start_offset<tensor_size && (next_nonzero_offset/block_size < tu.block_count))
        {
#ifdef USE_CUDA
            if (bitmap_async)
            {
                uint32_t b_chunk_id = (next_nonzero_offset-start_offset)*element_size/b_chunk_size;
                if (b_chunk_finished[b_chunk_id]==false)
                {
                    cudaEventSynchronize(b_events[b_chunk_id]);
                    cudaEventDestroy(b_events[b_chunk_id]);
                    b_chunk_finished[b_chunk_id] = true;                    
                }
            }
#endif
            if (tu.bitmap_ptr[next_nonzero_offset/block_size]==0)
                return next_nonzero_offset;
            next_nonzero_offset += num_slots_per_thread*message_size;
        }
        return max_index;
    }

    int post_send_client(OmniContext* dctx_ptr, uint32_t num, uint32_t *current_offsets, 
                            uint32_t *next_offsets, uint32_t slot, uint32_t qp_num, uint32_t buff_index)
    {
        struct ibv_send_wr sr;
        struct ibv_sge sge;
        struct ibv_send_wr *bad_wr = NULL;
        int qid, mid;
        if (unlikely(qp_num==0)) 
        {
            qid = (slot/num_slots_per_thread)*num_qps_per_aggregator_per_thread*num_aggregators
                    +slot%(num_qps_per_aggregator_per_thread*num_aggregators);
            mid = qp_num_to_peerid[dctx_ptr->qp[qid]->qp_num];
        }
        else 
        {
	        qid = qp_num_revert[qp_num];
	        mid = qp_num_to_peerid[qp_num];            
        }
        //std::cout<<"send: qp_num="<<dctx_ptr->qp[qid]->qp_num<<std::endl;
        int rc;
        memset(&sge, 0, sizeof(sge));
        uint8_t *tmp = (uint8_t *)dctx_ptr->comm_buf+(2*message_size*slot+buff_index*(2*message_size)*num_slots_per_thread*num_worker_threads)*buff_unit_size;
        for(uint32_t i=0; i<num; i++)
        {
            if (unlikely(current_offsets[i]+block_size-start_offset > tensor_size))
            {
                //memset(tmp+i*block_size*element_size, 0, block_size*element_size);
                if (current_offsets[i]-start_offset < tensor_size)
                    memcpy(tmp+i*block_size*element_size, (uint8_t *)tu.ptr+current_offsets[i]*element_size, (start_offset+tensor_size-current_offsets[i])*element_size);
            }
            else
            {   
                memcpy(tmp+i*block_size*element_size, (uint8_t *)tu.ptr+current_offsets[i]*element_size, block_size*element_size);
            }
        }
        memcpy(tmp+block_size*num*element_size, next_offsets, num*sizeof(uint32_t));
        sge.addr = (uintptr_t)tmp;
        sge.length = block_size*num*element_size+num*sizeof(uint32_t);
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&sr, 0, sizeof(sr));
        sr.wr_id = 0;
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        sr.send_flags = IBV_SEND_SIGNALED;
        sr.wr.rdma.remote_addr = dctx_ptr->remote_props_array[mid].addr+(2*message_size*slot
                                    +2*message_size*num_slots_per_thread*num_worker_threads*dctx_ptr->workerId)*buff_unit_size;
		sr.wr.rdma.rkey = dctx_ptr->remote_props_array[mid].rkey;
        sr.imm_data = (typecode << 28) + (num << 16) + slot;
        rc = ibv_post_send(dctx_ptr->qp[qid], &sr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post SR %d\n", rc);
        return rc;
    }

#ifdef USE_CUDA
    int cuda_sync_post_send_client(OmniContext* dctx_ptr, uint32_t num, uint32_t *current_offsets, 
                            uint32_t *next_offsets, uint32_t slot, uint32_t qp_num, uint32_t buff_index)
    {
        struct ibv_send_wr sr;
        struct ibv_sge sge;
        struct ibv_send_wr *bad_wr = NULL;
        int qid, mid;
        if (unlikely(qp_num==0)) 
        {
            qid = (slot/num_slots_per_thread)*num_qps_per_aggregator_per_thread*num_aggregators
                    +slot%(num_qps_per_aggregator_per_thread*num_aggregators);
            mid = qp_num_to_peerid[dctx_ptr->qp[qid]->qp_num];
        }
        else 
        {
	        qid = qp_num_revert[qp_num];
	        mid = qp_num_to_peerid[qp_num];            
        }
        //std::cout<<"send: qp_num="<<dctx_ptr->qp[qid]->qp_num<<std::endl;
        int rc;
        memset(&sge, 0, sizeof(sge));
        uint8_t *tmp = (uint8_t *)dctx_ptr->comm_buf+(2*message_size*slot+buff_index*(2*message_size)*num_slots_per_thread*num_worker_threads)*buff_unit_size;
        for(uint32_t i=0; i<num; i++)
        {
            if (unlikely(current_offsets[i]+block_size-start_offset > tensor_size))
            {
                //memset(tmp+i*block_size*element_size, 0, block_size*element_size);
                if (current_offsets[i]-start_offset < tensor_size)
                    cudaMemcpy(tmp+i*block_size*element_size, (uint8_t *)tu.ptr+current_offsets[i]*element_size, (start_offset+tensor_size-current_offsets[i])*element_size, cudaMemcpyDeviceToHost);
            }
            else
            {   
                cudaMemcpy(tmp+i*block_size*element_size, (uint8_t *)tu.ptr+current_offsets[i]*element_size, block_size*element_size, cudaMemcpyDeviceToHost);
            }
        }
        memcpy(tmp+block_size*num*element_size, next_offsets, num*sizeof(uint32_t));
        sge.addr = (uintptr_t)tmp;
        sge.length = block_size*num*element_size+num*sizeof(uint32_t);
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&sr, 0, sizeof(sr));
        sr.wr_id = 0;
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        sr.send_flags = IBV_SEND_SIGNALED;
        sr.wr.rdma.remote_addr = dctx_ptr->remote_props_array[mid].addr+(2*message_size*slot
                                    +2*message_size*num_slots_per_thread*num_worker_threads*dctx_ptr->workerId)*buff_unit_size;
		sr.wr.rdma.rkey = dctx_ptr->remote_props_array[mid].rkey;
        sr.imm_data = (typecode << 28) + (num << 16) + slot;
        rc = ibv_post_send(dctx_ptr->qp[qid], &sr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post SR %d\n", rc);
        return rc;
    }
    int cuda_async_post_send_client(OmniContext* dctx_ptr, uint32_t num, uint32_t *current_offsets, 
                            uint32_t *next_offsets, uint32_t slot, uint32_t qp_num, uint32_t buff_index)
    {
        struct ibv_send_wr sr;
        struct ibv_sge sge;
        struct ibv_send_wr *bad_wr = NULL;
        int qid, mid;
        if (unlikely(qp_num==0)) 
        {
            qid = (slot/num_slots_per_thread)*num_qps_per_aggregator_per_thread*num_aggregators
                    +slot%(num_qps_per_aggregator_per_thread*num_aggregators);
            mid = qp_num_to_peerid[dctx_ptr->qp[qid]->qp_num];
        }
        else 
        {
	        qid = qp_num_revert[qp_num];
	        mid = qp_num_to_peerid[qp_num];            
        }
        //std::cout<<"send: qp_num="<<dctx_ptr->qp[qid]->qp_num<<std::endl;
        int rc;
        memset(&sge, 0, sizeof(sge));
        uint8_t *tmp = (uint8_t *)dctx_ptr->comm_buf+(2*message_size*slot+buff_index*(2*message_size)*num_slots_per_thread*num_worker_threads)*buff_unit_size;
        for(uint32_t i=0; i<num; i++)
        {
            uint32_t chunk_id = (current_offsets[i]-start_offset)*element_size/chunk_size;
            if (chunk_finished[chunk_id]==false)
            {
                cudaEventSynchronize(events[chunk_id]);
                cudaEventDestroy(events[chunk_id]);
                chunk_finished[chunk_id] = true;
            }
            if (unlikely(current_offsets[i]+block_size-start_offset > tensor_size))
            {
                //memset(tmp+i*block_size*element_size, 0, block_size*element_size);
                if (current_offsets[i]-start_offset < tensor_size)
                    memcpy(tmp+i*block_size*element_size, (uint8_t *)dctx_ptr->host_tensor+current_offsets[i]*element_size, (start_offset+tensor_size-current_offsets[i])*element_size);
            }
            else
            {   
                memcpy(tmp+i*block_size*element_size, (uint8_t *)dctx_ptr->host_tensor+current_offsets[i]*element_size, block_size*element_size);
            }
        }
        memcpy(tmp+block_size*num*element_size, next_offsets, num*sizeof(uint32_t));
        sge.addr = (uintptr_t)tmp;
        sge.length = block_size*num*element_size+num*sizeof(uint32_t);
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&sr, 0, sizeof(sr));
        sr.wr_id = 0;
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        sr.send_flags = IBV_SEND_SIGNALED;
        sr.wr.rdma.remote_addr = dctx_ptr->remote_props_array[mid].addr+(2*message_size*slot
                                    +2*message_size*num_slots_per_thread*num_worker_threads*dctx_ptr->workerId)*buff_unit_size;
		sr.wr.rdma.rkey = dctx_ptr->remote_props_array[mid].rkey;
        sr.imm_data = (typecode << 28) + (num << 16) + slot;
        rc = ibv_post_send(dctx_ptr->qp[qid], &sr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post SR %d\n", rc);
        return rc;
    }
#endif

    int post_receive_client(OmniContext* dctx_ptr, uint32_t slot, uint32_t qp_num)
    {
        struct ibv_recv_wr rr;
    	struct ibv_sge sge;
	    struct ibv_recv_wr *bad_wr;
        int rc;
        int qid;
        if (unlikely(qp_num==0))
            qid = (slot/num_slots_per_thread)*num_qps_per_aggregator_per_thread*num_aggregators
                    +slot%(num_qps_per_aggregator_per_thread*num_aggregators);
        else
            qid = qp_num_revert[qp_num];
        memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)(dctx_ptr->comm_buf);
        sge.length = 0;
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&rr, 0, sizeof(rr));
        rr.wr_id = 0;
        rr.sg_list = &sge;
        rr.num_sge = 1;
        rc = ibv_post_recv(dctx_ptr->qp[qid], &rr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post RR\n");
        return rc;
    }

    void *worker(void* arg) {        
        OmniContext* dctx_ptr = (OmniContext*) arg;
        uint32_t total_num_msgs = 0;
        uint32_t first_burst = 0;
        int ret = 0;
        uint32_t finished_slots = 0;
        int ne = 0;
        uint32_t blocks_per_packet = 0;
        uint32_t slot = 0;
        uint32_t * meta_ptr = NULL;
        uint32_t nonzero_block_num = 0;
        uint32_t bid = 0;
        uint32_t copy_size = 0;
        buff_unit_size = omnireduce_par.getBuffUnitSize();
        num_worker_threads = omnireduce_par.getNumWorkerThreads();
        block_size = omnireduce_par.getBlockSize();
        message_size = omnireduce_par.getMessageSize();
        chunk_size = omnireduce_par.getChunkSize();
        b_chunk_size = omnireduce_par.getBitmapChunkSize();
        num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        num_blocks_per_thread = num_slots_per_thread*(message_size/block_size);
        num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        num_aggregators = omnireduce_par.getNumAggregators();
        prepost_recv_num = omnireduce_par.getPrepostRecvNum();
#ifdef USE_CUDA
        events = (cudaEvent_t *)malloc(1024*sizeof(cudaEvent_t));
        chunk_finished = (bool *)malloc(1024*sizeof(bool));
        chunk_completed = (uint32_t *)malloc(1024*sizeof(uint32_t));
        uint32_t *previous_chunk = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread*message_size);
        memset(previous_chunk, 0, sizeof(uint32_t)*num_slots_per_thread*message_size);
        b_events = (cudaEvent_t *)malloc(1024*sizeof(cudaEvent_t));
        b_chunk_finished = (bool *)malloc(1024*sizeof(bool));
#endif
        uint32_t *current_offset = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread*message_size);
        memset(current_offset, 0, sizeof(uint32_t)*num_slots_per_thread*message_size);
        uint32_t *current_offsets = (uint32_t *)malloc(sizeof(uint32_t)*message_size);
        memset(current_offsets, 0, sizeof(uint32_t)*message_size);
        uint32_t *next_offsets = (uint32_t *)malloc(sizeof(uint32_t)*message_size);
        memset(next_offsets, 0, sizeof(uint32_t)*message_size);
        uint32_t *buff_index = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(buff_index, 0, sizeof(uint32_t)*num_slots_per_thread);
        uint32_t *finished_blocks = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(finished_blocks, 0, sizeof(uint32_t)*num_slots_per_thread);
        struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];

        thread_id = dctx_ptr->threadid.fetch_add(1);
        dctx_ptr->set_master_ready();
        for (uint32_t i=0; i<num_slots_per_thread; i++)
            for (uint32_t j=0; j<prepost_recv_num; j++)
                post_receive_client(dctx_ptr, i+num_slots_per_thread*thread_id, 0);
        while (!force_quit) 
        {
            if (dctx_ptr->receive_tensor(tu, thread_id)) 
            {
                block_size = omnireduce_par.getBlockSize();
                num_blocks_per_thread = num_slots_per_thread*(message_size/block_size);
                memset(buff_index, 0, sizeof(uint32_t)*num_slots_per_thread);
                memset(finished_blocks, 0, sizeof(uint32_t)*num_slots_per_thread);
                finished_slots = 0;
                switch (tu.type)
                {
                    case INT32:
                        typecode = INT32;
                        element_size = sizeof(int32_t);
                        break;
                    case FLOAT32:
                        typecode = FLOAT32;
                        element_size = sizeof(float);
                        break;
                    default:
                        std::cerr<<"Data type error"<<std::endl;
                        exit(1);
                }
                devId = tu.devId;
                async = tu.async;
                bitmap_async = tu.bitmap_async;
                start_offset = tu.start_idx;
                tensor_size = tu.count;
                //std::cout<<thread_id<<":"<<tensor_size<<" "<<start_offset<<std::endl;
#ifdef USE_CUDA
                memset(previous_chunk, 0, sizeof(uint32_t)*num_blocks_per_thread);
                cudaSetDevice(devId);
                uint8_t *d_bitmap;
                if (bitmap_async && tensor_size>0)
                {
                    uint32_t block_num = tensor_size/block_size;
                    if (tensor_size%block_size!=0)
                        block_num += 1;
                    cudaMalloc((void **)&d_bitmap, block_num);
                    cudaStreamCreate(&b_stream);
                    b_chunk_num = tensor_size*element_size/b_chunk_size;
                    if (tensor_size*element_size%b_chunk_size!=0)
                        b_chunk_num += 1;
                    memset(b_chunk_finished, 0, sizeof(bool)*b_chunk_num);
                    for (uint32_t i=0; i<b_chunk_num-1; i++)
                    {
                        cudaEventCreate(&b_events[i]);
                        switch (tu.type)
                        {   
                            case INT32:
                                compute_bitmap((int32_t*)tu.ptr+start_offset+b_chunk_size*i/element_size,
                                                d_bitmap+b_chunk_size*i/element_size/block_size,
                                                b_chunk_size/element_size,
                                                block_size, b_stream, 0);
                            case FLOAT32:                    
                                compute_bitmap((float*)tu.ptr+start_offset+b_chunk_size*i/element_size,
                                                d_bitmap+b_chunk_size*i/element_size/block_size,
                                                b_chunk_size/element_size,
                                                block_size, b_stream, 0.0);
                            default:
                                std::cerr<<"Data type error"<<std::endl;
                                exit(1);
                        }
                        cudaMemcpyAsync(tu.bitmap_ptr+start_offset/block_size+b_chunk_size*i/element_size/block_size,
                                        d_bitmap+b_chunk_size*i/element_size/block_size,
                                        b_chunk_size/element_size/block_size,
                                        cudaMemcpyDeviceToHost, b_stream);
                        cudaEventRecord(b_events[i], b_stream);
                    }
                    cudaEventCreate(&b_events[b_chunk_num-1]);
                    switch (tu.type)
                    {   
                        case INT32:
                            compute_bitmap((int32_t*)tu.ptr+start_offset+b_chunk_size*(b_chunk_num-1)/element_size,
                                            d_bitmap+b_chunk_size*(b_chunk_num-1)/element_size/block_size,
                                            tensor_size-(b_chunk_num-1)*b_chunk_size/element_size,
                                            block_size, b_stream, 0);
                            break;                    
                        case FLOAT32:
                            compute_bitmap((float*)tu.ptr+start_offset+b_chunk_size*(b_chunk_num-1)/element_size,
                                            d_bitmap+b_chunk_size*(b_chunk_num-1)/element_size/block_size,
                                            tensor_size-(b_chunk_num-1)*b_chunk_size/element_size,
                                            block_size, b_stream, 0.0);
                            break;
                        default:
                            std::cerr<<"Data type error"<<std::endl;
                            exit(1);
                    }
                    cudaMemcpyAsync(tu.bitmap_ptr+start_offset/block_size+b_chunk_size*(b_chunk_num-1)/element_size/block_size,
                                    d_bitmap+b_chunk_size*(b_chunk_num-1)/element_size/block_size,
                                    block_num-(b_chunk_num-1)*b_chunk_size/element_size/block_size,
                                    cudaMemcpyDeviceToHost, b_stream);
                    cudaEventRecord(b_events[b_chunk_num-1], b_stream);          
                }
                if (async==true && tensor_size>0)
                {
                    cudaStreamCreate(&stream);
                    chunk_num = tensor_size*element_size/chunk_size;
                    if (tensor_size*element_size%chunk_size!=0)
                        chunk_num += 1;
                    memset(chunk_finished, 0, sizeof(bool)*chunk_num);
                    memset(chunk_completed, 0, sizeof(uint32_t)*chunk_num);
                    for (uint32_t i=0; i<chunk_num-1; i++)
                    {
                        cudaEventCreate(&events[i]);
                        cudaMemcpyAsync((uint8_t *)dctx_ptr->host_tensor+start_offset*element_size+chunk_size*i, 
                                            (uint8_t *)tu.ptr+start_offset*element_size+chunk_size*i, 
                                            chunk_size, cudaMemcpyDeviceToHost, stream);
                        cudaEventRecord(events[i], stream);
                    }
                    cudaEventCreate(&events[chunk_num-1]);
                    cudaMemcpyAsync((uint8_t *)dctx_ptr->host_tensor+start_offset*element_size+chunk_size*(chunk_num-1), 
                                        (uint8_t *)tu.ptr+start_offset*element_size+chunk_size*(chunk_num-1), 
                                        tensor_size*element_size-chunk_size*(chunk_num-1), cudaMemcpyDeviceToHost, stream);
                    cudaEventRecord(events[chunk_num-1], stream);
                }
#endif
                total_num_msgs = tensor_size/message_size;
                if (tensor_size%message_size != 0 && tensor_size>0)
                    total_num_msgs++;
                first_burst = (total_num_msgs < num_slots_per_thread) ? total_num_msgs:num_slots_per_thread;
                for (uint32_t i=0; i<first_burst; i++)
                {
                    for (uint32_t j=0; j<(message_size/block_size); j++)
                    {
                        current_offsets[j] = start_offset+i*message_size+j*block_size;
                        current_offset[(current_offsets[j]/block_size)%num_blocks_per_thread] = current_offsets[j];
                        next_offsets[j] = find_next_nonzero_block(current_offsets[j]+message_size*num_slots_per_thread);
                    }
                    if (devId<0)
                        ret = post_send_client(dctx_ptr, message_size/block_size, current_offsets, next_offsets, (start_offset/message_size+i)%num_slots_per_thread+num_slots_per_thread*thread_id, 0, 0);
#ifdef USE_CUDA
                    else
                    {
                        if (async==false)
                            ret = cuda_sync_post_send_client(dctx_ptr, message_size/block_size, current_offsets, next_offsets, (start_offset/message_size+i)%num_slots_per_thread+num_slots_per_thread*thread_id, 0, 0);
                        else
                            ret = cuda_async_post_send_client(dctx_ptr, message_size/block_size, current_offsets, next_offsets, (start_offset/message_size+i)%num_slots_per_thread+num_slots_per_thread*thread_id, 0, 0);
                    }                               
#endif
                }
                while (finished_slots<first_burst && !force_quit)
                {
                    ne = ibv_poll_cq(dctx_ptr->cq[thread_id], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
                    if (ne>0)
                    {
                        for (int i = 0; i < ne; ++i)
                        {
                            if (wc[i].status == IBV_WC_SUCCESS)
                            {
                                if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
                                {
                                    blocks_per_packet = (wc[i].imm_data & 0x0FFF0000) >> 16;
                                    slot = (wc[i].imm_data & 0x0000FFFF)%num_slots_per_thread;
                                    post_receive_client(dctx_ptr, slot+num_slots_per_thread*thread_id, wc[i].qp_num);
                                    meta_ptr = (uint32_t *)((uint8_t *)(dctx_ptr->comm_buf)+block_size*blocks_per_packet*element_size
                                                +(slot*(2*message_size)+thread_id*(2*message_size)*num_slots_per_thread
                                                +buff_index[slot]*(2*message_size)*num_slots_per_thread*num_worker_threads)*buff_unit_size);
                                    nonzero_block_num = 0;
                                    for(uint32_t k=0; k<blocks_per_packet; k++)
                                    {
                                        bid = (meta_ptr[k]/block_size)%num_blocks_per_thread;
                                        copy_size = (start_offset+tensor_size-current_offset[bid])*element_size;
                                        if (likely(copy_size>block_size*element_size))
                                            copy_size = block_size*element_size;
                                        if (likely(copy_size>0)) {
                                            if (devId<0) {
                                                //if (thread_id==0)
                                                //    std::cout<<bid<<":"<<current_offset[bid]<<std::endl;
                                                memcpy((uint8_t *)tu.ptr+current_offset[bid]*element_size, 
                                                    (uint8_t *)dctx_ptr->comm_buf+k*block_size*element_size
                                                    +(slot*(2*message_size)+thread_id*(2*message_size)*num_slots_per_thread
                                                    +buff_index[slot]*(2*message_size)*num_slots_per_thread*num_worker_threads)*buff_unit_size,
                                                    copy_size);
                                            }
#ifdef USE_CUDA
                                            else {
                                                if (async==false)
                                                    cudaMemcpy((uint8_t *)tu.ptr+current_offset[bid]*element_size, 
                                                        (uint8_t *)dctx_ptr->comm_buf+k*block_size*element_size
                                                        +(slot*(2*message_size)+thread_id*(2*message_size)*num_slots_per_thread
                                                        +buff_index[slot]*(2*message_size)*num_slots_per_thread*num_worker_threads)*buff_unit_size,
                                                        copy_size, cudaMemcpyHostToDevice);
                                                else
                                                {
                                                    uint32_t current_chunk = (current_offset[bid]-start_offset)*element_size/chunk_size;
                                                    if (chunk_finished[current_chunk]==false)
                                                    {
                                                        cudaEventSynchronize(events[current_chunk]);
                                                        cudaEventDestroy(events[current_chunk]);
                                                        chunk_finished[current_chunk] = true;
                                                    }
                                                    if (bitmap_async)
                                                    {
                                                        uint32_t b_current_chunk = (current_offset[bid]-start_offset)*element_size/b_chunk_size;
                                                        if (b_chunk_finished[b_current_chunk]==false)
                                                        {
                                                            cudaEventSynchronize(b_events[b_current_chunk]);
                                                            cudaEventDestroy(b_events[b_current_chunk]);
                                                            b_chunk_finished[b_current_chunk] = true;                    
                                                        }
                                                    }
                                                    memcpy((uint8_t *)dctx_ptr->host_tensor+current_offset[bid]*element_size, 
                                                        (uint8_t *)dctx_ptr->comm_buf+k*block_size*element_size
                                                        +(slot*(2*message_size)+thread_id*(2*message_size)*num_slots_per_thread
                                                        +buff_index[slot]*(2*message_size)*num_slots_per_thread*num_worker_threads)*buff_unit_size,
                                                        copy_size);
                                                    if (meta_ptr[k]>=omnireduce_par.getInfOffset(0))
                                                        current_chunk=chunk_num-1;
                                                    for(uint32_t cid=previous_chunk[bid]; cid<current_chunk; cid++) 
                                                    {
                                                        chunk_completed[cid]++;
                                                        if (chunk_completed[cid]==num_blocks_per_thread) 
                                                        {
                                                            //if (dctx_ptr->workerId==0)
                                                            //    std::cout<<chunk_num<<" "<<start_offset<<" "<<cid<<std::endl;
                                                            cudaMemcpyAsync((uint8_t *)tu.ptr+start_offset*element_size+chunk_size*cid, 
                                                                                (uint8_t *)dctx_ptr->host_tensor+start_offset*element_size+chunk_size*cid, 
                                                                                chunk_size, cudaMemcpyHostToDevice, stream);
                                                        }
                                                    }                                                      
                                                    previous_chunk[bid] = current_chunk;
                                                }                                                
                                            }
#endif
                                        }
                                        current_offset[bid] = meta_ptr[k];
                                        if (current_offset[bid]<omnireduce_par.getInfOffset(0))
                                        {
                                            if (tu.bitmap_ptr[current_offset[bid]/block_size]==0)
                                            {
                                                current_offsets[nonzero_block_num] = current_offset[bid];
                                                next_offsets[nonzero_block_num] = find_next_nonzero_block(current_offsets[nonzero_block_num]+message_size*num_slots_per_thread);
                                                nonzero_block_num++;
                                            }
                                        }
                                        else
                                        {
                                            finished_blocks[slot]++;
                                        }
                                    } // for(uint32_t k=0; k<blocks_per_packet; k++)
                                    buff_index[slot] = (buff_index[slot]+1)%omnireduce_par.getNumCommbuff();
                                    if (finished_blocks[slot] < (message_size/block_size))
                                    {
                                        if (nonzero_block_num>0)
                                        {
                                            if (devId<0)
                                                ret = post_send_client(dctx_ptr, nonzero_block_num, current_offsets, next_offsets, slot+num_slots_per_thread*thread_id, wc[i].qp_num, buff_index[slot]);
#ifdef USE_CUDA
                                            else
                                            {
                                                if (async==false)
                                                    ret = cuda_sync_post_send_client(dctx_ptr, nonzero_block_num, current_offsets, next_offsets, slot+num_slots_per_thread*thread_id, wc[i].qp_num, buff_index[slot]);
                                                else
                                                    ret = cuda_async_post_send_client(dctx_ptr, nonzero_block_num, current_offsets, next_offsets, slot+num_slots_per_thread*thread_id, wc[i].qp_num, buff_index[slot]);
                                            }        
#endif                                            
                                            if (ret)
                                            {
                                                fprintf(stderr, "failed to post SR\n");
                                                exit(1);
                                            }
                                        }
                                    }
                                    else
                                    {
                                        finished_slots++;
                                    }
                                } //if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
                            } //if (wc[i].status == IBV_WC_SUCCESS)
                        } //for (int i = 0; i < ne; ++i)
                    } //if (ne>0)
                } //while (finished_slots<first_burst)
#ifdef USE_CUDA
                if (async==true)
                {
                    cudaMemcpyAsync((uint8_t *)tu.ptr+start_offset*element_size+chunk_size*(chunk_num-1), 
                                        (uint8_t *)dctx_ptr->host_tensor+start_offset*element_size+chunk_size*(chunk_num-1), 
                                        tensor_size*element_size-chunk_size*(chunk_num-1), cudaMemcpyHostToDevice, stream);
                    cudaStreamSynchronize(stream);
                }
#endif            
                while (!dctx_ptr->send_result(tu.id) && !force_quit)
                    ;
#ifdef USE_CUDA           
                if (async==true)
                    cudaStreamDestroy(stream);
                cudaFree(d_bitmap);
#endif
            }//if (dctx_ptr->receive_tensor(tu, thread_id)) 
        }//while (!force_quit)
        return NULL;
    } //worker

    int dr_post_receive_client(OmniContext* dctx_ptr, uint32_t slot, uint32_t qp_num)
    {
        int rc=0;
        struct ibv_recv_wr rr;
        struct ibv_sge sge;
        struct ibv_recv_wr *bad_wr;
        int qid;
        if (unlikely(qp_num==0))
            qid = (slot/num_slots_per_thread)*num_qps_per_aggregator_per_thread*num_aggregators
                    +slot%(num_qps_per_aggregator_per_thread*num_aggregators);
        else
            qid = qp_num_revert[qp_num];
        memset(&sge, 0, sizeof(sge));
#ifdef USE_CUDA
        sge.addr = (uintptr_t)(dctx_ptr->cuda_comm_buf);
#else
        sge.addr = (uintptr_t)(dctx_ptr->comm_buf);
#endif
        sge.length = 0;
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&rr, 0, sizeof(rr));
        rr.wr_id = 0;
        rr.sg_list = &sge;
        rr.num_sge = 1;
        rc = ibv_post_recv(dctx_ptr->qp[qid], &rr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post RR\n");
        return rc;
    }

    int dr_post_send_client(OmniContext* dctx_ptr, uint32_t current_offset, uint32_t next_offset, uint32_t slot, uint32_t qp_num)
    {
        int rc=0;
        struct ibv_send_wr sr;
        struct ibv_sge sge;
        struct ibv_send_wr *bad_wr = NULL;
        int qid, mid;
        uint32_t length = tensor_size - (current_offset-start_offset);
        if (unlikely(qp_num==0)) 
        {
            qid = (slot/num_slots_per_thread)*num_qps_per_aggregator_per_thread*num_aggregators
                    +slot%(num_qps_per_aggregator_per_thread*num_aggregators);
            mid = qp_num_to_peerid[dctx_ptr->qp[qid]->qp_num];
        }
        else 
        {
	        qid = qp_num_revert[qp_num];
	        mid = qp_num_to_peerid[qp_num];            
        }
        if (length>block_size)
            length = block_size;
        memset(&sge, 0, sizeof(sge));
#ifdef USE_CUDA
        uint8_t *tmp = (uint8_t *)(dctx_ptr->cuda_comm_buf)+current_offset*element_size;
#else
        uint8_t *tmp = (uint8_t *)(dctx_ptr->comm_buf)+current_offset*element_size;
#endif
        sge.addr = (uintptr_t)tmp;
        sge.length = length*element_size;
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&sr, 0, sizeof(sr));
        sr.wr_id = 0;
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        sr.send_flags = IBV_SEND_SIGNALED;
        sr.wr.rdma.remote_addr =  dctx_ptr->remote_props_array[mid].addr+(block_size*slot
                                    +block_size*num_slots_per_thread*num_worker_threads*dctx_ptr->workerId)*buff_unit_size;
        sr.wr.rdma.rkey = dctx_ptr->remote_props_array[mid].rkey;
        sr.imm_data = next_offset;
        rc = ibv_post_send(dctx_ptr->qp[qid], &sr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post SR %d\n", rc);
        return rc;
    }

    uint32_t dr_find_next_nonzero_block(uint32_t next_offset)
    {
        uint32_t next_nonzero_offset = next_offset;
        uint32_t bid = (next_nonzero_offset/block_size)%num_slots_per_thread;
        uint32_t max_index = omnireduce_par.getInfOffset(bid);
        while (next_nonzero_offset-start_offset<tensor_size && (next_nonzero_offset/block_size < tu.block_count))
        {
            if (tu.bitmap_ptr[next_nonzero_offset/block_size]==0)
                return next_nonzero_offset;
            next_nonzero_offset += num_slots_per_thread*block_size;
        }
        return max_index;
    }

    void *dr_worker(void* arg) {
        OmniContext* dctx_ptr = (OmniContext*) arg;
        uint32_t total_num_msgs = 0;
        uint32_t first_burst = 0;
        int ret = 0;
        uint32_t finished_slots = 0;
        int ne = 0;
        uint32_t slot = 0;
        uint32_t bid = 0;
        uint32_t next_offset = 0;
        buff_unit_size = omnireduce_par.getBuffUnitSize();
        num_worker_threads = omnireduce_par.getNumWorkerThreads();
        block_size = omnireduce_par.getBlockSize();
        chunk_size = omnireduce_par.getChunkSize();
        num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        num_aggregators = omnireduce_par.getNumAggregators();
        prepost_recv_num = omnireduce_par.getPrepostRecvNum();
        uint32_t *current_offset = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(current_offset, 0, sizeof(uint32_t)*num_slots_per_thread);
        struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
        thread_id = dctx_ptr->threadid.fetch_add(1);
        dctx_ptr->set_master_ready();
        for (uint32_t i=0; i<num_slots_per_thread; i++)
            for (uint32_t j=0; j<prepost_recv_num; j++)
                dr_post_receive_client(dctx_ptr, i+num_slots_per_thread*thread_id, 0);
        while (!force_quit)
        {
            if (dctx_ptr->receive_tensor(tu, thread_id))
            {
                block_size = omnireduce_par.getBlockSize();
                finished_slots = 0;
                
                switch (tu.type)
                {
                    case INT32:
                        typecode = INT32;
                        element_size = sizeof(int32_t);
                        break;
                    case FLOAT32:
                        typecode = FLOAT32;
                        element_size = sizeof(float);
                        break;
                    default:
                        std::cerr<<"Data type error"<<std::endl;
                        exit(1);
                }
                devId = tu.devId;
                async = tu.async;
                start_offset = tu.start_idx;
                tensor_size = tu.count;

#ifdef USE_CUDA
                cudaStreamCreate(&stream);
                cudaEventCreate(&event);
                cudaMemcpyAsync((uint8_t*)(dctx_ptr->cuda_comm_buf)+start_offset*element_size, (uint8_t*)(tu.ptr)+start_offset*element_size, 
                                tensor_size*element_size, cudaMemcpyDeviceToDevice, stream);
                cudaEventRecord(event, stream);
                cudaEventSynchronize(event);
#else
                memcpy((uint8_t*)(dctx_ptr->comm_buf)+start_offset*element_size, (uint8_t*)(tu.ptr)+start_offset*element_size, tensor_size*element_size);
#endif


                total_num_msgs = tensor_size/block_size;
                if (tensor_size%block_size != 0 && tensor_size>0)
                    total_num_msgs++;
                first_burst = (total_num_msgs < num_slots_per_thread) ? total_num_msgs:num_slots_per_thread;
                for (uint32_t i=0; i<first_burst; i++)
                {
                    next_offset = dr_find_next_nonzero_block(start_offset+i*block_size+block_size*num_slots_per_thread);
                    ret = dr_post_send_client(dctx_ptr, start_offset+i*block_size, next_offset, 
                                                 (next_offset/block_size)%num_slots_per_thread+num_slots_per_thread*thread_id, 0);
                }
                
                int print_count=0;
                while (finished_slots<first_burst && !force_quit)
                {
                    ne = ibv_poll_cq(dctx_ptr->cq[thread_id], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
                    if (ne>0)
                    {
                        for (int i = 0; i < ne; ++i)
                        {
                            if (wc[i].status == IBV_WC_SUCCESS)
                            {
                                if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
                                {
                                    uint32_t imm_data = wc[i].imm_data;
                                    slot = (imm_data/block_size)%num_slots_per_thread;
                                    dr_post_receive_client(dctx_ptr, slot, wc[i].qp_num);
                                    if (imm_data<omnireduce_par.getInfOffset(0))
                                    {
                                        if (tu.bitmap_ptr[imm_data/block_size]==0) {
                                            next_offset = dr_find_next_nonzero_block(imm_data+block_size*num_slots_per_thread);
                                            ret = dr_post_send_client(dctx_ptr, imm_data, next_offset, slot+num_slots_per_thread*thread_id, wc[i].qp_num);
                                        }
                                    }
                                    else
                                    {
                                        finished_slots++;
                                    }
                                }
                            }
                            else
                            {
                                std::cout<<"error code "<<wc[i].status<<" operation code "<<wc[i].opcode<<std::endl;
                            }
                        }
                    } //if (ne>0)
                } //while (finished_slots<first_burst && !force_quit)

#ifdef USE_CUDA
                cudaMemcpyAsync((uint8_t*)(tu.ptr)+start_offset*element_size, (uint8_t*)(dctx_ptr->cuda_comm_buf)+start_offset*element_size, 
                                tensor_size*element_size, cudaMemcpyDeviceToDevice, stream);
                cudaEventRecord(event, stream);
                cudaEventSynchronize(event);                
                cudaStreamDestroy(stream);
                cudaEventDestroy(event);
#else
                memcpy((uint8_t*)(tu.ptr)+start_offset*element_size, (uint8_t*)(dctx_ptr->comm_buf)+start_offset*element_size, tensor_size*element_size);
#endif

                while (!dctx_ptr->send_result(tu.id) && !force_quit);
            } //if (dctx_ptr->receive_tensor(tu, thread_id))
        } //while (!force_quit)
        return NULL;             
    } //dr_worker
}
