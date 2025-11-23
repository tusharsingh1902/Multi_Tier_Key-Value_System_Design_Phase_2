#!/bin/bash
set -e
echo "Building server..."
g++ server/server.cpp -o server/bin/server -std=c++17 -lpqxx -lpq -pthread -I/usr/include
echo "Building loadgen..."
g++ loadgen/loadgen.cpp -o server/bin/loadgen -std=c++17 -lcurl -pthread
mkdir -p results
echo "Build complete."
