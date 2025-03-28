#!/bin/bash
# This script prepares everything for debugging

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
APP_DIR="$(dirname "$SCRIPT_DIR")"
APP_PATH="$APP_DIR/build/hoverApp"
TARGET_IP="192.168.4.52"

echo "Preparing for debugging..."

# Stop any running gdbserver
echo "Stopping any running gdbserver on target device..."
ssh root@$TARGET_IP 'killall gdbserver >/dev/null 2>&1'
sleep 1  # Give it time to fully stop

# Copy the binary
echo "Copying binary from $APP_PATH to target device..."
scp "$APP_PATH" root@$TARGET_IP:/storage/hoverApp 

# Start gdbserver
echo "Starting gdbserver on target device..."
ssh root@$TARGET_IP 'gdbserver localhost:3000 /storage/hoverApp >/dev/null 2>&1 &'
echo "GDB server running on $TARGET_IP:3000"

# Verify gdbserver is running
echo "Verifying gdbserver is running..."
GDB_RUNNING=$(ssh root@$TARGET_IP 'ps | grep gdbserver | grep -v grep')
if [ -n "$GDB_RUNNING" ]; then
  echo "GDB server process found on target."
else
  echo "WARNING: GDB server process NOT found on target. Trying to start it again..."
  ssh root@$TARGET_IP 'gdbserver localhost:3000 /storage/hoverApp >/dev/null 2>&1 &'
fi

echo "-----------------------------------------"
echo "Everything is prepared for debugging."
echo "Now you can use VS Code debugging (F5)"
echo "-----------------------------------------" 