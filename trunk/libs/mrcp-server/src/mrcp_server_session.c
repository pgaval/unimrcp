/*
 * Copyright 2008 Arsen Chaloyan
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mrcp_server.h"
#include "mrcp_server_session.h"
#include "mrcp_resource.h"
#include "mrcp_resource_factory.h"
#include "mrcp_resource_engine.h"
#include "mrcp_sig_agent.h"
#include "mrcp_server_connection.h"
#include "mrcp_session_descriptor.h"
#include "mrcp_control_descriptor.h"
#include "mrcp_state_machine.h"
#include "mrcp_message.h"
#include "mpf_termination.h"
#include "apt_consumer_task.h"
#include "apt_log.h"

#define MRCP_SESSION_ID_HEX_STRING_LENGTH 16

struct mrcp_channel_t {
	/** Memory pool */
	apr_pool_t             *pool;
	/** MRCP resource */
	apt_str_t               resource_name;
	/** MRCP resource */
	mrcp_resource_t        *resource;
	/** MRCP session entire channel belongs to */
	mrcp_session_t         *session;
	/** MRCP control channel */
	mrcp_control_channel_t *control_channel;
	/** MRCP resource engine channel */
	mrcp_engine_channel_t  *engine_channel;
	/** MRCP resource state machine  */
	mrcp_state_machine_t   *state_machine;
	/** media descriptor id (position in SDP message) */
	apr_size_t              id;
	/** control media id (used for resource grouping) */
	apr_size_t              cmid;
	/** waiting state of control media */
	apt_bool_t              waiting_for_channel;
	/** waiting state of media termination */
	apt_bool_t              waiting_for_termination;
};

typedef struct mrcp_termination_slot_t mrcp_termination_slot_t;

struct mrcp_termination_slot_t {
	/** RTP termination */
	mpf_termination_t  *termination;
	/** media descriptor id (position in SDP message) */
	apr_size_t          id;
	/** media id (used for resource grouping) */
	apr_size_t          mid;
	/** Array of associated MRCP channels (mrcp_channel_t*) */
	apr_array_header_t *channels;

	/** waiting state */
	apt_bool_t          waiting;
};

extern const mrcp_engine_channel_event_vtable_t engine_channel_vtable;

void mrcp_server_session_add(mrcp_server_session_t *session);
void mrcp_server_session_remove(mrcp_server_session_t *session);

static apt_bool_t mrcp_server_signaling_message_dispatch(mrcp_server_session_t *session, mrcp_signaling_message_t *signaling_message);

static apt_bool_t mrcp_server_resource_offer_process(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor);
static apt_bool_t mrcp_server_control_media_offer_process(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor);
static apt_bool_t mrcp_server_av_media_offer_process(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor);

static apt_bool_t mrcp_server_session_answer_send(mrcp_server_session_t *session);
static apt_bool_t mrcp_server_session_terminate_process(mrcp_server_session_t *session);
static apt_bool_t mrcp_server_session_terminate_send(mrcp_server_session_t *session);

static mrcp_channel_t* mrcp_server_channel_find(mrcp_server_session_t *session, const apt_str_t *resource_name);

static apt_bool_t state_machine_on_message_dispatch(mrcp_state_machine_t *state_machine, mrcp_message_t *message);
static apt_bool_t state_machine_on_deactivate(mrcp_state_machine_t *state_machine);


mrcp_server_session_t* mrcp_server_session_create()
{
	mrcp_server_session_t *session = (mrcp_server_session_t*) mrcp_session_create(sizeof(mrcp_server_session_t)-sizeof(mrcp_session_t));
	session->context = NULL;
	session->terminations = apr_array_make(session->base.pool,2,sizeof(mrcp_termination_slot_t));
	session->channels = apr_array_make(session->base.pool,2,sizeof(mrcp_channel_t*));
	session->active_request = NULL;
	session->request_queue = apt_list_create(session->base.pool);
	session->offer = NULL;
	session->answer = NULL;
	session->mpf_task_msg = NULL;
	session->subrequest_count = 0;
	session->state = SESSION_STATE_NONE;
	return session;
}

static APR_INLINE mrcp_version_e mrcp_session_version_get(mrcp_server_session_t *session)
{
	return session->base.signaling_agent->mrcp_version;
}

static mrcp_engine_channel_t* mrcp_server_engine_channel_create(mrcp_server_session_t *session, const apt_str_t *resource_name)
{
	mrcp_resource_engine_t *resource_engine = apr_hash_get(
												session->profile->engine_table,
												resource_name->buf,
												resource_name->length);
	if(!resource_engine) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Find Resource Engine [%s]",resource_name->buf);
		return NULL;
	}

	return mrcp_engine_channel_virtual_create(resource_engine,mrcp_session_version_get(session),session->base.pool);
}

