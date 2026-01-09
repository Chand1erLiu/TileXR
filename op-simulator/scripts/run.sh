rm -rf ./build
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j32 || {
    ret=$?
    echo "Failed to build."
    exit ${ret}
}

echo "Success to build."
cd ..