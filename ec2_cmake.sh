if [ ! -d "./build" ]; then
    mkdir build
fi
cd build
rm CMakeCache.txt
cmake -DUSE_LLVM=/usr/bin/llvm-config-6.0 -DUSE_CUDA=ON ..
make -j `nproc --all`
