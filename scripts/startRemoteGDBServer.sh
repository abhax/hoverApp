#!/bin/sh
# This script will be run on the remote device to basically start the GDB server. 
killall gdbserver
gdbserver localhost:3000 /storage/hoverApp > /dev/null 2>&1 &