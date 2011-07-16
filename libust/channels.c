/*
 * ltt/ltt-channels.c
 *
 * (C) Copyright 2008 - Mathieu Desnoyers (mathieu.desnoyers@polymtl.ca)
 *
 * LTTng channel management.
 *
 * Author:
 *	Mathieu Desnoyers (mathieu.desnoyers@polymtl.ca)
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

#include <stdlib.h>
#include <ust/marker.h>
#include <ust/marker-internal.h>
#include "channels.h"
#include "usterr_signal_safe.h"

/*
 * ltt_channel_mutex may be nested inside the LTT trace mutex.
 * ltt_channel_mutex mutex may be nested inside markers mutex.
 */
static DEFINE_MUTEX(ltt_channel_mutex);
static CDS_LIST_HEAD(ltt_channels);
/*
 * Index of next channel in array. Makes sure that as long as a trace channel is
 * allocated, no array index will be re-used when a channel is freed and then
 * another channel is allocated. This index is cleared and the array indexeds
 * get reassigned when the index_urcu_ref goes back to 0, which indicates that no
 * more trace channels are allocated.
 */
static unsigned int free_index;
static struct urcu_ref index_urcu_ref;	/* Keeps track of allocated trace channels */

int ust_channels_overwrite_by_default = 0;
int ust_channels_request_collection_by_default = 1;

static struct ltt_channel_setting *lookup_channel(const char *name)
{
	struct ltt_channel_setting *iter;

	cds_list_for_each_entry(iter, &ltt_channels, list)
		if (strcmp(name, iter->name) == 0)
			return iter;
	return NULL;
}

/*
 * Must be called when channel refcount falls to 0 _and_ also when the last
 * trace is freed. This function is responsible for compacting the channel and
 * event IDs when no users are active.
 *
 * Called with lock_markers() and channels mutex held.
 */
static void release_channel_setting(struct urcu_ref *urcu_ref)
{
	struct ltt_channel_setting *setting = _ust_container_of(urcu_ref,
		struct ltt_channel_setting, urcu_ref);
	struct ltt_channel_setting *iter;

	if (uatomic_read(&index_urcu_ref.refcount) == 0
	    && uatomic_read(&setting->urcu_ref.refcount) == 0) {
		cds_list_del(&setting->list);
		free(setting);

		free_index = 0;
		cds_list_for_each_entry(iter, &ltt_channels, list) {
			iter->index = free_index++;
			iter->free_event_id = 0;
		}
		/* FIXME: why not run this? */
//ust//		markers_compact_event_ids();
	}
}

/*
 * Perform channel index compaction when the last trace channel is freed.
 *
 * Called with lock_markers() and channels mutex held.
 */
static void release_trace_channel(struct urcu_ref *urcu_ref)
{
	struct ltt_channel_setting *iter, *n;

	cds_list_for_each_entry_safe(iter, n, &ltt_channels, list)
		release_channel_setting(&iter->urcu_ref);
}

/**
 * ltt_channels_register - Register a trace channel.
 * @name: channel name
 *
 * Uses refcounting.
 */
int ltt_channels_register(const char *name)
{
	struct ltt_channel_setting *setting;
	int ret = 0;

	pthread_mutex_lock(&ltt_channel_mutex);
	setting = lookup_channel(name);
	if (setting) {
		if (uatomic_read(&setting->urcu_ref.refcount) == 0)
			goto init_urcu_ref;
		else {
			urcu_ref_get(&setting->urcu_ref);
			goto end;
		}
	}
	setting = zmalloc(sizeof(*setting));
	if (!setting) {
		ret = -ENOMEM;
		goto end;
	}
	cds_list_add(&setting->list, &ltt_channels);
	strncpy(setting->name, name, PATH_MAX-1);
	setting->index = free_index++;
init_urcu_ref:
	urcu_ref_init(&setting->urcu_ref);
end:
	pthread_mutex_unlock(&ltt_channel_mutex);
	return ret;
}

/**
 * ltt_channels_unregister - Unregister a trace channel.
 * @name: channel name
 *
 * Must be called with markers mutex held.
 */
int ltt_channels_unregister(const char *name)
{
	struct ltt_channel_setting *setting;
	int ret = 0;

	pthread_mutex_lock(&ltt_channel_mutex);
	setting = lookup_channel(name);
	if (!setting || uatomic_read(&setting->urcu_ref.refcount) == 0) {
		ret = -ENOENT;
		goto end;
	}
	urcu_ref_put(&setting->urcu_ref, release_channel_setting);
end:
	pthread_mutex_unlock(&ltt_channel_mutex);
	return ret;
}

/**
 * ltt_channels_set_default - Set channel default behavior.
 * @name: default channel name
 * @subbuf_size: size of the subbuffers
 * @subbuf_cnt: number of subbuffers
 */
int ltt_channels_set_default(const char *name,
			     unsigned int subbuf_size,
			     unsigned int subbuf_cnt)
{
	struct ltt_channel_setting *setting;
	int ret = 0;

	pthread_mutex_lock(&ltt_channel_mutex);
	setting = lookup_channel(name);
	if (!setting || uatomic_read(&setting->urcu_ref.refcount) == 0) {
		ret = -ENOENT;
		goto end;
	}
	setting->subbuf_size = subbuf_size;
	setting->subbuf_cnt = subbuf_cnt;
end:
	pthread_mutex_unlock(&ltt_channel_mutex);
	return ret;
}

