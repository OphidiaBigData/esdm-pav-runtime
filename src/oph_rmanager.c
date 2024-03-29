/*
    Ophidia Server
    Copyright (C) 2012-2022 CMCC Foundation

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

#define _GNU_SOURCE

#include "oph_rmanager.h"

#include "oph_auth.h"
#include "oph_known_operators.h"
#include "oph_service_info.h"
#include "oph_utils.h"

#include <sqlite3.h>

#ifdef OPH_DB_SUPPORT
#include <mysql.h>
#endif
#include <grp.h>

#define SUBM_CMD_TO_SUBMIT				"SUBM_CMD_TO_SUBMIT"
#define SUBM_CMD_TO_START				"SUBM_CMD_TO_START"
#define SUBM_CMD_TO_MOUNT				"SUBM_CMD_TO_MOUNT"
#define SUBM_CMD_TO_CANCEL				"SUBM_CMD_TO_CANCEL"
#define SUBM_CMD_TO_STOP				"SUBM_CMD_TO_STOP"
#define SUBM_CMD_TO_UMOUNT				"SUBM_CMD_TO_UMOUNT"
#define SUBM_CMD_TO_CHECK				"SUBM_CMD_TO_CHECK"
#define SUBM_CMD_TO_COUNT				"SUBM_CMD_TO_COUNT"
#define SUBM_CMD_TO_CANCEL_ALL			"SUBM_CMD_TO_CANCEL_ALL"
#define SUBM_CMD_TO_PROGRESS			"SUBM_CMD_TO_PROGRESS"
#define SUBM_MULTIUSER					"SUBM_MULTIUSER"
#define SUBM_GROUP						"SUBM_GROUP"
#define SUBM_QUEUE_HIGH					"SUBM_QUEUE_HIGH"
#define SUBM_QUEUE_LOW					"SUBM_QUEUE_LOW"
#define SUBM_PREFIX						"SUBM_PREFIX"
#define SUBM_POSTFIX					"SUBM_POSTFIX"
#define SUBM_CMD_TO_DEPLOY_WORKERS_LOCAL_SCHEDULER      "SUBM_CMD_TO_DEPLOY_WORKERS_LOCAL_SCHEDULER"
#define SUBM_CMD_TO_DEPLOY_WORKERS_LSF_SCHEDULER      	"SUBM_CMD_TO_DEPLOY_WORKERS_LSF_SCHEDULER"
#define SUBM_CMD_TO_DEPLOY_WORKERS_SLURM_SCHEDULER      "SUBM_CMD_TO_DEPLOY_WORKERS_SLURM_SCHEDULER"

#define OPH_RMANAGER_SUDO			"sudo -u %s"
#define OPH_RMANAGER_DEFAULT_QUEUE	"ophidia"
#define OPH_RMANAGER_HOST_FILE		"%s/oph_count_%d.log"
#define OPH_RMANAGER_PROGRESS_FILE		"%s/oph_progress_%d.log"

#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
extern pthread_mutex_t global_flag;
extern pthread_mutex_t service_flag;
extern pthread_cond_t limit_flag;
#endif
extern char *oph_rmanager_conf_file;
extern char *oph_txt_location;
extern char *oph_operator_client;
extern char *oph_json_location;
extern char *oph_server_port;
extern oph_rmanager *orm;
extern char *oph_subm_user;
extern oph_service_info *service_info;
extern unsigned int oph_server_task_limit;
extern unsigned int oph_server_core_limit;
extern unsigned int oph_server_task_running;
extern unsigned int oph_server_core_running;

#ifdef MULTI_NODE_SUPPORT
#include "oph_ssh_submit.h"
#include "rabbitmq_utils.h"

extern char *db_location;
extern char *rabbitmq_username;
extern char *rabbitmq_password;
extern char *scheduler;

int list_index = 0;
int out_list_len = 0;
#endif

extern int oph_ssh_submit(const char *cmd);

extern int oph_workflow_notify(struct oph_plugin_data *state, char ttype, int jobid, char *data, char *json, int *response);

typedef struct _oph_command_data {
	char *command;
	char *error;
	struct oph_plugin_data *state;
	int delay;
	int (*postprocess) (int);
	int id;
} oph_command_data;

void __oph_system(oph_command_data * data)
{
	if (data) {
		if (data->command) {
			if (data->delay > 0) {
				pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Back off for %d seconds\n", data->delay);
				sleep(data->delay);
			}
#ifdef LOCAL_FRAMEWORK
			if (system(data->command))
#else
			if (oph_ssh_submit(data->command))
#endif
			{
				int jobid;
				pthread_mutex_lock(&global_flag);
				jobid = ++*data->state->jobid;
				pmesg(LOG_ERROR, __FILE__, __LINE__, "C%d: critical error in task submission\n", jobid);
				pthread_mutex_unlock(&global_flag);

				if (data->error) {
					int response = 0;
					oph_workflow_notify(data->state, 'C', jobid, data->error, NULL, &response);
					if (response)
						pmesg_safe(&global_flag, LOG_WARNING, __FILE__, __LINE__, "C%d: error %d in notify\n", jobid, response);
				}
			}
			free(data->command);
		}
		if (data->error)
			free(data->error);

		if (data->state) {
			if (data->state->serverid)
				free(data->state->serverid);
			free(data->state);
		}
	}
}

void *_oph_system(oph_command_data * data)
{
#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
	pthread_detach(pthread_self());
	oph_service_info_thread_incr(service_info);
#endif
	__oph_system(data);
	if (data) {
		if (data->id)
			data->postprocess(data->id);
		free(data);
	}
#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
	oph_service_info_thread_decr(service_info);
#ifdef OPH_DB_SUPPORT
	mysql_thread_end();
#endif
#endif
	return (void *) NULL;
}

int oph_system(const char *command, const char *error, struct oph_plugin_data *state, int delay, char blocking, int (*postprocess) (int), int id)
{
	if (!command) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}

	if (service_info) {
#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
		pthread_mutex_lock(&service_flag);
#endif
		service_info->submitted_tasks++;
#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
		pthread_mutex_unlock(&service_flag);
#endif
	}

	oph_command_data *data = (oph_command_data *) malloc(sizeof(oph_command_data));
	if (!data)
		return RMANAGER_ERROR;

	data->command = strdup(command);
	if (!data->command)
		return RMANAGER_ERROR;

	if (error) {
		data->error = strdup(error);
		if (!data->error) {
			free(data->command);
			free(data);
			return RMANAGER_ERROR;
		}
	} else
		data->error = NULL;

	data->state = (struct oph_plugin_data *) malloc(sizeof(struct oph_plugin_data));
	if (!data->state) {
		free(data->command);
		if (data->error)
			free(data->error);
		free(data);
		return RMANAGER_ERROR;
	}
	memcpy(data->state, (struct oph_plugin_data *) state, sizeof(struct oph_plugin_data));
	if (state->serverid)
		data->state->serverid = strdup(state->serverid);
	else
		data->state->serverid = NULL;
	data->state->is_copy = 1;
	data->state->job_info = state->job_info;
	data->delay = delay;
	data->postprocess = postprocess;
	data->id = id;

#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
	if (!blocking) {
		pthread_t tid;
		pthread_create(&tid, NULL, (void *(*)(void *)) &_oph_system, data);
		return RMANAGER_SUCCESS;
	}
#endif
	__oph_system(data);
	if (data) {
		if (data->id)
			data->postprocess(data->id);
		free(data);
	}

	return RMANAGER_SUCCESS;
}

int oph_read_rmanager_conf(oph_rmanager * orm)
{
	if (!orm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}

	pthread_mutex_lock(&global_flag);

	char config[OPH_MAX_STRING_SIZE];
	snprintf(config, sizeof(config), "%s", oph_rmanager_conf_file);

	FILE *file = fopen(config, "r");
	if (file == NULL) {
		pmesg(LOG_ERROR, __FILE__, __LINE__, "Configuration file not found\n");
		pthread_mutex_unlock(&global_flag);
		return RMANAGER_ERROR;
	}

	char buffer[OPH_MAX_STRING_SIZE];
	char *position, *target;
	pmesg(LOG_DEBUG, __FILE__, __LINE__, "Reading resource manager configuration file '%s'\n", config);

	while (!feof(file)) {

		if (fscanf(file, "%[^\n]", buffer) == EOF)
			break;

		if (*buffer != OPH_COMMENT_MARK) {

			position = strchr(buffer, '=');
			if (!position) {
				fgetc(file);
				continue;
			}
			*position = 0;
			target = strdup(++position);
			if (!target) {
				pmesg(LOG_ERROR, __FILE__, __LINE__, "Error allocating memory\n");
				pthread_mutex_unlock(&global_flag);
				fclose(file);
				return RMANAGER_MEMORY_ERROR;
			}
			pmesg(LOG_DEBUG, __FILE__, __LINE__, "Load parameter '%s=%s'\n", buffer, target);
			if (!strcmp(buffer, SUBM_CMD_TO_SUBMIT))
				orm->subm_cmd_submit = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_START))
				orm->subm_cmd_start = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_MOUNT))
				orm->subm_cmd_mount = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_CANCEL))
				orm->subm_cmd_cancel = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_STOP))
				orm->subm_cmd_stop = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_UMOUNT))
				orm->subm_cmd_umount = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_CHECK))
				orm->subm_cmd_check = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_COUNT))
				orm->subm_cmd_count = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_CANCEL_ALL))
				orm->subm_cmd_cancel_all = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_PROGRESS))
				orm->subm_cmd_progress = target;
			else if (!strcmp(buffer, SUBM_MULTIUSER)) {
				orm->subm_multiuser = !strcmp(target, "yes");
				free(target);
			} else if (!strcmp(buffer, SUBM_GROUP))
				orm->subm_group = target;
			else if (!strcmp(buffer, SUBM_QUEUE_HIGH))
				orm->subm_queue_high = target;
			else if (!strcmp(buffer, SUBM_QUEUE_LOW))
				orm->subm_queue_low = target;
			else if (!strcmp(buffer, SUBM_PREFIX))
				orm->subm_prefix = target;
			else if (!strcmp(buffer, SUBM_POSTFIX))
				orm->subm_postfix = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_DEPLOY_WORKERS_LOCAL_SCHEDULER))
				orm->subm_cmd_deploy_workers_local_scheduler = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_DEPLOY_WORKERS_LSF_SCHEDULER))
				orm->subm_cmd_deploy_workers_lsf_scheduler = target;
			else if (!strcmp(buffer, SUBM_CMD_TO_DEPLOY_WORKERS_SLURM_SCHEDULER))
				orm->subm_cmd_deploy_workers_slurm_scheduler = target;
			else {
				pmesg(LOG_WARNING, __FILE__, __LINE__, "Parameter '%s' will be negleted\n", buffer);
				free(target);
			}
		}
		fgetc(file);	// '\n'
	}

	fclose(file);

	if (!orm->subm_cmd_submit) {
		pmesg(LOG_ERROR, __FILE__, __LINE__, "Parameter '%s' is mandatory\n", SUBM_CMD_TO_SUBMIT);
		pthread_mutex_unlock(&global_flag);
		return RMANAGER_ERROR;
	}
	if (!orm->subm_queue_high || !strlen(orm->subm_queue_high)) {
		if (orm->subm_queue_high) {
			free(orm->subm_queue_high);
			pmesg(LOG_WARNING, __FILE__, __LINE__, "Parameter '%s' will be set to '%s'\n", SUBM_QUEUE_HIGH, OPH_RMANAGER_DEFAULT_QUEUE);
		}
		orm->subm_queue_high = strdup(OPH_RMANAGER_DEFAULT_QUEUE);
	}
	if (!orm->subm_queue_low || !strlen(orm->subm_queue_low)) {
		if (orm->subm_queue_low) {
			free(orm->subm_queue_low);
			pmesg(LOG_WARNING, __FILE__, __LINE__, "Parameter '%s' will be set to '%s'\n", SUBM_QUEUE_LOW, OPH_RMANAGER_DEFAULT_QUEUE);
		}
		orm->subm_queue_low = strdup(OPH_RMANAGER_DEFAULT_QUEUE);
	}
	if (!orm->subm_prefix)
		orm->subm_prefix = strdup("");
	if (!orm->subm_postfix)
		orm->subm_postfix = strdup("");

	pthread_mutex_unlock(&global_flag);

	return RMANAGER_SUCCESS;

}

int initialize_rmanager(oph_rmanager * orm)
{
	if (!orm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}

	orm->subm_cmd_submit = NULL;
	orm->subm_cmd_start = NULL;
	orm->subm_cmd_mount = NULL;
	orm->subm_cmd_cancel = NULL;
	orm->subm_cmd_stop = NULL;
	orm->subm_cmd_umount = NULL;
	orm->subm_cmd_check = NULL;
	orm->subm_cmd_count = NULL;
	orm->subm_cmd_cancel_all = NULL;
	orm->subm_cmd_progress = NULL;
	orm->subm_multiuser = 0;	// No
	orm->subm_group = NULL;
	orm->subm_queue_high = NULL;
	orm->subm_queue_low = NULL;
	orm->subm_prefix = NULL;
	orm->subm_postfix = NULL;
	orm->subm_cmd_deploy_workers_local_scheduler = NULL;
	orm->subm_cmd_deploy_workers_lsf_scheduler = NULL;
	orm->subm_cmd_deploy_workers_slurm_scheduler = NULL;
	orm->subm_taskid = 0;	// Used only for internal requests
	orm->subm_detached_tasks = NULL;

	return RMANAGER_SUCCESS;
}

int oph_abort_request(int jobid, const char *username, char *command)
{
	if (!jobid)
		return RMANAGER_NULL_PARAM;
	if (command) {
#ifdef LOCAL_FRAMEWORK
		UNUSED(username);
		pmesg_safe(&global_flag, LOG_WARNING, __FILE__, __LINE__, "Task %d cannot be stopped\n", jobid);
#else
		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Try to stop task %d\n", jobid);
		char *taskname = NULL;
		while (orm && orm->subm_cmd_check) {
			char outfile[OPH_MAX_STRING_SIZE];
			snprintf(outfile, OPH_MAX_STRING_SIZE, OPH_TXT_FILENAME, oph_txt_location, "job", "queue");
			size_t len = 4 + strlen(orm->subm_cmd_check) + strlen(outfile);
			char cmd[len];
			snprintf(cmd, len, "%s > %s", orm->subm_cmd_check, outfile);
			if (oph_ssh_submit(cmd)) {
				pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error during remote submission\n");
				break;
			}
			char *response = NULL;
			if (oph_get_result_from_file(outfile, &response) || !response)
				break;
			char prefix[1 + strlen(oph_server_port) + strlen(OPH_RMANAGER_PREFIX) + OPH_INT_STRING_SIZE];
			sprintf(prefix, "%s%s%d", oph_server_port, OPH_RMANAGER_PREFIX, jobid);
			char *pch, *pch2, *save_pointer = NULL;
			for (pch = strtok_r(response, "\n", &save_pointer); pch; pch = strtok_r(NULL, "\n", &save_pointer))
				if ((pch2 = strstr(pch, prefix))) {
					*pch2 = 0;
					pch2 = strrchr(pch, ' ');	// Remove possible fixString
					if (!pch2)
						continue;
					taskname = strndup(pch, pch2 - pch);
					break;
				}
			free(response);
			break;
		}
		if (!taskname) {
			pmesg_safe(&global_flag, LOG_WARNING, __FILE__, __LINE__, "Task %d cannot be stopped\n", jobid);
			return RMANAGER_SUCCESS;
		}
		char subm_username[10 + (username ? strlen(username) : 0)];
		if (username && orm->subm_multiuser)
			sprintf(subm_username, OPH_RMANAGER_SUDO, username);
		else		// Skip username for backward compatibility
			*subm_username = 0;
		size_t len =
		    6 + strlen(orm->subm_prefix) + strlen(subm_username) + strlen(command) + strlen(oph_server_port) + strlen(OPH_RMANAGER_PREFIX) + OPH_RMANAGER_MAX_INT_SIZE +
		    strlen(orm->subm_postfix);
		char cmd[len];
		snprintf(cmd, len, "%s %s %s %d %s%s '%s' %s", orm->subm_prefix, subm_username, command, jobid, oph_server_port, OPH_RMANAGER_PREFIX, taskname, orm->subm_postfix);
		free(taskname);
		if (oph_ssh_submit(cmd)) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error during remote submission\n");
			return RMANAGER_ERROR;
		}
		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Task %d has been stopped\n");
#endif
	}
	return RMANAGER_SUCCESS;
}

int oph_cancel_request(int jobid, const char *username)
{
	if (!jobid)
		return RMANAGER_NULL_PARAM;
	if (orm && orm->subm_cmd_cancel)
		return oph_abort_request(jobid, username, orm->subm_cmd_cancel);
	else
		return RMANAGER_SUCCESS;
}

int oph_stop_request(int jobid, const char *username)
{
	if (!jobid)
		return RMANAGER_NULL_PARAM;
	if (orm && orm->subm_cmd_stop)
		return oph_abort_request(jobid, username, orm->subm_cmd_stop);
	else
		return RMANAGER_SUCCESS;
}

int oph_umount_request(int jobid, const char *username)
{
	if (!jobid)
		return RMANAGER_NULL_PARAM;
	if (orm && orm->subm_cmd_umount)
		return oph_abort_request(jobid, username, orm->subm_cmd_umount);
	else
		return RMANAGER_SUCCESS;
}

int oph_cancel_all_request(int wid, const char *username)
{
	if (!wid)
		return RMANAGER_NULL_PARAM;
	if (orm && orm->subm_cmd_cancel_all)
		return oph_abort_request(wid, username, orm->subm_cmd_cancel_all);
	else
		return RMANAGER_SUCCESS;
}

int oph_read_job_queue(int **list, char ***username, unsigned int *n)
{
	if (!list || !username || !n) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null parameter\n");
		return RMANAGER_NULL_PARAM;
	}
	*list = NULL;
	*username = NULL;
	*n = 0;
#ifndef LOCAL_FRAMEWORK
	if (orm && orm->subm_cmd_check) {
		char outfile[OPH_MAX_STRING_SIZE];
		snprintf(outfile, OPH_MAX_STRING_SIZE, OPH_TXT_FILENAME, oph_txt_location, "job", "queue");
		size_t len = 4 + strlen(orm->subm_cmd_check) + strlen(outfile);
		char cmd[len];
		snprintf(cmd, len, "%s > %s", orm->subm_cmd_check, outfile);
		if (oph_ssh_submit(cmd)) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error during remote submission\n");
			return RMANAGER_ERROR;
		}

		char *response = NULL;
		if (oph_get_result_from_file(outfile, &response) || !response)
			return RMANAGER_SUCCESS;

		len = strlen(oph_server_port) + strlen(OPH_RMANAGER_PREFIX);
		char prefix[1 + len];
		sprintf(prefix, "%s%s", oph_server_port, OPH_RMANAGER_PREFIX);

		char *pch, *pch2, *save_pointer = NULL;
		for (pch = strtok_r(response, "\n", &save_pointer); pch; pch = strtok_r(NULL, "\n", &save_pointer))
			if ((pch = strstr(pch, prefix)))
				(*n)++;
		free(response);

		*list = (int *) calloc(*n, sizeof(int));
		if (!*list) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Memory error\n");
			free(response);
			return RMANAGER_ERROR;
		}
		*username = (char **) calloc(*n, sizeof(char *));
		if (!*username) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Memory error\n");
			free(response);
			free(*list);
			return RMANAGER_ERROR;
		}

		unsigned int i = 0;
		save_pointer = NULL;
		for (pch = strtok_r(response, "\n", &save_pointer); pch; pch = strtok_r(NULL, "\n", &save_pointer))
			if ((pch = strstr(pch, prefix))) {
				pch += len;
				if (!(pch2 = strchr(pch, ' '))) {
					*pch2 = 0;
					pch2++;
				}
				(*list)[i] = (int) strtol(pch, NULL, 10);
				if (pch2)
					(*username)[i] = strdup(pch2 + 1);
				i++;
			}
		free(response);
	}
#endif
	return RMANAGER_SUCCESS;
}

int oph_get_available_host_number(int *size, int jobid)
{
	if (!size)
		return RMANAGER_NULL_PARAM;
	*size = 0;
#ifndef LOCAL_FRAMEWORK
	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Try to get current cluster size\n");
	if (orm->subm_cmd_count) {
#ifdef SSH_SUPPORT
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "SSH support to get cluster size is not supported\n");
		return RMANAGER_ERROR;
#endif
		char workfile[OPH_MAX_STRING_SIZE], command[OPH_MAX_STRING_SIZE];
		snprintf(workfile, OPH_MAX_STRING_SIZE, OPH_RMANAGER_HOST_FILE, oph_txt_location, jobid);
		snprintf(command, OPH_MAX_STRING_SIZE, "%s %s", orm->subm_cmd_count, workfile);
		if (system(command)) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error during remote submission\n");
			return RMANAGER_ERROR;
		}
		FILE *file = fopen(workfile, "r");
		if (!file) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to open output file %s\n", workfile);
			return RMANAGER_ERROR;
		}
		char buffer[OPH_SHORT_STRING_SIZE];
		if (fgets(buffer, OPH_SHORT_STRING_SIZE, file))
			*size = (int) strtol(buffer, NULL, 10);
		fclose(file);
	}
	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Current cluster size is %d\n", *size);
#else
	UNUSED(jobid);
#endif
	return RMANAGER_SUCCESS;
}

int oph_form_subm_string(const char *request, const int ncores, char *outfile, short int interactive_subm, oph_rmanager * orm, int jobid, const char *username, const char *project,
			 const char *taskname, int wid, char **cmd, char type)
{
	if (!orm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}
	if (interactive_subm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Interactive submission is not longer supported\n");
		return RMANAGER_ERROR;
	}

	char *command = NULL;
	switch (type) {
		case 0:
			command = orm->subm_cmd_submit;
			break;
		case 1:
			if (!orm->subm_cmd_start) {
				pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Parameter '%s' is not set\n", SUBM_CMD_TO_START);
				return RMANAGER_ERROR;
			}
			command = orm->subm_cmd_start;
			break;
		case 2:
			if (!orm->subm_cmd_mount) {
				pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Parameter '%s' is not set\n", SUBM_CMD_TO_MOUNT);
				return RMANAGER_ERROR;
			}
			command = orm->subm_cmd_mount;
			break;
#ifdef MULTI_NODE_SUPPORT
		case 3:{
				if (!strcmp(scheduler, LOCAL_SCHEDULER)) {
					if (!orm->subm_cmd_deploy_workers_local_scheduler) {
						pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Parameter '%s' is not set\n", SUBM_CMD_TO_DEPLOY_WORKERS_LOCAL_SCHEDULER);
						return RMANAGER_ERROR;
					}
					command = orm->subm_cmd_deploy_workers_local_scheduler;
				} else if (!strcmp(scheduler, LSF_SCHEDULER)) {
					if (!orm->subm_cmd_deploy_workers_lsf_scheduler) {
						pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Parameter '%s' is not set\n", SUBM_CMD_TO_DEPLOY_WORKERS_LSF_SCHEDULER);
						return RMANAGER_ERROR;
					}
					command = orm->subm_cmd_deploy_workers_lsf_scheduler;
				} else if (!strcmp(scheduler, SLURM_SCHEDULER)) {
					if (!orm->subm_cmd_deploy_workers_slurm_scheduler) {
						pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Parameter '%s' is not set\n", SUBM_CMD_TO_DEPLOY_WORKERS_SLURM_SCHEDULER);
						return RMANAGER_ERROR;
					}
					command = orm->subm_cmd_deploy_workers_slurm_scheduler;
				} else {
					pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Scheduler \"%s\" is not supported\n", scheduler);
					return RMANAGER_ERROR;
				}

				int len = OPH_MAX_STRING_SIZE + strlen(request);
				if (!(*cmd = (char *) malloc(len * sizeof(char)))) {
					pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error allocating memory\n");
					return RMANAGER_MEMORY_ERROR;
				}

				sprintf(*cmd, "%s", command);
				pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Submission string:\n%s\n", *cmd);

				return RMANAGER_SUCCESS;
			}
#endif
		default:
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unknown command type\n");
			return RMANAGER_ERROR;
	}

	int len = OPH_MAX_STRING_SIZE + strlen(request);
	if (!(*cmd = (char *) malloc(len * sizeof(char)))) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error allocating memory\n");
		return RMANAGER_MEMORY_ERROR;
	}

	char subm_username[10 + (username ? strlen(username) : 0)];
	if (username && orm->subm_multiuser)
		sprintf(subm_username, OPH_RMANAGER_SUDO, username);
	else			// Skip username for backward compatibility
		*subm_username = 0;

	char _outfile[OPH_MAX_STRING_SIZE];
	snprintf(_outfile, OPH_MAX_STRING_SIZE, "%s", outfile);
	if (get_debug_level() != LOG_DEBUG)
		outfile = NULL;

	char internal_request = 0;
	if (!jobid) {
		pthread_mutex_lock(&global_flag);
		jobid = ++orm->subm_taskid;
		pthread_mutex_unlock(&global_flag);
		internal_request = 1;
	}

	sprintf(*cmd, "%s %s %s %s%d %d %s \"%s\" %s %s%s %d %s '%s' %s", orm->subm_prefix, subm_username, command, internal_request ? "_" : "", jobid, ncores, outfile ? outfile : OPH_NULL_FILENAME,
		request, ncores == 1 ? orm->subm_queue_high : orm->subm_queue_low, oph_server_port, OPH_RMANAGER_PREFIX, wid, project ? project : "''", taskname ? taskname : "", orm->subm_postfix);

	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Submission string:\n%s\n", *cmd);

	return RMANAGER_SUCCESS;
}

int free_oph_rmanager(oph_rmanager * orm)
{
	if (!orm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}
	if (orm->subm_cmd_submit) {
		free(orm->subm_cmd_submit);
		orm->subm_cmd_submit = NULL;
	}
	if (orm->subm_cmd_start) {
		free(orm->subm_cmd_start);
		orm->subm_cmd_start = NULL;
	}
	if (orm->subm_cmd_mount) {
		free(orm->subm_cmd_mount);
		orm->subm_cmd_mount = NULL;
	}
	if (orm->subm_cmd_cancel) {
		free(orm->subm_cmd_cancel);
		orm->subm_cmd_cancel = NULL;
	}
	if (orm->subm_cmd_stop) {
		free(orm->subm_cmd_stop);
		orm->subm_cmd_stop = NULL;
	}
	if (orm->subm_cmd_umount) {
		free(orm->subm_cmd_umount);
		orm->subm_cmd_umount = NULL;
	}
	if (orm->subm_cmd_check) {
		free(orm->subm_cmd_check);
		orm->subm_cmd_check = NULL;
	}
	if (orm->subm_cmd_count) {
		free(orm->subm_cmd_count);
		orm->subm_cmd_count = NULL;
	}
	if (orm->subm_cmd_cancel_all) {
		free(orm->subm_cmd_cancel_all);
		orm->subm_cmd_cancel_all = NULL;
	}
	if (orm->subm_cmd_progress) {
		free(orm->subm_cmd_progress);
		orm->subm_cmd_progress = NULL;
	}
	if (orm->subm_group) {
		free(orm->subm_group);
		orm->subm_group = NULL;
	}
	if (orm->subm_queue_high) {
		free(orm->subm_queue_high);
		orm->subm_queue_high = NULL;
	}
	if (orm->subm_queue_low) {
		free(orm->subm_queue_low);
		orm->subm_queue_low = NULL;
	}
	if (orm->subm_prefix) {
		free(orm->subm_prefix);
		orm->subm_prefix = NULL;
	}
	if (orm->subm_postfix) {
		free(orm->subm_postfix);
		orm->subm_postfix = NULL;
	}
	if (orm->subm_cmd_deploy_workers_local_scheduler) {
		free(orm->subm_cmd_deploy_workers_local_scheduler);
		orm->subm_cmd_deploy_workers_local_scheduler = NULL;
	}
	if (orm->subm_cmd_deploy_workers_lsf_scheduler) {
		free(orm->subm_cmd_deploy_workers_lsf_scheduler);
		orm->subm_cmd_deploy_workers_lsf_scheduler = NULL;
	}
	if (orm->subm_cmd_deploy_workers_slurm_scheduler) {
		free(orm->subm_cmd_deploy_workers_slurm_scheduler);
		orm->subm_cmd_deploy_workers_slurm_scheduler = NULL;
	}

	oph_detached_task *tmp;
	while (orm->subm_detached_tasks) {
		tmp = orm->subm_detached_tasks->next;
		free(orm->subm_detached_tasks);
		orm->subm_detached_tasks = tmp;
	}
	free(orm);
	return RMANAGER_SUCCESS;

}

int _oph_get_result_from_file(char *filename, char **response, pthread_mutex_t * flag)
{
	/* declare a file pointer */
	FILE *infile;
	long numbytes;

	/* open an existing file for reading */
	pmesg_safe(flag, LOG_DEBUG, __FILE__, __LINE__, "Opening file %s\n", filename);
	infile = fopen(filename, "r");

	/* quit if the file does not exist */
	if (infile == NULL) {
		pmesg_safe(flag, LOG_ERROR, __FILE__, __LINE__, "Unable to open output file: %s\n", filename);
		return RMANAGER_FILE_ERROR;
	}

	/* Get the number of bytes */
	fseek(infile, 0L, SEEK_END);
	numbytes = ftell(infile);

	/* reset the file position indicator to the beginning of the file */
	fseek(infile, 0L, SEEK_SET);

	/* grab sufficient memory for the buffer to hold the text */
	*response = (char *) malloc((1 + numbytes) * sizeof(char));

	/* memory error */
	if (*response == NULL) {
		fclose(infile);
		pmesg_safe(flag, LOG_ERROR, __FILE__, __LINE__, "Unable to alloc response\n");
		return RMANAGER_FILE_ERROR;
	}

	/* copy all the text into the buffer */
	size_t n = fread(*response, sizeof(char), numbytes, infile);
	(*response)[n] = '\0';
	fclose(infile);

	/* confirm we have read the file by outputing it to the console */
	pmesg_safe(flag, LOG_DEBUG, __FILE__, __LINE__, "The file called %s contains this text\n\n%s\n\n%d chars\n", filename, *response, n);

	return RMANAGER_SUCCESS;
}

