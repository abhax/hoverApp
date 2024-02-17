#!/bin/bash
# This script will copy the built binary to the remote device for debugging. 
scp  ../build/hoverApp root@$1:/storage/ 
plink root@$1 -m startRemoteGDBServer.sh -v