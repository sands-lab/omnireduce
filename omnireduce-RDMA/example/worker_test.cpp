#include "omnireduce/context.hpp"
#include <unistd.h>
#include <iostream>
#include "mpi.h"
#define DATA_TYPE float
//#define DATA_TYPE int

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    int myrank=0, worldsize=1;
    MPI_Comm_size(MPI_COMM_WORLD, &worldsize);
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    omnireduce::OmniContext& omniContext = omnireduce::OmniContext::getInstance();
    srand(omniContext.workerId+1);
    uint32_t block_size = omnireduce::omnireduce_par.getBlockSize();
    uint32_t tensor_size = 67108864;
    uint32_t block_count = tensor_size/block_size;
    if (tensor_size%block_size!=0)
        block_count += 1;
    DATA_TYPE *input = (DATA_TYPE *)malloc(tensor_size*sizeof(DATA_TYPE));
    DATA_TYPE *output = (DATA_TYPE *)malloc(tensor_size*sizeof(DATA_TYPE));
    DATA_TYPE *data = (DATA_TYPE *)malloc(tensor_size*sizeof(DATA_TYPE));
    memset(input, 0, tensor_size*sizeof(int));
    uint8_t *bitmap = (uint8_t *)malloc(block_count*sizeof(uint8_t));
    double density_ratio = 0.01;
    double rnum = 0;
    for(uint32_t i=0; i<block_count; i++)
    {
        rnum = rand()%100/(double)101;
        if (rnum < density_ratio && omniContext.workerId!=-1)
        {
            bitmap[i] = 0;
        }
        else
        {
            bitmap[i] = 1;
        }
        if (bitmap[i]==0)
        {
            for(uint32_t j=0; j<block_size; j++)
            {
                if(i*block_size+j<tensor_size)
                    input[i*block_size+j] = 1;
            }
        }
    }
    MPI_Allreduce(input, output, tensor_size, MPI_FLOAT, MPI_SUM, MPI_COMM_WORLD);
    //MPI_Allreduce(input, output, tensor_size, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    int round = 0;
    int warmups = 10;
    int num_rounds = 100;
    struct timeval cur_time;
    unsigned long start_time_usec;
    unsigned long diff_time_usec;
    while(round<warmups) {
        memcpy(data, input, sizeof(DATA_TYPE)*tensor_size);
        omniContext.AllReduce(data, tensor_size, bitmap, block_count);
        round++;
    }
    round = 0;
    while (round<num_rounds) {
        if(myrank==0)
            std::cout<<"round: "<<round<<std::endl;
        memcpy(data, input, sizeof(DATA_TYPE)*tensor_size);
        MPI_Barrier(MPI_COMM_WORLD);
        gettimeofday(&cur_time, NULL);
        start_time_usec = (cur_time.tv_sec * 1000000) + (cur_time.tv_usec);
        omniContext.AllReduce(data, tensor_size, bitmap, block_count);
        gettimeofday(&cur_time, NULL);
        diff_time_usec = (cur_time.tv_sec * 1000000) + (cur_time.tv_usec) - start_time_usec;
        if(myrank==0)
            std::cout<<"tensor size:"<<tensor_size*4<<" Bytes; time: "<<diff_time_usec<<" us; alg bw: "<<tensor_size*4*1.0/(1024*1024*1024)/((double)diff_time_usec/1000000)<<" GB/s"<<std::endl;
        round++;
        for(uint32_t i=0; i<tensor_size; i++)
            if(data[i]!=output[i])
            {
                std::cout<<"result check: error "<<myrank<<std::endl;
                std::cout<<i<<": "<<data[i]<<" "<<output[i]<<std::endl;
                break;
            }
        std::cout<<"result check: ok"<<std::endl;
    }
    for(uint32_t i=0; i<tensor_size; i++)
        if(data[i]!=output[i])
        {
            std::cout<<"result check: error"<<std::endl;
            std::cout<<i<<": "<<data[i]<<" "<<output[i]<<std::endl;
            return 0;
        }
    std::cout<<"result check: ok"<<std::endl;
    return 0;
}
