#!/bin/bash

WORKERS_NAME=$1

ESDM_PAV_RUNTIME_WORKER_PATH=/usr/local/ophidia/esdm-pav-runtime/bin/esdm_pav_runtime_worker

${ESDM_PAV_RUNTIME_WORKER_PATH} -C ${WORKERS_NAME} &


exit 0
