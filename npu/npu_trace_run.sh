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

mkdir -p nogit
output=nogit/npu_trace-sample_npu-s1-`date "+%Y-%m-%d-%H-%M-%S"`.txt
ssh $TARGET $SUDO LD_PRELOAD=./npu_trace.so /opt/eswin/sample-code/npu_sample/npu_runtime_sample/src/build/sample_npu -s 1 | tee $output
ln -sf $output nogit/npu_trace-latest.txt