static mrcp_channel_t* mrcp_server_channel_create(mrcp_server_session_t *session, const apt_str_t *resource_name, apr_size_t id, apr_size_t cmid)
{
	mrcp_channel_t *channel;
	apr_pool_t *pool = session->base.pool;

	channel = apr_palloc(pool,sizeof(mrcp_channel_t));
	channel->pool = pool;
	channel->session = &session->base;
	channel->resource = NULL;
	channel->control_channel = NULL;
	channel->state_machine = NULL;
	channel->engine_channel = NULL;
	channel->id = id;
	channel->cmid = cmid;
	channel->waiting_for_channel = FALSE;
	channel->waiting_for_termination = FALSE;
	apt_string_reset(&channel->resource_name);

	if(resource_name && resource_name->buf) {
		mrcp_resource_id resource_id;
		mrcp_resource_t *resource;
		mrcp_engine_channel_t *engine_channel;
		channel->resource_name = *resource_name;
		resource_id = mrcp_resource_id_find(
								session->profile->resource_factory,
								resource_name);
		resource = mrcp_resource_get(session->profile->resource_factory,resource_id);
		if(resource) {
			channel->resource = resource;
			if(mrcp_session_version_get(session) == MRCP_VERSION_2) {
				channel->control_channel = mrcp_server_control_channel_create(
									session->profile->connection_agent,
									channel,
									pool);
			}
			channel->state_machine = resource->create_server_state_machine(
								channel,
								mrcp_session_version_get(session),
								pool);
			if(channel->state_machine) {
				channel->state_machine->on_dispatch = state_machine_on_message_dispatch;
				channel->state_machine->on_deactivate = state_machine_on_deactivate;
			}

			engine_channel = mrcp_server_engine_channel_create(session,resource_name);
			if(engine_channel) {
				engine_channel->id = session->base.id;
				engine_channel->event_obj = channel;
				engine_channel->event_vtable = &engine_channel_vtable;
				channel->engine_channel = engine_channel;
			}
			else {
				apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Create Resource Engine Channel [%s]",resource_name->buf);
				session->answer->status = MRCP_SESSION_STATUS_UNACCEPTABLE_RESOURCE;
			}
		}
		else {
			apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"No Such Resource [%s]",resource_name->buf);
			session->answer->status = MRCP_SESSION_STATUS_NO_SUCH_RESOURCE;
		}
	}
	else {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Invalid Resource Identifier");
		session->answer->status = MRCP_SESSION_STATUS_NO_SUCH_RESOURCE;
	}

	return channel;
}

static APR_INLINE void mrcp_server_session_state_set(mrcp_server_session_t *session, mrcp_server_session_state_e state)
{
	if(session->subrequest_count != 0) {
		/* error case */
		session->subrequest_count = 0;
	}
	session->state = state;
}

static APR_INLINE void mrcp_server_session_subrequest_add(mrcp_server_session_t *session)
{
	session->subrequest_count++;
}

static void mrcp_server_session_subrequest_remove(mrcp_server_session_t *session)
{
	if(!session->subrequest_count) {
		/* error case */
		return;
	}
	session->subrequest_count--;
	if(!session->subrequest_count) {
		switch(session->state) {
			case SESSION_STATE_ANSWERING:
				/* send answer to client */
				mrcp_server_session_answer_send(session);
				break;
			case SESSION_STATE_DEACTIVATING:
				mrcp_server_session_terminate_process(session);
				break;
			case SESSION_STATE_TERMINATING:
				mrcp_server_session_terminate_send(session);
				break;
			default:
				break;
		}
	}
}

mrcp_session_t* mrcp_server_channel_session_get(mrcp_channel_t *channel)
{
	return channel->session;
}

apt_bool_t mrcp_server_signaling_message_process(mrcp_signaling_message_t *signaling_message)
{
	mrcp_server_session_t *session = signaling_message->session;
	if(session->active_request) {
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Push Request to Queue");
		apt_list_push_back(session->request_queue,signaling_message,session->base.pool);
	}
	else {
		session->active_request = signaling_message;
		mrcp_server_signaling_message_dispatch(session,signaling_message);
	}
	return TRUE;
}

apt_bool_t mrcp_server_on_channel_modify(mrcp_channel_t *channel, mrcp_control_descriptor_t *answer, apt_bool_t status)
{
	mrcp_server_session_t *session = (mrcp_server_session_t*)channel->session;
	apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"On Control Channel Modify");
	if(!answer) {
		return FALSE;
	}
	if(!channel->waiting_for_channel) {
		return FALSE;
	}
	channel->waiting_for_channel = FALSE;
	answer->session_id = session->base.id;
	mrcp_session_control_media_set(session->answer,channel->id,answer);
	mrcp_server_session_subrequest_remove(session);
	return TRUE;
}

