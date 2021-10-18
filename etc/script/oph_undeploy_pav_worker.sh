#!/bin/bash

KILLER=$1
JOB_TO_CANCEL=$2

if [ ${KILLER} == "bkill" ];
then
    bkill -s SIGINT -J "worker_${JOB_TO_CANCEL}" > /dev/null
else
    kill -s SIGINT ${JOB_TO_CANCEL} > /dev/null
fi

exit 0
