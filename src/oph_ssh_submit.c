/*
    Ophidia Server
    Copyright (C) 2012-2021 CMCC Foundation

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

#include <sys/types.h>
#include <sys/wait.h>

#ifdef SSH_SUPPORT
#include <libssh2.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#else
#define OPH_LIBSSH_SEPARATOR '\"'
#define OPH_LIBSSH_ESCAPE '\\'
#define OPH_LIBSSH_SYSTEM_COMMAND "ssh %s %c%s%c >/dev/null 2>&1 </dev/null"
#endif

#include "oph_ssh_submit.h"

#ifdef MULTI_NODE_SUPPORT
#include "debug.h"
#include "oph_server_confs.h"
#include "rabbitmq_utils.h"

#include <sqlite3.h>

#ifndef MULTI_RABBITMQ_CONN_SUPPORT
amqp_connection_state_t single_rabbitmq_publish_conn = NULL;
amqp_channel_t channel = 1;
#endif

amqp_basic_properties_t props;

char *master_hostname = NULL;
char *master_port = NULL;
char *rabbitmq_username = NULL;
char *rabbitmq_password = NULL;
char *rabbitmq_task_queue_name = NULL;
char *max_workers = NULL;
char *killer = NULL;
char *db_location = NULL;
char *wid_tocancel = NULL;

extern HASHTBL *oph_rabbit_hashtbl;
extern pthread_mutex_t rabbitmq_flag;
extern char *oph_rabbit_conf;
#endif

extern pthread_mutex_t global_flag;
extern pthread_mutex_t libssh2_flag;
extern char *oph_ip_target_host;
extern char oph_subm_ssh;
extern char *oph_subm_user;
extern char *oph_subm_user_publk;
extern char *oph_subm_user_privk;

#ifdef MULTI_NODE_SUPPORT
extern char *oph_rabbit_conf;
#endif

#ifdef MULTI_NODE_SUPPORT
int oph_rabbitmq_open_connection()
{
	short unsigned int instance = 0;

	if (oph_server_conf_load(instance, &oph_rabbit_hashtbl, oph_rabbit_conf)) {
		pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to open configuration file\n");
		return 1;
	}

	if (!master_hostname) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "MASTER_HOSTNAME", &master_hostname)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get MASTER_HOSTNAME param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM MASTER_HOSTNAME: %s\n", master_hostname);

	if (!master_port) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "MASTER_PORT", &master_port)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get MASTER_PORT param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM MASTER_PORT: %s\n", master_port);

	if (!rabbitmq_username) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "RABBITMQ_USERNAME", &rabbitmq_username)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get RABBITMQ_USERNAME param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM RABBITMQ_USERNAME: %s\n", rabbitmq_username);

	if (!rabbitmq_password) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "RABBITMQ_PASSWORD", &rabbitmq_password)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get RABBITMQ_PASSWORD param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM RABBITMQ_PASSWORD: %s\n", rabbitmq_password);

	if (!rabbitmq_task_queue_name) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "TASK_QUEUE_NAME", &rabbitmq_task_queue_name)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get TASK_QUEUE_NAME param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM TASK_QUEUE_NAME: %s\n", rabbitmq_task_queue_name);

	if (!max_workers) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "MAX_WORKERS", &max_workers)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get MAX_WORKERS param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM MAX_WORKERS: %s\n", max_workers);

	if (!killer) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "KILLER", &killer)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get KILLER param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM KILLER: %s\n", killer);

	if (!db_location) {
		if (oph_server_conf_get_param(oph_rabbit_hashtbl, "DATABASE_PATH", &db_location)) {
			pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to get DATABASE_PATH param\n");
			oph_server_conf_unload(&oph_rabbit_hashtbl);
			return 1;
		}
	}
	pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "LOADED PARAM DB_LOCATION: %s\n", db_location);

	props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
	props.content_type = amqp_cstring_bytes("text/plain");
	props.delivery_mode = AMQP_DELIVERY_PERSISTENT;

#ifndef MULTI_RABBITMQ_CONN_SUPPORT

	if (rabbitmq_publish_connection(&single_rabbitmq_publish_conn, channel, master_hostname, master_port, rabbitmq_username, rabbitmq_password, rabbitmq_task_queue_name) == RABBITMQ_FAILURE) {
		pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Fail to connect to RabbitMQ for publish on %s\n", rabbitmq_task_queue_name);
		return 1;
	}
#endif

	return 0;
}

int oph_rabbitmq_close_connection()
{
#ifndef MULTI_RABBITMQ_CONN_SUPPORT
	if (close_rabbitmq_connection(single_rabbitmq_publish_conn, channel) == RABBITMQ_SUCCESS)
		pmesg_safe(&rabbitmq_flag, LOG_INFO, __FILE__, __LINE__, "RabbitMQ connection for publish on %s has been closed\n", rabbitmq_task_queue_name);
	else
		return 1;
#endif

	if (oph_server_conf_unload(&oph_rabbit_hashtbl) != 0) {
		pmesg_safe(&rabbitmq_flag, LOG_ERROR, __FILE__, __LINE__, "Error on oph_server_conf_unload\n");
		return 1;
	}

	return 0;
}

int send_delete_message(void *NotUsed, int argc, char **argv, char **azColName)
{
	UNUSED(NotUsed);
	UNUSED(argc);
	UNUSED(azColName);

	if (!argv[0] || !argv[1] || !argv[2])
		return 1;

	amqp_connection_state_t conn;
	amqp_channel_t channel = 1;

	rabbitmq_publish_connection(&conn, channel, argv[0], argv[1], rabbitmq_username, rabbitmq_password, argv[2]);
	int messages = get_number_queued_messages(master_hostname, master_port, rabbitmq_username, rabbitmq_password, rabbitmq_task_queue_name);

	int neededSize = snprintf(NULL, 0, "%s***%d", wid_tocancel, messages);
	char *final_message = (char *) malloc(neededSize + 1);
	snprintf(final_message, neededSize + 1, "%s***%d", wid_tocancel, messages);

	int status = amqp_basic_publish(conn,
					channel,
					amqp_cstring_bytes(""),
					amqp_cstring_bytes(argv[2]),
					0,	// mandatory (message must be routed to a queue)
					0,	// immediate (message must be delivered to a consumer immediately)
					&props,	// properties
					amqp_cstring_bytes(final_message));

	if (status == AMQP_STATUS_OK)
		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Message %s has been sent to %s on ip_address %s and port %s\n", final_message, argv[2], argv[0], argv[1]);
	else
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot send message to %s on ip_address %s and port %s\n", argv[2], argv[0], argv[1]);

	if (final_message)
		free(final_message);

	close_rabbitmq_connection(conn, channel);

	return 0;
}
#endif


#ifdef SSH_SUPPORT
int waitsocket(int socket_fd, LIBSSH2_SESSION * session)
{
	struct timeval timeout;
	int rc;
	fd_set fd;
	fd_set *writefd = NULL;
	fd_set *readfd = NULL;
	int dir;

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	FD_ZERO(&fd);

	FD_SET(socket_fd, &fd);

	dir = libssh2_session_block_directions(session);

	if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
		readfd = &fd;

	if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
		writefd = &fd;

	rc = select(socket_fd + 1, readfd, writefd, NULL, &timeout);

	return rc;
}
#else
int _system(const char *command)
{
	if (!command)
		return -1;

	int status;

#ifdef MULTI_NODE_SUPPORT
	char *my_command = strdup(command);

	char *current = 0, *next = 0;

	if (split_by_delimiter(my_command, ' ', 1, &current, &next) != 0) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
		return 1;
	}

	int neededSize = snprintf(NULL, 0, "%s", current);
	char *aaa = (char *) malloc(neededSize + 1);
	snprintf(aaa, neededSize + 1, "%s", current);
	free(aaa);

	if (strstr(command, "oph_cancel") != NULL) {
		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		char *wid_tocancel = (char *) malloc(neededSize + 1);
		snprintf(wid_tocancel, neededSize + 1, "%s", current);

		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "WORKFLOW_ID to cancel: %s\n", wid_tocancel);

		sqlite3 *db = NULL;
		char *err_msg = 0;

		if (sqlite3_open_v2(db_location, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Fail to cancel workflow %s! Cannot open jobs database %s\n", wid_tocancel, db_location);
			sqlite3_close(db);

			return 1;
		}

		neededSize = snprintf(NULL, 0, "SELECT ip_address, port, delete_queue_name FROM job_table GROUP BY ip_address, port, delete_queue_name;");
		char *select_distinct_sql = (char *) malloc(neededSize + 1);
		snprintf(select_distinct_sql, neededSize + 1, "SELECT ip_address, port, delete_queue_name FROM job_table GROUP BY ip_address, port, delete_queue_name;");

		while (sqlite3_exec(db, select_distinct_sql, send_delete_message, 0, &err_msg) != SQLITE_OK)
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "SQL error on select query: %s\n", err_msg);

		sqlite3_close(db);
		free(select_distinct_sql);

		free(wid_tocancel);

		return 0;
	} else if (strstr(command, "submit_local.sh") != NULL) {
		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		char *job_id = (char *) malloc(neededSize + 1);
		snprintf(job_id, neededSize + 1, "%s", current);

		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "JOB_ID: %s\n", job_id);

		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		char *ncores = (char *) malloc(neededSize + 1);
		snprintf(ncores, neededSize + 1, "%s", current);

		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "NCORES: %s\n", ncores);

		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		aaa = (char *) malloc(neededSize + 1);
		snprintf(aaa, neededSize + 1, "%s", current);
		free(aaa);

		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		char *submission_string = (char *) malloc(neededSize + 1);
		snprintf(submission_string, neededSize + 1, "%s", current);

		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "SUBMISSION_STRING: %s\n", submission_string);

		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		aaa = (char *) malloc(neededSize + 1);
		snprintf(aaa, neededSize + 1, "%s", current);
		free(aaa);

		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		aaa = (char *) malloc(neededSize + 1);
		snprintf(aaa, neededSize + 1, "%s", current);
		free(aaa);

		if (split_by_delimiter(next, ' ', 1, &current, &next) != 0) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to split by delimiter\n");
			return 1;
		}

		neededSize = snprintf(NULL, 0, "%s", current);
		char *workflow_id = (char *) malloc(neededSize + 1);
		snprintf(workflow_id, neededSize + 1, "%s", current);

		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "WORKFLOW_ID: %s\n", workflow_id);

#ifdef MULTI_RABBITMQ_CONN_SUPPORT
		amqp_connection_state_t multi_rabbitmq_publish_conn = NULL;
		amqp_channel_t channel = 1;

		if (rabbitmq_publish_connection(&multi_rabbitmq_publish_conn, channel, master_hostname, master_port, rabbitmq_username, rabbitmq_password, rabbitmq_task_queue_name) ==
		    RABBITMQ_FAILURE)
			return 1;
#endif

		neededSize = snprintf(NULL, 0, "%s***%s***%s***%s", submission_string, workflow_id, job_id, ncores);
		char *final_message = (char *) malloc(neededSize + 1);
		snprintf(final_message, neededSize + 1, "%s***%s***%s***%s", submission_string, workflow_id, job_id, ncores);

#ifndef MULTI_RABBITMQ_CONN_SUPPORT
		pthread_mutex_lock(&rabbitmq_flag);

		int res_status = amqp_basic_publish(single_rabbitmq_publish_conn,
						    channel,
						    amqp_cstring_bytes(""),
						    amqp_cstring_bytes(rabbitmq_task_queue_name),
						    0,
						    0,
						    &props,
						    amqp_cstring_bytes(final_message));

		pthread_mutex_unlock(&rabbitmq_flag);
#else
		int res_status = amqp_basic_publish(multi_rabbitmq_publish_conn,
						    channel,
						    amqp_cstring_bytes(""),
						    amqp_cstring_bytes(rabbitmq_task_queue_name),
						    0,
						    0,
						    &props,
						    amqp_cstring_bytes(final_message));
#endif

		if (res_status == AMQP_STATUS_OK)
			pmesg_safe(&global_flag, LOG_INFO, __FILE__, __LINE__, "Message has been sent on %s: %s\n", rabbitmq_task_queue_name, final_message);
		else {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot send message to %s\n", rabbitmq_task_queue_name);
			if (final_message)
				free(final_message);
			if (my_command)
				free(my_command);
			return 1;
		}

		if (final_message)
			free(final_message);
		if (my_command)
			free(my_command);
		if (job_id)
			free(job_id);
		if (ncores)
			free(ncores);
		if (submission_string)
			free(submission_string);
		if (workflow_id)
			free(workflow_id);

#ifdef MULTI_RABBITMQ_CONN_SUPPORT
		if (close_rabbitmq_connection(multi_rabbitmq_publish_conn, channel) == RABBITMQ_SUCCESS)
			pmesg_safe(&global_flag, LOG_INFO, __FILE__, __LINE__, "RabbitMQ connection for publish on %s has been closed\n", rabbitmq_task_queue_name);
		else
			return 1;
#endif

		status = 0;
	}
#else
	pid_t childPid;

	switch (childPid = fork()) {

		case -1:
			status = -1;
			break;

		case 0:
			execl("/bin/sh", "sh", "-c", command, (char *) NULL);
			_exit(127);

		default:
			while (waitpid(childPid, &status, 0) == -1) {
				if (errno != EINTR) {
					status = -1;
					break;
				}
			}
			break;
	}
#endif

	return status;
}
#endif

int oph_ssh_submit(const char *cmd)
{
	if (!cmd || !strlen(cmd)) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null pointer\n");
		return OPH_LIBSSH_ERROR;
	}
#ifdef SSH_SUPPORT

	int sock;
	struct sockaddr_in sin;
	struct addrinfo hints, *result;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	int rc;
	int exitcode;
	char *exitsignal = (char *) "none";
	int bytecount = 0;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;
	result = NULL;
	rc = getaddrinfo(oph_ip_target_host, NULL, &hints, &result);
	if (rc != 0) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to resolve address from target hostname: %s\n", gai_strerror(rc));
		return OPH_LIBSSH_ERROR;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(22);
	sin.sin_addr.s_addr = ((struct sockaddr_in *) result->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(result);
	if (connect(sock, (struct sockaddr *) (&sin), sizeof(struct sockaddr_in)) != 0) {
#ifdef WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to connect to submission host\n");
		return OPH_LIBSSH_ERROR;
	}

	pthread_mutex_lock(&libssh2_flag);	// Lock the access to SSH library
	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "SSH2 library locked\n");

	rc = libssh2_init(0);
	if (rc != 0) {
#ifdef WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		pthread_mutex_unlock(&libssh2_flag);
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "libssh2 initialization failed (%d)\n", rc);
		return OPH_LIBSSH_ERROR;
	}

	char *errmsg = NULL;
	int errmsg_len = 0;

	session = libssh2_session_init();
	if (!session) {
#ifdef WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		libssh2_exit();
		pthread_mutex_unlock(&libssh2_flag);
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to init ssh session\n");
		return OPH_LIBSSH_ERROR;
	}

	libssh2_session_set_blocking(session, 0);

	while ((rc = libssh2_session_handshake(session, sock)) == LIBSSH2_ERROR_EAGAIN);
	if (rc) {
		libssh2_session_last_error(session, &errmsg, &errmsg_len, 1);
		libssh2_session_disconnect(session, "Session disconnected");
		libssh2_session_free(session);
#ifdef WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		libssh2_exit();
		pthread_mutex_unlock(&libssh2_flag);
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failure establishing SSH session (%d): %s\n", rc, errmsg ? errmsg : "no additional info");
		if (errmsg)
			free(errmsg);
		return OPH_LIBSSH_ERROR;
	}

	while ((rc = libssh2_userauth_publickey_fromfile(session, oph_subm_user, oph_subm_user_publk, oph_subm_user_privk, "")) == LIBSSH2_ERROR_EAGAIN);
	if (rc) {
		libssh2_session_last_error(session, &errmsg, &errmsg_len, 1);
		libssh2_session_disconnect(session, "Session disconnected");
		libssh2_session_free(session);
#ifdef WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		libssh2_exit();
		pthread_mutex_unlock(&libssh2_flag);
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Authentication by public key failed (%d): %s\n", rc, errmsg ? errmsg : "no additional info");
		if (errmsg)
			free(errmsg);
		return OPH_LIBSSH_ERROR;
	}

	while ((channel = libssh2_channel_open_session(session)) == NULL && libssh2_session_last_error(session, NULL, NULL, 0) == LIBSSH2_ERROR_EAGAIN) {
		waitsocket(sock, session);
	}
	if (channel == NULL) {
		libssh2_session_last_error(session, &errmsg, &errmsg_len, 1);
		libssh2_session_disconnect(session, "Session disconnected");
		libssh2_session_free(session);
#ifdef WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		libssh2_exit();
		pthread_mutex_unlock(&libssh2_flag);
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error during opening channel session: %s\n", errmsg ? errmsg : "no additional info");
		if (errmsg)
			free(errmsg);
		return OPH_LIBSSH_ERROR;
	}
	while ((rc = libssh2_channel_exec(channel, cmd)) == LIBSSH2_ERROR_EAGAIN) {
		waitsocket(sock, session);
	}
	if (rc) {
		libssh2_session_last_error(session, &errmsg, &errmsg_len, 1);
		libssh2_channel_free(channel);
		libssh2_session_disconnect(session, "Session disconnected");
		libssh2_session_free(session);
#ifdef WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		libssh2_exit();
		pthread_mutex_unlock(&libssh2_flag);
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error during sending commands over ssh channel (%d): %s\n", rc, errmsg ? errmsg : "no additional info");
		if (errmsg)
			free(errmsg);
		return OPH_LIBSSH_ERROR;
	}

	int flag = 0;
	for (;;) {
		int rc;
		do {
			char buffer[0x4000];
			rc = libssh2_channel_read(channel, buffer, sizeof(buffer));

			if (rc > 0) {
				int i;
				bytecount += rc;
				if (!flag) {
					pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "ssh submission returned:\n");
					flag = 1;
				}
				for (i = 0; i < rc; ++i)
					pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "%c\n", buffer[i]);
			} else if (rc != LIBSSH2_ERROR_EAGAIN)
				pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "ssh channel read returned %d\n", rc);
		}
		while (rc > 0);

		if (rc == LIBSSH2_ERROR_EAGAIN) {
			waitsocket(sock, session);
		} else
			break;
	}
	exitcode = 127;
	while ((rc = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN)
		waitsocket(sock, session);

	if (rc == 0) {
		exitcode = libssh2_channel_get_exit_status(channel);
		libssh2_channel_get_exit_signal(channel, &exitsignal, NULL, NULL, NULL, NULL, NULL);
	}

	if (exitsignal)
		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "ssh got signal %s\n", exitsignal);
	else
		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "ssh exit code %d with: bytecount %d\n", exitcode, bytecount);

	libssh2_channel_free(channel);
	channel = NULL;

	libssh2_session_disconnect(session, "Session ended normally");
	libssh2_session_free(session);
#ifdef WIN32
	closesocket(sock);
#else
	close(sock);
#endif
	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Session ended normally\n");

	libssh2_exit();

	pthread_mutex_unlock(&libssh2_flag);	// Release the lock for SSH library
	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "SSH2 library unlocked\n");

#else

	int result = 0;

	if (oph_subm_ssh) {

		size_t i, j, size_cmd = strlen(cmd);
		char scmd[2 * size_cmd];
		for (i = j = 0; i < size_cmd; ++i, ++j) {
			if (cmd[i] == OPH_LIBSSH_SEPARATOR) {
				scmd[j++] = OPH_LIBSSH_ESCAPE;
			}
			scmd[j] = cmd[i];
		}
		scmd[j] = 0;

		char rcmd[25 + strlen(oph_ip_target_host) + j];
		sprintf(rcmd, OPH_LIBSSH_SYSTEM_COMMAND, oph_ip_target_host, OPH_LIBSSH_SEPARATOR, scmd, OPH_LIBSSH_SEPARATOR);

		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Execute:\n%s\n", rcmd);
		result = _system(rcmd);

	} else {

		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Execute:\n%s\n", cmd);
		result = _system(cmd);
	}

	if (result) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to submit the command %s\n", cmd);
		return OPH_LIBSSH_ERROR;
	}
#endif

	return OPH_LIBSSH_OK;
}
