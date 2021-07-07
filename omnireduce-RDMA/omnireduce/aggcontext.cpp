#include "omnireduce/aggcontext.hpp"
#include "omnireduce/omnireduce.hpp"


namespace omnireduce {

    int AggContext::post_receive_address(uint32_t workerId)
    {
        int rc = 0;
        struct ibv_sge list;
        list.addr = (uint64_t)srcs_[workerId];
        list.length = 3*sizeof(uint32_t);
        list.lkey = mrs_[workerId]->lkey;
        struct ibv_recv_wr wr;
        memset(&wr, 0, sizeof(wr));
        wr.sg_list = &list;
        wr.num_sge = 1;
        struct ibv_recv_wr* bad_wr = nullptr;
        rc = ibv_post_recv(qp_address[workerId], &wr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post address RR %d\n", rc);
        return rc;
    }

    int AggContext::post_send_ready(uint32_t workerId)
    {
        int rc = 0;
        struct ibv_sge list;
        list.addr = (uint64_t)srcs_[workerId];
        list.length = 0;
        list.lkey = mrs_[workerId]->lkey;
        struct ibv_send_wr wr;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = 0;
        wr.sg_list = &list;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_SEND_WITH_IMM;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.imm_data = workerId;
        struct ibv_send_wr* bad_wr = nullptr;
        rc = ibv_post_send(qp_address[workerId], &wr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post ready %d\n", rc);
        return rc;
    }

    void *OmniAggregatorMaster(void *ctx) {
        AggContext* d_ctx_ptr = (AggContext *) ctx;

        d_ctx_ptr->ret = aggmaster(d_ctx_ptr);

        return NULL;
    }
    AggContext::AggContext() :
            num_server_threads (1) {
        threadid.store(0);
        init();
        StartMaster();
    }

    void AggContext::wait_master_ready() {
        boost::unique_lock<boost::mutex> lock(master_ready_mutex);

        while (master_ready!=num_server_threads)
            master_ready_event.wait(lock);
    }    

    void AggContext::set_master_ready() {

        boost::unique_lock<boost::mutex> lock(master_ready_mutex);

        if ((++master_ready) == num_server_threads)
            master_ready_event.notify_one();
    }

    void AggContext::StartMaster() {
        int ret = 0;
        int coreid = omnireduce_par.getAggregatorCoreId(0);
        if (coreid<0)
        {
            ret = pthread_create(&aggmasterThread, NULL, OmniAggregatorMaster, this);
        }
        else
        {
            pthread_attr_t attr;
            cpu_set_t cpus;
            pthread_attr_init(&attr);
            CPU_ZERO(&cpus);
            CPU_SET(coreid, &cpus);
            pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
            ret = pthread_create(&aggmasterThread, &attr, OmniAggregatorMaster, this);
        }
        if (ret) {
            std::cerr<<"Error starting aggregator master thread"<<std::endl;
            exit(1);
        }
        wait_master_ready();
        int ne=0;
        int worker_count = 0;
        uint32_t block_size = omnireduce_par.getBlockSize();
        uint32_t num_workers = omnireduce_par.getNumWorkers();
        uint32_t num_server_threads = omnireduce_par.getNumWorkerThreads();
        uint32_t num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        uint32_t message_size = omnireduce_par.getMessageSize();
        uint32_t direct_memory = omnireduce_par.getDirectMemory();
        uint32_t adaptive_blocksize = omnireduce_par.getAdaptiveBlockSize();
        struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
        if (direct_memory || adaptive_blocksize==1)
        {
            for (uint32_t i=0; i<num_workers; i++)
            {
                post_receive_address(i);
            }
        }
        while (!force_quit)
        {
            if (direct_memory==1 || adaptive_blocksize==1)
            {
                ne = ibv_poll_cq(cq_address, MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
                if (ne>0)
                {
                    for (int i = 0; i < ne; ++i)
                    {
                        if (wc[i].status == IBV_WC_SUCCESS)
                        {
                            if (wc[i].opcode == IBV_WC_RECV)
                            {
                                worker_count++;
                                if (worker_count==num_workers)
                                {
                                    uint32_t count = 0;
                                    for (uint32_t w=0; w<num_workers; w++)
                                    {
                                        tensor_size = srcs_[w][0];
                                        switch(srcs_[w][1])
                                        {
                                            case INT32:
                                                typecode = INT32;
                                                element_size = 4;
                                                break;
                                            case FLOAT32:
                                                typecode = FLOAT32;
                                                element_size = 4;
                                                break;
                                            default:
                                                std::cerr<<"Data type error"<<std::endl;
                                                exit(1);
                                        }
                                        omnireduce_par.setBlockSize(srcs_[w][2]);
                                        if (direct_memory==1)
                                            omnireduce_par.setMessageSize(srcs_[w][2]);
                                        block_size = omnireduce_par.getBlockSize();
                                        message_size = omnireduce_par.getMessageSize();
                                        uint32_t num_blocks_per_thread = omnireduce_par.getNumSlotsPerTh()*(message_size/block_size);
                                        omnireduce_par.setInfOffset(num_blocks_per_thread);
                                        //std::cout<<tensor_size<<" "<<typecode<<" "<<block_size<<std::endl;
                                    }
                                    count = tensor_size;
                                    uint32_t block_count = 0;
                                    uint32_t start_offset = 0;
                                    for (int t=num_server_threads-1; t>=0; t--)
                                    {
                                        uint32_t thread_count=0;
                                        block_count = count/block_size;
                                        if ((count%block_size)!=0)
                                            block_count+=1;
                                        if (t!=0)
                                        {
                                            if (block_count%num_server_threads>t)
                                                block_count = block_count/num_server_threads+1;
                                            else
                                                block_count /= num_server_threads;
                                            thread_count = block_count * block_size;                                        
                                        }
                                        else
                                        {
                                            thread_count = count - start_offset;
                                        }
                                        //std::cout<<"thread "<<t<<" start_offset "<<start_offset<<std::endl;
                                        for (uint32_t c=0; c<num_slots_per_thread; c++)
                                        {
                                            uint32_t bid = ((start_offset+c*block_size)/block_size)%num_slots_per_thread;
                                            current_offset_thread[t][bid] = start_offset+c*block_size;
                                            //std::cout<<"slot "<<bid<<" current offset "<<start_offset+c*block_size<<std::endl;
                                        }
                                        start_offset += thread_count;                               
                                    }
                                    worker_count = 0;
                                    for (uint32_t w=0; w<num_workers; w++)
                                        post_receive_address(w);
                                    for (uint32_t w=0; w<num_workers; w++)
                                        post_send_ready(w);
                                }
                            }
                        }
                        else {
                            std::cout<<"error "<<wc[i].status<<std::endl;
                        }
                    }
                }
            } //if (direct_memory)
        } //while (!force_quit)
    }
    void AggContext::StopMaster() {
        force_quit = true;
        int join_ret = pthread_join(aggmasterThread, NULL);
        if (join_ret) {
            std::cerr<<"Error joining master thread: returned"<<std::endl;
            exit(1);            
        }
        if (this->ret < 0) {
            std::cerr<<"Master thread returned"<<std::endl;
            exit(1);              
        }        
    }
    AggContext::~AggContext() {
        StopMaster();
    }
    void AggContext::init() {
        //step 1 - read and set para
        parse_parameters();
        int cycle_buffer = sysconf(_SC_PAGESIZE);
        int num_devices;
        char *dev_name = (char*)malloc(20*sizeof(char));;
        struct ibv_device **dev_list = NULL;
        struct ibv_qp_init_attr *qp_init_attr = NULL;
        struct ibv_qp_init_attr qp_address_attr;
        struct ibv_device *ib_dev = NULL;
        int ib_port = 1;
        int cq_size = 0;
        int mr_flags = 0;
        uint32_t buff_unit_size = omnireduce_par.getBuffUnitSize();
        uint32_t num_workers = omnireduce_par.getNumWorkers();
        uint32_t num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        uint32_t num_qps_per_thread = num_qps_per_aggregator_per_thread*num_workers;
        uint32_t num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        uint32_t message_size = omnireduce_par.getMessageSize();
        uint32_t num_comm_buff = omnireduce_par.getNumCommbuff();
        uint32_t direct_memory = omnireduce_par.getDirectMemory();
        uint32_t block_size = omnireduce_par.getBlockSize();
        num_server_threads = omnireduce_par.getNumWorkerThreads();
        size_t comm_buf_size = 0;
        remote_props_array = (struct remote_con_data_t *)malloc(num_workers*sizeof(struct remote_con_data_t));
        //step 2 - create resources
        /* get device names in the system */
        dev_list = ibv_get_device_list(&num_devices);
        if (!dev_list) {
            std::cerr<<"failed to get IB devices list"<<std::endl;
            exit(1);
        }
	    if (!num_devices)
	    {
	    	std::cerr<<"Found %d device(s)"<<std::endl;
	    	exit(1);
	    }
        /* search for the specific device we want to work with */
        strcpy(dev_name, omnireduce_par.getIbHca());
	    for (int i = 0; i < num_devices; i++)
	    {
	    	if (!dev_name)
	    	{
	    		dev_name = strdup(ibv_get_device_name(dev_list[i]));
	    		std::cout<<"IB device not specified, using first one found: "<<dev_name<<std::endl;
	    	}
	    	if (!strcmp(ibv_get_device_name(dev_list[i]), dev_name))
	    	{
                std::cout<<"IB device: "<<dev_name<<std::endl;
	    		ib_dev = dev_list[i];
	    		break;
	    	}
	    }
        /* if the device wasn't found in host */
	    if (!ib_dev)
	    {
	    	std::cerr<<"IB device %s wasn't found"<<std::endl;
	    	exit(1);
	    }
        /* get device handle */
        ib_ctx = ibv_open_device(ib_dev);
        if(!ib_ctx)
        {
	    	std::cerr<<"failed to open device "<<dev_name<<std::endl;
	    	exit(1);            
        }
        /* Free device list */
        ibv_free_device_list(dev_list);
        dev_list = NULL;
        ib_dev = NULL;
        /* query port properties */
        if (ibv_query_port(ib_ctx, ib_port, &port_attr))
        {
            std::cerr<<"ibv_query_port on port "<<ib_port<<" failed"<<std::endl;
            exit(1);
        }
        /* allocate Protection Domain */
        pd = ibv_alloc_pd(ib_ctx);
        if (!pd)
        {
	    	std::cerr<<"ibv_alloc_pd failed"<<std::endl;
	    	exit(1);
        }
        /* create completion queue */
        cq_size = MAX_CONCURRENT_WRITES * 2;
        cq = (struct ibv_cq **)malloc(num_server_threads*sizeof(struct ibv_cq *));	
        for (size_t i=0; i<num_server_threads; i++)
        {
            cq[i] = ibv_create_cq(ib_ctx, cq_size, NULL, NULL, 0);
            if (!cq[i])
            {
                std::cerr<<"failed to create CQ with "<<cq_size<<" entries"<<std::endl;
                exit(1);
            }
        }
        cq_address = ibv_create_cq(ib_ctx, cq_size, NULL, NULL, 0);
        /* allocate the memory worker send/recv buffer */
        current_offset_thread = (uint32_t **)malloc(sizeof(uint32_t*)*num_server_threads);
        for (size_t i=0; i<num_server_threads; i++)
        {
            current_offset_thread[i] = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        }
        if (direct_memory)
            comm_buf_size = num_slots_per_thread*block_size*num_server_threads*(num_workers+num_comm_buff);
        else
            comm_buf_size = num_slots_per_thread*(message_size*2)*num_server_threads*(num_comm_buff+num_workers)+num_slots_per_thread*message_size*num_server_threads*2;
        ret = posix_memalign(reinterpret_cast<void**>(&comm_buf), cycle_buffer, comm_buf_size*buff_unit_size);
        if (ret!=0)
        {
            std::cerr<<"failed to malloc "<<comm_buf_size*buff_unit_size<<" bytes to communication memory buffer"<<std::endl;
            exit(1);                    
        }
        memset(comm_buf, 0, comm_buf_size*buff_unit_size);
        /* register the memory buffer */
        srcs_ = (uint32_t **)malloc(sizeof(uint32_t*)*num_workers);
        mrs_ = (struct ibv_mr **)malloc(sizeof(struct ibv_mr*)*num_workers);
        for (uint32_t i=0; i<num_workers; i++)
        {
            srcs_[i] = (uint32_t *)malloc(3*sizeof(uint32_t));
            memset(srcs_[i], 0, 3*sizeof(uint32_t));
            mrs_[i] = ibv_reg_mr(pd, srcs_[i], 3*sizeof(uint32_t), IBV_ACCESS_LOCAL_WRITE);
            if (!mrs_[i]) {
                std::cerr<<"ibv_reg_mr failed with mr_flags="<<mr_flags<<std::endl;
                exit(1);
            }            
        }    
        mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
        mr = ibv_reg_mr(pd, comm_buf, comm_buf_size*buff_unit_size, mr_flags);
        if (!mr) {
            std::cerr<<"ibv_reg_mr failed with mr_flags="<<mr_flags<<std::endl;
            exit(1);
        }
        /* create queue pair */
        qp_init_attr = (struct ibv_qp_init_attr *)malloc(num_server_threads*sizeof(struct ibv_qp_init_attr));
        memset(qp_init_attr, 0, num_server_threads*sizeof(ibv_qp_init_attr));
        for (size_t i=0; i<num_server_threads; i++)
        {
            qp_init_attr[i].qp_type = IBV_QPT_RC;
            qp_init_attr[i].sq_sig_all = 1;
            qp_init_attr[i].send_cq = cq[i];
            qp_init_attr[i].recv_cq = cq[i];
            qp_init_attr[i].cap.max_send_wr = QUEUE_DEPTH_DEFAULT;
            qp_init_attr[i].cap.max_recv_wr = QUEUE_DEPTH_DEFAULT;
            qp_init_attr[i].cap.max_send_sge = 1;
            qp_init_attr[i].cap.max_recv_sge = 1;
        }
        memset(&qp_address_attr, 0, sizeof(ibv_qp_init_attr));
        qp_address_attr.qp_type = IBV_QPT_RC;
        qp_address_attr.sq_sig_all = 1;
        qp_address_attr.send_cq = cq_address;
        qp_address_attr.recv_cq = cq_address;
        qp_address_attr.cap.max_send_wr = QUEUE_DEPTH_DEFAULT;
        qp_address_attr.cap.max_recv_wr = QUEUE_DEPTH_DEFAULT;
        qp_address_attr.cap.max_send_sge = 1;
        qp_address_attr.cap.max_recv_sge = 1;

        qp = (struct ibv_qp **)malloc(num_qps_per_thread*num_server_threads*sizeof(struct ibv_qp *));
        for (size_t i=0; i<num_qps_per_thread*num_server_threads; i++)
        {
            qp[i] = ibv_create_qp(pd, &qp_init_attr[i/num_qps_per_thread]);
            if (!qp[i])
            {
                std::cerr<<"failed to create QP"<<std::endl;
                exit(1);
            }
            qp_num_revert.insert(std::make_pair(qp[i]->qp_num, i));
        }
        qp_address = (struct ibv_qp **)malloc(num_workers*sizeof(struct ibv_qp *));
        for (size_t i=0; i<num_workers; i++)
        {
            qp_address[i] = ibv_create_qp(pd, &qp_address_attr);
            if (!qp_address[i])
            {
                std::cerr<<"failed to create QP: "<< qp_address[i]<<std::endl;
                exit(1);
            }            
        }
    }// init
}