apt_bool_t mrcp_server_on_channel_remove(mrcp_channel_t *channel, apt_bool_t status)
{
	mrcp_server_session_t *session = (mrcp_server_session_t*)channel->session;
	apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"On Control Channel Remove");
	if(!channel->waiting_for_channel) {
		return FALSE;
	}
	channel->waiting_for_channel = FALSE;
	mrcp_server_session_subrequest_remove(session);
	return TRUE;
}

apt_bool_t mrcp_server_on_channel_message(mrcp_channel_t *channel, mrcp_message_t *message)
{
	mrcp_server_session_t *session = (mrcp_server_session_t*)channel->session;
	mrcp_signaling_message_t *signaling_message;
	signaling_message = apr_palloc(session->base.pool,sizeof(mrcp_signaling_message_t));
	signaling_message->type = SIGNALING_MESSAGE_CONTROL;
	signaling_message->session = session;
	signaling_message->descriptor = NULL;
	signaling_message->channel = channel;
	signaling_message->message = message;
	return mrcp_server_signaling_message_process(signaling_message);
}

apt_bool_t mrcp_server_on_disconnect(mrcp_channel_t *channel)
{
	/* to be processed */
	return TRUE;
}

apt_bool_t mrcp_server_on_engine_channel_open(mrcp_channel_t *channel, apt_bool_t status)
{
	mrcp_server_session_t *session = (mrcp_server_session_t*)channel->session;
	apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"On Engine Channel Open [%s]", status == TRUE ? "OK" : "Failed");
	if(status == FALSE) {
		session->answer->status = MRCP_SESSION_STATUS_UNAVAILABLE_RESOURCE;
	}
	mrcp_server_session_subrequest_remove(session);
	return TRUE;
}

apt_bool_t mrcp_server_on_engine_channel_close(mrcp_channel_t *channel)
{
	mrcp_server_session_t *session = (mrcp_server_session_t*)channel->session;
	apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"On Engine Channel Close");
	mrcp_server_session_subrequest_remove(session);
	return TRUE;
}

apt_bool_t mrcp_server_on_engine_channel_message(mrcp_channel_t *channel, mrcp_message_t *message)
{
	if(!channel->state_machine) {
		return FALSE;
	}
	/* update state machine */
	return mrcp_state_machine_update(channel->state_machine,message);
}


static mrcp_session_descriptor_t* mrcp_session_answer_create(mrcp_session_descriptor_t *offer, apr_pool_t *pool)
{
	int i;
	void **control_slot;
	mpf_rtp_media_descriptor_t **av_slot;
	mrcp_session_descriptor_t *answer = apr_palloc(pool,sizeof(mrcp_session_descriptor_t));
	apt_string_reset(&answer->origin);
	apt_string_reset(&answer->ip);
	apt_string_reset(&answer->ext_ip);
	answer->resource_name = offer->resource_name;
	answer->resource_state = offer->resource_state;
	answer->status = offer->status;
	answer->control_media_arr = apr_array_make(pool,offer->control_media_arr->nelts,sizeof(void*));
	for(i=0; i<offer->control_media_arr->nelts; i++) {
		control_slot = apr_array_push(answer->control_media_arr);
		*control_slot = NULL;
	}
	answer->audio_media_arr = apr_array_make(pool,offer->audio_media_arr->nelts,sizeof(mpf_rtp_media_descriptor_t*));
	for(i=0; i<offer->audio_media_arr->nelts; i++) {
		av_slot = apr_array_push(answer->audio_media_arr);
		*av_slot = NULL;
	}
	answer->video_media_arr = apr_array_make(pool,offer->video_media_arr->nelts,sizeof(mpf_rtp_media_descriptor_t*));
	for(i=0; i<offer->video_media_arr->nelts; i++) {
		av_slot = apr_array_push(answer->video_media_arr);
		*av_slot = NULL;
	}
	return answer;
}

static apt_bool_t mrcp_server_session_offer_process(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor)
{
	if(!session->context) {
		/* initial offer received, generate session id and add to session's table */
		if(!session->base.id.length) {
			apt_unique_id_generate(&session->base.id,MRCP_SESSION_ID_HEX_STRING_LENGTH,session->base.pool);
		}
		mrcp_server_session_add(session);

		session->context = mpf_engine_context_create(session->profile->media_engine,session,5,session->base.pool);
	}
	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Receive Offer "APT_SID_FMT" [c:%d a:%d v:%d]",
		MRCP_SESSION_SID(&session->base),
		descriptor->control_media_arr->nelts,
		descriptor->audio_media_arr->nelts,
		descriptor->video_media_arr->nelts);

	/* store received offer */
	session->offer = descriptor;
	session->answer = mrcp_session_answer_create(descriptor,session->base.pool);

	mrcp_server_session_state_set(session,SESSION_STATE_ANSWERING);

	/* first, reset/destroy existing associations and topology */
	if(mpf_engine_topology_message_add(
				session->profile->media_engine,
				MPF_RESET_ASSOCIATIONS,session->context,
				&session->mpf_task_msg) == TRUE){
		mrcp_server_session_subrequest_add(session);
	}

	if(mrcp_session_version_get(session) == MRCP_VERSION_1) {
		if(mrcp_server_resource_offer_process(session,descriptor) == TRUE) {
			mrcp_server_av_media_offer_process(session,descriptor);
		}
		else {
			session->answer->resource_state = FALSE;
		}
	}
	else {
		mrcp_server_control_media_offer_process(session,descriptor);
		mrcp_server_av_media_offer_process(session,descriptor);
	}

	/* apply topology based on assigned associations */
	if(mpf_engine_topology_message_add(
				session->profile->media_engine,
				MPF_APPLY_TOPOLOGY,session->context,
				&session->mpf_task_msg) == TRUE) {
		mrcp_server_session_subrequest_add(session);
	}
	mpf_engine_message_send(session->profile->media_engine,&session->mpf_task_msg);

	if(!session->subrequest_count) {
		/* send answer to client */
		mrcp_server_session_answer_send(session);
	}
	return TRUE;
}

