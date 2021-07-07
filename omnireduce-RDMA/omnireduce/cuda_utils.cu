#include "cuda_utils.hpp"

template <typename scalar_t>
__global__ void bitmap_cuda_kernel(scalar_t* input, uint8_t* bitmap, int64_t len, scalar_t threshold) {
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    __shared__ bool zero_block;
    if (threadIdx.x == 0) zero_block = true;
    __syncthreads();
    if(index < len) {
      if(std::abs(input[index]) > threshold) zero_block=false;
    }
    __syncthreads();
    if(index < len) {
      if(zero_block) {
        input[index]=0.0;
        bitmap[blockIdx.x]=1;
      }
      else {
        bitmap[blockIdx.x]=0;
      }
    }
    __syncthreads();
}

void compute_bitmap(float* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, float threshold) {
    uint32_t block_num = tensor_size/block_size;
    if (tensor_size%block_size!=0)
        block_num += 1;
    bitmap_cuda_kernel<<<block_num, block_size, 0, stream>>>(d_tensor, d_bitmap, tensor_size, threshold);
}

void compute_bitmap(int* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, int threshold) {
  uint32_t block_num = tensor_size/block_size;
  if (tensor_size%block_size!=0)
      block_num += 1;
  bitmap_cuda_kernel<<<block_num, block_size, 0, stream>>>(d_tensor, d_bitmap, tensor_size, threshold);
}