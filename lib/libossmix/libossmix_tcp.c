#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2009. All rights reserved.
/*
 * TCP driver for libossmix
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <soundcard.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#define OSSMIX_REMOTE

#include "libossmix.h"
#include "libossmix_impl.h"

static int initialized=0;
static void tcp_disconnect(void);
static int sockfd=-1;

static int
get_response(void)
{
	  ossmix_commad_packet_t msg;
	  char payload[4096];
	  int l;

	  if (sockfd==-1)
	     return -1;

	  payload[0]=0;

	  if ((l=read(sockfd, &msg, sizeof(msg)))!=sizeof(msg))
	  {
		  if (l==0) /* Connection closed */
		     return -1;

		  perror("get response");
		  return -1;
	  }

	  if (msg.payload_size > 0)
	  {
	  	if ((l=read(sockfd, payload, msg.payload_size)) != msg.payload_size)
		{
			perror("Get error message");
			return -1;
		}

		payload[l]=0;
	  }

	  if (msg.cmd == OSSMIX_CMD_ERROR)
	  {
		  fprintf(stderr, "Remote error: %s\n", payload);
	  }

	  return msg.cmd;
}

static int
wait_payload(void *payload, int len)
{
	  ossmix_commad_packet_t msg;
	  int l;

	  if (sockfd==-1)
	     return -1;

	  if ((l=read(sockfd, &msg, sizeof(msg)))!=sizeof(msg))
	  {
		  if (l==0) /* Connection closed */
		     return -1;

		  perror("get response");
		  return -1;
	  }

	  if (msg.payload_size > 0)
	  {
	  	if ((l=read(sockfd, payload, msg.payload_size)) != msg.payload_size)
		{
			perror("Get error message");
			return -1;
		}
	  }

	  if (msg.cmd == OSSMIX_CMD_ERROR)
	  {
		  fprintf(stderr, "Remote error: %s\n", payload);
	  }

	  if (msg.payload_size != len)
	  {
		  fprintf(stderr, "Payload size mismatch (%d/%d)\n", 
				  msg.payload_size, len);
		  return -1;
	  }

	  return msg.cmd;
}

int
send_request(int cmd, int p1, int p2, int p3, int p4, int p5)
{
	  ossmix_commad_packet_t msg;

	  memset(&msg, 0, sizeof(msg));

	  msg.cmd=cmd;
	  msg.p1=p1;
	  msg.p2=p2;
	  msg.p3=p3;
	  msg.p4=p4;
	  msg.p5=p5;
	  msg.ack_rq=1;

	  if (write(sockfd, &msg, sizeof(msg))!=sizeof(msg))
	  {
		  fprintf(stderr, "Write to socket failed\n");
	  }
	  return get_response();
}

void
send_request_noreply(int cmd, int p1, int p2, int p3, int p4, int p5)
{
	  ossmix_commad_packet_t msg;

	  memset(&msg, 0, sizeof(msg));

	  msg.cmd=cmd;
	  msg.p1=p1;
	  msg.p2=p2;
	  msg.p3=p3;
	  msg.p4=p4;
	  msg.p5=p5;
	  msg.ack_rq=0;

	  if (write(sockfd, &msg, sizeof(msg))!=sizeof(msg))
	  {
		  fprintf(stderr, "Write to socket failed\n");
	  }
	  //send(sockfd, &msg, sizeof(msg), 0);
}

int
send_request_long(int cmd, int p1, int p2, int p3, int p4, int p5, const char *payload)
{
	  ossmix_commad_packet_t msg;

	  memset(&msg, 0, sizeof(msg));

	  msg.cmd=cmd;
	  msg.p1=p1;
	  msg.p2=p2;
	  msg.p3=p3;
	  msg.p4=p4;
	  msg.p5=p5;
	  msg.ack_rq=1;
	  msg.payload_size=strlen(payload);

	  if (write(sockfd, &msg, sizeof(msg))!=sizeof(msg))
	  {
		  fprintf(stderr, "Write to socket failed\n");
	  }
	  if (write(sockfd, payload, msg.payload_size) != msg.payload_size)
	  {
		  fprintf(stderr, "Write to socket failed\n");
	  }
	  return get_response();
}

