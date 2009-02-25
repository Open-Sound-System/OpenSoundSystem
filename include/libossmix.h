/*
 * Declaration file for the ossmix library.
 */
#define COPYING2 Copyright (C) Hannu Savolainen and Dev Mazumdar 2009. All rights reserved.

extern int ossmix_init(void);
extern void ossmix_close(void);

extern int ossmix_connect(const char *hostname, int port);
extern void ossmix_disconnect(void);

extern int ossmix_get_nmixers(void);
extern int ossmix_get_mixerinfo(int mixernum, oss_mixerinfo *mi);

extern int ossmix_open_mixer(int mixernum);
extern void ossmix_close_mixer(int mixernum);
extern int ossmix_get_nrext(int mixernum);
extern int ossmix_get_nodeinfo(int mixernum, int node, oss_mixext *ext);
extern int ossmix_get_enuminfo(int mixernum, int node, oss_mixer_enuminfo *ei);
extern int ossmix_get_description(int mixernum, int node, oss_mixer_enuminfo *desc);

extern int ossmix_get_value(int mixernum, int ctl, int timestamp);
extern void ossmix_set_value(int mixernum, int ctl, int timestamp, int value);

#ifdef OSSMIX_REMOTE
/*
 * This block contains definitions that cannot be used by client applications
 */

typedef struct
{
	int cmd;
#define OSSMIX_CMD_HALOO			0	/* Haloo means hello in finnish */
#define OSSMIX_CMD_OK				0	
#define OSSMIX_CMD_ERROR			-1
#define OSSMIX_CMD_INIT				1
#define OSSMIX_CMD_EXIT				2
#define OSSMIX_CMD_GET_NMIXERS			3
#define OSSMIX_CMD_GET_MIXERINFO		4
#define OSSMIX_CMD_OPEN_MIXER			5
#define OSSMIX_CMD_CLOSE_MIXER			6
#define OSSMIX_CMD_GET_NREXT			7
#define OSSMIX_CMD_GET_NODEINFO			8
#define OSSMIX_CMD_GET_ENUMINFO			9
#define OSSMIX_CMD_GET_DESCRIPTION		10
#define OSSMIX_CMD_GET_VALUE			11
#define OSSMIX_CMD_SET_VALUE			12

	int p1, p2, p3, p4, p5;

#define OSSMIX_P1_MAGIC				0x12345678
	
	int ack_rq;
	int unsolicited;
	int payload_size;
} ossmix_commad_packet_t;
#endif
