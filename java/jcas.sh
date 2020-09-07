#!/bin/bash

proc_name=$1
null_arg=$2
env=$3

echo $proc_name
echo $env

java -jar \
-Djava.library.path=$LD_LIBRARY_PATH \
-Djava.util.logging.config.file=$CUBRID/java/logging.properties \
-Xrs \
$CUBRID/java/jspserver.jar $proc_name $null_arg $env