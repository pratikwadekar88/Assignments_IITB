#!/bin/bash

## Building the applications
make 

gcc -o soldier soldier.c 
gcc -o control_station control_station.c


## Build your ioctl driver named chardev.c and load it here
sudo insmod chardev.ko
sudo chmod 666 /dev/chardev
###############################################

# Launching the control station
./control_station 5 &
c_pid=$!
echo "Control station PID: $c_pid"

# Launching the soldiers
./soldier $c_pid  &
s_pid1=$!
echo "Soldier PID: $s_pid1"

./soldier $c_pid  &
s_pid2=$!
echo "Soldier PID: $s_pid2"

./soldier $c_pid  &
s_pid3=$!
echo "Soldier PID: $s_pid3"

./soldier $c_pid  &
s_pid4=$!
echo "Soldier PID: $s_pid4"

sleep 2
kill -9 $s_pid1
kill -9 $s_pid2

sleep 10
## Remove the driver here
sudo rmmod chardev
make clean
rm soldier
rm control_station
