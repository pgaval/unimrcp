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

#ifdef WIN32
#pragma warning(disable: 4127)
#endif
#include <apr_ring.h> 
#include "mpf_context.h"
#include "mpf_object.h"
#include "mpf_termination.h"
#include "mpf_stream.h"
#include "mpf_encoder.h"
#include "mpf_decoder.h"
#include "mpf_bridge.h"
#include "apt_log.h"

/** Item of the association matrix */
typedef struct {
	char on;
} matrix_item_t;

/** Item of the association matrix header */
typedef struct {
	mpf_termination_t *termination;
	char               tx_count;
	char               rx_count;
} header_item_t;

/** Media processing context */
struct mpf_context_t {
	/** Ring entry */
	APR_RING_ENTRY(mpf_context_t) link;
	/** Back pointer to the context factory */
	mpf_context_factory_t        *factory;
	/** Pool to allocate memory from */
	apr_pool_t                   *pool;
	/** External object */
	void                         *obj;

	/** Max number of terminations in the context */
	apr_size_t                    capacity;
	/** Current number of terminations in the context */
	apr_size_t                    count;
	/** Header of the association matrix */
	header_item_t                *header;
	/** Association matrix, which represents the topology */
	matrix_item_t                **matrix;

	/** Array of media processing objects constructed while 
	applying topology based on association matrix */
	apr_array_header_t           *mpf_objects;
};

/** Factory of media contexts */
struct mpf_context_factory_t {
	/** Ring head */
	APR_RING_HEAD(mpf_context_head_t, mpf_context_t) head;
};


static APR_INLINE apt_bool_t stream_mode_compatibility_check(mpf_termination_t *termination1, mpf_termination_t *termination2);
static mpf_object_t* mpf_context_connection_create(mpf_context_t *context, mpf_termination_t *src_termination, mpf_termination_t *sink_termination);


MPF_DECLARE(mpf_context_factory_t*) mpf_context_factory_create(apr_pool_t *pool)
{
	mpf_context_factory_t *factory = apr_palloc(pool, sizeof(mpf_context_factory_t));
	APR_RING_INIT(&factory->head, mpf_context_t, link);
	return factory;
}

MPF_DECLARE(void) mpf_context_factory_destroy(mpf_context_factory_t *factory)
{
	mpf_context_t *context;
	while(!APR_RING_EMPTY(&factory->head, mpf_context_t, link)) {
		context = APR_RING_FIRST(&factory->head);
		mpf_context_destroy(context);
		APR_RING_REMOVE(context, link);
	}
}

MPF_DECLARE(apt_bool_t) mpf_context_factory_process(mpf_context_factory_t *factory)
{
	mpf_context_t *context;
	for(context = APR_RING_FIRST(&factory->head);
			context != APR_RING_SENTINEL(&factory->head, mpf_context_t, link);
				context = APR_RING_NEXT(context, link)) {
		
		mpf_context_process(context);
	}

	return TRUE;
}

 
MPF_DECLARE(mpf_context_t*) mpf_context_create(
								mpf_context_factory_t *factory,
								void *obj,
								apr_size_t max_termination_count,
								apr_pool_t *pool)
{
	apr_size_t i,j;
	matrix_item_t *matrix_item;
	header_item_t *header_item;
	mpf_context_t *context = apr_palloc(pool,sizeof(mpf_context_t));
	context->factory = factory;
	context->obj = obj;
	context->pool = pool;
	context->capacity = max_termination_count;
	context->count = 0;
	context->mpf_objects = apr_array_make(pool,1,sizeof(mpf_object_t*));
	context->header = apr_palloc(pool,context->capacity * sizeof(header_item_t));
	context->matrix = apr_palloc(pool,context->capacity * sizeof(matrix_item_t*));
	for(i=0; i<context->capacity; i++) {
		header_item = &context->header[i];
		header_item->termination = NULL;
		header_item->tx_count = 0;
		header_item->rx_count = 0;
		context->matrix[i] = apr_palloc(pool,context->capacity * sizeof(matrix_item_t));
		for(j=0; j<context->capacity; j++) {
			matrix_item = &context->matrix[i][j];
			matrix_item->on = 0;
		}
	}

	return context;
}

MPF_DECLARE(apt_bool_t) mpf_context_destroy(mpf_context_t *context)
{
	apr_size_t i;
	mpf_termination_t *termination;
	for(i=0; i<context->capacity; i++){
		termination = context->header[i].termination;
		if(termination) {
			mpf_context_termination_subtract(context,termination);
			if(termination->audio_stream) {
				mpf_audio_stream_destroy(termination->audio_stream);
			}
		}
	}
	return TRUE;
}

MPF_DECLARE(void*) mpf_context_object_get(mpf_context_t *context)
{
	return context->obj;
}