static apt_bool_t mrcp_server_session_terminate_process(mrcp_server_session_t *session)
{
	mrcp_channel_t *channel;
	mrcp_termination_slot_t *slot;
	int i;
	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Receive Terminate Request "APT_SID_FMT,MRCP_SESSION_SID(&session->base));

	mrcp_server_session_state_set(session,SESSION_STATE_TERMINATING);

	if(session->context) {
		/* first, destroy existing topology */
		if(mpf_engine_topology_message_add(
					session->profile->media_engine,
					MPF_RESET_ASSOCIATIONS,session->context,
					&session->mpf_task_msg) == TRUE){
			mrcp_server_session_subrequest_add(session);
		}
	}

	for(i=0; i<session->channels->nelts; i++) {
		channel = ((mrcp_channel_t**)session->channels->elts)[i];
		if(!channel) continue;

		/* send remove channel request */
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Remove Control Channel [%d]",i);
		if(channel->control_channel) {
			if(mrcp_server_control_channel_remove(channel->control_channel) == TRUE) {
				channel->waiting_for_channel = TRUE;
				mrcp_server_session_subrequest_add(session);
			}
		}

		if(channel->engine_channel) {
			mpf_termination_t *termination = channel->engine_channel->termination;
			/* send subtract termination request */
			if(termination) {
				apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Subtract Channel Termination");
				if(mpf_engine_termination_message_add(
							session->profile->media_engine,
							MPF_SUBTRACT_TERMINATION,session->context,termination,NULL,
							&session->mpf_task_msg) == TRUE) {
					channel->waiting_for_termination = TRUE;
					mrcp_server_session_subrequest_add(session);
				}
			}

			/* close resource engine channel */
			if(mrcp_engine_channel_virtual_close(channel->engine_channel) == TRUE) {
				mrcp_server_session_subrequest_add(session);
			}
		}
	}
	for(i=0; i<session->terminations->nelts; i++) {
		/* get existing termination */
		slot = &((mrcp_termination_slot_t*)session->terminations->elts)[i];
		if(!slot || !slot->termination) continue;

		/* send subtract termination request */
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Subtract RTP Termination [%d]",i);
		if(mpf_engine_termination_message_add(
				session->profile->media_engine,
				MPF_SUBTRACT_TERMINATION,session->context,slot->termination,NULL,
				&session->mpf_task_msg) == TRUE) {
			slot->waiting = TRUE;
			mrcp_server_session_subrequest_add(session);
		}
	}

	if(session->context) {
		mpf_engine_message_send(session->profile->media_engine,&session->mpf_task_msg);
	}

	mrcp_server_session_remove(session);

	if(!session->subrequest_count) {
		mrcp_server_session_terminate_send(session);
	}

	return TRUE;
}

static apt_bool_t mrcp_server_session_deactivate(mrcp_server_session_t *session)
{
	mrcp_channel_t *channel;
	int i;
	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Deactivate Session "APT_SID_FMT,MRCP_SESSION_SID(&session->base));
	mrcp_server_session_state_set(session,SESSION_STATE_DEACTIVATING);
	for(i=0; i<session->channels->nelts; i++) {
		channel = ((mrcp_channel_t**)session->channels->elts)[i];
		if(!channel || !channel->state_machine) continue;

		if(mrcp_state_machine_deactivate(channel->state_machine) == TRUE) {
			mrcp_server_session_subrequest_add(session);
		}
	}
	
	if(!session->subrequest_count) {
		mrcp_server_session_terminate_process(session);
	}

	return TRUE;
}

static apt_bool_t mrcp_server_on_message_receive(mrcp_server_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	if(!channel) {
		channel = mrcp_server_channel_find(session,&message->channel_id.resource_name);
		if(!channel) {
			apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"No Such Channel");
			return FALSE;
		}
	}
	if(!channel->resource || !channel->state_machine) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"No Resource");
		return FALSE;
	}

	/* update state machine */
	return mrcp_state_machine_update(channel->state_machine,message);
}

