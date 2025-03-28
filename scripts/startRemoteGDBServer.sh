#!/bin/sh
# This script will be run on the remote device to start the GDB server.

# Kill any existing gdbserver instances
killall gdbserver

# Start gdbserver on the specified port and the hoverApp binary
gdbserver localhost:3000 /storage/hoverApp > /dev/null 2>&1 &