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

#ifndef __MRCP_UNIRTSP_CLIENT_AGENT_H__
#define __MRCP_UNIRTSP_CLIENT_AGENT_H__

/**
 * @file mrcp_unirtsp_client_agent.h
 * @brief Implementation of MRCP Signaling Interface using UniRTSP
 */ 

#include <apr_network_io.h>
#include "apt_string.h"
#include "mrcp_sig_agent.h"

APT_BEGIN_EXTERN_C

/** UniRTSP config declaration */
typedef struct rtsp_client_config_t rtsp_client_config_t;

/** UniRTSP config */
struct rtsp_client_config_t {
	/** Server IP address */
	char      *server_ip;
	/** Server port */
	apr_port_t server_port;
	/** Resource location */
	char      *resource_location;
	/** SDP origin */
	char      *origin;

	/** Number of max RTSP connections */
	apr_size_t max_connection_count;
};

/**
 * Create UniRTSP signaling agent.
 */
MRCP_DECLARE(mrcp_sig_agent_t*) mrcp_unirtsp_client_agent_create(rtsp_client_config_t *config, apr_pool_t *pool);

/**
 * Allocate UniRTSP config.
 */
MRCP_DECLARE(rtsp_client_config_t*) mrcp_unirtsp_client_config_alloc(apr_pool_t *pool);

APT_END_EXTERN_C

#endif /*__MRCP_UNIRTSP_CLIENT_AGENT_H__*/