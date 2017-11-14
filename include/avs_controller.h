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
 * \brief Adaptor for AVS component.
 *
 * \author Kimi Zhou <lzhou@grandstream.cn>
 *
 *	avs_controllor is a bridge between AVS and Conference Manager, it acts
 *  as a role like an adaptor.
 *
 ***************************************************************************/

#ifndef AVS_CONTROLLER_H
#define AVS_CONTROLLER_H

#define MAX_IPADDR_LEN		16	/* e.g: 192.168.111.111 */
#define MAX_IPPORTADDR_LEN  	22	/* e.g: 192.168.111.111:65531 */
#define MAX_TURN_USERNAME_LEN 	20
#define MAX_TURN_PASSWORD_LEN	20
#define MAX_SOUNDFILE_LEN 	20
#define MAX_CONFID_LEN 		20
#define MAX_CHANID_LEN 		20
#define MAX_PORTID_LEN		20
#define MAX_UNIQUE_ID		20
#define MAX_MESSAGE_REPONSE	50
#define MAX_FINGERPRINT_LEN	70	/* e.g: sha-512 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB */
#define MAX_ICE_UFRAG		5	/* e.g: 8hhY */
#define MAX_ICE_PASSWROD	23	/* e.g: asd88fgpdd777uzjYhagZg */
#define MAX_ICE_QOS		3
#define MAX_SRTP_KEY_LEN	100	/* ref: rfc4568 */

/**
 * enum avs_audio_codec - Audio codecs.
 */
enum avs_audio_codec 
{
	AVS_AUDIO_CODEC_PCMU,
	AVS_AUDIO_CODEC_PCMA,
	AVS_AUDIO_CODEC_GSM,
	AVS_AUDIO_CODEC_ILBC,
	AVS_AUDIO_CODEC_G722,
	AVS_AUDIO_CODEC_G722_1,
	AVS_AUDIO_CODEC_G722_1C,
	AVS_AUDIO_CODEC_G729,
	AVS_AUDIO_CODEC_G723_1,
	AVS_AUDIO_CODEC_G726,
	AVS_AUDIO_CODEC_ADPCM32
};

/**
 * enum avs_video_codec - Video codecs.
 */
enum avs_video_codec 
{
	AVS_VIDEO_CODEC_H264,
	AVS_VIDEO_CODEC_H265,
	AVS_VIDEO_CODEC_VP8,
	AVS_VIDEO_CODEC_VP9
};

/**
 * enum avs_runctrl_chan_opt - Operation of run command for a channel.
 * 
 * @AVS_RUNCTRL_CHAN_OPT_START:  Let the channel begin to work.
 * @AVS_RUNCTRL_CHAN_OPT_RESET:  Release channel resources.
 * @AVS_RUNCTRL_CHAN_OPT_SUSPEND:  Work suspended to a channel.
 * @AVS_RUNCTRL_CHAN_OPT_RESUME:  Restore to work to a channel.
 */
enum avs_runctrl_chan_opt 
{
	AVS_RUNCTRL_CHAN_OPT_START,
	AVS_RUNCTRL_CHAN_OPT_RESET,
	AVS_RUNCTRL_CHAN_OPT_SUSPEND,
	AVS_RUNCTRL_CHAN_OPT_RESUME
};

/**
 * enum avs_runctrl_chan_mtype - Media type of run command.
 *
 * @AVS_RUNCTRL_CHAN_TYPE_AUDIO:  Audio only when suspend or resume to a channel.
 * @AVS_RUNCTRL_CHAN_TYPE_VIDEO:  Video only when suspend or resume to a channel.
 * @AVS_RUNCTRL_CHAN_TYPE_ALL:  Audio and Video.
 */
enum avs_runctrl_chan_mtype
{
	AVS_RUNCTRL_CHAN_TYPE_AUDIO,
	AVS_RUNCTRL_CHAN_TYPE_VIDEO,
	AVS_RUNCTRL_CHAN_TYPE_ALL
};

