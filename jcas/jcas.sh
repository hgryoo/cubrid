#!/bin/bash

proc_name=$1
null_arg=$2
env=$3

echo $proc_name
echo $env

java -jar \
-Djava.library.path=$LD_LIBRARY_PATH \
-Djava.class.path=../java/jspserver.jar \
-Djava.util.logging.config.file=../java/logging.properties \
-Xrs \
-Xdebug \
-agentlib:jdwp=transport=dt_socket,server=y,address=9999,suspend=n \
../jcas/jcas.jar $proc_name $null_arg $env