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
#include <jansson.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "avs_controller.h"

#define AVS_SERVER_SOCKET_PATH		"/tmp/GSSFUSrv"	/* Unix socket file path. Server. */
#define AVS_CLIENT_SOCKET_PATH		"/tmp/GSTmp"	/* Unix socket file path. Client. */

#define RECV_BUFFER_SIZE		2000	/* Buffer size for receiving AVS messages. */

#define MAXIMUM_CMD_TIMEOUT		5	/* Timeout waiting for AVS to response. */

static char *recv_buffer = NULL;

static int sockfd = -1;	/* Unix socket for communication with AVS. */

static pthread_t recv_thread;	/* A thread used to receive messages sent by AVS. May be responses or notifications. */
static pthread_cond_t cond_v;	/* Conditional variable between the receiving thread and the sending command thread. */
static pthread_mutex_t p_mutex;	/* Mutual exclusion for synchronization. avs_controller handles commands in a serial way. */

/* Command type. */
typedef enum command_type
{
	ST_AVS_SET_GLOBAL_PARAM,
	ST_AVS_ALLOC_PORT_NORMAL,
	ST_AVS_ALLOC_PORT_ICE,
	ST_AVS_DEALLOC_PORT,
	ST_AVS_SET_PEERPORT_PARAM_NORMAL,
	ST_AVS_SET_PEERPORT_PARAM_ICE,
	ST_AVS_SET_AUDIO_CODEC_PARAM,
	ST_AVS_SET_VIDEO_CODEC_PARAM,
	ST_AVS_RUNCTRL_CHAN,
	ST_AVS_PLAYSOUND,
	ST_AVS_IDLE
} CMD_TYPE_STATE;

/* The return value of the function. */
typedef enum func_return
{
	R_SUCCESS,
	R_FAIL
} FUNC_RETURN;

/* The result of JSON message parsing received from AVS. */
typedef enum msg_parse_from_avs_result
{
	MSG_PARSE_RESULT_FAIL = -1,
	MSG_PARSE_RESULT_SUCCESS
} MSG_PARSE_RESULT;

/* Data structure for storing common response received from AVS. */
struct resp_common_info
{
	unsigned int code;
	char message[MAX_MESSAGE_REPONSE];
	char comm_id[MAX_UNIQUE_ID];
};

/* Data structure for storing "alloc_port_normal" response received from AVS. */
struct resp_alloc_port_normal_info
{
	unsigned int rtp_port;
	unsigned int rtcp_port;
	char port_id[MAX_PORTID_LEN];
	char fingerprint[MAX_FINGERPRINT_LEN];
	char comm_id[MAX_UNIQUE_ID];
	struct resp_common_info common_resp;
};

/* Data structure for storing "alloc_port_ice" response received from AVS. */
struct resp_alloc_port_ice_info
{
	char ice_ufrag[MAX_ICE_UFRAG];
	char ice_pwd[MAX_ICE_PASSWROD];
	char fingerprint[MAX_FINGERPRINT_LEN];
	char port_id[MAX_PORTID_LEN];
	char comm_id[MAX_UNIQUE_ID];
	struct candidate *candidates;
	struct resp_common_info common_resp;
};

/* Global data area section. */
static CMD_TYPE_STATE cmd_current_state = ST_AVS_IDLE;	/* Context of the current command. */
static MSG_PARSE_RESULT msg_parse_result = MSG_PARSE_RESULT_SUCCESS;	/* Parsing result of message received from AVS. */
static struct resp_common_info g_data_storage_resp_common;	/* Global data area for storing common response received from AVS. */
static struct resp_alloc_port_normal_info g_data_storage_resp_alloc_port_normal;	/* Global data area for storing "alloc_port_normal" response received from AVS. */
static struct resp_alloc_port_ice_info g_data_storage_resp_alloc_port_ice;	/* Global data area for storing "alloc_port_ice" response received from AVS. */
/* */

/* Generel abstract functions section. */
static FUNC_RETURN general_action(void *param, void *resp, CMD_TYPE_STATE cmd_type);
static void *general_json_dec(char *msg);
static const char *general_json_enc(void *param, CMD_TYPE_STATE cmd_type);
static void *general_fill_resp(void *resp, CMD_TYPE_STATE cmd_type);
/* */

/* Synchronism section.*/
static int wait_on_socket();
static void *wakeup_intruder();
static FUNC_RETURN wait_for_avs();
static void *recv_task(void *data);
/* */

/* Module init section. */
static void *data_init();
static FUNC_RETURN sock_init(void);
static FUNC_RETURN cmd_send(const char *cmd);
static FUNC_RETURN msg_recv_process(char *msg);
/* */

/* Encode JSON section. */
static const char *enc_json_set_global_param(struct avs_global_param *param);
static const char *enc_json_alloc_port_normal(struct avs_alloc_port_normal_param *param);
static const char *enc_json_alloc_port_ice(struct avs_alloc_port_ice_param *param);
static const char *enc_json_del_port(struct avs_dealloc_port_param *param);
static const char *enc_json_set_peerport_normal(struct avs_set_peerport_normal_param *param);
static const char *enc_json_set_peerport_ice(struct avs_set_peerport_ice_param *param);
static const char *enc_json_set_audio_codec(struct avs_codec_audio_param *param);
static const char *enc_json_set_video_codec(struct avs_codec_video_param *param);
/* */

/* Decode and fillback section. */
static FUNC_RETURN dec_json_common_resp(json_t *root, struct resp_common_info *resp);
static FUNC_RETURN dec_json_alloc_port_normal_resp(json_t *root, struct resp_alloc_port_normal_info *resp);
static FUNC_RETURN dec_json_alloc_port_ice_resp(json_t *root, struct resp_alloc_port_ice_info *resp);
static void *fill_common_resp(struct avs_common_resp_info *resp);
static void *fill_alloc_port_normal_resp(struct avs_alloc_port_normal_resp_info *resp);
static void *fill_alloc_port_ice_resp(struct avs_alloc_port_ice_resp_info *resp);
/* */

