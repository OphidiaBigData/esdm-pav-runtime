#
#    Ophidia Server
#    Copyright (C) 2012-2021 CMCC Foundation
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

#!/bin/bash

# Input parameters
taskid=${1}
ncores=${2}
log=${3}
submissionstring=${4}
queue=${5}
serverid=${6}
workflowid=${7}

# Const
fixString=
FRAMEWORK_PATH=/usr/local/ophidia/oph-cluster/oph-analytics-framework
LAUNCHER=/usr/lib64/mpich/bin/mpirun

# Body
mkdir -p ${HOME}/.esdmpav
> ${HOME}/.esdmpav/${serverid}${taskid}.submit.sh
echo "#!/bin/bash" >> ${HOME}/.esdmpav/${serverid}${taskid}.submit.sh
echo "${FRAMEWORK_PATH}/bin/oph_analytics_framework \"${submissionstring}\"" >> ${HOME}/.esdmpav/${serverid}${taskid}.submit.sh
chmod +x ${HOME}/.esdmpav/${serverid}${taskid}.submit.sh

${LAUNCHER} -n ${ncores} ${HOME}/.esdmpav/${serverid}${taskid}.submit.sh 2>&1 > ${log}
if [ $? -ne 0 ]; then
	echo "Unable to submit ${HOME}/.esdmpav/${serverid}${taskid}.submit.sh"
	exit -1
fi

rm ${HOME}/.esdmpav/${serverid}${taskid}.submit.sh

exit 0
