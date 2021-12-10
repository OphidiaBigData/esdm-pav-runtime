/*
    ESDM-PAV Runtime
    Copyright (C) 2020-2021 CMCC Foundation

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

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