static const struct codec_audio_tran {
	enum avs_audio_codec codec;
	const char *name;
} codec_audio_trans[] = {
	{ AVS_AUDIO_CODEC_PCMU, "audio/pcmu" },
	{ AVS_AUDIO_CODEC_PCMA, "audio/pcma" },
	{ AVS_AUDIO_CODEC_GSM, "audio/gsm" },
	{ AVS_AUDIO_CODEC_ILBC, "audio/ilbc" },
	{ AVS_AUDIO_CODEC_G722, "audio/g722" },
	{ AVS_AUDIO_CODEC_G722_1, "audio/g722.1" },
	{ AVS_AUDIO_CODEC_G722_1C, "audio/g722.1c" },
	{ AVS_AUDIO_CODEC_G729, "audio/g729"},
	{ AVS_AUDIO_CODEC_G723_1, "audio/g723.1"},
	{ AVS_AUDIO_CODEC_G726, "audio/adpcm32"},
	{ AVS_AUDIO_CODEC_OPUS, "audio/opus"},
};

static const struct codec_video_tran {
	enum avs_video_codec codec;
	const char *name;
} codec_video_trans[] = {
	{ AVS_VIDEO_CODEC_H264, "video/avc" },
	{ AVS_VIDEO_CODEC_H265, "video/hevc" },
	{ AVS_VIDEO_CODEC_VP8, "video/vp8" },
	{ AVS_VIDEO_CODEC_VP9, "video/vp9" },
};

enum media_transmode
{
	MEDIA_TRANSMODE_SENDRECV = 1,
	MEDIA_TRANSMODE_SENDONLY,
	MEDIA_TRANSMODE_RECVONLY
};

static const struct transmode {
	enum media_transmode mode;
	const char *name;
} transmodes[] = {
	{ MEDIA_TRANSMODE_SENDONLY, "sendOnly" },
	{ MEDIA_TRANSMODE_RECVONLY, "recvOnly" },
	{ MEDIA_TRANSMODE_SENDRECV, "sendRecv" },
};

/* Fill the common type response data to the command requester. */
static void *fill_common_resp(struct avs_common_resp_info *resp)
{
	resp->code = g_data_storage_resp_common.code;
	strncpy(resp->message, g_data_storage_resp_common.message, sizeof(resp->message));
	strncpy(resp->comm_id, g_data_storage_resp_common.comm_id, sizeof(resp->comm_id) - 1);
	
	return NULL;
}

/* Fill the "alloc_port_normal" type response data to the command requester. */
static void *fill_alloc_port_normal_resp(struct avs_alloc_port_normal_resp_info *resp)
{
	resp->rtp_port = g_data_storage_resp_alloc_port_normal.rtp_port;
	resp->rtcp_port = g_data_storage_resp_alloc_port_normal.rtcp_port;
	strncpy(resp->port_id, g_data_storage_resp_alloc_port_normal.port_id, sizeof(resp->port_id) - 1);
	strncpy(resp->fingerprint, g_data_storage_resp_alloc_port_normal.fingerprint, sizeof(resp->fingerprint) - 1);
	strncpy(resp->comm_id, g_data_storage_resp_alloc_port_normal.comm_id, sizeof(resp->comm_id) - 1);
	
	resp->resp.code = g_data_storage_resp_alloc_port_normal.common_resp.code;
	strncpy(resp->resp.message, g_data_storage_resp_alloc_port_normal.common_resp.message, sizeof(resp->resp.message) - 1);
	
	return NULL;
}

static void *fill_alloc_port_ice_resp(struct avs_alloc_port_ice_resp_info *resp)
{
	strncpy(resp->port_id, g_data_storage_resp_alloc_port_ice.port_id, sizeof(resp->port_id) - 1);
	strncpy(resp->ice_ufrag, g_data_storage_resp_alloc_port_ice.ice_ufrag, sizeof(resp->ice_ufrag) - 1);
	strncpy(resp->ice_pwd, g_data_storage_resp_alloc_port_ice.ice_ufrag, sizeof(resp->ice_pwd) - 1);
	strncpy(resp->fingerprint, g_data_storage_resp_alloc_port_ice.fingerprint, sizeof(resp->fingerprint) - 1);
	
	resp->resp.code = g_data_storage_resp_alloc_port_ice.common_resp.code;
	strncpy(resp->resp.message, g_data_storage_resp_alloc_port_ice.common_resp.message, sizeof(resp->resp.message) - 1);
		
	return NULL;	
}

