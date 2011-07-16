/* Copyright (C) 2009  Pierre-Marc Fournier, Philippe Proulx-Barrette
 * Copyright (C) 2011  Ericsson AB
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>

#include "ustcomm.h"
#include "ust/ustctl.h"
#include "usterr.h"

static int do_cmd(int sock,
		  const struct ustcomm_header *req_header,
		  const char *req_data,
		  struct ustcomm_header *res_header,
		  char **res_data)
{
	int result, saved_errno = 0;
	char *recv_buf;

	recv_buf = zmalloc(USTCOMM_BUFFER_SIZE);
	if (!recv_buf) {
		saved_errno = ENOMEM;
		goto out;
	}

	result = ustcomm_req(sock, req_header, req_data, res_header, recv_buf);
	if (result > 0) {
		saved_errno = -res_header->result;
		if (res_header->size == 0 || saved_errno > 0) {
			free(recv_buf);
		} else {
			if (res_data) {
				*res_data = recv_buf;
			} else {
				free(recv_buf);
			}
		}
	} else {
		ERR("ustcomm req failed");
		if (result == 0) {
			saved_errno = ENOTCONN;
		} else {
			saved_errno = -result;
		}
		free(recv_buf);
	}

out:
	errno = saved_errno;
	if (errno) {
		return -1;
	}

	return 0;
}

int ustctl_connect_pid(pid_t pid)
{
	int sock;

	if (ustcomm_connect_app(pid, &sock)) {
		ERR("could not connect to PID %u", (unsigned int) pid);
		errno = ENOTCONN;
		return -1;
	}

	return sock;
}

pid_t *ustctl_get_online_pids(void)
{
	struct dirent *dirent;
	DIR *dir;
	unsigned int ret_size = 1 * sizeof(pid_t), i = 0;

	dir = opendir(SOCK_DIR);
	if (!dir) {
		return NULL;
	}

	pid_t *ret = (pid_t *) malloc(ret_size);

	while ((dirent = readdir(dir))) {
		if (!strcmp(dirent->d_name, ".") ||
		    !strcmp(dirent->d_name, "..")) {

			continue;
		}

		if (dirent->d_type != DT_DIR &&
		    !!strcmp(dirent->d_name, "ust-consumer")) {

			sscanf(dirent->d_name, "%u", (unsigned int *) &ret[i]);
			/* FIXME: Here we previously called pid_is_online, which
			 * always returned 1, now I replaced it with just 1.
			 * We need to figure out an intelligent way of solving
			 * this, maybe connect-disconnect.
			 */
			if (1) {
				ret_size += sizeof(pid_t);
				ret = (pid_t *) realloc(ret, ret_size);
				++i;
			}
		}
	}

	ret[i] = 0; /* Array end */

	if (ret[0] == 0) {
		/* No PID at all */
		free(ret);
		return NULL;
	}

	closedir(dir);
	return ret;
}

/**
 * Sets marker state (USTCTL_MS_ON or USTCTL_MS_OFF).
 *
 * @param mn	Marker name
 * @param state	Marker's new state
 * @param pid	Traced process ID
 * @return	0 if successful, or errors {USTCTL_ERR_GEN, USTCTL_ERR_ARG}
 */
int ustctl_set_marker_state(int sock, const char *trace, const char *channel,
			    const char *marker, int state)
{
	struct ustcomm_header req_header, res_header;
	struct ustcomm_marker_info marker_inf;
	int result;

	result = ustcomm_pack_marker_info(&req_header,
					  &marker_inf,
					  trace,
					  channel,
					  marker);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	req_header.command = state ? ENABLE_MARKER : DISABLE_MARKER;

	return do_cmd(sock, &req_header, (char *)&marker_inf,
		      &res_header, NULL);
}

/**
 * Set subbuffer size.
 *
 * @param channel_size	Channel name and size
 * @param pid		Traced process ID
 * @return		0 if successful, or error
 */