/**
 * enum avs_playsound_chan_type - Play mode.
 *
 * @AVS_PLAYSOUND_CHAN_SINGLE:  Playing sound on a single channel according to "chan_id".
 * @AVS_PALYSOUND_CHAN_ALL_EXPT_CHAN:  Playing sound on all channel except itself("chan_id").
 */
enum avs_playsound_chan_type
{
	AVS_PLAYSOUND_CHAN_SINGLE,
	AVS_PALYSOUND_CHAN_ALL_EXPT_CHAN
};

/**
 * enum avs_cmd_result - The return result of "avs_" APIs.
 *
 * @LINK_DISCONNECT:  The HTTP connection between AVS and avs_conntroller has been broken.
 * @ERROR:  Maybe socket error???
 * @SUCCESS:  Sending commanders to AVS sucessfully.
 */
typedef enum avs_cmd_result 
{
	LINK_DISCONNECT = -2,
	ERROR,
	SUCCESS
} AVS_CMD_RESULT;

/**
 * struct avs_common_resp_info - General type of AVS return value. This is used when AVS does not return any valuable informations, but just SUCCESS or FAIL.
 *
 * @code:  0:sucess, -1:error.
 * @message:  A simple description of the error return code.
 * @comm_id:  Unique ID of a command to AVS.
 */
