/*
    ESDM-PAV Runtime
    Copyright (C) 2021

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

#include "rabbitmq_utils.h"
#include "debug.h"

#include "assert.h"

#include <pthread.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define SINGLE_QUOTE '\''
#define DOUBLE_QUOTE '"'

pthread_mutex_t global_flag;

int set_rabbitmq_connection(amqp_connection_state_t ** conn, amqp_channel_t channel, char *hostname, char *port, char *username, char *password, char *queue_name, amqp_bytes_t * queuename)
{

	if (!hostname || !port || !username || !password || !queue_name) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to open RabbitMQ connection. Wrong parameters\n");
		return RABBITMQ_FAILURE;
	}

	amqp_socket_t *socket = NULL;
	amqp_rpc_reply_t reply;

	**conn = amqp_new_connection();
	assert(**conn != NULL);
	socket = amqp_tcp_socket_new(**conn);
	assert(socket != NULL);

	while (amqp_socket_open(socket, hostname, atoi(port)) != AMQP_STATUS_OK) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to open TCP socket, is rabbitmq server running? I'll try to connect shortly\n");
		sleep(5);
	}

	reply = amqp_login(**conn, AMQP_DEFAULT_VHOST, AMQP_DEFAULT_MAX_CHANNELS, AMQP_DEFAULT_FRAME_SIZE, AMQP_DEFAULT_HEARTBEAT, AMQP_SASL_METHOD_PLAIN, username, password);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot login - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	amqp_channel_open(**conn, channel);
	reply = amqp_get_rpc_reply(**conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot open channel - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	amqp_queue_declare_ok_t *rq = amqp_queue_declare(**conn,
							 channel,
							 amqp_cstring_bytes(queue_name),
							 0,
							 1,
							 0,
							 0,
							 amqp_empty_table);

	reply = amqp_get_rpc_reply(**conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot declare queue - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	*queuename = amqp_bytes_malloc_dup(rq->queue);
	if (queuename->bytes == NULL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Out of memory while copying queue name\n");
		return RABBITMQ_FAILURE;
	}

	return RABBITMQ_SUCCESS;
}

int rabbitmq_consume_connection(amqp_connection_state_t * conn, amqp_channel_t channel, char *hostname, char *port,
				char *username, char *password, char *queue_name, char *exchange, int rabbitmq_deliveries)
{

	amqp_bytes_t queuename;
	if (set_rabbitmq_connection(&conn, channel, hostname, port, username, password, queue_name, &queuename) == RABBITMQ_FAILURE)
		return RABBITMQ_FAILURE;

	amqp_queue_bind(*conn, channel, queuename, amqp_cstring_bytes(exchange), amqp_cstring_bytes(queue_name), amqp_empty_table);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(*conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot bind queue - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	amqp_bytes_free(queuename);

	amqp_basic_qos(*conn, channel, 0, rabbitmq_deliveries, 0);
	reply = amqp_get_rpc_reply(*conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot set basic qos - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	amqp_basic_consume(*conn, channel, amqp_cstring_bytes(queue_name), amqp_empty_bytes, 0, 0, 0, amqp_empty_table);
	reply = amqp_get_rpc_reply(*conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot set basic consume - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Consume connection to RabbitMQ broker on queue %s, ip_address %s and port %s\n", queue_name, hostname, port);

	return RABBITMQ_SUCCESS;
}

int rabbitmq_publish_connection(amqp_connection_state_t * conn, amqp_channel_t channel, char *hostname, char *port, char *username, char *password, char *queue_name)
{

	amqp_bytes_t queuename;
	if (set_rabbitmq_connection(&conn, channel, hostname, port, username, password, queue_name, &queuename) == RABBITMQ_FAILURE)
		return RABBITMQ_FAILURE;

	amqp_bytes_free(queuename);

	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Publish connection to RabbitMQ broker on queue %s, ip_address %s and port %s\n", queue_name, hostname, port);

	return RABBITMQ_SUCCESS;
}

int close_rabbitmq_connection(amqp_connection_state_t conn, amqp_channel_t channel)
{
	if (conn != NULL && (int) channel > 0) {
		amqp_maybe_release_buffers_on_channel(conn, channel);

		amqp_rpc_reply_t reply = amqp_channel_close(conn, channel, AMQP_REPLY_SUCCESS);
		if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot close channel - %s\n", amqp_error_string2(reply.library_error));

			return RABBITMQ_FAILURE;
		}

		reply = amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
		if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot close connection - %s\n", amqp_error_string2(reply.library_error));
			return RABBITMQ_FAILURE;
		}

		int status = amqp_destroy_connection(conn);
		if (status != AMQP_STATUS_OK) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot destroy connection\n");
			return RABBITMQ_FAILURE;
		}

		return RABBITMQ_SUCCESS;
	} else {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot close RabbitMQ connection\n");
		return RABBITMQ_FAILURE;
	}
}

int get_number_queued_messages(char *hostname, char *port, char *username, char *password, char *queue_name)
{
	amqp_socket_t *socket = NULL;
	amqp_rpc_reply_t reply;
	amqp_connection_state_t conn;
	amqp_channel_t channel = 1;

	conn = amqp_new_connection();
	assert(conn != NULL);
	socket = amqp_tcp_socket_new(conn);
	assert(socket != NULL);

	while (amqp_socket_open(socket, hostname, atoi(port)) != AMQP_STATUS_OK) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Failed to open TCP socket, is rabbitmq server running? I'll try to connect shortly\n");
		sleep(5);
	}

	reply = amqp_login(conn, AMQP_DEFAULT_VHOST, AMQP_DEFAULT_MAX_CHANNELS, AMQP_DEFAULT_FRAME_SIZE, AMQP_DEFAULT_HEARTBEAT, AMQP_SASL_METHOD_PLAIN, username, password);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot login - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	amqp_channel_open(conn, channel);
	reply = amqp_get_rpc_reply(conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot open channel - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	amqp_queue_declare_ok_t *rq = amqp_queue_declare(conn,
							 channel,
							 amqp_cstring_bytes(queue_name),
							 1,
							 1,
							 0,
							 0,
							 amqp_empty_table);

	int messages = (int) (*rq).message_count;

	reply = amqp_get_rpc_reply(conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Cannot declare queue - %s\n", amqp_error_string2(reply.library_error));
		return RABBITMQ_FAILURE;
	}

	if (close_rabbitmq_connection(conn, channel) == RABBITMQ_FAILURE)
		return RABBITMQ_FAILURE;
	else
		return messages;
}

int split_by_delimiter(char *message, char delimiter, int n_chars, char **result1, char **result2)
{
	if (!message) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Message to split is empty\n");
		return 1;
	}

	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Splitting message: %s\n", message);

	int singleQuotes_count = 0;
	int doubleQuotes_count = 0;

	while (*message == delimiter)
		message += 1;

	int len = strlen(message);
	int ii, n_ch = 0;

	for (ii = 0; ii < len; ii++) {
		if (*(message + ii) == DOUBLE_QUOTE) {
			if (doubleQuotes_count > 0)
				doubleQuotes_count--;
			else
				doubleQuotes_count++;
		} else if (*(message + ii) == SINGLE_QUOTE) {
			if (singleQuotes_count > 0)
				singleQuotes_count--;
			else
				singleQuotes_count++;
		}

		if (singleQuotes_count || doubleQuotes_count)
			continue;

		if (*(message + ii) == delimiter)
			n_ch++;
		else {
			n_ch = 0;
			continue;
		}

		if (n_ch == n_chars) {
			*result1 = message;
			*(*result1 + ii - n_chars + 1) = 0;
			*result2 = message + ii + 1;

			return 0;
		}
	}

	return 1;
}
