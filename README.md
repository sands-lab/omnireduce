# OmniReduce
OmniReduce is an efficient sparse collective communication library. It maximizes effective bandwidth use by exploiting the sparsity of data.

For clusters without RDMA support, OmniReduce uses Intel DPDK for kernel bypass. GPUDirect can also be used where available.

## Contents
- omnireduce-DPDK: source code of DPDK-based OmniReduce
- omnireduce-RDMA: source code of RDMA-based OmniReduce
- [experiments](https://github.com/sands-lab/omnireduce-experiments): micro-benchmark and end-to-end scripts

## Publications

[OmniReduce](https://sands.kaust.edu.sa/project/omnireduce/) accepted at SIGCOMM’21.