static apt_bool_t mrcp_server_signaling_message_dispatch(mrcp_server_session_t *session, mrcp_signaling_message_t *signaling_message)
{
	apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Dispatch Signaling Message [%d]",signaling_message->type);
	switch(signaling_message->type) {
		case SIGNALING_MESSAGE_OFFER:
			mrcp_server_session_offer_process(signaling_message->session,signaling_message->descriptor);
			break;
		case SIGNALING_MESSAGE_CONTROL:
			mrcp_server_on_message_receive(signaling_message->session,signaling_message->channel,signaling_message->message);
			break;
		case SIGNALING_MESSAGE_TERMINATE:
			mrcp_server_session_deactivate(signaling_message->session);
			break;
		default:
			break;
	}
	return TRUE;
}

static apt_bool_t mrcp_server_resource_offer_process(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor)
{
	if(descriptor->resource_state == TRUE) {
		/* setup */
		mrcp_channel_t *channel;
		mrcp_channel_t **slot;
		int count = session->channels->nelts;
		channel = mrcp_server_channel_find(session,&descriptor->resource_name);
		if(channel) {
			/* channel already exists */
			return TRUE;
		}
		/* create new MRCP channel instance */
		channel = mrcp_server_channel_create(session,&descriptor->resource_name,count,0);
		if(!channel || !channel->resource) {
			return FALSE;
		}
		/* add to channel array */
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Add Control Channel [%d]",count);
		slot = apr_array_push(session->channels);
		*slot = channel;

		if(channel->engine_channel) {
			/* open resource engine channel */
			if(mrcp_engine_channel_virtual_open(channel->engine_channel) == TRUE) {
				mpf_termination_t *termination = channel->engine_channel->termination;
				mrcp_server_session_subrequest_add(session);

				if(termination) {
					/* send add termination request (add to media context) */
					if(mpf_engine_termination_message_add(
							session->profile->media_engine,
							MPF_ADD_TERMINATION,session->context,termination,NULL,
							&session->mpf_task_msg) == TRUE) {
						channel->waiting_for_termination = TRUE;
						mrcp_server_session_subrequest_add(session);
					}

					if(termination->audio_stream) {
						mpf_rtp_media_descriptor_t *rtp_media_descriptor = mrcp_session_audio_media_get(descriptor,0);
						if(rtp_media_descriptor) {
							mpf_stream_mode_e mode = termination->audio_stream->mode;
							rtp_media_descriptor->mode |= mode;
						}
					}
				}
			}
		}
	}
	else {
		/* teardown */
	}
	return TRUE;
}

static apt_bool_t mrcp_server_control_media_offer_process(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor)
{
	mrcp_channel_t *channel;
	mrcp_control_descriptor_t *control_descriptor;
	int i;
	int count = session->channels->nelts;
	if(count > descriptor->control_media_arr->nelts) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Number of Control Channels [%d] > Number of Control Media in Offer [%d]",
			count,descriptor->control_media_arr->nelts);
		count = descriptor->control_media_arr->nelts;
	}
	
	/* update existing control channels */
	for(i=0; i<count; i++) {
		/* get existing termination */
		channel = *((mrcp_channel_t**)session->channels->elts + i);
		if(!channel) continue;

		channel->waiting_for_channel = FALSE;
		/* get control descriptor */
		control_descriptor = mrcp_session_control_media_get(descriptor,i);
		if(!control_descriptor) continue;

		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Modify Control Channel [%d]",i);
		if(channel->control_channel) {
			/* send offer */
			if(mrcp_server_control_channel_modify(channel->control_channel,control_descriptor) == TRUE) {
				channel->waiting_for_channel = TRUE;
				mrcp_server_session_subrequest_add(session);
			}
		}

		if(channel->waiting_for_channel == FALSE) {
			mrcp_control_descriptor_t *answer = mrcp_control_answer_create(control_descriptor,channel->pool);
			answer->port = 0;
			answer->session_id = session->base.id;
			mrcp_session_control_media_set(session->answer,channel->id,answer);
		}
	}
	
	/* add new control channels */
	for(; i<descriptor->control_media_arr->nelts; i++) {
		mrcp_channel_t **slot;
		/* get control descriptor */
		control_descriptor = mrcp_session_control_media_get(descriptor,i);
		if(!control_descriptor) continue;

		/* create new MRCP channel instance */
		channel = mrcp_server_channel_create(session,&control_descriptor->resource_name,i,control_descriptor->cmid);
		if(!channel) continue;

		control_descriptor->session_id = session->base.id;
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Add Control Channel [%d]",i);
		slot = apr_array_push(session->channels);
		*slot = channel;

		if(channel->control_channel) {
			/* send modify connection request */
			if(mrcp_server_control_channel_add(channel->control_channel,control_descriptor) == TRUE) {
				channel->waiting_for_channel = TRUE;
				mrcp_server_session_subrequest_add(session);
			}
		}

		if(channel->waiting_for_channel == FALSE) {
			mrcp_control_descriptor_t *answer = mrcp_control_answer_create(control_descriptor,channel->pool);
			answer->port = 0;
			answer->session_id = session->base.id;
			mrcp_session_control_media_set(session->answer,channel->id,answer);
		}
		
		if(channel->engine_channel) {
			/* open resource engine channel */
			if(mrcp_engine_channel_virtual_open(channel->engine_channel) == TRUE) {
				mpf_termination_t *termination = channel->engine_channel->termination;
				mrcp_server_session_subrequest_add(session);

				if(termination) {
					/* send add termination request (add to media context) */
					if(mpf_engine_termination_message_add(
							session->profile->media_engine,
							MPF_ADD_TERMINATION,session->context,termination,NULL,
							&session->mpf_task_msg) == TRUE) {
						channel->waiting_for_termination = TRUE;
						mrcp_server_session_subrequest_add(session);
					}
				}
			}
		}
	}

	return TRUE;
}

