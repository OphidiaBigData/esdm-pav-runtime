#
#    Ophidia Server
#    Copyright (C) 2012-2022 CMCC Foundation
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

AUTOMAKE_OPTIONS = subdir-objects

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = ophidia-analytics-framework . src test

sysconf_DATA = etc/*.conf etc/oph_server_logrotate etc/esdm-pav-runtime-db.sql

bindir=${prefix}/etc
bin_SCRIPTS = etc/oph_server

sbindir = $(prefix)/sbin
sbin_SCRIPTS = etc/esdm_pav_config.sh etc/run_esdm_pav.sh

scriptdir=${prefix}/etc/script
dist_script_SCRIPTS = etc/script/*.sh

certdir = $(prefix)/etc/cert
dist_cert_DATA = .empty

logdir = $(prefix)/log
dist_log_DATA = .empty

rmsdir = $(prefix)/etc/rms
dist_rms_DATA = etc/rms/*.conf

sessionsdir = $(OPH_WEB_SERVER_LOCATION)/sessions
dist_sessions_DATA = .empty

if OPH_WEB_ACCESS
webdir = $(OPH_WEB_SERVER_LOCATION)
dist_web_DATA = etc/web/*.php etc/web/style.css etc/web/.htaccess
endif

AM_DEFAULT_VERBOSITY = 1
CODE_COVERAGE_LCOV_OPTIONS = --gcov-tool "$(GCOV)" --no-external
CODE_COVERAGE_BRANCH_COVERAGE = 1
CODE_COVERAGE_IGNORE_PATTERN = "*oph_server_test*"
@CODE_COVERAGE_RULES@

clean-local:
	-rm -f *~ authz/*~ etc/*~ etc/cert/*~ etc/ophidia_dtd/*~ etc/rms/*~ etc/operators_xml/*~ etc/script/*~

distclean-local:
	-rm -f *~ authz/*~ etc/*~ etc/cert/*~ etc/ophidia_dtd/*~ etc/rms/*~ etc/operators_xml/*~ etc/script/*~

