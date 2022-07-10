#include <stdio.h>
#include <assert.h>
#include "cuda_runtime.h"
#include "device_launch_parameters.h"

inline cudaError_t checkCuda(cudaError_t result)
{
    if (result != cudaSuccess) {
        fprintf(stderr, "CUDA Runtime Error: %s\n", cudaGetErrorString(result));
        assert(result == cudaSuccess);
    }
    return result;
}
