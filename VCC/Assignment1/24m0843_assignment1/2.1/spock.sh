#!/bin/bash

# Set variables
DRIVER_NAME="ioctl_vtop"
DEVICE_FILE="/dev/${DRIVER_NAME}"
MAJOR_NUMBER=245  # Set the correct major number here if known
KERNEL_MODULE="${DRIVER_NAME}.ko"
USER_APP="test3.c"  # Source file for user-space app
USER_APP_BINARY="test3"  # Compiled binary of the user-space app

# Helper function to check for errors
check_error() {
    if [ $? -ne 0 ]; then
        echo "Error: $1"
        exit 1
    fi
}

echo "=== Step 1: Compile kernel module and user-space application ==="
make 

# Compile the user-space application
gcc -o $USER_APP_BINARY $USER_APP

echo "=== Step 2: Load kernel module and create device file ==="
sudo insmod $KERNEL_MODULE

# Modify the permissions of the device file (to ensure user-space app can access it)
sudo chmod 666 $DEVICE_FILE

echo "=== Step 3: Run the user-space application ==="
#If we want to change the count , we can change from here.
./$USER_APP_BINARY 5

echo "=== Step 4: Clean up ==="
sudo rm -f $DEVICE_FILE

sudo rmmod $DRIVER_NAME

echo "=== Script completed successfully! ==="