int oph_get_result_from_file(char *filename, char **response)
{
	return _oph_get_result_from_file(filename, response, &global_flag);
}

int oph_get_result_from_file_unsafe(char *filename, char **response)
{
	return _oph_get_result_from_file(filename, response, NULL);
}

int oph_serve_request(const char *request, const int ncores, const char *sessionid, const char *markerid, const char *error, struct oph_plugin_data *state, int *odb_wf_id, int *task_id,
		      int *light_task_id, int *odb_jobid, int delay, char **response, char **jobid_response, enum oph__oph_odb_job_status *exit_code, int *exit_output, char *username, char *project,
		      char *taskname, int wid)
{
	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Incoming request '%s' to run job '%s#%s' with %d cores\n", request, sessionid, markerid, ncores);

	if (service_info) {
#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
		pthread_mutex_lock(&service_flag);
#endif
		service_info->incoming_tasks++;
#if defined(_POSIX_THREADS) || defined(_SC_THREADS)
		pthread_mutex_unlock(&service_flag);
#endif
	}

	if (exit_code)
		*exit_code = OPH_ODB_STATUS_COMPLETED;
	if (exit_output)
		*exit_output = 1;

	int _ncores = ncores;
	if (ncores < 1) {
		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "The job will be executed with 1 core!\n");
		_ncores = 1;
	}

	int result;
	if ((result =
	     oph_serve_known_operator(state, request, _ncores, sessionid, markerid, odb_wf_id, task_id, light_task_id, odb_jobid, response, jobid_response, exit_code, exit_output,
				      username, project, taskname)) != OPH_SERVER_UNKNOWN)
		return result;

	// Check for limits: known operators do not use resources
	if (oph_server_task_limit || oph_server_core_limit) {
		pthread_mutex_lock(&global_flag);
		while ((oph_server_task_limit && (oph_server_task_running >= oph_server_task_limit)) || (oph_server_core_limit && (oph_server_core_running + _ncores > oph_server_core_limit))) {
			pmesg(LOG_DEBUG, __FILE__, __LINE__, "Waiting for a task to be completed\n");
			pthread_cond_wait(&limit_flag, &global_flag);
			pmesg(LOG_DEBUG, __FILE__, __LINE__, "A task has been completed\n");
		}
		if (oph_server_task_limit)
			oph_server_task_running++;
		if (oph_server_core_limit)
			oph_server_core_running += _ncores;
		pthread_mutex_unlock(&global_flag);
	}

	char *cmd = NULL;

	if (!orm) {
		orm = (oph_rmanager *) malloc(sizeof(oph_rmanager));
		if (initialize_rmanager(orm)) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error on initialization OphidiaDB\n");
			return OPH_SERVER_ERROR;
		}
		if (oph_read_rmanager_conf(orm)) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error on read resource manager parameters\n");
			return OPH_SERVER_ERROR;
		}
	}

	char outfile[OPH_MAX_STRING_SIZE];
	snprintf(outfile, OPH_MAX_STRING_SIZE, OPH_NULL_FILENAME);
	char code[OPH_MAX_STRING_SIZE];
	if (!oph_get_session_code(sessionid, code)) {
		if (username && oph_subm_user && strcmp(username, oph_subm_user)) {
			snprintf(outfile, OPH_MAX_STRING_SIZE, "%s/%s", oph_txt_location, username);
			if (!oph_mkdir2(outfile, 0775) && orm->subm_group) {
				char group[1 + strlen(orm->subm_group)], *_group;
				strcpy(group, orm->subm_group);
				_group = strstr(group, "=");
				if (_group)
					_group++;
				else
					_group = group;
				if (strlen(_group) > 0) {
					struct group space, *gp = NULL;
					long size = sysconf(_SC_GETGR_R_SIZE_MAX);
					if (size) {
						char buf[size];
						if (!getgrnam_r(_group, &space, buf, sizeof buf, &gp) && gp && !chown(outfile, getuid(), gp->gr_gid))
							pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Group ownership on folder '%s' set to '%s'\n", outfile, _group);
					}
				}
			}
			snprintf(outfile, OPH_MAX_STRING_SIZE, "%s/" OPH_TXT_FILENAME, oph_txt_location, username, code, markerid);
		} else
			snprintf(outfile, OPH_MAX_STRING_SIZE, OPH_TXT_FILENAME, oph_txt_location, code, markerid);
	}
