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

#define RECV_BUFFER_SIZE		500	/* Buffer size for receiving AVS messages. */

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
	ST_AVS_SET_CODEC_PARAM,
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
	unsigned int code;
	char message[MAX_MESSAGE_REPONSE];
	char comm_id[MAX_UNIQUE_ID];
};

/* Data structure for storing "alloc_port_ice" response received from AVS. */
struct resp_alloc_port_ice_info
{
	unsigned int code;
	char message[MAX_MESSAGE_REPONSE];
	char comm_id[MAX_UNIQUE_ID];
};

static CMD_TYPE_STATE cmd_current_state = ST_AVS_IDLE;	/* Context of the current command. */

static MSG_PARSE_RESULT msg_parse_result = MSG_PARSE_RESULT_SUCCESS;	/* Parsing result of message received from AVS. */

static struct resp_common_info resp_common_data;	/* Data area for storing common response received from AVS. */
//static struct resp_alloc_port_normal_info resp_alloc_port_normal_data;
//static struct resp_alloc_port_ice_info resp_alloc_port_ice_data;

static int wait_on_socket();
static void *wakeup_intruder();
static FUNC_RETURN wait_for_avs();
static void *recv_task(void *data);

static void *data_init();
static FUNC_RETURN sock_init(void);
static FUNC_RETURN cmd_send(const char *cmd);
static FUNC_RETURN msg_recv_process(char *msg);

static const char *enc_json_set_global_param(struct avs_global_param *param);
static FUNC_RETURN dec_json_common_resp(json_t *root);

static void *fill_common_resp(struct avs_common_resp_info *resp);

static void *fill_common_resp(struct avs_common_resp_info *resp)
{
	resp->code = resp_common_data.code;
	strncpy(resp->message, resp_common_data.message, sizeof(resp->message));
	
	return NULL;
}

static FUNC_RETURN dec_json_common_resp(json_t *root)
{
	json_t *id, *error, *code, *message;
	
	if (!json_is_object(root))
	{
		printf("error: root is not an object\n");
		return R_FAIL;
	}
	
	if ((id = json_object_get(root, "id")))
	{
		if (!json_is_string(id))
		{
			printf("error: id is not a string\n");
			return R_FAIL;
		}
		printf("resp id: %s\n", json_string_value(id));
		strncpy(resp_common_data.comm_id, json_string_value(id), sizeof(resp_common_data.comm_id) - 1);
	}
	
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
			resp_common_data.code = (unsigned int)json_integer_value(code);
		}
		
		if ((message = json_object_get(error, "message")))
		{
			printf("resp message: %s\n", json_string_value(message));
			if (json_is_string(message))
			{
				strncpy(resp_common_data.message, json_string_value(message), sizeof(resp_common_data.message) - 1);
			}
			else
			{
				strcpy(resp_common_data.message, "Nothing to Say! Fuck U!");	
			}
		}
	}

	return R_SUCCESS;
}

/* Encapulating global parameters json object and return it's string shape. */
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
	json_object_set_new(obj_top, "id", json_string("112233445566778899"));
	
	return json_dumps(obj_top, JSON_COMPACT);
}

/* Initialize all global variables. */
static void *data_init()
{
	/* resp_common_data */
	resp_common_data.code = -1;
	memset(resp_common_data.message, 0, sizeof(resp_common_data.code));
	memset(resp_common_data.comm_id, 0, sizeof(resp_common_data.comm_id));
	
	/* */
	
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
 * 1. Parse JSON.
 * 2. Fill the data into a global variable.
 * 3. Wake up the thread which send the command.
 */
static FUNC_RETURN msg_recv_process(char *msg)
{
	json_t *root;
	json_error_t error;

    printf("recv msg: %s\n", msg);

	root = json_loads(msg, 0, &error);

	if (!root)
	{
		printf("json load error: on line %d: %s\n", error.line, error.text);
		return R_FAIL;
	}
	
	memset(recv_buffer, 0, RECV_BUFFER_SIZE);
	
	switch (cmd_current_state)
	{
		case ST_AVS_IDLE:
			/* do nothing. */
			break;
			
		case ST_AVS_SET_GLOBAL_PARAM:
			
			if (dec_json_common_resp(root) != R_SUCCESS)
			{
				printf("decode json from AVS failed\n");
				msg_parse_result = MSG_PARSE_RESULT_FAIL;
			}

			wakeup_intruder();

			break;

		default:
			break;
	}
	
	cmd_current_state = ST_AVS_IDLE;
	json_decref(root);
	
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
	const char *json_s = NULL;
	FUNC_RETURN ret = R_SUCCESS;
	
	pthread_mutex_lock(&p_mutex);

	/* Converting parameters to JSON format. */
	json_s = enc_json_set_global_param(param);
	
	if (!json_s)
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
	cmd_current_state = ST_AVS_SET_GLOBAL_PARAM;
	
	/* waiting here... */
	ret = wait_for_avs();
	
	if (ret != R_SUCCESS)
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
	
	/* Fill the message returned from the AVS to the command requester. */
	fill_common_resp(resp);
	
	pthread_mutex_unlock(&p_mutex);
	
	return SUCCESS;
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
	struct avs_global_param param;
	struct avs_common_resp_info resp;

	if (avs_create_conn() != SUCCESS) 
	{
		printf("Connect to AVS failed\n");
		return -1;
	}

	strcpy(param.stun_ipaddr, "192.168.3.3");
	param.stun_port = 5333;
	strcpy(param.turn_ipaddr, "192.168.5.5");
	param.turn_port = 6333;
	strcpy(param.turn_username, "zhoulei");
	strcpy(param.turn_password, "123456789");

	if (avs_set_global_param(&param, &resp) == SUCCESS)
	{
		printf("Good Job! resp: code is %d, message is %s\n", resp.code, resp.message);
	}
	else
	{
		printf("Fuck!\n");
	}
		
	for (;;)
	{
		sleep(1);	
	}

	avs_shutdown();

	return 0;
}
#endif
