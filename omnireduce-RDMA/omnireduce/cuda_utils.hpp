#include <cuda_runtime.h>
#include <stdio.h>
#include <stdint.h>

void compute_bitmap(float* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, float threshold);
void compute_bitmap(int* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, int threshold);