#ifdef LOCAL_FRAMEWORK
	char command[OPH_MAX_STRING_SIZE];
#ifdef USE_MPI
	snprintf(command, OPH_MAX_STRING_SIZE, "rm -f %s; mpirun -np %d %s \"%s\" >> %s 2>> %s", outfile, _ncores, oph_operator_client, request, outfile, outfile);
#else
	snprintf(command, OPH_MAX_STRING_SIZE, "rm -f %s; %s \"%s\" >> %s 2>> %s", outfile, oph_operator_client, request, outfile, outfile);
	if (ncores > 1)
		pmesg_safe(&global_flag, LOG_WARNING, __FILE__, __LINE__, "MPI is disabled. Only one core will be used\n");
#endif
	pmesg_safe(&global_flag, LOG_INFO, __FILE__, __LINE__, "Execute command: %s\n", command);
	if (oph_system(command, error, state, delay, 0, NULL, 0)) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error on executing the command\n");
		if (cmd) {
			free(cmd);
			cmd = NULL;
		}
		return OPH_SERVER_ERROR;
	}
#else
	if (oph_form_subm_string(request, _ncores, outfile, 0, orm, odb_jobid ? *odb_jobid : 0, username, project, taskname, wid, &cmd, 0)) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error on forming submission string\n");
		if (cmd) {
			free(cmd);
			cmd = NULL;
		}
		return OPH_SERVER_ERROR;
	}
	if (oph_system(cmd, error, state, delay, 0, NULL, 0)) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Error during remote submission\n");
		if (cmd) {
			free(cmd);
			cmd = NULL;
		}
		return OPH_SERVER_ERROR;
	}
