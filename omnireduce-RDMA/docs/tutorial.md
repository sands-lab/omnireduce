# Tutorial
In this tutorial, we will introduce how to use OmniReduce. We use the docker image to ensure that you don't encounter problems with the system environment. We take the benchmark in [this repo](https://github.com/sands-lab/omnireduce-experiments) as an example to introduce how to use OmniReduce step by step.

## Build Image
Build the docker image according to [this](https://github.com/sands-lab/omnireduce/tree/master/omnireduce-RDMA/docker).

## Distributed Training (RDMA)
Let's say you have two workers and two aggregators. Each worker has one GPU. Assume that the network interface to use is `eth0` and the IP addresses are as follows:

| Machine | IP address |
|--|--|
| worker-0 | 10.0.0.10 |
| worker-1 | 10.0.0.11 |
| aggregator-0 | 10.0.0.20 |
| aggregator-1 | 10.0.0.21 |

### Create configuration file
Firstly, you need to create the `omnireduce.cfg` according to [this](https://github.com/sands-lab/omnireduce/tree/master/omnireduce-RDMA/example#1-configuration-file). The following parameters need to be updated:

| Parameter | Value |
|--|--|
| `num_worker` | 2 |
| `num_aggregator` | 2 |
| `worker_ips` | 10.0.0.10,10.0.0.11 |
| `worker_ips` | 10.0.0.20,10.0.0.21 |

If your worker supports GPUDirect, set `direct_memory` to be 1. With regard to RDMA configuration, you need to update related parameters according to you system information. You can use the MLNX OFED's [show_gids](https://community.mellanox.com/s/article/understanding-show-gids-script) script to get the device (`ib_hca`), port(`ib_port`) and index GID(`gid_idx`).

### Run benchmark

For aggregator-0 and aggregator-1:

    docker run -it --net=host --cap-add=IPC_LOCK --device=/dev/infiniband/uverbs1 omnireduce/pytorch:exps /bin/bash
    # now you are in docker environment
    # step 1: update /usr/local/omnireduce/example/omnireduce.cfg
    # step 2: start aggregator
    cd /usr/local/omnireduce/example
    ./aggregator

For worker-0

    docker run -it --gpus all --net=host --cap-add=IPC_LOCK --device=/dev/infiniband/uverbs1 omnireduce/pytorch:exps /bin/bash
    # now you are in docker environment
    # step 1: update /usr/local/omnireduce/example/omnireduce.cfg
    # step 2: start worker 0
    cd /home/exps/benchmark
    CUDA_VISIBLE_DEVICES=0 GLOO_SOCKET_IFNAME=eth0 python benchmark.py  -d 1.0 --backend gloo -t 26214400 -r 0 -s 2 --ip 10.0.0.10

For worker-1

    docker run -it --gpus all --net=host --cap-add=IPC_LOCK --device=/dev/infiniband/uverbs1 omnireduce/pytorch:exps /bin/bash
    # now you are in docker environment
    # step 1: update /usr/local/omnireduce/example/omnireduce.cfg
    # step 2: start worker 0
    cd /home/exps/benchmark
    CUDA_VISIBLE_DEVICES=0 GLOO_SOCKET_IFNAME=eth0 python benchmark.py  -d 1.0 --backend gloo -t 26214400 -r 1 -s 2 --ip 10.0.0.10

### Run end-to-end
To run the end-to-end experiments, please refer to [this](https://github.com/sands-lab/omnireduce-experiments/tree/master/models).  Here we take LSTM training as an example.

#### LSTM training

For aggregator-0 and aggregator-1:

    docker run -it --net=host --cap-add=IPC_LOCK --device=/dev/infiniband/uverbs1 omnireduce/pytorch:exps /bin/bash
    # now you are in docker environment
    # step 1: update /usr/local/omnireduce/example/omnireduce.cfg
    # step 2: start aggregator
    cd /usr/local/omnireduce/example
    ./aggregator

For worker-0

    docker run -it --gpus all --net=host --cap-add=IPC_LOCK --device=/dev/infiniband/uverbs1 omnireduce/pytorch:exps /bin/bash
    # now you are in docker environment
    # step 1: update /usr/local/omnireduce/example/omnireduce.cfg
    # step 2: start worker 0
    cd /home/exps/models/LSTM
    CUDA_VISIBLE_DEVICES=0 GLOO_SOCKET_IFNAME=eth0 OMPI_COMM_WORLD_SIZE=2 OMPI_COMM_WORLD_RANK=0 OMPI_COMM_WORLD_LOCAL_RANK=0 ./run.sh --init tcp://10.0.0.10:4000 --backend gloo

For worker-1

    docker run -it --gpus all --net=host --cap-add=IPC_LOCK --device=/dev/infiniband/uverbs1 omnireduce/pytorch:exps /bin/bash
    # now you are in docker environment
    # step 1: update /usr/local/omnireduce/example/omnireduce.cfg
    # step 2: start worker 0
    cd /home/exps/models/LSTM
    CUDA_VISIBLE_DEVICES=0 GLOO_SOCKET_IFNAME=eth0 OMPI_COMM_WORLD_SIZE=2 OMPI_COMM_WORLD_RANK=1 OMPI_COMM_WORLD_LOCAL_RANK=0 ./run.sh --init tcp://10.0.0.10:4000 --backend gloo