/**
 * ltt_channels_get_name_from_index - get channel name from channel index
 * @index: channel index
 *
 * Allows to lookup the channel name given its index. Done to keep the name
 * information outside of each trace channel instance.
 */
const char *ltt_channels_get_name_from_index(unsigned int index)
{
	struct ltt_channel_setting *iter;

	cds_list_for_each_entry(iter, &ltt_channels, list)
		if (iter->index == index && uatomic_read(&iter->urcu_ref.refcount))
			return iter->name;
	return NULL;
}

static struct ltt_channel_setting *
ltt_channels_get_setting_from_name(const char *name)
{
	struct ltt_channel_setting *iter;

	cds_list_for_each_entry(iter, &ltt_channels, list)
		if (!strcmp(iter->name, name)
		    && uatomic_read(&iter->urcu_ref.refcount))
			return iter;
	return NULL;
}

/**
 * ltt_channels_get_index_from_name - get channel index from channel name
 * @name: channel name
 *
 * Allows to lookup the channel index given its name. Done to keep the name
 * information outside of each trace channel instance.
 * Returns -1 if not found.
 */
int ltt_channels_get_index_from_name(const char *name)
{
	struct ltt_channel_setting *setting;

	setting = ltt_channels_get_setting_from_name(name);
	if (setting)
		return setting->index;
	else
		return -1;
}

/**
 * ltt_channels_trace_alloc - Allocate channel structures for a trace
 * @subbuf_size: subbuffer size. 0 uses default.
 * @subbuf_cnt: number of subbuffers per per-cpu buffers. 0 uses default.
 * @flags: Default channel flags
 *
 * Use the current channel list to allocate the channels for a trace.
 * Called with trace lock held. Does not perform the trace buffer allocation,
 * because we must let the user overwrite specific channel sizes.
 */
struct ust_channel *ltt_channels_trace_alloc(unsigned int *nr_channels,
						    int overwrite,
						    int request_collection,
						    int active)
{
	struct ust_channel *channel = NULL;
	struct ltt_channel_setting *iter;

	pthread_mutex_lock(&ltt_channel_mutex);
	if (!free_index) {
		WARN("ltt_channels_trace_alloc: no free_index; are there any probes connected?");
		goto end;
	}
	if (!uatomic_read(&index_urcu_ref.refcount))
		urcu_ref_init(&index_urcu_ref);
	else
		urcu_ref_get(&index_urcu_ref);
	*nr_channels = free_index;
	channel = zmalloc(sizeof(struct ust_channel) * free_index);
	if (!channel) {
		WARN("ltt_channel_struct: channel null after alloc");
		goto end;
	}
	cds_list_for_each_entry(iter, &ltt_channels, list) {
		if (!uatomic_read(&iter->urcu_ref.refcount))
			continue;
		channel[iter->index].subbuf_size = iter->subbuf_size;
		channel[iter->index].subbuf_cnt = iter->subbuf_cnt;
		channel[iter->index].overwrite = overwrite;
		channel[iter->index].request_collection = request_collection;
		channel[iter->index].active = active;
		channel[iter->index].channel_name = iter->name;
	}
end:
	pthread_mutex_unlock(&ltt_channel_mutex);
	return channel;
}

/**
 * ltt_channels_trace_free - Free one trace's channels
 * @channels: channels to free
 *
 * Called with trace lock held. The actual channel buffers must be freed before
 * this function is called.
 */
void ltt_channels_trace_free(struct ust_channel *channels)
{
	lock_ust_marker();
	pthread_mutex_lock(&ltt_channel_mutex);
	free(channels);
	urcu_ref_put(&index_urcu_ref, release_trace_channel);
	pthread_mutex_unlock(&ltt_channel_mutex);
	unlock_ust_marker();
}

/**
 * _ltt_channels_get_event_id - get next event ID for a marker
 * @channel: channel name
 * @name: event name
 *
 * Returns a unique event ID (for this channel) or < 0 on error.
 * Must be called with channels mutex held.
 */
int _ltt_channels_get_event_id(const char *channel, const char *name)
{
	struct ltt_channel_setting *setting;
	int ret;

	setting = ltt_channels_get_setting_from_name(channel);
	if (!setting) {
		ret = -ENOENT;
		goto end;
	}
	if (strcmp(channel, "metadata") == 0) {
		if (strcmp(name, "core_marker_id") == 0)
			ret = 0;
		else if (strcmp(name, "core_marker_format") == 0)
			ret = 1;
		else if (strcmp(name, "testev") == 0)
			ret = 2;
		else
			ret = -ENOENT;
		goto end;
	}
	if (setting->free_event_id == EVENTS_PER_CHANNEL - 1) {
		ret = -ENOSPC;
		goto end;
	}
	ret = setting->free_event_id++;
end:
	return ret;
}

/**
 * ltt_channels_get_event_id - get next event ID for a marker
 * @channel: channel name
 * @name: event name
 *
 * Returns a unique event ID (for this channel) or < 0 on error.
 */
int ltt_channels_get_event_id(const char *channel, const char *name)
{
	int ret;

	pthread_mutex_lock(&ltt_channel_mutex);
	ret = _ltt_channels_get_event_id(channel, name);
	pthread_mutex_unlock(&ltt_channel_mutex);
	return ret;
}
