# OmniReduce-RDMA

## Getting Started
The simplest way to start is to use our [docker image](https://github.com/sands-lab/omnireduce/tree/master/omnireduce-RDMA/docker). We provide a [tutorial](https://github.com/sands-lab/omnireduce/blob/master/omnireduce-RDMA/docs/tutorial.md) to help you run RDMA-based OmniReduce with docker image quickly.
Below, we introduce how to build and use OmniReduce.

### Building
OmniReduce is built to run on Linux and the dependencies include CUDA, ibverbs and Boost C++ library.
To build OmniReduce, run:

    git clone https://github.com/sands-lab/omnireduce
    cd omnireduce-RDMA
    make USE_CUDA=ON

### Examples
Basic examples are provided under the [example](https://github.com/sands-lab/omnireduce/tree/master/omnireduce-RDMA/example) folder. 
To reproduce the evaluation in our SIGCOMM'21 paper, find the code at this [repo](https://github.com/sands-lab/omnireduce-experiments).

## Frameworks Integration
OmniReduce is only integrated with PyTorch currently. The integration method is under the [frameworks_integration](https://github.com/sands-lab/omnireduce/tree/master/omnireduce-RDMA/frameworks_integration/pytorch_patch) folder.

## Limitations

- Only support AllReduce operation
- Only support int32 and float data type
