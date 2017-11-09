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
#include "include/avs_controller.h"

#define AVS_URL		"http://127.0.0.1:8080/com/grandstream/httpjson/avs"
//#define AVS_URL		"http://192.168.124.172:80"

static CURL *curl_handle = NULL;

typedef enum func_return {
	R_SUCCESS,
	R_FAIL
} FUNC_RETURN;

static FUNC_RETURN http_init(void);
static FUNC_RETURN http_shutdown(void);
static FUNC_RETURN http_send(char *payload);
static char *json_encapulation_hello(void);
static char *json_encapulation_setparam(struct avs_param *param);

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

static char *json_encapulation_hello(void)
{
	json_t *obj = json_object();
	
	json_object_set_new(obj, "UCM", json_string("Hello! AVS."));
	
	return json_dumps(obj, JSON_COMPACT);
}

static char *json_encapulation_setparam(struct avs_param *param)
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
	json_object_set_new(obj_turn, "protocal", json_integer(param->turn_protocal));
	json_object_set_new(obj_setparam, "stunserver", obj_stun);
	json_object_set_new(obj_setparam, "turnserver", obj_turn);
	
	return json_dumps(obj_setparam, JSON_COMPACT);
}

AVS_CMD_RESULT avs_setparam(struct avs_param *param, struct avs_common_resp_info *resp)
{
	char *json_s = NULL;
	AVS_CMD_RESULT result = ERROR;

	json_s = json_encapulation_setparam(param);
	if (!json_s)
		return result;

	if (curl_handle)
	{
		if (R_SUCCESS == http_send(json_s))
			result = SUCCESS;
	}
	
	free(json_s);
	
	return result;
}

AVS_CMD_RESULT avs_init(void)
{
	char *json_s;
	AVS_CMD_RESULT result = ERROR;

	if (R_FAIL == http_init())
		return result;

	json_s = json_encapulation_hello();
	if (!json_s)
		return result;
	
	if (R_SUCCESS == http_send(json_s))
		result = SUCCESS;

	return result;
}

void avs_shutdown(void)
{
	http_shutdown();
}

int main(void)
{
	struct avs_param param;
	struct avs_common_resp_info resp;

	if (avs_init() != SUCCESS)
	{
		fprintf(stderr, "Connect to AVS failed!\n");
		return 0;
	}

	strcpy(param.stun_ipaddr, "192.168.1.1");
	param.stun_port = 5333;
	strcpy(param.turn_ipaddr, "192.168.2.2");
	param.turn_port = 6333;
	strcpy(param.turn_username, "zhoulei");
	strcpy(param.turn_password, "123456789");
	param.turn_protocal = PROTOCAL1;
	strcpy(param.moh_filepath, "/home/mohfile/en");

	if (avs_setparam(&param, &resp) == SUCCESS)
		printf("Good Job!\n");
	else
		printf("Fuck!\n");
		
	avs_shutdown();
		
	return 0;
}