int ustctl_set_subbuf_size(int sock, const char *trace, const char *channel,
			   unsigned int subbuf_size)
{
	struct ustcomm_header req_header, res_header;
	struct ustcomm_channel_info ch_inf;
	int result;

	result = ustcomm_pack_channel_info(&req_header,
					   &ch_inf,
					   trace,
					   channel);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	req_header.command = SET_SUBBUF_SIZE;
	ch_inf.subbuf_size = subbuf_size;

	return do_cmd(sock, &req_header, (char *)&ch_inf,
		      &res_header, NULL);
}

/**
 * Set subbuffer num.
 *
 * @param channel_num	Channel name and num
 * @param pid		Traced process ID
 * @return		0 if successful, or error
 */
int ustctl_set_subbuf_num(int sock, const char *trace, const char *channel,
			  unsigned int num)
{
	struct ustcomm_header req_header, res_header;
	struct ustcomm_channel_info ch_inf;
	int result;

	result = ustcomm_pack_channel_info(&req_header,
					   &ch_inf,
					   trace,
					   channel);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	req_header.command = SET_SUBBUF_NUM;
	ch_inf.subbuf_num = num;

	return do_cmd(sock, &req_header, (char *)&ch_inf,
		      &res_header, NULL);

}


static int ustctl_get_subbuf_num_size(int sock, const char *trace, const char *channel,
				      int *num, int *size)
{
	struct ustcomm_header req_header, res_header;
	struct ustcomm_channel_info ch_inf, *ch_inf_res;
	int result;


	result = ustcomm_pack_channel_info(&req_header,
					   &ch_inf,
					   trace,
					   channel);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	req_header.command = GET_SUBBUF_NUM_SIZE;

	result = do_cmd(sock, &req_header, (char *)&ch_inf,
			&res_header, (char **)&ch_inf_res);
	if (result < 0) {
		return -1;
	}

	*num = ch_inf_res->subbuf_num;
	*size = ch_inf_res->subbuf_size;

	free(ch_inf_res);

	return 0;
}

/**
 * Get subbuffer num.
 *
 * @param channel	Channel name
 * @param pid		Traced process ID
 * @return		subbuf cnf if successful, or error
 */
int ustctl_get_subbuf_num(int sock, const char *trace, const char *channel)
{
	int num, size, result;

	result = ustctl_get_subbuf_num_size(sock, trace, channel,
					    &num, &size);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	return num;
}

/**
 * Get subbuffer size.
 *
 * @param channel	Channel name
 * @param pid		Traced process ID
 * @return		subbuf size if successful, or error
 */
int ustctl_get_subbuf_size(int sock, const char *trace, const char *channel)
{
	int num, size, result;

	result = ustctl_get_subbuf_num_size(sock, trace, channel,
					    &num, &size);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	return size;
}

static int do_trace_cmd(int sock, const char *trace, int command)
{
	struct ustcomm_header req_header, res_header;
	struct ustcomm_single_field trace_inf;
	int result;

	result = ustcomm_pack_single_field(&req_header,
					   &trace_inf,
					   trace);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	req_header.command = command;

	return do_cmd(sock, &req_header, (char *)&trace_inf, &res_header, NULL);
}

/**
 * Destroys an UST trace according to a PID.
 *
 * @param pid	Traced process ID
 * @return	0 if successful, or error USTCTL_ERR_GEN
 */
int ustctl_destroy_trace(int sock, const char *trace)
{
	return do_trace_cmd(sock, trace, DESTROY_TRACE);
}

/**
 * Starts an UST trace (and setups it) according to a PID.
 *
 * @param pid	Traced process ID
 * @return	0 if successful, or error USTCTL_ERR_GEN
 */
int ustctl_setup_and_start(int sock, const char *trace)
{
	return do_trace_cmd(sock, trace, START);
}

/**
 * Creates an UST trace according to a PID.
 *
 * @param pid	Traced process ID
 * @return	0 if successful, or error USTCTL_ERR_GEN
 */
int ustctl_create_trace(int sock, const char *trace)
{
	return do_trace_cmd(sock, trace, CREATE_TRACE);
}