static mpf_rtp_termination_descriptor_t* mrcp_server_associations_build(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor, mrcp_termination_slot_t *slot)
{
	int i;
	mrcp_channel_t *channel;
	mpf_rtp_termination_descriptor_t *rtp_descriptor;
	mpf_rtp_media_descriptor_t *media_descriptor = mrcp_session_audio_media_get(descriptor,slot->id);
	if(!media_descriptor) {
		return NULL;
	}
	/* construct termination descriptor */
	rtp_descriptor = apr_palloc(session->base.pool,sizeof(mpf_rtp_termination_descriptor_t));
	mpf_rtp_termination_descriptor_init(rtp_descriptor);
	rtp_descriptor->audio.local = NULL;
	rtp_descriptor->audio.remote = media_descriptor;

	slot->mid = media_descriptor->mid;
	slot->channels = apr_array_make(session->base.pool,1,sizeof(mrcp_channel_t*));
	for(i=0; i<session->channels->nelts; i++) {
		channel = ((mrcp_channel_t**)session->channels->elts)[i];
		if(!channel) continue;

		if(channel->cmid == slot->mid) {
			APR_ARRAY_PUSH(slot->channels, mrcp_channel_t*) = channel;
		}
	}
	return rtp_descriptor;
}

static apt_bool_t mrcp_server_associations_set(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor, mrcp_termination_slot_t *slot)
{
	int i;
	mrcp_channel_t *channel;
	for(i=0; i<slot->channels->nelts; i++) {
		channel = ((mrcp_channel_t**)slot->channels->elts)[i];
		if(!channel || !channel->engine_channel) continue;

		if(mpf_engine_assoc_message_add(
				session->profile->media_engine,
				MPF_ADD_ASSOCIATION,session->context,slot->termination,channel->engine_channel->termination,
				&session->mpf_task_msg) == TRUE) {
			mrcp_server_session_subrequest_add(session);
		}
	}
	return TRUE;
}

static apt_bool_t mrcp_server_av_media_offer_process(mrcp_server_session_t *session, mrcp_session_descriptor_t *descriptor)
{
	mpf_rtp_termination_descriptor_t *rtp_descriptor;
	mrcp_termination_slot_t *slot;
	int i;
	int count = session->terminations->nelts;
	if(!descriptor->audio_media_arr->nelts) {
		/* no media to process */
		return TRUE;
	}
	if(count > descriptor->audio_media_arr->nelts) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Number of Terminations [%d] > Number of Audio Media in Offer [%d]",
			count,descriptor->audio_media_arr->nelts);
		count = descriptor->audio_media_arr->nelts;
	}
	
	/* update existing terminations */
	for(i=0; i<count; i++) {
		/* get existing termination */
		slot = &((mrcp_termination_slot_t*)session->terminations->elts)[i];
		if(!slot || !slot->termination) continue;

		/* build associations between specified RTP termination and control channels */
		rtp_descriptor = mrcp_server_associations_build(session,descriptor,slot);
		if(!rtp_descriptor) continue;

		/* send modify termination request */
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Modify RTP Termination [%d]",i);
		if(mpf_engine_termination_message_add(
				session->profile->media_engine,
				MPF_MODIFY_TERMINATION,session->context,slot->termination,rtp_descriptor,
				&session->mpf_task_msg) == TRUE) {
			slot->waiting = TRUE;
			mrcp_server_session_subrequest_add(session);
		}

		/* set built associations */
		mrcp_server_associations_set(session,descriptor,slot);
	}
	
	/* add new terminations */
	for(; i<descriptor->audio_media_arr->nelts; i++) {
		mpf_termination_t *termination;
		/* create new RTP termination instance */
		termination = mpf_termination_create(session->profile->rtp_termination_factory,session,session->base.pool);
		/* add to termination array */
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Add RTP Termination [%d]",i);
		slot = apr_array_push(session->terminations);
		slot->id = i;
		slot->mid = 0;
		slot->waiting = FALSE;
		slot->termination = termination;
		slot->channels = NULL;

		/* build associations between specified RTP termination and control channels */
		rtp_descriptor = mrcp_server_associations_build(session,descriptor,slot);
		if(!rtp_descriptor) continue;

		/* send add termination request (add to media context) */
		if(mpf_engine_termination_message_add(
				session->profile->media_engine,
				MPF_ADD_TERMINATION,session->context,termination,rtp_descriptor,
				&session->mpf_task_msg) == TRUE) {
			slot->waiting = TRUE;
			mrcp_server_session_subrequest_add(session);
		}

		/* set built associations */
		mrcp_server_associations_set(session,descriptor,slot);
	}
	return TRUE;
}

