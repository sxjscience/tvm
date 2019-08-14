if [ ! -d "./build" ]; then
    mkdir build
fi
cd build
rm -r *
cmake -DUSE_LLVM=/usr/bin/llvm-config-6.0 -DUSE_CUDA=ON ..