struct avs_common_resp_info 
{
	unsigned int code;
	char message[MAX_MESSAGE_REPONSE];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_global_param - The global parameters set to AVS.
 *
 * @stun_port:  Port of STUN server.
 * @turn_port:  Port of TURN server.
 * @turn_ipaddr:  IP address of TURN server.
 * @stun_ipaddr:  IP address of STUN server.
 * @turn_username:  Username for authentication of TURN server.
 * @turn_password:  Password for authentication of TURN server.
 * @comm_id:  Unique ID of a command to AVS.
 */
struct avs_global_param 
{
	unsigned int stun_port;
	unsigned int turn_port;
	char stun_ipaddr[MAX_IPADDR_LEN];
	char turn_ipaddr[MAX_IPADDR_LEN];
	char turn_username[MAX_TURN_USERNAME_LEN];
	char turn_password[MAX_TURN_PASSWORD_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_alloc_port_normal_param - The parameters set to AVS for allocating port resources with normal mode(no ICE).
 *
 * @enable_dtls:  Whether to turn on DTLS.
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @comm_id:  Unique ID of a command to AVS.
 */
struct avs_alloc_port_normal_param 
{
	unsigned int enable_dtls:1;
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char comm_id[MAX_UNIQUE_ID];	
};

/**
 * struct avs_alloc_port_ice_param - The parameters set to AVS for allocating port resources with ICE mode.
 *
 * @enable_dtls:  Whether to turn on DTLS.
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @comm_id:  Unique ID of a command to AVS.
 */
struct avs_alloc_port_ice_param 
{
	unsigned int enable_dtls:1;
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_alloc_port_normal_resp_info - The response values from AVS according to avs_addport_normal() command.
 *
 * @rtp_port:  RTP port for audio or video stream.
 * @rtcp_port:  RTP port for audio or video stream.
 * @fingerprint_port:  fingerprint port.
 * @port_id:  Unique ID for a port resource.
 * @comm_id:  Unique ID of a commander to AVS.
 * @resp:  Response informations from AVS.
 */
struct avs_alloc_port_normal_resp_info 
{
	unsigned int rtp_port;
	unsigned int rtcp_port;
	char fingerprint[MAX_FINGERPRINT_LEN];
	char pord_id[MAX_PORTID_LEN];
	char comm_id[MAX_UNIQUE_ID];
	struct avs_response_common_info *resp;
};

/**
 * struct avs_alloc_port_ice_resp_info - The response values from AVS according to avs_addport_ice() command.
 * 
 * @ice_ufrag:  Ice credentials
 * @ice_pwd:  Ice credentials
 * @fingerprint:  fingerprint.
 * @port_id:  Unique ID for a port resource.
 * @comm_id:  Unique ID of a commander to AVS.
 * @resp:  Response informations from AVS. 
 */
struct avs_alloc_port_ice_resp_info 
{
	char ice_ufrag[MAX_ICE_UFRAG];
	char ice_pwd[MAX_ICE_PASSWROD];
	char fingerprint[MAX_FINGERPRINT_LEN];
	char pord_id[MAX_PORTID_LEN];
	char comm_id[MAX_UNIQUE_ID];
	struct avs_response_common_info *resp;
};

/**
 * struct avs_dealloc_port_param - The parameters for AVS to deallocate port resources.
 *
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @port_id:  Unique ID for a port resource.
 * @comm_id:  Unique ID of a commander to AVS.
 */
struct avs_dealloc_port_param 
{
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char port_id[MAX_PORTID_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_set_peerport_normal_param - The parameters for AVS to set peer port information with normal mode(no ICE).
 *
 * @rtcpmux:  Whether RTP and RTCP use same port. 0: no-mux, 1: mux.
 * @symrtp:  Whether to use symrtp. 0: switch off, 1: switch on.
 * @srtpmode:  The encryption method. 2: AES256_CM_SHA1_80, 3: AES256_CM_SHA1_32, 4: AES128_CM_SHA1_80, 5: AES128_CM_SHA1_32
 * @qos:  0 - 255.
 * @srtpsendkey:  Key needed for encryption.
 * @srtprecvkey:  Key needed for decryption.
 * @targetaddr:  Where AVS send media stream to.
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @port_id:  Unique ID for a port resource.
 * @comm_id:  Unique ID of a command to AVS.
 *
 */
struct avs_set_peerport_normal_param 
{
	unsigned int rtcpmux:1;
	unsigned int symrtp:1;
	unsigned int srtpmode:2;
	unsigned int qos;
	char srtpsendkey[MAX_SRTP_KEY_LEN];
	char srtprecvkey[MAX_SRTP_KEY_LEN];
	char targetaddr[MAX_IPPORTADDR_LEN];
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char port_id[MAX_PORTID_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_set_peerport_ice_param - The parameters for AVS to set peer port informations with ICE mode.
 * icerole:  0: controlling, 1: controlled.
 * sslrole:  0: ssl client, 1: ssl server.
 * fingerprint;  fingerprint.
 * @ice_ufrag:  Ice credentials
 * @ice_pwd:  Ice credentials
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @port_id:  Unique ID for a port resource.
 * @comm_id:  Unique ID of a command to AVS.
 *
 */
struct avs_set_peerport_ice_param 
{
	unsigned int icerole:1;
	unsigned int sslrole:1;
	char fingerprint[MAX_FINGERPRINT_LEN];
	char ice_ufrag[MAX_ICE_UFRAG];
	char ice_pwd[MAX_ICE_PASSWROD];
	char candidate[MAX_IPPORTADDR_LEN];
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char port_id[MAX_PORTID_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_codec_param - The parameters to set codec to AVS.
 *
 * @a_codec:  Audio encoder\decoder type.
 * @v_codec:  Video encoder\decoder type.
 * @audio_payloadtype:  Audio payloadtype.
 * @video_payloadtype:  Video payloadtype.
 * @audio_transmode:  1: sendrecv, 2: sendonly, 3: recvonly.
 * @video_transmode:  1: sendrecv, 2: sendonly, 3: recvonly.
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @port_id:  Unique ID for a port resource.
 * @comm_id:  Unique ID of a command to AVS.
 */
struct avs_codec_param 
{
	enum avs_audio_codec a_codec;
	enum avs_video_codec v_codec;
	unsigned int audio_payloadtype;
	unsigned int video_payloadtype;
	unsigned int audio_transmode;
	unsigned int video_transmode;
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char port_id[MAX_PORTID_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_runctrl_chan_param - Send run command to AVS.
 *
 * @opt:  Operation of run command.
 * @mtype:  Media type of run command.
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @comm_id:  Unique ID of a command to AVS.
 */
struct avs_runctrl_chan_param 
{
	enum avs_runctrl_chan_opt opt;
	enum avs_runctrl_chan_mtype mtype;
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * struct avs_playsound_chan_param - The parameters of playing sound on channels.
 *
 * @ptype:  Play mode.
 * @soundfile:  Name of sound file.
 * @conf_id:  Conference id.
 * @chan_id:  Channel id.
 * @comm_id:  Unique ID of a command to AVS.
 */
struct avs_playsound_chan_param
{
	enum avs_playsound_chan_type ptype;
	char soundfile[MAX_SOUNDFILE_LEN];
	char conf_id[MAX_CONFID_LEN];
	char chan_id[MAX_CHANID_LEN];
	char comm_id[MAX_UNIQUE_ID];
};

/**
 * avs_create_conn - Establish a HTTP connection to AVS. "Say hello..."
 *
 * Return: AVS_CMD_RESULT.
 */
AVS_CMD_RESULT avs_create_conn(void);

/**
 * avs_shutdown - Close the connection with AVS, and release related resources.
 *
 * Return:.
 */
void avs_shutdown(void);

/**
 * avs_set_global_param - Set global parameters to AVS.
 * @param: parameters to be set. 
 * @resp: response informations from AVS 
 *
 * Return: AVS_CMD_RESULT.
 */
AVS_CMD_RESULT avs_set_global_param(struct avs_global_param *param, struct avs_common_resp_info *resp);

/**
 * avs_alloc_port_normal/avs_alloc_port_ice/avs_dealloc_port - Allocating/Deallocating port resources to AVS with normal mode or ICE mode.
 * @param: parameters for allocating port resources with normal mode or ICE mode.
 * @resp: response informations for avs_addport_normal()/avs_addport_ice() from AVS.
 *
 * Return: AVS_CMD_RESULT.
 */
AVS_CMD_RESULT avs_alloc_port_normal(struct avs_alloc_port_normal_param *param, struct avs_alloc_port_normal_resp_info *resp);
AVS_CMD_RESULT avs_alloc_port_ice(struct avs_alloc_port_ice_param *param, struct avs_alloc_port_ice_resp_info *resp);
AVS_CMD_RESULT avs_dealloc_port(struct avs_dealloc_port_param *param, struct avs_common_resp_info *resp);

/**
 * avs_set_peerport_param_normal/avs_set_peerport_param_ice - Set peer parameters to AVS with normal mode or ICE mode.
 * @param:  The parameters of peer which set to AVS.
 * @resp:  The response informations returned from AVS.
 *
 * Return: AVS_CMD_RESULT.
 */
AVS_CMD_RESULT avs_set_peerport_param_normal(struct avs_set_peerport_normal_param *param, struct avs_common_resp_info *resp);
AVS_CMD_RESULT avs_set_peerport_param_ice(struct avs_set_peerport_ice_param *param, struct avs_common_resp_info *resp);

/**
 * avs_set_codec_param - Set the parameters of codec.
 * @param:  The parameters to set codec to AVS.
 * @resp:  The response informations returned from AVS.
 *
 * Return: AVS_CMD_RESULT.
 */
AVS_CMD_RESULT avs_set_codec_param(struct avs_codec_param *param, struct avs_common_resp_info *resp);

/**
 * avs_runctrl_chan - Run control to a channel.
 * @param:  Contents of Run control command.
 * @resp:  The response informations returned from AVS.
 *
 * Return: AVS_CMD_RESULT.
 */
AVS_CMD_RESULT avs_runctrl_chan(struct avs_runctrl_chan_param *param, struct avs_common_resp_info *resp);

/**
 * avs_playsound - Play sound on channels.
 * @param:  The parameters of playing sound on channels.
 * @resp:  The response informations returned from AVS.
 *
 * Return: AVS_CMD_RESULT.
 */
AVS_CMD_RESULT avs_playsound(struct avs_playsound_chan_param *param, struct avs_common_resp_info *resp);
#endif /* AVS_CONTROLLER_H */