MPF_DECLARE(apt_bool_t) mpf_context_termination_add(mpf_context_t *context, mpf_termination_t *termination)
{
	apr_size_t i;
	header_item_t *header_item;
	for(i=0; i<context->capacity; i++) {
		header_item = &context->header[i];
		if(header_item->termination) {
			continue;
		}
		if(!context->count) {
			apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Add Context");
			APR_RING_INSERT_TAIL(&context->factory->head,context,mpf_context_t,link);
		}

		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Add Termination");
		header_item->termination = termination;
		header_item->tx_count = 0;
		header_item->rx_count = 0;
		
		termination->slot = i;
		context->count++;
		return TRUE;
	}
	return FALSE;
}

MPF_DECLARE(apt_bool_t) mpf_context_termination_subtract(mpf_context_t *context, mpf_termination_t *termination)
{
	header_item_t *header_item1;
	header_item_t *header_item2;
	matrix_item_t *item;
	apr_size_t j,k;
	apr_size_t i = termination->slot;
	if(i >= context->capacity) {
		return FALSE;
	}
	header_item1 = &context->header[i];
	if(header_item1->termination != termination) {
		return FALSE;
	}

	apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Subtract Termination");
	for(j=0,k=0; j<context->capacity && k<context->count; j++) {
		header_item2 = &context->header[j];
		if(!header_item2->termination) {
			continue;
		}
		k++;

		item = &context->matrix[i][j];
		if(item->on) {
			item->on = 0;
			header_item1->tx_count--;
			header_item2->rx_count--;
		}

		item = &context->matrix[j][i];
		if(item->on) {
			item->on = 0;
			header_item2->tx_count--;
			header_item1->rx_count--;
		}
	}
	header_item1->termination = NULL;

	termination->slot = (apr_size_t)-1;
	context->count--;
	if(!context->count) {
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Remove Context");
		APR_RING_REMOVE(context,link);
	}
	return TRUE;
}

MPF_DECLARE(apt_bool_t) mpf_context_association_add(mpf_context_t *context, mpf_termination_t *termination1, mpf_termination_t *termination2)
{
	header_item_t *header_item1;
	matrix_item_t *matrix_item1;
	header_item_t *header_item2;
	matrix_item_t *matrix_item2;
	apr_size_t i = termination1->slot;
	apr_size_t j = termination2->slot;
	if(i >= context->capacity || j >= context->capacity) {
		return FALSE;
	}

	header_item1 = &context->header[i];
	header_item2 = &context->header[j];

	if(header_item1->termination != termination1 || header_item2->termination != termination2) {
		return FALSE;
	}

	matrix_item1 = &context->matrix[i][j];
	matrix_item2 = &context->matrix[j][i];

	/* 1 -> 2 */
	if(!matrix_item1->on) {
		if(stream_mode_compatibility_check(header_item1->termination,header_item2->termination) == TRUE) {
			matrix_item1->on = 1;
			header_item1->tx_count ++;
			header_item2->rx_count ++;
		}
	}

	/* 2 -> 1 */
	if(!matrix_item2->on) {
		if(stream_mode_compatibility_check(header_item2->termination,header_item1->termination) == TRUE) {
			matrix_item2->on = 1;
			header_item2->tx_count ++;
			header_item1->rx_count ++;
		}
	}
	return TRUE;
}

MPF_DECLARE(apt_bool_t) mpf_context_association_remove(mpf_context_t *context, mpf_termination_t *termination1, mpf_termination_t *termination2)
{
	header_item_t *header_item1;
	matrix_item_t *matrix_item1;
	header_item_t *header_item2;
	matrix_item_t *matrix_item2;
	apr_size_t i = termination1->slot;
	apr_size_t j = termination2->slot;
	if(i >= context->capacity || j >= context->capacity) {
		return FALSE;
	}

	header_item1 = &context->header[i];
	header_item2 = &context->header[j];

	if(header_item1->termination != termination1 || header_item2->termination != termination2) {
		return FALSE;
	}

	matrix_item1 = &context->matrix[i][j];
	matrix_item2 = &context->matrix[j][i];

	/* 1 -> 2 */
	if(matrix_item1->on == 1) {
		matrix_item1->on = 0;
		header_item1->tx_count --;
		header_item2->rx_count --;
	}

	/* 2 -> 1 */
	if(matrix_item2->on == 1) {
		matrix_item2->on = 0;
		header_item2->tx_count --;
		header_item1->rx_count --;
	}
	return TRUE;
}

MPF_DECLARE(apt_bool_t) mpf_context_associations_reset(mpf_context_t *context)
{
	apr_size_t i,j,k;
	header_item_t *header_item1;
	header_item_t *header_item2;
	matrix_item_t *item;

	/* destroy existing topology / if any */
	mpf_context_topology_destroy(context);

	/* reset assigned associations */
	for(i=0,k=0; i<context->capacity && k<context->count; i++) {
		header_item1 = &context->header[i];
		if(!header_item1->termination) {
			continue;
		}
		k++;
		
		if(!header_item1->tx_count && !header_item1->rx_count) {
			continue;
		}
		
		for(j=i; j<context->capacity; j++) {
			header_item2 = &context->header[j];
			if(!header_item2->termination) {
				continue;
			}
			
			item = &context->matrix[i][j];
			if(item->on) {
				item->on = 0;
				header_item1->tx_count--;
				header_item2->rx_count--;
			}

			item = &context->matrix[j][i];
			if(item->on) {
				item->on = 0;
				header_item2->tx_count--;
				header_item1->rx_count--;
			}
		}
	}
	return TRUE;
}