static int
tcp_connect(const char *remotehost, int port)
{
  struct sockaddr_in sa;
  struct hostent *he;

	if (mixlib_trace > 0)
fprintf(stderr, "Entered tcp_connect(%s, %d)\n", remotehost, port);

  if (port == 0)
    port = 7777;

  if (initialized)
    {
      fprintf (stderr, "Panic: ossmixlib already initialized\n");
      exit (-1);
    }

  initialized = 1;

  /*
   * Open the network connection
   */

  if ((sockfd = socket (PF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("socket");
      return -1;
    }

  if ((he = gethostbyname (remotehost)) == NULL)
    {
      herror(remotehost);
      fprintf (stderr, "Cannot find the OSSMIX server \"%s\"\n", remotehost);
      return -1;
    }

  sa.sin_family = AF_INET;
  sa.sin_port = htons (port);

  memcpy ((void *) &sa.sin_addr, *he->h_addr_list, he->h_length);
  if (connect (sockfd, (void *) &sa, sizeof (sa)) == -1)
    {
      switch (errno)
      {
      case ECONNREFUSED:
	fprintf(stderr, "Remote OSSMIX server is not running (Connection refused)\n");
	break;

      default:
      	perror("connect");
      }
      fprintf (stderr, "Cannot connect OSSMIX server %s:%d\n", remotehost, port);
      return -1;
    }
  atexit(tcp_disconnect);
  return send_request(OSSMIX_CMD_INIT, 0, 0, 0, 0, 0);
}

static void
tcp_disconnect(void)
{
	if (mixlib_trace > 0)
fprintf(stderr, "Entered tcp_disconnect()\n");

        send_request(OSSMIX_CMD_EXIT, 0, 0, 0, 0, 0);
	close(sockfd);
	sockfd=-1;
}

static int
tcp_get_nmixers(void)
{
        return send_request(OSSMIX_CMD_GET_NMIXERS, 0, 0, 0, 0, 0);
}

static int
tcp_get_mixerinfo(int mixernum, oss_mixerinfo *mi)
{
        send_request_noreply(OSSMIX_CMD_GET_MIXERINFO, mixernum, 0, 0, 0, 0);
	return wait_payload(mi, sizeof(*mi));
}

static int
tcp_open_mixer(int mixernum)
{
        return send_request(OSSMIX_CMD_OPEN_MIXER, mixernum, 0, 0, 0, 0);
}

static void
tcp_close_mixer(int mixernum)
{
        send_request_noreply(OSSMIX_CMD_CLOSE_MIXER, mixernum, 0, 0, 0, 0);
}

static int
tcp_get_nrext(int mixernum)
{
        return send_request(OSSMIX_CMD_GET_NREXT, mixernum, 0, 0, 0, 0);
}

static int
tcp_get_nodeinfo(int mixernum, int node, oss_mixext *ext)
{
        send_request_noreply(OSSMIX_CMD_GET_NODEINFO, mixernum, node, 0, 0, 0);
	return wait_payload(ext, sizeof(*ext));
}

static int
tcp_get_enuminfo(int mixernum, int node, oss_mixer_enuminfo *ei)
{
        send_request_noreply(OSSMIX_CMD_GET_ENUMINFO, mixernum, node, 0, 0, 0);
	return wait_payload(ei, sizeof(*ei));
}

static int
tcp_get_description(int mixernum, int node, oss_mixer_enuminfo *desc)
{
        send_request_noreply(OSSMIX_CMD_GET_DESCRIPTION, mixernum, node, 0, 0, 0);
	return wait_payload(desc, sizeof(*desc));
}

static int
tcp_get_value(int mixernum, int ctl, int timestamp)
{
        return send_request(OSSMIX_CMD_GET_VALUE, mixernum, ctl, timestamp, 0, 0);
}

static void
tcp_set_value(int mixernum, int ctl, int timestamp, int value)
{
        send_request_noreply(OSSMIX_CMD_SET_VALUE, mixernum, ctl, timestamp, value, 0);
}

ossmix_driver_t ossmix_tcp_driver =
{
	tcp_connect,
	tcp_disconnect,
	tcp_get_nmixers,
	tcp_get_mixerinfo,
	tcp_open_mixer,
	tcp_close_mixer,
	tcp_get_nrext,
	tcp_get_nodeinfo,
	tcp_get_enuminfo,
	tcp_get_description,
	tcp_get_value,
	tcp_set_value
};
