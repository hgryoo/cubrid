#!/bin/bash

proc_name=$1
null_arg=$2
env=$3

echo $proc_name
echo $env

java -jar -Djava.library.path=$LD_LIBRARY_PATH ../jcas/jcas.jar $proc_name $null_arg $env