#endif
	if (cmd) {
		free(cmd);
		cmd = NULL;
	}

	return OPH_SERVER_OK;
}

int oph_detach_task(int id)
{
	if (!orm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}

	if (id) {
		oph_detached_task *tmp = (oph_detached_task *) malloc(sizeof(oph_detached_task));
		if (!tmp) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Memory error\n");
			return RMANAGER_MEMORY_ERROR;
		}
		tmp->id = id;
		tmp->next = orm->subm_detached_tasks;
		orm->subm_detached_tasks = tmp;
		pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Task %d added to list of detached tasks\n", id);
	}

	return RMANAGER_SUCCESS;
}

int oph_is_detached_task(int id)
{
	if (!orm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}

	int res = 0;
	if (id) {
		oph_detached_task *tmp = orm->subm_detached_tasks;
		while (tmp && (tmp->id != id))
			tmp = tmp->next;
		if (tmp)
			res = 1;
	}

	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Task %d %sis in list of detached tasks\n", id, res ? "" : "not ");

	return res;
}

int oph_remove_detached_task(int id)
{
	if (!orm) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Null input parameter\n");
		return RMANAGER_NULL_PARAM;
	}

	if (id) {
		oph_detached_task *tmp = orm->subm_detached_tasks, *prev = NULL;
		while (tmp && (tmp->id != id))
			tmp = tmp->next;
		if (tmp) {
			if (prev)
				prev->next = tmp->next;
			else
				orm->subm_detached_tasks = tmp->next;
			free(tmp);
			pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Task %d removed from list of detached tasks\n", id);
		}
	}

	return RMANAGER_SUCCESS;
}

