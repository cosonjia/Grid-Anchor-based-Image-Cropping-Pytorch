#!/usr/bin/env bash

# CUDA_PATH=/usr/local/cuda/
export CUDA_HOME=/usr/local/cuda-11.6

cd src
echo "Compiling my_lib kernels by nvcc..."
nvcc -c -o rod_align_kernel.cu.o rod_align_kernel.cu -x cu -Xcompiler -fPIC -arch=sm_52