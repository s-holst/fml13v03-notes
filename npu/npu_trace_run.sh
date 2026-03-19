#!/bin/sh

# Compile and run on target: FML13V03 laptop 'roma' on local network, password-less login and sudo

scp npu_trace.c roma:
ssh roma gcc -shared -fPIC -o npu_trace.so npu_trace.c -ldl -Werror
if [ "$?" != "0" ]; then
    exit $?

output=npu_trace-sample_npu-s1-`date "+%Y-%m-%d-%H-%M-%S"`.txt
ssh roma sudo LD_PRELOAD=./npu_trace.so /opt/eswin/sample-code/npu_sample/npu_runtime_sample/src/build/sample_npu -s 1 | tee $output
ln -sf $output npu_trace-latest.txt
