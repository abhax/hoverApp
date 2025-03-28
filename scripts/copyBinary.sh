#!/bin/bash
# This script will copy the built binary to the remote device for debugging. 
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
APP_DIR="$(dirname "$SCRIPT_DIR")"
APP_PATH="$APP_DIR/build/hoverApp"

echo "Stopping any running gdbserver on target device..."
ssh root@$1 'killall gdbserver >/dev/null 2>&1'
sleep 1  # Give it time to fully stop

echo "Copying binary from $APP_PATH to target device..."
scp "$APP_PATH" root@$1:/storage/hoverApp 

echo "Starting gdbserver on target device..."
ssh root@$1 'gdbserver localhost:3000 /storage/hoverApp >/dev/null 2>&1 &'
echo "GDB server should be running on $1:3000"