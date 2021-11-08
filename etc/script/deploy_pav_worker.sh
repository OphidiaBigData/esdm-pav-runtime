#!/bin/bash

WORKERS_NAME=$1
ESDM_PAV_RUNTIME_WORKER_PATH="/usr/local/ophidia/esdm-pav-runtime/bin/esdm_pav_runtime_worker -C"

${ESDM_PAV_RUNTIME_WORKER_PATH} ${WORKERS_NAME} &

exit 0
