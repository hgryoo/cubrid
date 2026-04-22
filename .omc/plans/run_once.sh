#!/bin/bash
export CUBRID=/home/hgryoo/cubrid/install.out
export CUBRID_DATABASES=$CUBRID/databases
export LD_LIBRARY_PATH=$CUBRID/lib
export PATH=$CUBRID/bin:$PATH

pkill -9 -f cub_server 2>/dev/null
pkill -9 -f cub_master 2>/dev/null
sleep 2
rm -f $CUBRID/log/server/perfboot_*
taskset -c 0 cubrid server start perfboot > /tmp/perf_start.log 2>&1 &
START_PID=$!
sleep 10
echo "--- procs (should see cub_server and cub_master) ---"
ps aux | grep -E "cub_(server|master)" | grep -v grep
echo "--- err log tail ---"
tail -60 $CUBRID/log/server/perfboot_latest.err
echo "--- stopping ---"
cubrid server stop perfboot > /dev/null 2>&1
wait $START_PID 2>/dev/null
