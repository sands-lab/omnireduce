# OmniReduce Examples
## Building
MPI compiler (MPICH/OpenMPI) is required to build the example. 
To build example, run:
``` shell
make USE_CUDA=ON
```
After building, the output programs include `worker`, `cuda_worker` and `aggregator`.
## Run example
### 1. Configuration file
Before running the example, the [omnireduce.cfg](https://github.com/sands-lab/omnireduce/blob/master/omnireduce-RDMA/example/omnireduce.cfg) requires to be edited according to the cluster. This file needs to be copied to all the workers and aggregators.
Below, we introduce the parameters in the configuration file.
- **RDMA configuration**
	- **`ib_hca`** specify which RDMA interfaces to use for communication. Example：mlx5_1.
	- **`ib_port`**: specify the port number of the RDMA interface.
	- **`gid_idx`**: specify GID index.
	- **`sl`**: set the service level.
	- **`num_threads`**: number of threads used for communication for both workers and aggregators.
	- **`worker_cores`** and **`aggregator_cores`**: set CPU affinity for threads. The number of values should be equal to the `num_threads` parameter. value -1 means no CPU affinity setting and values $\geq$ 0 mean the core ids for different threads.
- **Worker configuration**
	- **`num_workers`**: number of workers.
	- **`threshold`**: threshold for calculating block bitmap.
	- **`direct_memory`**: enable GPUDirect. Value 1 means using GDR.
	- **`buffer_size`**: send/recv buffer size (only used when `direct_memory`=1). 
	- **`message_size`**: RDMA message size.
	- **`block_size`**: block size used in OmniReduce algorithm.
	- **`gpu_devId`**: index of the used GPU.
	- **`worker_ips`**: IP addresses of workers, used for negotiation.
- **Aggregator configuration**
	- **`num_aggregators`**: number of workers.
	- **`aggregator_ips`**: IP addresses of aggregators, used for negotiation.
	
`bitmap_chunk_size` and `adaptive_blocksize` are not used in current version.

### 2. Run aggregators
For each aggregator, copy program to each aggregator and run:

    ./aggregator

### 3. Run workers
Run `worker` program with `mpirun` on one worker. Here is an example with MPICH.

    mpirun -n num_workers -hosts IP_1,...,IP_n ./cuda_worker
