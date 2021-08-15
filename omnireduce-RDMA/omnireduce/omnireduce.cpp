#include "omnireduce/omnireduce.hpp"
#include "omnireduce/worker.hpp"
#include "omnireduce/common.hpp"
#include "omnireduce/params.hpp"
#include "omnireduce/aggregator.hpp"

namespace omnireduce {

    int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
    {
    	int rc;
    	int read_bytes = 0;
    	int total_read_bytes = 0;
    	rc = write(sock, local_data, xfer_size);
    	if (rc < xfer_size)
    		fprintf(stderr, "Failed writing data during sock_sync_data\n");
    	else
    		rc = 0;
    	while (!rc && total_read_bytes < xfer_size)
    	{
    		read_bytes = read(sock, remote_data, xfer_size);
    		if (read_bytes > 0)
    			total_read_bytes += read_bytes;
    		else
    			rc = read_bytes;
    	}
    	return rc;
    }

    int modify_qp_to_init(struct ibv_qp *qp, int ib_port)
    {
    	struct ibv_qp_attr attr;
	    int flags;
	    int rc;
	    memset(&attr, 0, sizeof(attr));
	    attr.qp_state = IBV_QPS_INIT;
	    attr.port_num = ib_port;
	    attr.pkey_index = 0;
	    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	    flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
	    rc = ibv_modify_qp(qp, &attr, flags);
	    if (rc)
	    	fprintf(stderr, "failed to modify QP state to INIT\n");
	    return rc;    
    }