/* Parse a common type of JSON message. */
static FUNC_RETURN dec_json_common_resp(json_t *root, struct resp_common_info *resp)
{
	json_t *error, *code, *message;
	
	if ((error = json_object_get(root, "error")))
	{
		if (!json_is_object(error))
		{
			printf("error: error is not an object\n");
			return R_FAIL;
		}

		if ((code = json_object_get(error, "code")))
		{
			if (!json_is_integer(code))
			{
				printf("error: code is not an integer\n");
				return R_FAIL;
			}
			printf("resp code: %d\n", (int)json_integer_value(code));
			resp->code = (unsigned int)json_integer_value(code);
		}
		
		if ((message = json_object_get(error, "message")))
		{
			printf("resp message: %s\n", json_string_value(message));
			if (json_is_string(message))
			{
				strncpy(resp->message, json_string_value(message), sizeof(resp->message) - 1);
			}
			else
			{
				strcpy(resp->message, "Nothing to Say! Fuck U!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
			}
		}
	}
	else
	{
		printf("decode error oject failed\n");
		return R_FAIL;
	}

	return R_SUCCESS;
}

/* Parse a "alloc_port_normal" type of JSON message. */
static FUNC_RETURN dec_json_alloc_port_normal_resp(json_t *root, struct resp_alloc_port_normal_info *resp)
{
	json_t *infoport, *port_id, *rtp_port, *rtcp_port, *fingerprint;
	
	if (dec_json_common_resp(root, &(resp->common_resp)) != R_SUCCESS)
	{
		printf("decode JSON from AVS failed (\"common\" resp in \"alloc_port_normal\").\n");
		msg_parse_result = MSG_PARSE_RESULT_FAIL;
		return R_FAIL;
	}
	
	if ((port_id = json_object_get(root, "port_id")))
	{
		if (!json_is_string(port_id))
		{
			printf("error: port_id is not an string\n");
			return R_FAIL;
		}
		printf("port_id: %s\n", json_string_value(port_id));
		strncpy(resp->port_id, json_string_value(port_id), sizeof(resp->port_id) - 1);
	}
	
	if ((infoport = json_object_get(root, "InfoPort")))
	{
		if (!json_is_object(infoport))
		{
			printf("error: infoport is not an object\n");
			return R_FAIL;
		}
		
		if ((rtp_port = json_object_get(infoport, "rtp_port")))
		{
			if (!json_is_string(rtp_port))
			{
				printf("error: rtp_port is not an string\n");
				return R_FAIL;
			}
			printf("rtp_port: %s\n", json_string_value(rtp_port));
			resp->rtp_port = (unsigned int)atoi(json_string_value(rtp_port));
		}
		
		if ((rtcp_port = json_object_get(infoport, "rtcp_port")))
		{
			if (!json_is_string(rtcp_port))
			{
				printf("error: rtcp_port is not an string\n");
				return R_FAIL;
			}
			printf("rtcp_port: %s\n", json_string_value(rtcp_port));
			resp->rtcp_port = (unsigned int)atoi(json_string_value(rtcp_port));
		}
		if ((fingerprint = json_object_get(infoport, "fingerprint")))
		{
			if (json_is_string(fingerprint))
			{
				strncpy(resp->fingerprint, json_string_value(fingerprint), sizeof(resp->fingerprint) - 1);
			}
			else
			{
				strcpy(resp->fingerprint, "don't need fingerprint.\n");
			}
			printf("resp fingerprint: %s\n", json_string_value(fingerprint));
		}
		
	}
	else
	{
		printf("decode InfoPort object failed.\n");
		return R_FAIL;		
	}
	
	return R_SUCCESS;
}

/* Parse a "alloc_port_ice" type of JSON message. */
static FUNC_RETURN dec_json_alloc_port_ice_resp(json_t *root, struct resp_alloc_port_ice_info *resp)
{
	json_t *infoice, *port_id, *candidate, *fingerprint, *ice_ufrag, *ice_pwd;
	
	if (dec_json_common_resp(root, &(resp->common_resp)) != R_SUCCESS)
	{
		printf("decode JSON from AVS failed (\"common\" resp in \"alloc_port_ice\").\n");
		msg_parse_result = MSG_PARSE_RESULT_FAIL;
		return R_FAIL;
	}
	
	if ((port_id = json_object_get(root, "port_id")))
	{
		if (!json_is_string(port_id))
		{
			printf("error: port_id is not an string\n");
			return R_FAIL;
		}
		printf("port_id: %s\n", json_string_value(port_id));
		strncpy(resp->port_id, json_string_value(port_id), sizeof(resp->port_id) - 1);
	}
	
	if ((infoice = json_object_get(root, "InfoICE")))
	{
		if (!json_is_object(infoice))
		{
			printf("error: InfoICE is not an object\n");
			return R_FAIL;
		}
		
		if ((candidate = json_object_get(infoice, "candidate")))
		{
			if (!json_is_array(candidate))
			{
				printf("error: candidate is not an array\n");
				return R_FAIL;
			}
			strcpy(resp->candidates->cands_str, "candidate:190205851 0 udp 2122260224 192.168.124.110 57391 typ host generation 0 ufrag XY1f network-id 1 network-cost 50");
			resp->candidates->next = NULL;
			//printf("candidate: %s\n", json_string_value(candidate));
		}
		
		if ((fingerprint = json_object_get(infoice, "fingerprint")))
		{
			if (json_is_string(fingerprint))
			{
				strncpy(resp->fingerprint, json_string_value(fingerprint), sizeof(resp->fingerprint) - 1);
			}
			else
			{
				strcpy(resp->fingerprint, "don't need fingerprint.\n");
			}
			printf("resp fingerprint: %s\n", json_string_value(fingerprint));
		}
		
		if ((ice_ufrag = json_object_get(infoice, "ice_ufrag")))
		{
			if (json_is_string(ice_ufrag))
			{
				strncpy(resp->ice_ufrag, json_string_value(ice_ufrag), sizeof(resp->ice_ufrag) - 1);
			}
			else
			{
				strcpy(resp->ice_ufrag, "don't need ice_ufrag.\n");
			}
			printf("resp ice_ufrag: %s\n", json_string_value(ice_ufrag));
		}
		
		if ((ice_pwd = json_object_get(infoice, "ice_pwd")))
		{
			if (json_is_string(ice_pwd))
			{
				strncpy(resp->ice_pwd, json_string_value(ice_pwd), sizeof(resp->ice_pwd) - 1);
			}
			else
			{
				strcpy(resp->ice_pwd, "don't need ice_pwd.\n");
			}
			printf("resp ice_pwd: %s\n", json_string_value(ice_pwd));
		}
	}
	else
	{
		printf("decode infoice object failed.\n");
		return R_FAIL;		
	}
	
	return R_SUCCESS;
}

/* Encapsulating "setParam" JSON object and return it's string shape. */
static const char *enc_json_set_global_param(struct avs_global_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_setparam = json_object();
	json_t *obj_stun = json_object();
	json_t *obj_turn = json_object();
	json_t *array_stun = json_array();
	json_t *array_turn = json_array();
	char stun_port[6], turn_port[6];
	
	sprintf(stun_port, "%d", param->stun_port);
	sprintf(turn_port, "%d", param->turn_port);
	
	json_object_set_new(obj_stun, "address", json_string(param->stun_ipaddr));
	json_object_set_new(obj_stun, "port", json_string(stun_port));
	json_object_set_new(obj_turn, "address", json_string(param->turn_ipaddr));
	json_object_set_new(obj_turn, "port", json_string(turn_port));
	json_object_set_new(obj_turn, "username", json_string(param->turn_username));
	json_object_set_new(obj_turn, "password", json_string(param->turn_username));
	
	json_array_insert_new(array_stun, 0, obj_stun);
	json_array_insert_new(array_turn, 0, obj_turn);

	json_object_set_new(obj_setparam, "stunserver", array_stun);
	json_object_set_new(obj_setparam, "turnserver", array_turn);
	
	json_object_set_new(obj_top, "setParam", obj_setparam);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Encapsulating "addPort" JSON object with normal mode and return it's string shape. */
static const char *enc_json_alloc_port_normal(struct avs_alloc_port_normal_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_addport = json_object();
	char dtls[2] = {0};
	
	sprintf(dtls, "%d", param->enable_dtls);
	
	json_object_set_new(obj_addport, "conf_id", json_string(param->conf_id));
	json_object_set_new(obj_addport, "chan_id", json_string(param->chan_id));
	json_object_set_new(obj_addport, "ICE", json_string("0"));	/* set to "0" */
	json_object_set_new(obj_addport, "DTLS", json_string(dtls));
	
	json_object_set_new(obj_top, "addPort", obj_addport);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Encapsulating "addPort" JSON object with ICE mode and return it's string shape. */
static const char *enc_json_alloc_port_ice(struct avs_alloc_port_ice_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_addport = json_object();
	char dtls[2] = {0};
	
	sprintf(dtls, "%d", param->enable_dtls);
	
	json_object_set_new(obj_addport, "conf_id", json_string(param->conf_id));
	json_object_set_new(obj_addport, "chan_id", json_string(param->chan_id));
	json_object_set_new(obj_addport, "ICE", json_string("1"));	/* set to "1" */
	json_object_set_new(obj_addport, "DTLS", json_string(dtls));
	
	json_object_set_new(obj_top, "addPort", obj_addport);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Encapsulating "delPort" JSON object and return it's string shape. */
static const char *enc_json_del_port(struct avs_dealloc_port_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_delport = json_object();
	
	json_object_set_new(obj_delport, "conf_id", json_string(param->conf_id));
	json_object_set_new(obj_delport, "chan_id", json_string(param->chan_id));
	json_object_set_new(obj_delport, "port_id", json_string(param->port_id));
	
	json_object_set_new(obj_top, "delPort", obj_delport);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Encapsulating "setPortParam" JSON object with normal mode and return it's string shape. */
static const char *enc_json_set_peerport_normal(struct avs_set_peerport_normal_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_setportparam = json_object();
	json_t *obj_infoport = json_object();
	char rtcpmux[2] = {0};
	char symrtp[2] = {0};
	char qos[6] = {0};
	char srtpmode[2] = {0};
	
	sprintf(rtcpmux, "%d", param->rtcpmux);
	sprintf(symrtp, "%d", param->symrtp);
	sprintf(qos, "%d", param->qos);
	sprintf(srtpmode, "%d", param->srtpmode);
	
	json_object_set_new(obj_infoport, "targetAddr", json_string(param->targetaddr));
	json_object_set_new(obj_infoport, "RtcpMux", json_string(rtcpmux));
	json_object_set_new(obj_infoport, "SymRTP", json_string(symrtp));
	json_object_set_new(obj_infoport, "Qos", json_string(qos));
	json_object_set_new(obj_infoport, "srtpMode", json_string(srtpmode));
	json_object_set_new(obj_infoport, "srtpSendKey", json_string(param->srtpsendkey));
	json_object_set_new(obj_infoport, "srtpRecvKey", json_string(param->srtprecvkey));
	json_object_set_new(obj_infoport, "fingerprint", json_string(param->fingerprint));
	
	json_object_set_new(obj_setportparam, "conf_id", json_string(param->conf_id));
	json_object_set_new(obj_setportparam, "chan_id", json_string(param->chan_id));
	json_object_set_new(obj_setportparam, "port_id", json_string(param->port_id));
	json_object_set_new(obj_setportparam, "InfoPort", obj_infoport);

	json_object_set_new(obj_top, "setPortParam", obj_setportparam);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Encapsulating "setPortParam" JSON object with ICE mode and return it's string shape. */
static const char *enc_json_set_peerport_ice(struct avs_set_peerport_ice_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_setportparam = json_object();
	json_t *obj_infoice = json_object();
	char icerole[2] = {0};
	char sslrole[2] = {0};

	sprintf(icerole, "%d", param->icerole);
	sprintf(sslrole, "%d", param->sslrole);
	
	json_object_set_new(obj_infoice, "IceRole", json_string(icerole));
	json_object_set_new(obj_infoice, "SslRole", json_string(sslrole));
	json_object_set_new(obj_infoice, "fingerprint", json_string(param->fingerprint));
	json_object_set_new(obj_infoice, "ice_ufrag", json_string(param->ice_ufrag));
	json_object_set_new(obj_infoice, "ice_pwd", json_string(param->ice_pwd));
	json_object_set_new(obj_infoice, "candidate", json_string(param->candidate));
	
	json_object_set_new(obj_setportparam, "conf_id", json_string(param->conf_id));
	json_object_set_new(obj_setportparam, "chan_id", json_string(param->chan_id));
	json_object_set_new(obj_setportparam, "port_id", json_string(param->port_id));
	json_object_set_new(obj_setportparam, "InfoICE", obj_infoice);

	
	json_object_set_new(obj_top, "setPortParam", obj_setportparam);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Encapsulating "addTrack" JSON object with audio param and return it's string shape. */
static const char *enc_json_set_audio_codec(struct avs_codec_audio_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_addtrack = json_object();
	json_t *obj_a_tx_param = json_object();
	json_t *obj_a_rx_param = json_object();
	json_t *audio_transport = json_object();
	
	char a_payloadtype[6] = {0};
	char a_ptime[6] = {0};
	
	sprintf(a_payloadtype, "%d", param->audio_payloadtype);
	sprintf(a_ptime, "%d", param->ptime);
	
	json_object_set_new(obj_a_tx_param, "MainCoder", json_string(codec_audio_trans[param->a_codec].name));
	json_object_set_new(obj_a_tx_param, "PayloadType", json_string(a_payloadtype));
	json_object_set_new(obj_a_tx_param, "Ptime", json_string(a_ptime));
	
	json_object_set_new(obj_a_rx_param, "Codecs", json_string(codec_audio_trans[param->a_codec].name));
	json_object_set_new(obj_a_rx_param, "PayloadType", json_string(a_payloadtype));
	
	json_object_set_new(audio_transport, "audio_transport", json_string(transmodes[param->audio_transmode].name));
	
	json_object_set_new(obj_addtrack, "conf_id", json_string(param->conf_id));
	json_object_set_new(obj_addtrack, "chan_id", json_string(param->chan_id));
	json_object_set_new(obj_addtrack, "port_id", json_string(param->port_id));
	json_object_set_new(obj_addtrack, "track_id", json_string("222222222222222"));
	json_object_set_new(obj_addtrack, "mediaType", json_string("audio"));
	json_object_set_new(obj_addtrack, "audio_tx_param", obj_a_tx_param);
	json_object_set_new(obj_addtrack, "audio_rx_param", obj_a_rx_param);
	json_object_set_new(obj_addtrack, "audio_transport", audio_transport);
	
	json_object_set_new(obj_top, "addTrack", obj_addtrack);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Encapsulating "addTrack" JSON object with video param and return it's string shape. */
static const char *enc_json_set_video_codec(struct avs_codec_video_param *param)
{
	json_t *obj_top = json_object();
	json_t *obj_addtrack = json_object();
	json_t *obj_v_tx_param = json_object();
	json_t *obj_v_rx_param = json_object();
	json_t *video_transport = json_object();
	
	char v_payloadtype[6] = {0};
	
	sprintf(v_payloadtype, "%d", param->video_payloadtype);
	
	json_object_set_new(obj_v_tx_param, "MainCoder", json_string(codec_video_trans[param->v_codec].name));
	json_object_set_new(obj_v_tx_param, "PayloadType", json_string(v_payloadtype));
	
	json_object_set_new(obj_v_rx_param, "Codecs", json_string(codec_video_trans[param->v_codec].name));
	json_object_set_new(obj_v_rx_param, "PayloadType", json_string(v_payloadtype));
	
	json_object_set_new(video_transport, "video_transport", json_string(transmodes[param->video_transmode].name));
	
	json_object_set_new(obj_addtrack, "conf_id", json_string(param->conf_id));
	json_object_set_new(obj_addtrack, "chan_id", json_string(param->chan_id));
	json_object_set_new(obj_addtrack, "port_id", json_string(param->port_id));
	json_object_set_new(obj_addtrack, "track_id", json_string("222222222222222"));
	json_object_set_new(obj_addtrack, "mediaType", json_string("video"));
	json_object_set_new(obj_addtrack, "video_tx_param", obj_v_tx_param);
	json_object_set_new(obj_addtrack, "video_rx_param", obj_v_rx_param);
	json_object_set_new(obj_addtrack, "video_transport", video_transport);
	
	json_object_set_new(obj_top, "addTrack", obj_addtrack);
	json_object_set_new(obj_top, "id", json_string(param->comm_id));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Initialize data. */
static void *data_init()
{
	memset(&g_data_storage_resp_common, 0, sizeof(g_data_storage_resp_common));
	g_data_storage_resp_common.code = -1;
	
	memset(&g_data_storage_resp_alloc_port_normal, 0, sizeof(g_data_storage_resp_alloc_port_normal));

	memset(&g_data_storage_resp_alloc_port_ice, 0, sizeof(g_data_storage_resp_alloc_port_ice));
	
	return NULL;
}

/* socket Initialization */
static FUNC_RETURN sock_init(void)
{
	struct sockaddr_un sock_addr;
	
	sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	
	if (sockfd < 0)
	{
		printf("Open a socket failed\n");
		return R_FAIL;
	}
	
	unlink(AVS_CLIENT_SOCKET_PATH);
	
	memset(&sock_addr, 0, sizeof(struct sockaddr_un));
	
	sock_addr.sun_family = AF_UNIX;
	strncpy(sock_addr.sun_path, AVS_CLIENT_SOCKET_PATH, sizeof(sock_addr.sun_path) - 1);
	
	if (bind(sockfd, (const struct sockaddr *) &sock_addr, sizeof(struct sockaddr_un)) == -1)
	{
		perror("bind socket failed");
		close(sockfd);
		return R_FAIL;
	}
	
	return R_SUCCESS;
}

/* Send the command to AVS */
static FUNC_RETURN cmd_send(const char *cmd)
{
	int sent_num = 0;
	struct sockaddr_un sock_addr;
	
	if (sockfd)
	{
		memset(&sock_addr, 0, sizeof(struct sockaddr_un));
		
		sock_addr.sun_family = AF_UNIX;
		strncpy(sock_addr.sun_path, AVS_SERVER_SOCKET_PATH, sizeof(sock_addr.sun_path) - 1);
		
		printf("sent cmd is %s\n", cmd);
		
		sent_num = sendto(sockfd, cmd, strlen(cmd), 0, (struct sockaddr *)(&sock_addr), sizeof(struct sockaddr_un));
		
		if (sent_num <= 0) {
			printf("send commands to AVS failed\n");
			return R_FAIL;	
		}
		
		return R_SUCCESS;
	}
	else
	{
		printf("Socket is closed\na");	
		return R_FAIL;
	}
}

/* Processing messages received from AVS.
 * 1. Parse JSON and store into the global data area.
 * 2. Wake up the thread which send the command.
 */
static FUNC_RETURN msg_recv_process(char *msg)
{
    printf("recv msg: %s\n", msg);
	
	general_json_dec(msg);
	
	memset(msg, 0, RECV_BUFFER_SIZE);
	
	wakeup_intruder();
	
	return R_SUCCESS;
}

/* Send single to wake up the thread which sent command to AVS. */
static void *wakeup_intruder()
{
	pthread_mutex_lock(&p_mutex);
	
	pthread_cond_signal(&cond_v);
	
	pthread_mutex_unlock(&p_mutex);
	
	return NULL;
}

/* Using "pthread_cond_wait" to wait for the response of the AVS. */
static FUNC_RETURN wait_for_avs()
{
	time_t tm;
	struct timespec timeout;
	FUNC_RETURN result = R_SUCCESS;
	int ret = 0;

	time(&tm);
	timeout.tv_sec = tm + MAXIMUM_CMD_TIMEOUT;
	timeout.tv_nsec = 0;

	ret = pthread_cond_timedwait(&cond_v, &p_mutex, &timeout);
	
	if (ret != 0)
	{
		if (EAGAIN == ret)
		{
			printf("avs response timeout.\n");
		}
		else if (EINVAL == ret)
		{
			printf("pthread_cond_timedwait: invalid arguments.\n");
		}
		else
		{
			printf("pthread_cond_timewait error, return value: %d\n", ret);	
		}
		result = R_FAIL;
	}
	
	return result;
}

/* Monitor file descriptor(sockfd). Waiting until the file descriptors become "ready" for some class of I/O operation (e.g., output possible.). */
static int wait_on_socket()
{
	struct timeval tv;
	fd_set rds;
	int res;

	tv.tv_sec = 10;	/* 10 sec??? */
	tv.tv_usec = 0;

	FD_ZERO(&rds);
	FD_SET(sockfd, &rds);
	
	res = select((int)sockfd + 1, &rds, NULL, NULL, &tv); 

	return res;
}

/* Main loop to receive and process messages from AVS. */
static void *recv_task(void *data)
{
	struct sockaddr_un peer_addr;
	socklen_t peer_addr_len;
	int ret = 0;
	ssize_t recv_num = 0;;

	for (;;)
	{
		if ((ret = wait_on_socket()) == 0) {
			printf("recv msg timeout\n");
		}
		else if (ret == -1)
		{
			printf("socket fd is not work\n");		
		}
		else
		{
			peer_addr_len = sizeof(struct sockaddr_un);
			recv_num = recvfrom(sockfd, recv_buffer, RECV_BUFFER_SIZE, 0, (struct sockaddr *) &peer_addr, &peer_addr_len);
			if (recv_num == -1) {
				printf("Receive data failed\n");
			}
			else
			{
				recv_buffer[recv_num] = '\0';
				if (msg_recv_process(recv_buffer) != R_SUCCESS)
				{
					printf("process responses from AVS failed\n");		
				}
			}
				
		}
	}
	
	return NULL;
}

/* General function of encapsulating JSON data. */
static const char *general_json_enc(void *param, CMD_TYPE_STATE cmd_type)
{
	const char *json_s = NULL;
	
	switch (cmd_type)
	{
		case ST_AVS_SET_GLOBAL_PARAM:
			{
				struct avs_global_param *p = (struct avs_global_param *)param;
				json_s = enc_json_set_global_param(p);
			}
			break;

		case ST_AVS_ALLOC_PORT_NORMAL:
			{
				struct avs_alloc_port_normal_param *p = (struct avs_alloc_port_normal_param *)param;
				json_s = enc_json_alloc_port_normal(p);
			}
			break;
			
		case ST_AVS_ALLOC_PORT_ICE:
			{
				struct avs_alloc_port_ice_param *p = (struct avs_alloc_port_ice_param *)param;
				json_s = enc_json_alloc_port_ice(p);
			}
			break;
			
		case ST_AVS_DEALLOC_PORT:
			{
				struct avs_dealloc_port_param  *p = (struct avs_dealloc_port_param *)param;
				json_s = enc_json_del_port(p);
			}
			break;
			
		case ST_AVS_SET_PEERPORT_PARAM_NORMAL:
			{
				struct avs_set_peerport_normal_param *p = (struct avs_set_peerport_normal_param *)param;
				json_s = enc_json_set_peerport_normal(p);
			}
			break;
			
		case ST_AVS_SET_PEERPORT_PARAM_ICE:
			{
				struct avs_set_peerport_ice_param *p = (struct avs_set_peerport_ice_param *)param;
				json_s = enc_json_set_peerport_ice(p);
			}
			break;
			
		case ST_AVS_SET_AUDIO_CODEC_PARAM:
			{
				struct avs_codec_audio_param *p = (struct avs_codec_audio_param *)param;
				json_s = enc_json_set_audio_codec(p);
			}
			break;
			
		case ST_AVS_SET_VIDEO_CODEC_PARAM:
			{
				struct avs_codec_video_param *p = (struct avs_codec_video_param *)param;
				json_s = enc_json_set_video_codec(p);
			}
			break;
			
		default:
			break;
	}
	
	return json_s;
}

/* General function of decoding JSON data. */
void *general_json_dec(char *msg)
{
	json_t *root;
	json_error_t error;
	json_t *id;
	
	root = json_loads(msg, 0, &error);

	if (!root)
	{
		printf("json load error: on line %d: %s\n", error.line, error.text);
		msg_parse_result = MSG_PARSE_RESULT_FAIL;
		return NULL;
	}
	
	if ((id = json_object_get(root, "id")))
	{
		if (!json_is_string(id))
		{
			printf("error: id is not a string\n");
			msg_parse_result = MSG_PARSE_RESULT_FAIL;
			return NULL;
		}
		printf("resp id: %s\n", json_string_value(id));
	}
	else
	{
		printf("Maybe, It's a notification from AVS.....!");
		msg_parse_result = MSG_PARSE_RESULT_FAIL;
		return NULL;	
	}
	
	switch (cmd_current_state)
	{
		case ST_AVS_IDLE:
			/* do nothing. */
			break;
			
		case ST_AVS_SET_GLOBAL_PARAM:
		case ST_AVS_SET_PEERPORT_PARAM_NORMAL:
		case ST_AVS_SET_PEERPORT_PARAM_ICE:
			if (dec_json_common_resp(root, &g_data_storage_resp_common) != R_SUCCESS)
			{
				printf("decode json from AVS failed (\"common\" resp).\n");
				msg_parse_result = MSG_PARSE_RESULT_FAIL;
			}
			strncpy(g_data_storage_resp_common.comm_id, json_string_value(id), sizeof(g_data_storage_resp_common.comm_id) - 1);
			break;
			
		case ST_AVS_ALLOC_PORT_NORMAL:
			if (dec_json_alloc_port_normal_resp(root, &g_data_storage_resp_alloc_port_normal) != R_SUCCESS)
			{
				printf("decode json from AVS failed (\"alloc_port_normal\").\n");
				msg_parse_result = MSG_PARSE_RESULT_FAIL;
			}
			strncpy(g_data_storage_resp_alloc_port_normal.comm_id, json_string_value(id), sizeof(g_data_storage_resp_alloc_port_normal.comm_id) - 1);
			break;
			
		case ST_AVS_ALLOC_PORT_ICE:
			if (dec_json_alloc_port_ice_resp(root, &g_data_storage_resp_alloc_port_ice) != R_SUCCESS)
			{
				printf("decode json from AVS failed (\"alloc_port_ice\").\n");
				msg_parse_result = MSG_PARSE_RESULT_FAIL;
			}
			strncpy(g_data_storage_resp_alloc_port_ice.comm_id, json_string_value(id), sizeof(g_data_storage_resp_alloc_port_ice.comm_id) - 1);
			break;
			
		default:
			break;
	}
	
	json_decref(root);
	
	return NULL;

}// lalalala
//merge testing.
/* General function of backfilling response data to the caller */
static void *general_fill_resp(void *resp, CMD_TYPE_STATE cmd_type)
{
	switch (cmd_type)
	{
		case ST_AVS_SET_GLOBAL_PARAM:
		case ST_AVS_SET_PEERPORT_PARAM_NORMAL:
		case ST_AVS_SET_PEERPORT_PARAM_ICE:
			{
				struct avs_common_resp_info *r = (struct avs_common_resp_info *)resp;
				fill_common_resp(r);	/* Fill the message returned from the AVS to the command requester. */
			}
			break;
		
		case ST_AVS_ALLOC_PORT_NORMAL:
			{
				struct avs_alloc_port_normal_resp_info *r = (struct avs_alloc_port_normal_resp_info *)resp;
				fill_alloc_port_normal_resp(r);	/* Fill the message returned from the AVS to the command requester. */
			}
			break;
			
		case ST_AVS_ALLOC_PORT_ICE:
			{
				struct avs_alloc_port_ice_resp_info *r = (struct avs_alloc_port_ice_resp_info *)resp;
				fill_alloc_port_ice_resp(r);	
			}
			break;
			
		default:
			break;
	}
	
	int c = 0;
	
	return NULL;
}
//hzdev-----testing
/* General processing function of command request.
 * 1. Encapsulate JSON.
 * 2. Send JSON to AVS.
 * 3. Set the current context of command request.
 * 4. Wait for response from AVS(Conditional variable).
 * 5. Backfill response data to the caller.
 */
static FUNC_RETURN general_action(void *param, void *resp, CMD_TYPE_STATE cmd_type)
{
	const char *json_s = NULL;
	FUNC_RETURN ret = R_SUCCESS;
	
	if (-1 == sockfd)
	{
		printf("socket is not created!\n");
		return ERROR;		
	}
	
	pthread_mutex_lock(&p_mutex);
	
	if (!(json_s = general_json_enc(param, cmd_type)))
	{
		pthread_mutex_unlock(&p_mutex);
		return ERROR;
	}
	
	/* Send JSON message to AVS. */
	if (cmd_send(json_s) != R_SUCCESS)
	{
		pthread_mutex_unlock(&p_mutex);
		free((void *)json_s);
		return ERROR;
	}
	
	/* "json_s" is pointed to memory which allocated by the JSON Library, then is no longer useful, so we can free it. */
	free((void *)json_s);
	
	/* Set current context of command. */
	cmd_current_state = cmd_type;
	
	/* waiting here... */
	ret = wait_for_avs();
	
	if (R_SUCCESS != ret)
	{
		printf("send command to AVS failed.\n");
		pthread_mutex_unlock(&p_mutex);
		return ERROR;	
	}
	
	/* If parse the JSON format error, return ERROR.  */
	if (MSG_PARSE_RESULT_FAIL == msg_parse_result)
	{
		pthread_mutex_unlock(&p_mutex);
		return ERROR;
	}
	
	/* Backfill response data to the caller. */
	general_fill_resp(resp, cmd_type);
	
	cmd_current_state = ST_AVS_IDLE;
	
	pthread_mutex_unlock(&p_mutex);
	
	return SUCCESS;
}

AVS_CMD_RESULT avs_playsound(struct avs_playsound_chan_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_runctrl_chan(struct avs_runctrl_chan_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_set_audio_codec_param(struct avs_codec_audio_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_set_video_codec_param(struct avs_codec_video_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;
}

AVS_CMD_RESULT avs_set_peerport_param_normal(struct avs_set_peerport_normal_param *param, struct avs_common_resp_info *resp)
{
	return general_action(param, resp, ST_AVS_SET_PEERPORT_PARAM_NORMAL);
}

AVS_CMD_RESULT avs_set_peerport_param_ice(struct avs_set_peerport_ice_param *param, struct avs_common_resp_info *resp)
{
	return general_action(param, resp, ST_AVS_SET_PEERPORT_PARAM_ICE);
}

AVS_CMD_RESULT avs_alloc_port_normal(struct avs_alloc_port_normal_param *param, struct avs_alloc_port_normal_resp_info *resp)
{
	return general_action(param, resp, ST_AVS_ALLOC_PORT_NORMAL);
}

AVS_CMD_RESULT avs_alloc_port_ice(struct avs_alloc_port_ice_param *param, struct avs_alloc_port_ice_resp_info *resp)
{
	return general_action(param, resp, ST_AVS_ALLOC_PORT_ICE);
}

AVS_CMD_RESULT avs_dealloc_port(struct avs_dealloc_port_param *param, struct avs_common_resp_info *resp)
{
	return SUCCESS;	
}

AVS_CMD_RESULT avs_set_global_param(struct avs_global_param *param, struct avs_common_resp_info *resp)
{
	return general_action(param, resp, ST_AVS_SET_GLOBAL_PARAM);
}

AVS_CMD_RESULT avs_create_conn(void)
{
	if (sock_init() != R_SUCCESS)
		return ERROR;
		
	if (!(recv_buffer = malloc(RECV_BUFFER_SIZE)))
	{
		printf("Malloc recv buffer failed\n");
		return ERROR;	
	}
	
	if (pthread_mutex_init(&p_mutex, NULL) != 0)
    {
    	printf("mutex init failed.\n");
        return ERROR;
    }
	
	if (pthread_cond_init(&cond_v, NULL) != 0)
	{
		printf("condition variable init failed.\n");
		return ERROR;
	}
	
	data_init();
		
	if (pthread_create(&recv_thread, NULL, recv_task, NULL))
	{
		printf("Create recv_thread failed\n");
		return ERROR;
	}
	
	return SUCCESS;
}

void avs_shutdown(void)
{
	pthread_cond_destroy(&cond_v);
	close(sockfd);
	sockfd = -1;
}

#if 1
/* main - Just for testing APIs..*/
int main(void)
{
	
	if (avs_create_conn() != SUCCESS) 
	{
		printf("Connect to AVS failed\n");
		return -1;
	}
#if 0	/* set global param. */
{
	struct avs_global_param param;
	struct avs_common_resp_info resp;

	strcpy(param.stun_ipaddr, "192.168.3.3");
	param.stun_port = 5333;
	strcpy(param.turn_ipaddr, "192.168.5.5");
	param.turn_port = 6333;
	strcpy(param.turn_username, "zhoulei");
	strcpy(param.turn_password, "123456789");
	strcpy(param.comm_id, "1111111111");

	if (avs_set_global_param(&param, &resp) == SUCCESS)
	{
		printf("Good Job! resp: code is %d, message is %s\n", resp.code, resp.message);
	}
	else
	{
		printf("Fuck!\n");
	}
}
#endif

#if 0	/* alloc port with no ICE mode. */
{
	struct avs_alloc_port_normal_param param;
	struct avs_alloc_port_normal_resp_info resp;
	
	strcpy(param.conf_id, "85883");
	strcpy(param.chan_id, "00001");
	param.enable_dtls = 0;
	strcpy(param.comm_id, "2222222222");
	
	if (avs_alloc_port_normal(&param, &resp) == SUCCESS)
	{
		printf("Good Job! resp->code is %d, message is %s, port_id is %s, rtp_port is %d, rtcp_port is %d\n", resp.resp.code, resp.resp.message, resp.port_id, resp.rtp_port, resp.rtcp_port);
	}
	else
	{
		printf("Fuck!\n");
	}
}
#endif

#if 0	/* alloc port with ICE mode. */
{
	struct avs_alloc_port_ice_param param;
	struct avs_alloc_port_ice_resp_info resp;
	
	strcpy(param.conf_id, "85883");
	strcpy(param.chan_id, "00001");
	param.enable_dtls = 1;
	strcpy(param.comm_id, "3333333333");
	
	if (avs_alloc_port_ice(&param, &resp) == SUCCESS)
	{
		printf("Good Job!\n");
	}
	else
	{
		printf("Fuck!\n");
	}
}
#endif

#if 0	/* dealloc port. */
{
	struct avs_dealloc_port_param param;
	struct avs_common_resp_info resp;

	strcpy(param.stun_ipaddr, "stun1.l.google.com");
	param.stun_port = 19302;
	strcpy(param.turn_ipaddr, "192.168.5.5");
	param.turn_port = 6333;
	strcpy(param.turn_username, "zhoulei");
	strcpy(param.turn_password, "123456789");
	strcpy(param.comm_id, "1111111111");

	if (avs_set_global_param(&param, &resp) == SUCCESS)
	{
		printf("Good Job! resp: code is %d, message is %s\n", resp.code, resp.message);
	}
	else
	{
		printf("Fuck!\n");
	}
		
}
#endif

#if 0	/* set peerport normal. */
{
	struct avs_set_peerport_normal_param param;
	struct avs_common_resp_info resp;
	
	param.rtcpmux = 0;
	param.symrtp = 0;
	param.srtpmode = 2;
	param.qos = 5;
	strcpy(param.fingerprint, "5x5x5x5x5x5x5x");
	strcpy(param.srtpsendkey, "5x5x5x5x5x5x5x");
	strcpy(param.srtprecvkey, "5x5x5x5x5x5x5x");
	strcpy(param.targetaddr, "2.2.2.2");
	strcpy(param.conf_id, "85883");
	strcpy(param.chan_id, "00001");
	strcpy(param.port_id, "99999");
	strcpy(param.comm_id, "5555555555");
	
	if (avs_set_peerport_param_normal(&param, &resp) == SUCCESS)
	{
		printf("Good Job! resp: code is %d, message is %s\n", resp.code, resp.message);
	}
	else
	{
		printf("Fuck!\n");
	}
}
#endif

#if 1	/* set peerport ICE. */
{
	struct avs_set_peerport_ice_param param;
	struct avs_common_resp_info resp;
	
	param.icerole = 0;
	param.sslrole = 0;
	strcpy(param.fingerprint, "5x5x5x5x5x5x5x");
	strcpy(param.ice_ufrag, "7x7x7x7x7x7x7x");
	strcpy(param.ice_pwd, "8x8x8x8x8x8x");
	strcpy(param.candidate, "asdfasfdasdfasdf");
	strcpy(param.conf_id, "85883");
	strcpy(param.chan_id, "00001");
	strcpy(param.port_id, "99999");
	strcpy(param.comm_id, "5555555555");
	
	if (avs_set_peerport_param_ice(&param, &resp) == SUCCESS)
	{
		printf("Good Job! resp: code is %d, message is %s\n", resp.code, resp.message);
	}
	else
	{
		printf("Fuck!\n");
	}
}
#endif
		
	for (;;)
	{
		sleep(1);	
	}

	avs_shutdown();

	return 0;
}
#endif