static apt_bool_t mrcp_server_session_answer_send(mrcp_server_session_t *session)
{
	apt_bool_t status;
	mrcp_session_descriptor_t *descriptor = session->answer;
	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Send Answer "APT_SID_FMT" [c:%d a:%d v:%d] Status %s",
		MRCP_SESSION_SID(&session->base),
		descriptor->control_media_arr->nelts,
		descriptor->audio_media_arr->nelts,
		descriptor->video_media_arr->nelts,
		mrcp_session_status_phrase_get(descriptor->status));
	status = mrcp_session_answer(&session->base,descriptor);
	session->offer = NULL;
	session->answer = NULL;

	session->active_request = apt_list_pop_front(session->request_queue);
	if(session->active_request) {
		mrcp_server_signaling_message_dispatch(session,session->active_request);
	}
	return status;
}

static apt_bool_t mrcp_server_session_terminate_send(mrcp_server_session_t *session)
{
	int i;
	mrcp_channel_t *channel;
	for(i=0; i<session->channels->nelts; i++) {
		channel = ((mrcp_channel_t**)session->channels->elts)[i];
		if(!channel) continue;

		if(channel->control_channel) {
			mrcp_server_control_channel_destroy(channel->control_channel);
			channel->control_channel = NULL;
		}
		if(channel->engine_channel) {
			mrcp_engine_channel_virtual_destroy(channel->engine_channel);
			channel->engine_channel = NULL;
		}
	}
	apt_log(APT_LOG_MARK,APT_PRIO_INFO,"Send Terminate Response "APT_SID_FMT,MRCP_SESSION_SID(&session->base));
	mrcp_session_terminate_response(&session->base);
	return TRUE;
}


static mrcp_termination_slot_t* mrcp_server_rtp_termination_find(mrcp_server_session_t *session, mpf_termination_t *termination)
{
	int i;
	mrcp_termination_slot_t *slot;
	for(i=0; i<session->terminations->nelts; i++) {
		slot = &((mrcp_termination_slot_t*)session->terminations->elts)[i];
		if(slot && slot->termination == termination) {
			return slot;
		}
	}
	return NULL;
}

static mrcp_channel_t* mrcp_server_channel_termination_find(mrcp_server_session_t *session, mpf_termination_t *termination)
{
	int i;
	mrcp_channel_t *channel;
	for(i=0; i<session->channels->nelts; i++) {
		channel = ((mrcp_channel_t**)session->channels->elts)[i];
		if(!channel) continue;

		if(channel->engine_channel && channel->engine_channel->termination == termination) {
			return channel;
		}
	}
	return NULL;
}

static mrcp_channel_t* mrcp_server_channel_find(mrcp_server_session_t *session, const apt_str_t *resource_name)
{
	int i;
	mrcp_channel_t *channel;
	for(i=0; i<session->channels->nelts; i++) {
		channel = ((mrcp_channel_t**)session->channels->elts)[i];
		if(!channel) continue;

		if(apt_string_compare(&channel->resource_name,resource_name) == TRUE) {
			return channel;
		}
	}
	return NULL;
}

static apt_bool_t mrcp_server_on_termination_modify(mrcp_server_session_t *session, const mpf_message_t *mpf_message)
{
	mrcp_termination_slot_t *termination_slot;
	if(!session) {
		return FALSE;
	}
	termination_slot = mrcp_server_rtp_termination_find(session,mpf_message->termination);
	if(termination_slot) {
		/* rtp termination */
		mpf_rtp_termination_descriptor_t *rtp_descriptor;
		if(termination_slot->waiting == FALSE) {
			return FALSE;
		}
		termination_slot->waiting = FALSE;
		rtp_descriptor = mpf_message->descriptor;
		if(rtp_descriptor->audio.local) {
			session->answer->ip = rtp_descriptor->audio.local->base.ip;
			session->answer->ext_ip = rtp_descriptor->audio.local->base.ext_ip;
			mrcp_session_audio_media_set(session->answer,termination_slot->id,rtp_descriptor->audio.local);
		}
		mrcp_server_session_subrequest_remove(session);
	}
	else {
		/* engine channel termination */
		mrcp_channel_t *channel = mrcp_server_channel_termination_find(session,mpf_message->termination);
		if(channel && channel->waiting_for_termination == TRUE) {
			channel->waiting_for_termination = FALSE;
			mrcp_server_session_subrequest_remove(session);
		}
	}
	return TRUE;
}

