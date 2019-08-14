if [ ! -d "./build" ]; then
    mkdir build
fi
cd build
rm -r *
cmake -DUSE_LLVM=/usr/local/opt/llvm/bin/llvm-config -DUSE_SORT=ON ..
make -j`sysctl -n hw.ncpu`
cd ..

