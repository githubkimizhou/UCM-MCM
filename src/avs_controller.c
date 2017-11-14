/****************************************************************************
 *
 * Multiedia Controller Module(MCM).     
 *
 * Copyright (c) 2017 by Grandstream Networks, Inc.
 * All rights reserved.
 *
 * This material is proprietary to Grandstream Networks, Inc. and,
 * in addition to the above mentioned Copyright, may be
 * subject to protection under other intellectual property
 * regimes, including patents, trade secrets, designs and/or
 * trademarks.
 *
 * Any use of this material for any purpose, except with an
 * express license from Grandstream Networks, Inc. is strictly
 * prohibited.
 *
 *
 * \brief AVS Component Adaptor.
 *
 * \author Kimi Zhou <lzhou@grandstream.cn>
 *
 *	avs_controllor is a bridge between AVS and Conference Manager, it acts
 *  as a role like an adaptor.
 *
 ***************************************************************************/

#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include <jansson.h>
#include <pthread.h>
#include "include/avs_controller.h"

//#define AVS_URL		"http://127.0.0.1:8080/com/grandstream/httpjson/avs"
#define AVS_URL		"http://192.168.124.172:80"

static CURL *curl_handle = NULL;

typedef enum func_return {
	R_SUCCESS,
	R_FAIL
} FUNC_RETURN;

static FUNC_RETURN http_init(void);
static FUNC_RETURN http_shutdown(void);
static FUNC_RETURN http_send(char *payload);
static char *json_encapulation_hello(void);
static char *json_encapulation_set_global_param(struct avs_global_param *param);

static FUNC_RETURN http_init(void)
{
	FUNC_RETURN ret = R_FAIL;
	
	curl_global_init(CURL_GLOBAL_ALL);
	curl_handle = curl_easy_init();
	
	if (curl_handle)
		ret = R_SUCCESS;
		
	return ret;
}

static FUNC_RETURN http_shutdown(void)
{
	FUNC_RETURN ret = R_SUCCESS;
	
	curl_easy_cleanup(curl_handle);
	curl_global_cleanup();
	
	return ret;
}

static FUNC_RETURN http_send(char *payload)
{
	struct curl_slist *headers = NULL;
	CURLcode res_curl;
	FUNC_RETURN ret = R_FAIL;

	headers = curl_slist_append(headers, "User-Agent: UCM System");
	headers = curl_slist_append(headers, "Connection: keep-alive");
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl_handle, CURLOPT_URL, AVS_URL);
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload);
	
	res_curl = curl_easy_perform(curl_handle);
	if (CURLE_OK != res_curl)
	{
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res_curl));
		ret = R_FAIL;
	}
	else
	{
		ret = R_SUCCESS;
	}

	if (headers)
		curl_slist_free_all(headers);
		
	return ret;
}

/* Encapulating "hello" json object and return it's string shape. */
static char *json_encapulation_hello(void)
{
	json_t *obj = json_object();
	
	json_object_set_new(obj, "UCM", json_string("Hello! AVS."));
	
	return json_dumps(obj, JSON_COMPACT);
}

/* Encapulating global parameters json object and return it's string shape. */
static char *json_encapulation_set_global_param(struct avs_global_param *param)
{
	json_t *obj_setparam = json_object();
	json_t *obj_stun = json_object();
	json_t *obj_turn = json_object();

	json_object_set_new(obj_stun, "address", json_string(param->stun_ipaddr));
	json_object_set_new(obj_stun, "port", json_integer(param->stun_port));
	json_object_set_new(obj_turn, "address", json_string(param->turn_ipaddr));
	json_object_set_new(obj_turn, "port", json_integer(param->turn_port));
	json_object_set_new(obj_turn, "username", json_string(param->turn_username));
	json_object_set_new(obj_turn, "password", json_string(param->turn_username));
	json_object_set_new(obj_setparam, "stunserver", obj_stun);
	json_object_set_new(obj_setparam, "turnserver", obj_turn);
	
	return json_dumps(obj_setparam, JSON_COMPACT);
}

AVS_CMD_RESULT avs_playsound(struct avs_playsound_chan_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_runctrl_chan(struct avs_runctrl_chan_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_set_codec_param(struct avs_codec_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_set_peerport_param_normal(struct avs_set_peerport_normal_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;	
}

AVS_CMD_RESULT avs_set_peerport_param_ice(struct avs_set_peerport_ice_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;	
}

AVS_CMD_RESULT avs_alloc_port_normal(struct avs_alloc_port_normal_param *param, struct avs_alloc_port_normal_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_alloc_port_ice(struct avs_alloc_port_ice_param *param, struct avs_alloc_port_ice_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_dealloc_port(struct avs_dealloc_port_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;	
}

AVS_CMD_RESULT avs_set_global_param(struct avs_global_param *param, struct avs_common_resp_info *resp)
{
	char *json_s = NULL;
	AVS_CMD_RESULT result = ERROR;

	json_s = json_encapulation_set_global_param(param);
	if (!json_s)
		return result;

	if (curl_handle)
	{
		if (http_send(json_s) == R_SUCCESS)
			result = SUCCESS;
	}
	
	free(json_s);
	
	return result;
}

AVS_CMD_RESULT avs_create_conn(void)
{
	char *json_s;
	AVS_CMD_RESULT result = ERROR;

	if (http_init() == R_FAIL)
		return result;

	json_s = json_encapulation_hello();
	if (!json_s)
		return result;
	
	if (http_send(json_s) == R_SUCCESS)
		result = SUCCESS;

	return result;
}

void avs_shutdown(void)
{
	http_shutdown();
}

#if 1
/* main - Just for testing APIs..*/
int main(void)
{
	struct avs_global_param param;
	struct avs_common_resp_info resp;

	if (avs_create_conn() != SUCCESS) 
	{
		printf("connect AVS failed\n");
		return -1;
	}

	strcpy(param.stun_ipaddr, "192.168.3.3");
	param.stun_port = 5333;
	strcpy(param.turn_ipaddr, "192.168.5.5");
	param.turn_port = 6333;
	strcpy(param.turn_username, "zhoulei");
	strcpy(param.turn_password, "123456789");

	if (avs_set_global_param(&param, &resp) == SUCCESS)
		printf("Good Job!\n");
	else
		printf("Fuck!\n");
		
	avs_shutdown();
		
	return 0;
}
#endif