/**
 * Starts an UST trace according to a PID.
 *
 * @param pid	Traced process ID
 * @return	0 if successful, or error USTCTL_ERR_GEN
 */
int ustctl_start_trace(int sock, const char *trace)
{
	return do_trace_cmd(sock, trace, START_TRACE);
}

/**
 * Alloc an UST trace according to a PID.
 *
 * @param pid	Traced process ID
 * @return	0 if successful, or error USTCTL_ERR_GEN
 */
int ustctl_alloc_trace(int sock, const char *trace)
{
	return do_trace_cmd(sock, trace, ALLOC_TRACE);
}


int ustctl_force_switch(int sock, const char *trace)
{
	return do_trace_cmd(sock, trace, FORCE_SUBBUF_SWITCH);
}

/**
 * Stops an UST trace according to a PID.
 *
 * @param pid	Traced process ID
 * @return	0 if successful, or error USTCTL_ERR_GEN
 */
int ustctl_stop_trace(int sock, const char *trace)
{
	return do_trace_cmd(sock, trace, STOP_TRACE);
}

/**
 * Counts newlines ('\n') in a string.
 *
 * @param str	String to search in
 * @return	Total newlines count
 */
unsigned int ustctl_count_nl(const char *str)
{
	unsigned int i = 0, tot = 0;

	while (str[i] != '\0') {
		if (str[i] == '\n') {
			++tot;
		}
		++i;
	}

	return tot;
}

/**
 * Frees a CMSF array.
 *
 * @param cmsf	CMSF array to free
 * @return	0 if successful, or error USTCTL_ERR_ARG
 */
int ustctl_free_cmsf(struct marker_status *cmsf)
{
	if (cmsf == NULL) {
		return USTCTL_ERR_ARG;
	}

	unsigned int i = 0;
	while (cmsf[i].channel != NULL) {
		free(cmsf[i].channel);
		free(cmsf[i].marker);
		free(cmsf[i].fs);
		++i;
	}
	free(cmsf);

	return 0;
}

/**
 * Gets channel/marker/state/format string for a given PID.
 *
 * @param cmsf	Pointer to CMSF array to be filled (callee allocates, caller
 *		frees with `ustctl_free_cmsf')
 * @param pid	Targeted PID
 * @return	0 if successful, or -1 on error
 */
int ustctl_get_cmsf(int sock, struct marker_status **cmsf)
{
	struct ustcomm_header req_header, res_header;
	char *big_str = NULL;
	int result;
	struct marker_status *tmp_cmsf = NULL;
	unsigned int i = 0, cmsf_ind = 0;

	if (cmsf == NULL) {
		return -1;
	}

	req_header.command = LIST_MARKERS;
	req_header.size = 0;

	result = ustcomm_send(sock, &req_header, NULL);
	if (result <= 0) {
		PERROR("error while requesting markers list");
		return -1;
	}

	result = ustcomm_recv_alloc(sock, &res_header, &big_str);
	if (result <= 0) {
		ERR("error while receiving markers list");
		return -1;
	}

	tmp_cmsf = (struct marker_status *) zmalloc(sizeof(struct marker_status) *
						    (ustctl_count_nl(big_str) + 1));
	if (tmp_cmsf == NULL) {
		ERR("Failed to allocate CMSF array");
		return -1;
	}

	/* Parse received reply string (format: "[chan]/[mark] [st] [fs]"): */
	while (big_str[i] != '\0') {
		char state;

		sscanf(big_str + i, "marker: %a[^/]/%a[^ ] %c %a[^\n]",
		       &tmp_cmsf[cmsf_ind].channel,
		       &tmp_cmsf[cmsf_ind].marker,
		       &state,
		       &tmp_cmsf[cmsf_ind].fs);
		tmp_cmsf[cmsf_ind].state = (state == USTCTL_MS_CHR_ON ?
					    USTCTL_MS_ON : USTCTL_MS_OFF); /* Marker state */

		while (big_str[i] != '\n') {
			++i; /* Go to next '\n' */
		}
		++i; /* Skip current pointed '\n' */
		++cmsf_ind;
	}
	tmp_cmsf[cmsf_ind].channel = NULL;
	tmp_cmsf[cmsf_ind].marker = NULL;
	tmp_cmsf[cmsf_ind].fs = NULL;

	*cmsf = tmp_cmsf;

	free(big_str);
	return 0;
}