#ifdef MULTI_NODE_SUPPORT
int get_single_param_callback(void *number, int argc, char **argv, char **azColName)
{
	UNUSED(azColName);

	int *result = (int *) number;

	if (argv[0])
		*result = atoi(argv[0]);

	return 0;
}

int oph_get_workers_number_by_status(int *workers_number, char *status)
{
	if (!workers_number || !status)
		return RMANAGER_NULL_PARAM;
	*workers_number = 0;

	sqlite3 *db = NULL;
	char *err_msg = 0;

	while (sqlite3_open_v2(db_location, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK)
		pmesg(LOG_ERROR, __FILE__, __LINE__, "Cannot open database. Database %s is locked or does not exist\n", db_location);

	int neededSize = snprintf(NULL, 0, "SELECT COUNT(id_worker) FROM worker WHERE status=\"%s\";", status);
	char *get_workers_info = (char *) malloc(neededSize + 1);
	snprintf(get_workers_info, neededSize + 1, "SELECT COUNT(id_worker) FROM worker WHERE status=\"%s\";", status);

	while (sqlite3_exec(db, get_workers_info, get_single_param_callback, workers_number, &err_msg) != SQLITE_OK)
		pmesg(LOG_ERROR, __FILE__, __LINE__, "SQL error on select query: %s\n", err_msg);
	free(get_workers_info);

	sqlite3_free(err_msg);
	sqlite3_close(db);

	return RMANAGER_SUCCESS;
}

int get_count_callback(void *number, int argc, char **argv, char **azColName)
{
	UNUSED(azColName);

	int *result = (int *) number;

	if (argv[0])
		*result += 1;

	return 0;
}

int get_workers_list_by_query_callback(void *array, int argc, char **argv, char **azColName)
{
	UNUSED(argc);
	UNUSED(azColName);

	worker_struct *list = (worker_struct *) array;

	if (!argv[0] || !argv[1] || !argv[2] || !argv[3] || !argv[4] || !argv[5])
		return 1;
	else {
		int neededSize = snprintf(NULL, 0, "%s", argv[0]);
		list[out_list_len].id_worker = (char *) malloc(neededSize + 1);
		snprintf(list[out_list_len].id_worker, neededSize + 1, "%s", argv[0]);

		neededSize = snprintf(NULL, 0, "%s", argv[1]);
		list[out_list_len].hostname = (char *) malloc(neededSize + 1);
		snprintf(list[out_list_len].hostname, neededSize + 1, "%s", argv[1]);

		neededSize = snprintf(NULL, 0, "%s", argv[2]);
		list[out_list_len].port = (char *) malloc(neededSize + 1);
		snprintf(list[out_list_len].port, neededSize + 1, "%s", argv[2]);

		neededSize = snprintf(NULL, 0, "%s", argv[3]);
		list[out_list_len].delete_queue_name = (char *) malloc(neededSize + 1);
		snprintf(list[out_list_len].delete_queue_name, neededSize + 1, "%s", argv[3]);

		neededSize = snprintf(NULL, 0, "%s", argv[4]);
		list[out_list_len].status = (char *) malloc(neededSize + 1);
		snprintf(list[out_list_len].status, neededSize + 1, "%s", argv[4]);

		neededSize = snprintf(NULL, 0, "%s", argv[5]);
		list[out_list_len].pid = (char *) malloc(neededSize + 1);
		snprintf(list[out_list_len].pid, neededSize + 1, "%s", argv[5]);

		out_list_len++;
	}

	return 0;
}

int oph_get_workers_list_by_query_status(worker_struct ** out_list, int *len, char *query)
{
	sqlite3 *db = NULL;
	char *err_msg = 0;

	while (sqlite3_open_v2(db_location, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK)
		pmesg(LOG_ERROR, __FILE__, __LINE__, "Cannot open database. Database %s is locked or does not exist\n", db_location);

	int workers_number = 0;
	if (sqlite3_exec(db, query, get_count_callback, &workers_number, &err_msg) != SQLITE_OK) {
		pmesg(LOG_ERROR, __FILE__, __LINE__, "SQL error on select query: %s\n", err_msg);
		return RMANAGER_ERROR;
	}

	if (workers_number == 0)
		return RMANAGER_SUCCESS;

	out_list_len = 0;
	*out_list = (worker_struct *) malloc(sizeof(worker_struct) * workers_number);

	pmesg_safe(&global_flag, LOG_DEBUG, __FILE__, __LINE__, "Executing query: %s\n", query);

	while (sqlite3_exec(db, query, get_workers_list_by_query_callback, *out_list, &err_msg) != SQLITE_OK)
		pmesg(LOG_ERROR, __FILE__, __LINE__, "SQL error on select query: %s\n", err_msg);

	sqlite3_free(err_msg);
	sqlite3_close(db);

	*len = out_list_len;

	return RMANAGER_SUCCESS;
}

int oph_undeploy_worker(worker_struct worker)
{
	if (!worker.hostname || !worker.port || !worker.delete_queue_name)
		return RMANAGER_ERROR;

	amqp_connection_state_t conn;
	amqp_channel_t channel = 1;

	rabbitmq_publish_connection(&conn, channel, worker.hostname, worker.port, rabbitmq_username, rabbitmq_password, worker.delete_queue_name);

	amqp_basic_properties_t props;
	props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
	props.content_type = amqp_cstring_bytes("text/plain");
	props.delivery_mode = AMQP_DELIVERY_PERSISTENT;

	int status = amqp_basic_publish(conn,
					channel,
					amqp_cstring_bytes(""),
					amqp_cstring_bytes(worker.delete_queue_name),
					0,	// mandatory (message must be routed to a queue)
					0,	// immediate (message must be delivered to a consumer immediately)
					&props,	// properties
					amqp_cstring_bytes(WORKER_SHUTDOWN_MESSAGE));

	close_rabbitmq_connection(conn, channel);

	if (status == AMQP_STATUS_OK)
		pmesg(LOG_DEBUG, __FILE__, __LINE__, "Message %s has been sent to %s on ip_address %s and port %s\n", WORKER_SHUTDOWN_MESSAGE, worker.delete_queue_name, worker.hostname, worker.port);
	else {
		pmesg(LOG_ERROR, __FILE__, __LINE__, "Cannot send message to %s on ip_address %s and port %s\n", worker.delete_queue_name, worker.hostname, worker.port);

		return RMANAGER_ERROR;
	}

	return RMANAGER_SUCCESS;
}

int oph_undeploy_workers(int w_number)
{
	worker_struct *idle_list = NULL;
	int list_len = 0;
	if (oph_get_workers_list_by_query_status(&idle_list, &list_len, "SELECT * FROM worker WHERE status=\"up\" and id_worker NOT IN (select id_worker from job);")) {
		pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to retrieve number of idle workers!\n");
		return RMANAGER_ERROR;
	}

	if (list_len == 0) {	// NO IDLE WORKERS. UNDEPLOY WORKERS FROM THE ACTIVE ONES
		worker_struct *kill_list = NULL;
		int kill_list_len = 0;

		if (oph_get_workers_list_by_query_status(&kill_list, &kill_list_len, "SELECT * FROM worker WHERE id_worker IN (select id_worker from job);")) {
			pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to retrieve active workers to kill\n");
			return RMANAGER_ERROR;
		}

		int ii;
		for (ii = kill_list_len - 1; ii >= (kill_list_len - w_number); ii--) {
			if (oph_undeploy_worker(kill_list[ii])) {
				pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to undeploy active workers\n");
				return RMANAGER_ERROR;
			}
		}

		for (ii = 0; ii < kill_list_len; ii++) {
			if (kill_list[ii].id_worker)
				free(kill_list[ii].id_worker);
			if (kill_list[ii].hostname)
				free(kill_list[ii].hostname);
			if (kill_list[ii].port)
				free(kill_list[ii].port);
			if (kill_list[ii].delete_queue_name)
				free(kill_list[ii].delete_queue_name);
			if (kill_list[ii].status)
				free(kill_list[ii].status);
			if (kill_list[ii].pid)
				free(kill_list[ii].pid);
		}
		free(kill_list);
	} else {		// THERE ARE IDLE WORKERS
		if (w_number <= list_len) {	// UNDEPLOY n_workers WORKERS FROM THE IDLE ONES
			int ii;
			for (ii = list_len - 1; ii >= (list_len - w_number); ii--) {
				if (oph_undeploy_worker(idle_list[ii])) {
					pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to undeploy idle workers\n");
					return RMANAGER_ERROR;
				}
			}
		} else {	// UNDEPLOY ALL IDLE WORKERS AND THE REST FROM THE ACTIVE ONES
			int ii;
			for (ii = 0; ii < list_len; ii++) {
				if (oph_undeploy_worker(idle_list[ii])) {
					pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to undeploy workers\n");
					return RMANAGER_ERROR;
				}
			}

			// UNDEPLOYING w_number-list_len ACTIVE WORKERS
			worker_struct *kill_list = NULL;
			int kill_list_len = 0;

			if (oph_get_workers_list_by_query_status(&kill_list, &kill_list_len, "SELECT * FROM worker WHERE id_worker IN (select id_worker from job);")) {
				pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to retrieve number of active workers!\n");
				return RMANAGER_ERROR;
			}

			for (ii = kill_list_len - 1; ii >= (kill_list_len - w_number + list_len); ii--) {
				if (oph_undeploy_worker(kill_list[ii])) {
					pmesg_safe(&global_flag, LOG_ERROR, __FILE__, __LINE__, "Unable to undeploy workers\n");
					return RMANAGER_ERROR;
				}
			}

			for (ii = 0; ii < kill_list_len; ii++) {
				if (kill_list[ii].id_worker)
					free(kill_list[ii].id_worker);
				if (kill_list[ii].hostname)
					free(kill_list[ii].hostname);
				if (kill_list[ii].port)
					free(kill_list[ii].port);
				if (kill_list[ii].delete_queue_name)
					free(kill_list[ii].delete_queue_name);
				if (kill_list[ii].status)
					free(kill_list[ii].status);
				if (kill_list[ii].pid)
					free(kill_list[ii].pid);
			}
			free(kill_list);
		}
	}

	int ii;
	for (ii = 0; ii < list_len; ii++) {
		if (idle_list[ii].id_worker)
			free(idle_list[ii].id_worker);
		if (idle_list[ii].hostname)
			free(idle_list[ii].hostname);
		if (idle_list[ii].port)
			free(idle_list[ii].port);
		if (idle_list[ii].delete_queue_name)
			free(idle_list[ii].delete_queue_name);
		if (idle_list[ii].status)
			free(idle_list[ii].status);
		if (idle_list[ii].pid)
			free(idle_list[ii].pid);
	}
	free(idle_list);

	return RMANAGER_SUCCESS;

}
#endif

// Unsafe
int oph_load_datacube_status(int *jobs, int *tot, int *current, int size, int jobid)
{
	if (!jobs || !tot || !current)
		return RMANAGER_NULL_PARAM;

	char *id_list = NULL;
	if (size && orm->subm_cmd_progress) {
		int k, n;
		char *prev;
		for (k = 0; k < size; ++k) {
			tot[k] = current[k] = 0;
			if (!jobs[k])
				continue;
			if (id_list) {
				prev = id_list;
				n = asprintf(&id_list, "%s,%d", prev, jobs[k]);
			} else
				n = asprintf(&id_list, "%d", jobs[k]);
		}
		if (id_list) {
			char workfile[OPH_MAX_STRING_SIZE], command[OPH_MAX_STRING_SIZE];
			snprintf(workfile, OPH_MAX_STRING_SIZE, OPH_RMANAGER_PROGRESS_FILE, oph_txt_location, jobid);
			snprintf(command, OPH_MAX_STRING_SIZE, "%s %s %s", orm->subm_cmd_progress, id_list, workfile);
			free(id_list);
			if (system(command)) {
				pmesg(LOG_ERROR, __FILE__, __LINE__, "Error during remote submission\n");
				return RMANAGER_ERROR;
			}
			FILE *file = fopen(workfile, "r");
			if (!file) {
				pmesg(LOG_ERROR, __FILE__, __LINE__, "Unable to open output file %s\n", workfile);
				return RMANAGER_ERROR;
			}
			int wid, pid;
			char buffer[OPH_SHORT_STRING_SIZE], *pch, *save_pointer = NULL;
			while (fgets(buffer, OPH_SHORT_STRING_SIZE, file)) {
				pch = strtok_r(buffer, " ", &save_pointer);
				if (!pch)
					continue;
				jobid = (int) strtol(pch, NULL, 10);
				pch = strtok_r(NULL, " ", &save_pointer);
				if (!pch)
					continue;
				wid = (int) strtol(pch, NULL, 10);
				pch = strtok_r(NULL, " ", &save_pointer);
				if (!pch)
					continue;
				pid = (int) strtol(pch, NULL, 10);
				for (n = 0; n < size; ++n) {
					if (++k >= size)
						k = 0;
					if (!jobs[k] || (jobs[k] != jobid))
						continue;
					tot[k] = wid;
					current[k] = pid;
					break;
				}
			}
			fclose(file);
		}
	}

	return RMANAGER_SUCCESS;
}
