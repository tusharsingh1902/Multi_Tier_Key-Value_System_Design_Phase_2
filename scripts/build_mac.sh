#!/bin/bash
set -e

echo "🔧 Building server on macOS..."

SCRIPT_DIR="$(dirname "$0")"
PROJECT_ROOT="$SCRIPT_DIR/.."

SRC="$PROJECT_ROOT/server/server.cpp"
OUT="$PROJECT_ROOT/server/server"

BREW_PREFIX=$(brew --prefix)

INCLUDE_FLAGS="-I$BREW_PREFIX/include -I$BREW_PREFIX/opt/libpq/include -I$BREW_PREFIX/opt/libpqxx/include"
LIB_FLAGS="-L$BREW_PREFIX/lib -L$BREW_PREFIX/opt/libpq/lib -L$BREW_PREFIX/opt/libpqxx/lib -lpqxx -lpq -pthread"

echo "Using include paths:"
echo "  $BREW_PREFIX/include"
echo "  $BREW_PREFIX/opt/libpq/include"
echo "  $BREW_PREFIX/opt/libpqxx/include"

echo "Using library paths:"
echo "  $BREW_PREFIX/lib"
echo "  $BREW_PREFIX/opt/libpq/lib"
echo "  $BREW_PREFIX/opt/libpqxx/lib"

clang++ -std=c++17 $SRC -o $OUT $INCLUDE_FLAGS $LIB_FLAGS

echo "✅ Build complete → server/server"
