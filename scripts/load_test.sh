#!/bin/bash
set -e

echo ""
echo "=============================="
echo "📊  RUNNING LOAD TEST SUITE"
echo "=============================="
echo ""

mkdir -p ../results

# Example workload levels
LOADS=(10 20 30 40 50)

ENDPOINT=$1  # /create or /compute or /read

if [ -z "$ENDPOINT" ]; then
  echo "Usage: ./load_test.sh /endpoint"
  exit 1
fi

for L in "${LOADS[@]}"
do
  echo "▶ Running loadgen with $L clients..."
  ../loadgen/loadgen $L 200 $ENDPOINT
done

echo ""
echo "📁 Results stored in results/loadtest.csv"
