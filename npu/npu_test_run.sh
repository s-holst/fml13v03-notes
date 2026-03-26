#!/bin/sh

# Compile and run on target

# FML13V03 laptop '$TARGET' on local network
TARGET=arch

# set to 'sudo' if needed (should be configured NOPASSWD)
SUDO=""

scp npu_trace.c $TARGET:
ssh $TARGET gcc -shared -fPIC -ldl -Werror -o npu_trace.so npu_trace.c
if [ "$?" != "0" ]; then
    exit $?
fi

scp npu_test.c $TARGET:
ssh $TARGET gcc -O0 -g npu_test.c -o npu_test
if [ "$?" != "0" ]; then
    exit $?
fi

mkdir -p nogit
output=nogit/npu_trace-npu_test-`date "+%Y-%m-%d-%H-%M-%S"`.txt
ssh $TARGET $SUDO LD_PRELOAD=./npu_trace.so ./npu_test | tee $output
ln -sf $output nogit/npu_trace-latest.txt