MPF_DECLARE(apt_bool_t) mpf_context_topology_apply(mpf_context_t *context)
{
	apr_size_t i,j,k;
	header_item_t *header_item1;
	header_item_t *header_item2;
	matrix_item_t *item;
	mpf_object_t *object;
	
	/* first destroy existing topology / if any */
	mpf_context_topology_destroy(context);

	for(i=0,k=0; i<context->capacity && k<context->count; i++) {
		header_item1 = &context->header[i];
		if(!header_item1->termination) {
			continue;
		}
		k++;
		
		if(!header_item1->tx_count && !header_item1->rx_count) {
			continue;
		}
		
		for(j=i; j<context->capacity; j++) {
			header_item2 = &context->header[j];
			if(!header_item2->termination) {
				continue;
			}
			item = &context->matrix[i][j];
			if(item->on) {
				/* create connection i -> j */
				object = mpf_context_connection_create(context,header_item1->termination,header_item2->termination);
				if(object) {
					APR_ARRAY_PUSH(context->mpf_objects, mpf_object_t*) = object;
				}
			}
			if(i != j) {
				item = &context->matrix[j][i];
				if(item->on) {
					/* create connection j -> i */
					object = mpf_context_connection_create(context,header_item2->termination,header_item1->termination);
					if(object) {
						APR_ARRAY_PUSH(context->mpf_objects, mpf_object_t*) = object;
					}
				}
			}
		}
	}

	return TRUE;
}

MPF_DECLARE(apt_bool_t) mpf_context_topology_destroy(mpf_context_t *context)
{
	if(context->mpf_objects->nelts) {
		int i;
		mpf_object_t *object;
		for(i=0; i<context->mpf_objects->nelts; i++) {
			object = APR_ARRAY_IDX(context->mpf_objects,i,mpf_object_t*);
			if(object && object->destroy) {
				object->destroy(object);
			}
		}
		apr_array_clear(context->mpf_objects);
	}
	return TRUE;
}

MPF_DECLARE(apt_bool_t) mpf_context_process(mpf_context_t *context)
{
	int i;
	mpf_object_t *object;
	for(i=0; i<context->mpf_objects->nelts; i++) {
		object = APR_ARRAY_IDX(context->mpf_objects,i,mpf_object_t*);
		if(object && object->process) {
			object->process(object);
		}
	}
	return TRUE;
}


static mpf_object_t* mpf_context_connection_create(mpf_context_t *context, mpf_termination_t *src_termination, mpf_termination_t *sink_termination)
{
	mpf_object_t *object = NULL;
	mpf_audio_stream_t *source;
	mpf_audio_stream_t *sink;
	if(!src_termination || !sink_termination) {
		return NULL;
	}
	source = src_termination->audio_stream;
	sink = sink_termination->audio_stream;
	if(source && (source->mode & STREAM_MODE_RECEIVE) == STREAM_MODE_RECEIVE &&
		sink && (sink->mode & STREAM_MODE_SEND) == STREAM_MODE_SEND) {
		mpf_codec_t *rx_codec = source->rx_codec;
		mpf_codec_t *tx_codec = sink->tx_codec;
		if(rx_codec && tx_codec) {
			if(mpf_codec_descriptors_match(rx_codec->descriptor,tx_codec->descriptor) == TRUE) {
				object = mpf_null_bridge_create(source,sink,context->pool);
			}
			else {
				if(rx_codec->descriptor->sampling_rate != tx_codec->descriptor->sampling_rate) {
					apt_log(APT_LOG_MARK,APT_PRIO_WARNING,
						"Resampling is not supported now. "
						"Try to configure and use the same sampling rate on both ends");
					return NULL;
				}
				if(rx_codec->vtable && rx_codec->vtable->decode) {
					/* set decoder before bridge */
					mpf_audio_stream_t *decoder = mpf_decoder_create(source,context->pool);
					source = decoder;
				}
				if(tx_codec->vtable && tx_codec->vtable->encode) {
					/* set encoder after bridge */
					mpf_audio_stream_t *encoder = mpf_encoder_create(sink,context->pool);
					sink = encoder;
				}
				object = mpf_bridge_create(source,sink,context->pool);
			}
		}
	}
	return object;
}

static APR_INLINE apt_bool_t stream_mode_compatibility_check(mpf_termination_t *termination1, mpf_termination_t *termination2)
{
	mpf_audio_stream_t *source = termination1->audio_stream;
	mpf_audio_stream_t *sink = termination2->audio_stream;
	if(source && (source->mode & STREAM_MODE_RECEIVE) == STREAM_MODE_RECEIVE &&
		sink && (sink->mode & STREAM_MODE_SEND) == STREAM_MODE_SEND) {
		return TRUE;
	}
	return FALSE;
}