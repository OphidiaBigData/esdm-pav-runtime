#!/bin/bash

WORKERS_NUMBER=$1
ESDM_PAV_RUNTIME_WORKER_PATH=/usr/local/ophidia/esdm-pav-runtime/bin/esdm_pav_runtime_worker

ps -ef | grep "${ESDM_PAV_RUNTIME_WORKER_PATH}" | awk -v var="${WORKERS_NUMBER}" 'NR<=var{print $2}' | xargs -L1 kill -s SIGINT > /dev/null

exit 0
