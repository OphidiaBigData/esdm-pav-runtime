#!/bin/bash

WORKER_PID=$1

kill -s SIGINT ${WORKER_PID} > /dev/null

exit 0
