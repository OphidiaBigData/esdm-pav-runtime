#!/bin/bash

WORKERS_NUMBER=$1
ESDM_PAV_RUNTIME_WORKER_PATH=/usr/local/ophidia/esdm-pav-runtime/bin/esdm_pav_runtime_worker

for (( ii=0; ii < ${WORKERS_NUMBER}; ++ii ))
do
  ${ESDM_PAV_RUNTIME_WORKER_PATH} &
done

exit 0