/**
 * Frees a TES array.
 *
 * @param tes	TES array to free
 * @return	0 if successful, or error USTCTL_ERR_ARG
 */
int ustctl_free_tes(struct trace_event_status *tes)
{
	if (tes == NULL) {
		return USTCTL_ERR_ARG;
	}

	unsigned int i = 0;
	while (tes[i].name != NULL) {
		free(tes[i].name);
		++i;
	}
	free(tes);

	return 0;
}

/**
 * Gets trace_events string for a given PID.
 *
 * @param tes	Pointer to TES array to be filled (callee allocates, caller
 *		frees with `ustctl_free_tes')
 * @param pid	Targeted PID
 * @return	0 if successful, or -1 on error
 */
int ustctl_get_tes(int sock, struct trace_event_status **tes)
{
	struct ustcomm_header req_header, res_header;
	char *big_str = NULL;
	int result;
	struct trace_event_status *tmp_tes = NULL;
	unsigned int i = 0, tes_ind = 0;

	if (tes == NULL) {
		return -1;
	}

	req_header.command = LIST_TRACE_EVENTS;
	req_header.size = 0;

	result = ustcomm_send(sock, &req_header, NULL);
	if (result != 1) {
		ERR("error while requesting trace_event list");
		return -1;
	}

	result = ustcomm_recv_alloc(sock, &res_header, &big_str);
	if (result != 1) {
		ERR("error while receiving markers list");
		return -1;
	}

	tmp_tes = (struct trace_event_status *)
		zmalloc(sizeof(struct trace_event_status) *
			(ustctl_count_nl(big_str) + 1));
	if (tmp_tes == NULL) {
		ERR("Failed to allocate TES array");
		return -1;
	}

	/* Parse received reply string (format: "[name]"): */
	while (big_str[i] != '\0') {
		sscanf(big_str + i, "trace_event: %a[^\n]",
		       &tmp_tes[tes_ind].name);
		while (big_str[i] != '\n') {
			++i; /* Go to next '\n' */
		}
		++i; /* Skip current pointed '\n' */
		++tes_ind;
	}
	tmp_tes[tes_ind].name = NULL;

	*tes = tmp_tes;

	free(big_str);
	return 0;
}

/**
 * Set sock path
 *
 * @param sock_path	Sock path
 * @param pid		Traced process ID
 * @return		0 if successful, or error
 */
int ustctl_set_sock_path(int sock, const char *sock_path)
{
	int result;
	struct ustcomm_header req_header, res_header;
	struct ustcomm_single_field sock_path_msg;

	result = ustcomm_pack_single_field(&req_header,
					   &sock_path_msg,
					   sock_path);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	req_header.command = SET_SOCK_PATH;

	return do_cmd(sock, &req_header, (char *)&sock_path_msg,
		      &res_header, NULL);
}

/**
 * Get sock path
 *
 * @param sock_path	Pointer to where the sock path will be returned
 * @param pid		Traced process ID
 * @return		0 if successful, or error
 */
int ustctl_get_sock_path(int sock, char **sock_path)
{
	int result;
	struct ustcomm_header req_header, res_header;
	struct ustcomm_single_field *sock_path_msg;

	req_header.command = GET_SOCK_PATH;
	req_header.size = 0;

	result = do_cmd(sock, &req_header, NULL, &res_header,
			(char **)&sock_path_msg);
	if (result < 0) {
		return -1;
	}

	result = ustcomm_unpack_single_field(sock_path_msg);
	if (result < 0) {
		return result;
	}

	*sock_path = strdup(sock_path_msg->field);

	free(sock_path_msg);

	return 0;
}
