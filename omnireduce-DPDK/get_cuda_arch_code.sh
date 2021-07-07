#!/bin/bash

# create a 'here document' that is code we compile and use to probe the card
cat << EOF > /tmp/cudaComputeVersion.cu
#include <stdio.h>
int main()
{
cudaDeviceProp prop;
cudaGetDeviceProperties(&prop,0);
printf("%d.%d\n", prop.major,prop.minor);
}
EOF

# probe the card and cleanup
/usr/local/cuda/bin/nvcc /tmp/cudaComputeVersion.cu -o /tmp/cudaComputeVersion
/tmp/cudaComputeVersion
rm /tmp/cudaComputeVersion.cu
rm /tmp/cudaComputeVersion
