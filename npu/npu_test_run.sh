#!/bin/sh

# Compile and run on target: FML13V03 laptop 'roma' on local network, password-less login and sudo

scp npu_trace.c roma:
ssh roma gcc -shared -fPIC -o npu_trace.so npu_trace.c -ldl -Werror
if [ "$?" != "0" ]; then
    exit $?

scp npu_test.c roma:
ssh roma gcc -O0 -g npu_test.c -o npu_test
if [ "$?" != "0" ]; then
    exit $?

output=npu_trace-npu_test-`date "+%Y-%m-%d-%H-%M-%S"`.txt
ssh roma sudo LD_PRELOAD=./npu_trace.so ./npu_test | tee $output
ln -sf $output npu_trace-latest.txt