    int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid, int sl, int ib_port, int gid_idx)
    {
    	struct ibv_qp_attr attr;
    	int flags;
    	int rc;
    	memset(&attr, 0, sizeof(attr));
    	attr.qp_state = IBV_QPS_RTR;
    	attr.path_mtu = IBV_MTU_1024; // default MTU is 1024
    	attr.dest_qp_num = remote_qpn;
    	attr.rq_psn = 0;
    	attr.max_dest_rd_atomic = 1;
    	attr.min_rnr_timer = 0x12;
    	attr.ah_attr.is_global = 0;
    	attr.ah_attr.dlid = dlid;
    	attr.ah_attr.sl = sl;
    	attr.ah_attr.src_path_bits = 0;
    	attr.ah_attr.port_num = ib_port;
    	if (gid_idx >= 0)
    	{
    		attr.ah_attr.is_global = 1;
    		attr.ah_attr.port_num = ib_port;
    		memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
    		attr.ah_attr.grh.flow_label = 0;
    		attr.ah_attr.grh.hop_limit = 1;
    		attr.ah_attr.grh.sgid_index = gid_idx;
    		attr.ah_attr.grh.traffic_class = 0;
    	}
    	flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
    			IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    	rc = ibv_modify_qp(qp, &attr, flags);
    	if (rc)
    		fprintf(stderr, "failed to modify QP state to RTR\n");
    	return rc;
    }

    int modify_qp_to_rts(struct ibv_qp *qp)
    {
    	struct ibv_qp_attr attr;
    	int flags;
    	int rc;
    	memset(&attr, 0, sizeof(attr));
    	attr.qp_state = IBV_QPS_RTS;
    	attr.timeout = 14;
    	attr.retry_cnt = 7;
    	attr.rnr_retry = 7;
    	attr.sq_psn = 0;
    	attr.max_rd_atomic = 1;
    	flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
    			IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    	rc = ibv_modify_qp(qp, &attr, flags);
    	if (rc)
    		fprintf(stderr, "failed to modify QP state to RTS\n");
    	return rc;
    }

    int master(OmniContext* dctx_ptr) {

        force_quit = false;

        uint32_t num_worker_threads = omnireduce_par.getNumWorkerThreads();
        uint32_t num_workers = omnireduce_par.getNumWorkers();
        uint32_t num_aggregators = omnireduce_par.getNumAggregators();
        uint32_t num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        uint32_t num_qps_per_thread = num_qps_per_aggregator_per_thread*num_aggregators;
        uint32_t direct_memory = omnireduce_par.getDirectMemory();
        int ib_port = omnireduce_par.getIbPort();
        int gid_idx = omnireduce_par.getGidIdx();
        int sl = omnireduce_par.getServiceLevel();
        int ret = 0;

        //step 1 - establish TCP connection for info exchange
        uint32_t tcp_port = omnireduce_par.getTcpPort();;
        dctx_ptr->socks = (int *)malloc(num_aggregators*sizeof(int));
        struct addrinfo *resolved_addr = NULL;
        struct addrinfo *iterator;
        int tmp;
        char service[6];
        int sockfd = -1;
	    struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (sprintf(service, "%d", tcp_port) < 0)
        {
            std::cerr<<"failed to set the tcp port"<<std::endl;
            exit(1);
        }
        for (size_t i=0; i<num_aggregators; i++)
        {
            // Resolve DNS address, use sockfd as temp storage 
            sockfd = getaddrinfo(omnireduce_par.getAggregatorIP(i), service, &hints, &resolved_addr);
            if (sockfd < 0)
            {
                fprintf(stderr, "%s for %s:%d\n", gai_strerror(sockfd), omnireduce_par.getAggregatorIP(i), tcp_port);
                exit(1);
            }
            // Search through results and find the one we want 
            for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
            {
                sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
                if (sockfd >= 0)
                {
                    // Client mode. Initiate connection to remote 
                    if ((tmp = connect(sockfd, iterator->ai_addr, iterator->ai_addrlen)))
                    {
                        fprintf(stdout, "failed connect \n");
                        close(sockfd);
                        sockfd = -1;
                        exit(1);
                    }
                }
            }
            dctx_ptr->socks[i] = sockfd;
        }

        //step 2 - QPs connection
        struct cm_con_data_t local_con_datas[num_aggregators];
        struct cm_con_data_t tmp_con_data;
        union ibv_gid my_gid;
        if (gid_idx  >= 0)
        {
            ret = ibv_query_gid(dctx_ptr->ib_ctx, ib_port, gid_idx, &my_gid);
            if (ret)
            {
                fprintf(stderr, "could not get gid for port %d, index %d\n", ib_port, gid_idx);
                exit(1);
            }
        }
        else
            memset(&my_gid, 0, sizeof my_gid);
        for (size_t i=0; i<num_aggregators; i++)
        {
            local_con_datas[i].remoteId = i;
            local_con_datas[i].num_peers = num_aggregators;
            if (direct_memory)
            {
#ifdef USE_CUDA
                local_con_datas[i].addr = htonll((uintptr_t)(dctx_ptr->cuda_comm_buf));
#else
                local_con_datas[i].addr = htonll((uintptr_t)(dctx_ptr->comm_buf));
#endif
            }
            else
            {
                local_con_datas[i].addr = htonll((uintptr_t)(dctx_ptr->comm_buf));
            }     
            local_con_datas[i].rkey = htonl(dctx_ptr->mr->rkey);
            for(size_t j=0; j<num_qps_per_thread*num_worker_threads; j++)
            {
                local_con_datas[i].qp_num[j] = htonl(dctx_ptr->qp[j]->qp_num);
            }
            local_con_datas[i].qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS] = htonl(dctx_ptr->qp_address[i]->qp_num);
            local_con_datas[i].lid = htons(dctx_ptr->port_attr.lid);
            memcpy(local_con_datas[i].gid, &my_gid, 16);
        }
        for (size_t i=0; i<num_aggregators; i++)
        {
            if (sock_sync_data(dctx_ptr->socks[i], sizeof(struct cm_con_data_t), (char *)&local_con_datas[i], (char *)&tmp_con_data) < 0)
            {
                fprintf(stderr, "failed to exchange connection data between sides\n");
                exit(1);
            }
            if (i==0)
                dctx_ptr->workerId = tmp_con_data.remoteId;
            else if (dctx_ptr->workerId != tmp_con_data.remoteId || num_workers != tmp_con_data.num_peers)
            {
                fprintf(stderr, "machine ID or number error\n");
                exit(1);
            }
            dctx_ptr->remote_props_array[i].remoteId = tmp_con_data.remoteId;
            dctx_ptr->remote_props_array[i].addr = ntohll(tmp_con_data.addr);
            dctx_ptr->remote_props_array[i].rkey = ntohl(tmp_con_data.rkey);
            for(size_t j=0; j<num_workers*num_qps_per_aggregator_per_thread*num_worker_threads; j++)
                dctx_ptr->remote_props_array[i].qp_num[j] = ntohl(tmp_con_data.qp_num[j]);
            dctx_ptr->remote_props_array[i].qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS] = ntohl(tmp_con_data.qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS]);
            dctx_ptr->remote_props_array[i].lid = ntohs(tmp_con_data.lid);
            memcpy(dctx_ptr->remote_props_array[i].gid, tmp_con_data.gid, 16);
        }
        for (uint32_t i=0; i<num_qps_per_thread*num_worker_threads; i++)
        {
            int tid = i/num_qps_per_thread;
            int mid = (i%num_qps_per_thread)%num_aggregators;
            int qid = ((i%num_qps_per_thread)/num_aggregators)*num_workers+dctx_ptr->workerId+tid*num_workers*num_qps_per_aggregator_per_thread;
            qp_num_to_peerid.insert(std::make_pair(dctx_ptr->qp[i]->qp_num, mid));
            ret = modify_qp_to_init(dctx_ptr->qp[i], ib_port);
            if (ret)
            {
                fprintf(stderr, "change QP state to INIT failed\n");
                exit(1);
            }
            ret = modify_qp_to_rtr(dctx_ptr->qp[i], dctx_ptr->remote_props_array[mid].qp_num[qid], dctx_ptr->remote_props_array[mid].lid, dctx_ptr->remote_props_array[mid].gid, sl, ib_port, gid_idx);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }
            ret = modify_qp_to_rts(dctx_ptr->qp[i]);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }
        }

        for (uint32_t i=0; i<num_aggregators; i++)
        {
            ret = modify_qp_to_init(dctx_ptr->qp_address[i], ib_port);
            if (ret)
            {
                fprintf(stderr, "change QP state to INIT failed\n");
                exit(1);
            }
            ret = modify_qp_to_rtr(dctx_ptr->qp_address[i], dctx_ptr->remote_props_array[i].qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS], 
                            dctx_ptr->remote_props_array[i].lid, dctx_ptr->remote_props_array[i].gid, sl, ib_port, gid_idx);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }
            ret = modify_qp_to_rts(dctx_ptr->qp_address[i]);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }            
        }

        //step 3 - Sync
        char temp_char;
	    for (uint32_t i=0; i<num_aggregators; i++)
	    {
	        /* sync to make sure that both sides are in states that they can connect to prevent packet loose */
	        if (sock_sync_data(dctx_ptr->socks[i], 1, (char *)"Q", &temp_char)) /* just send a dummy char back and forth */
	        {
	    	    fprintf(stderr, "sync error after QPs are were moved to RTS\n");
                exit(1);
	        }
	    }

        //create slave threads
        uint32_t num_slave_threads=num_worker_threads-1;
        pthread_t *slaveThreads = (pthread_t *)malloc(num_slave_threads*sizeof(pthread_t));
        pthread_attr_t attr;
        cpu_set_t cpus;
        pthread_attr_init(&attr);
        for (uint32_t i=0; i<num_slave_threads; i++) 
        {
            int coreid = omnireduce_par.getWorkerCoreId(i+1);
            if (direct_memory)
            {
                if (coreid<0)
                {
                    ret = pthread_create(&(slaveThreads[i]), NULL, dr_worker, dctx_ptr);
                }
                else
                {
                    CPU_ZERO(&cpus);
                    CPU_SET(coreid, &cpus);
                    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
                    ret = pthread_create(&(slaveThreads[i]), &attr, dr_worker, dctx_ptr);
                }
                if (ret) {
                    std::cerr<<"Error starting slave thread "<<ret<<std::endl;
                    exit(1);
                }
            }
            else
            {
                if (coreid<0)
                {
                    ret = pthread_create(&(slaveThreads[i]), NULL, worker, dctx_ptr);
                }
                else
                {
                    CPU_ZERO(&cpus);
                    CPU_SET(coreid, &cpus);
                    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
                    ret = pthread_create(&(slaveThreads[i]), &attr, worker, dctx_ptr);
                }
                if (ret) {
                    std::cerr<<"Error starting slave thread"<<std::endl;
                    exit(1);
                }
            }
        }
        
        //start worker on master
        if (direct_memory)
            (*dr_worker)(dctx_ptr);
        else
            (*worker)(dctx_ptr);
        //join slave threads
        for (uint32_t i=0; i<num_slave_threads; i++)
            pthread_join(slaveThreads[i], NULL);

        return 0;
    }

    int aggmaster(AggContext *dctx_ptr)
    {
        force_quit = false;

        uint32_t num_server_threads = omnireduce_par.getNumWorkerThreads();
        uint32_t num_workers = omnireduce_par.getNumWorkers();
        uint32_t num_aggregators = omnireduce_par.getNumAggregators();
        uint32_t num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        uint32_t num_qps_per_thread = num_qps_per_aggregator_per_thread*num_workers;
        uint32_t direct_memory = omnireduce_par.getDirectMemory();
        int ib_port = omnireduce_par.getIbPort();
        int gid_idx = omnireduce_par.getGidIdx();
        int sl = omnireduce_par.getServiceLevel();
        int ret = 0;
        //step 1 - establish TCP connection for info exchange
        uint32_t tcp_port = omnireduce_par.getTcpPort();
        dctx_ptr->socks = (int *)malloc(num_workers*sizeof(int));
        struct addrinfo *resolved_addr = NULL;
        struct addrinfo *iterator;
        char service[6];
        int sockfd = -1;
        int listenfd = 0;
        int c;
        struct sockaddr_in client;
        char ipAddr[INET_ADDRSTRLEN];
	    struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (sprintf(service, "%d", tcp_port) < 0)
        {
            std::cerr<<"failed to set the tcp port"<<std::endl;
            exit(1);
        }
        /* Resolve DNS address, use sockfd as temp storage */
        sockfd = getaddrinfo(NULL, service, &hints, &resolved_addr);
        if (sockfd < 0)
        {
            fprintf(stderr, "%s for NULL:%d\n", gai_strerror(sockfd), tcp_port);
            exit(1);
        }
        /* Search through results and find the one we want */
        for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
        {
            sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
            if (sockfd >= 0)
            {
                /* Server mode. Set up listening socket an accept a connection */
                listenfd = sockfd;
                sockfd = -1;
                if (bind(listenfd, iterator->ai_addr, iterator->ai_addrlen))
                {
                    fprintf(stderr, "socket bind error\n");
                    exit(1);
                }
                listen(listenfd, 1);
                c = sizeof(struct sockaddr_in);
                uint32_t connect_num = 0;
                while (connect_num<num_workers)
                {
                    sockfd = accept(listenfd, (struct sockaddr *)&client, (socklen_t*)&c);
                    for (uint32_t i=0; i<num_workers; i++)
                    {
                        const char* temp = inet_ntop(AF_INET, &client.sin_addr, ipAddr, sizeof(ipAddr));
                        if (strcmp(omnireduce_par.getWorkerIP(i), temp)==0)
                        {
                            printf("connected peer address = %s; index = %d\n", temp, i);
                            dctx_ptr->socks[i] = sockfd;
                        }
                    }
                    connect_num++;
                }
            }
        }
        //step 2 - QPs connection
        struct cm_con_data_t local_con_datas[num_workers];
        struct cm_con_data_t tmp_con_data;
        union ibv_gid my_gid;
        if (gid_idx  >= 0)
        {
            ret = ibv_query_gid(dctx_ptr->ib_ctx, ib_port, gid_idx, &my_gid);
            if (ret)
            {
                fprintf(stderr, "could not get gid for port %d, index %d\n", ib_port, gid_idx);
                exit(1);
            }
        }
        else
            memset(&my_gid, 0, sizeof my_gid);
        for (size_t i=0; i<num_workers; i++)
        {
            local_con_datas[i].remoteId = i;
            local_con_datas[i].num_peers = num_workers;
            local_con_datas[i].addr = htonll((uintptr_t)(dctx_ptr->comm_buf));
            local_con_datas[i].rkey = htonl(dctx_ptr->mr->rkey);
            for(size_t j=0; j<num_qps_per_thread*num_server_threads; j++)
            {
                local_con_datas[i].qp_num[j] = htonl(dctx_ptr->qp[j]->qp_num);
            }
            local_con_datas[i].qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS] = htonl(dctx_ptr->qp_address[i]->qp_num);
            local_con_datas[i].lid = htons(dctx_ptr->port_attr.lid);
            memcpy(local_con_datas[i].gid, &my_gid, 16);
        }
        for (size_t i=0; i<num_workers; i++)
        {
            if (sock_sync_data(dctx_ptr->socks[i], sizeof(struct cm_con_data_t), (char *)&local_con_datas[i], (char *)&tmp_con_data) < 0)
            {
                fprintf(stderr, "failed to exchange connection data between sides\n");
                exit(1);
            }
            if (i==0)
                dctx_ptr->serverId = tmp_con_data.remoteId;
            else if (dctx_ptr->serverId != tmp_con_data.remoteId || num_aggregators != tmp_con_data.num_peers)
            {
                fprintf(stderr, "machine ID or number error\n");
                exit(1);
            }
            dctx_ptr->remote_props_array[i].remoteId = tmp_con_data.remoteId;
            dctx_ptr->remote_props_array[i].addr = ntohll(tmp_con_data.addr);
            dctx_ptr->remote_props_array[i].rkey = ntohl(tmp_con_data.rkey);
            for(size_t j=0; j<num_aggregators*num_qps_per_aggregator_per_thread*num_server_threads; j++)
                dctx_ptr->remote_props_array[i].qp_num[j] = ntohl(tmp_con_data.qp_num[j]);
            dctx_ptr->remote_props_array[i].qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS] = ntohl(tmp_con_data.qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS]);
            dctx_ptr->remote_props_array[i].lid = ntohs(tmp_con_data.lid);
            memcpy(dctx_ptr->remote_props_array[i].gid, tmp_con_data.gid, 16);                    
        }
        for (uint32_t i=0; i<num_qps_per_thread*num_server_threads; i++)
        {
            int tid = i/num_qps_per_thread;
            int mid = (i%num_qps_per_thread)%num_workers;
            int qid = ((i%num_qps_per_thread)/num_workers)*num_aggregators+dctx_ptr->serverId+tid*num_aggregators*num_qps_per_aggregator_per_thread;
            qp_num_to_peerid.insert(std::make_pair(dctx_ptr->qp[i]->qp_num, mid));
            ret = modify_qp_to_init(dctx_ptr->qp[i], ib_port);
            if (ret)
            {
                fprintf(stderr, "change QP state to INIT failed\n");
                exit(1);
            }
            ret = modify_qp_to_rtr(dctx_ptr->qp[i], dctx_ptr->remote_props_array[mid].qp_num[qid], dctx_ptr->remote_props_array[mid].lid, dctx_ptr->remote_props_array[mid].gid, sl, ib_port, gid_idx);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }
            ret = modify_qp_to_rts(dctx_ptr->qp[i]);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }
        }
        for (uint32_t i=0; i<num_workers; i++)
        {
            ret = modify_qp_to_init(dctx_ptr->qp_address[i], ib_port);
            if (ret)
            {
                fprintf(stderr, "change QP state to INIT failed\n");
                exit(1);
            }
            ret = modify_qp_to_rtr(dctx_ptr->qp_address[i], dctx_ptr->remote_props_array[i].qp_num[MAX_NUM_AGGS*MAX_NUM_QPS*MAX_NUM_THREADS], 
                            dctx_ptr->remote_props_array[i].lid, dctx_ptr->remote_props_array[i].gid, sl, ib_port, gid_idx);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }
            ret = modify_qp_to_rts(dctx_ptr->qp_address[i]);
            if (ret)
            {
                fprintf(stderr, "failed to modify QP state to RTR\n");
                exit(1);
            }            
        }
        
        //step 3 - Sync
        char temp_char;
	    for (uint32_t i=0; i<num_workers; i++)
	    {
	        /* sync to make sure that both sides are in states that they can connect to prevent packet loose */
	        if (sock_sync_data(dctx_ptr->socks[i], 1, (char *)"Q", &temp_char)) /* just send a dummy char back and forth */
	        {
	    	    fprintf(stderr, "sync error after QPs are were moved to RTS\n");
                exit(1);
	        }
	    }

        //create slave threads
        uint32_t num_slave_threads=num_server_threads-1;
        pthread_t *slaveThreads = (pthread_t *)malloc(num_slave_threads*sizeof(pthread_t));
        pthread_attr_t attr;
        cpu_set_t cpus;
        pthread_attr_init(&attr);
        for (uint32_t i=0; i<num_slave_threads; i++) 
        {
            int coreid = omnireduce_par.getAggregatorCoreId(i+1);
            if (direct_memory)
            {
                if (coreid<0)
                {
                    ret = pthread_create(&(slaveThreads[i]), NULL, dr_aggregator, dctx_ptr);
                }
                else
                {
                    CPU_ZERO(&cpus);
                    CPU_SET(coreid, &cpus);
                    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
                    ret = pthread_create(&(slaveThreads[i]), &attr, dr_aggregator, dctx_ptr);
                }
                if (ret) {
                    std::cerr<<"Error starting slave thread"<<std::endl;
                    exit(1);
                }
            }
            else
            {
                if (coreid<0)
                {
                    ret = pthread_create(&(slaveThreads[i]), NULL, aggregator, dctx_ptr);
                }
                else
                {
                    CPU_ZERO(&cpus);
                    CPU_SET(coreid, &cpus);
                    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
                    ret = pthread_create(&(slaveThreads[i]), &attr, aggregator, dctx_ptr);
                }
                if (ret) {
                    std::cerr<<"Error starting slave thread"<<std::endl;
                    exit(1);                    
                }
            }
        }
        //start aggregator on master
        if (direct_memory)
            (*dr_aggregator)(dctx_ptr);
        else
            (*aggregator)(dctx_ptr);
        //join slave threads
        for (uint32_t i=0; i<num_slave_threads; i++)
            pthread_join(slaveThreads[i], NULL);
            
        return 0;
    }

}
