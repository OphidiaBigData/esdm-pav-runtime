#!/bin/bash

set -e

SERVER=${1}
FRAMEWORK_PATH=${2}
DEBUG=${3}
HOST_USER=${4}

# esdmpav Configuration
cp /home/esdmpav/.ssh/id_rsa.pub /connect/

sed -i "s/HOST=.*/HOST=${SERVER}/g" /usr/local/esdmpav/esdm-pav-runtime/etc/server.conf
sed -i "s/IP_TARGET_HOST=.*/IP_TARGET_HOST=${SERVER}/g" /usr/local/esdmpav/esdm-pav-runtime/etc/server.conf
sed -Ei "s|/usr/local/ophidia/oph-cluster/oph-analytics-framework|${FRAMEWORK_PATH}|g" /usr/local/esdmpav/esdm-pav-runtime/etc/server.conf

# Start the server
sed -i "s/MY_SSH-SERVER_IP/${SERVER}/g" /usr/local/esdmpav/esdm-pav-runtime/etc/script/submit_container.sh
sed -i "s/THE_HOST_USER=.*/THE_HOST_USER=${HOST_USER}/g" /usr/local/esdmpav/esdm-pav-runtime/etc/script/submit_container.sh
sed -Ei "s|PATH_TO_FRAMEWORK|${FRAMEWORK_PATH}|g" /usr/local/esdmpav/esdm-pav-runtime/etc/script/submit_container.sh

if [[ $DEBUG == "no" ]]
then
	/usr/local/esdmpav/esdm-pav-runtime/bin/esdm_pav_runtime 2>&1 > /dev/null < /dev/null
else
	/usr/local/esdmpav/esdm-pav-runtime/bin/esdm_pav_runtime -d 2>&1 > /dev/null < /dev/null
fi
