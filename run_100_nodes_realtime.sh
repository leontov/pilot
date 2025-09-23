#!/bin/bash

# Stop all previously running kolibri_node_v1 processes
pkill -f kolibri_node_v1
sleep 0.5

# Create logs directory if it doesn't exist
mkdir -p logs

# Launch 100 nodes
for digit in {0..99}; do
    port=$((9000 + digit))
    echo "Launching node $digit on port $port"
    ./bin/kolibri_node_v1 --id node$digit --port $port --digit $digit > logs/node_$digit.log 2>&1 &
    sleep 0.1
    echo "Node $digit launched, log: logs/node_$digit.log"
done

echo "✅ All available nodes launched"