static apt_bool_t mrcp_server_on_termination_subtract(mrcp_server_session_t *session, const mpf_message_t *mpf_message)
{
	mrcp_termination_slot_t *termination_slot;
	if(!session) {
		return FALSE;
	}
	termination_slot = mrcp_server_rtp_termination_find(session,mpf_message->termination);
	if(termination_slot) {
		/* rtp termination */
		if(termination_slot->waiting == FALSE) {
			return FALSE;
		}
		termination_slot->waiting = FALSE;
		mrcp_server_session_subrequest_remove(session);
	}
	else {
		/* engine channel termination */
		mrcp_channel_t *channel = mrcp_server_channel_termination_find(session,mpf_message->termination);
		if(channel && channel->waiting_for_termination == TRUE) {
			channel->waiting_for_termination = FALSE;
			mrcp_server_session_subrequest_remove(session);
		}
	}
	return TRUE;
}

apt_bool_t mrcp_server_mpf_message_process(mpf_message_container_t *mpf_message_container)
{
	apr_size_t i;
	mrcp_server_session_t *session;
	const mpf_message_t *mpf_message;
	for(i=0; i<mpf_message_container->count; i++) {
		mpf_message = &mpf_message_container->messages[i];
		if(mpf_message->context) {
			session = mpf_engine_context_object_get(mpf_message->context);
		}
		else {
			session = NULL;
		}
		
		if(mpf_message->message_type == MPF_MESSAGE_TYPE_RESPONSE) {
			switch(mpf_message->command_id) {
				case MPF_ADD_TERMINATION:
					apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"On Termination Add");
					mrcp_server_on_termination_modify(session,mpf_message);
					break;
				case MPF_MODIFY_TERMINATION:
					apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"On Termination Modify");
					mrcp_server_on_termination_modify(session,mpf_message);
					break;
				case MPF_SUBTRACT_TERMINATION:
					apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"On Termination Subtract");
					mrcp_server_on_termination_subtract(session,mpf_message);
					break;
				case MPF_ADD_ASSOCIATION:
				case MPF_REMOVE_ASSOCIATION:
				case MPF_RESET_ASSOCIATIONS:
				case MPF_APPLY_TOPOLOGY:
				case MPF_DESTROY_TOPOLOGY:
					mrcp_server_session_subrequest_remove(session);
					break;
				default:
					break;
			}
		}
		else if(mpf_message->message_type == MPF_MESSAGE_TYPE_EVENT) {
			apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Process MPF Event");
		}
	}
	return TRUE;
}

static apt_bool_t state_machine_on_message_dispatch(mrcp_state_machine_t *state_machine, mrcp_message_t *message)
{
	mrcp_channel_t *channel = state_machine->obj;

	if(message->start_line.message_type == MRCP_MESSAGE_TYPE_REQUEST) {
		/* send request message to resource engine for actual processing */
		if(channel->engine_channel) {
			mrcp_engine_channel_request_process(channel->engine_channel,message);
		}
	}
	else if(message->start_line.message_type == MRCP_MESSAGE_TYPE_RESPONSE) {
		mrcp_server_session_t *session = (mrcp_server_session_t*)channel->session;
		/* send response message to client */
		if(channel->control_channel) {
			/* MRCPv2 */
			mrcp_server_control_message_send(channel->control_channel,message);
		}
		else {
			/* MRCPv1 */
			mrcp_session_control_response(channel->session,message);
		}

		session->active_request = apt_list_pop_front(session->request_queue);
		if(session->active_request) {
			mrcp_server_signaling_message_dispatch(session,session->active_request);
		}
	}
	else { 
		/* send event message to client */
		if(channel->control_channel) {
			/* MRCPv2 */
			mrcp_server_control_message_send(channel->control_channel,message);
		}
		else {
			/* MRCPv1 */
			mrcp_session_control_response(channel->session,message);
		}
	}
	return TRUE;
}

static apt_bool_t state_machine_on_deactivate(mrcp_state_machine_t *state_machine)
{
	mrcp_channel_t *channel = state_machine->obj;
	mrcp_server_session_t *session = (mrcp_server_session_t*)channel->session;
	mrcp_server_session_subrequest_remove(session);
	return TRUE;
}