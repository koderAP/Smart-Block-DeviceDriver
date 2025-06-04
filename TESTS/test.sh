#!/bin/bash

DEV_NAME="/dev/smart_block"  
MOD_NAME="smart_block"

echo "My PID is $$"


insmod ../smart_block.ko cache_size=65536 || exit 1
sleep 1

# Automatically find the new device if needed
if [ "$DEV_NAME" == "/dev/sdX" ]; then
    DEV_NAME=$(lsblk -ndo NAME,MODEL | grep smart_block | awk '{print "/dev/" $1}')
    if [ -z "$DEV_NAME" ]; then
        echo "Error: smartblock device not found"
        sudo rmmod $MOD_NAME
        exit 1
    fi
fi

echo "[INFO] Using device: $DEV_NAME"

# echo "[TEST] Writing test data..."
# echo "Hello from SmartBlock2!" > /tmp/test_input.txt
# dd if=/tmp/test_input.txt of=$DEV_NAME bs=512 count=1 conv=notrunc oflag=direct seek=2

# echo "[TEST] Writing test data..."
# echo "Hello from SmartBlock**1!" > /tmp/test_input.txt
# dd if=/tmp/test_input.txt of=$DEV_NAME bs=512 count=1 conv=notrunc oflag=direct seek=1

# echo "[TEST] Writing test data..."
# echo "Hello from SmartBlock**2!" > /tmp/test_input.txt
# dd if=/tmp/test_input.txt of=$DEV_NAME bs=512 count=1 conv=notrunc oflag=direct seek=3

# echo "[TEST] Writing test data..."
# echo "Hello from SmartBlock**3!" > /tmp/test_input.txt
# dd if=/tmp/test_input.txt of=$DEV_NAME bs=512 count=1 conv=notrunc oflag=direct seek=4

# echo "[TEST] Reading it back..."
# dd if=$DEV_NAME of=/tmp/test_output.txt bs=512 count=100 iflag=direct seek=2
# echo "Output:"
# cat /tmp/test_output.txt

# echo "[TEST] Checking procfs stats..."
# cat /proc/smart_block/stats

# echo "[TEST] Forcing flush via procfs..."
# echo "-1" > /proc/smart_block/flush
# cat /proc/smart_block/stats

# echo "[TEST] Resizing cache to 1KB..."
# echo "1024" > /proc/smart_block/resize_cache
# cat /proc/smart_block/stats

# echo "[CLEANUP] Removing module..."
# rmmod $MOD_NAME
