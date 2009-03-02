/*
 * OSS mixer service daemon (used by libossmix)
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <soundcard.h>

#define OSSMIX_REMOTE
#include "libossmix.h"

int connfd;
int listenfd;
int verbose=0;

static void
send_response(int cmd, int p1, int p2, int p3, int p4, int p5)
{
	  ossmix_commad_packet_t msg;

	  memset(&msg, 0, sizeof(msg));

	  msg.cmd=cmd;
	  msg.p1=p1;
	  msg.p2=p2;
	  msg.p3=p3;
	  msg.p4=p4;
	  msg.p5=p5;

	  if (write(connfd, &msg, sizeof(msg)) != sizeof(msg))
	  {
		  fprintf(stderr, "Write to socket failed\n");
	  }
}

static void
send_response_long(int cmd, int p1, int p2, int p3, int p4, int p5, const char *payload, int plsize)
{
	  ossmix_commad_packet_t msg;

	  memset(&msg, 0, sizeof(msg));

	  msg.cmd=cmd;
	  msg.p1=p1;
	  msg.p2=p2;
	  msg.p3=p3;
	  msg.p4=p4;
	  msg.p5=p5;
	  msg.payload_size=plsize;

	  if (write(connfd, &msg, sizeof(msg))!=sizeof(msg))
	  {
		  fprintf(stderr, "Write to socket failed\n");
	  }

	  if (write(connfd, payload, msg.payload_size) != msg.payload_size)
	  {
		  fprintf(stderr, "Write to socket failed\n");
	  }
}

static void
send_error(const char *msg)
{
	int l=strlen(msg)+1;

	send_response_long(OSSMIX_CMD_ERROR, 0, 0, 0, 0, 0, msg, l);
}

int
wait_connect (void)
{
  if (listen (listenfd, 1) == -1)
    {
      perror ("listen");
      exit (-1);
    }

  if ((connfd = accept (listenfd, NULL, NULL)) == -1)
    {
      perror ("accept");
      exit (-1);
    }

  return 1;
}

static void
send_ack(void)
{
	send_response(OSSMIX_CMD_OK, 0, 0, 0, 0, 0);
}

static void
return_value(int val)
{
	send_response(val, 0, 0, 0, 0, 0);
}

static void
send_multiple_nodes(ossmix_commad_packet_t *pack)
{
	int i,n;
	oss_mixext nodes[MAX_NODES];

	n=0;
	for (i=pack->p2;i<=pack->p3;i++)
	{
		if (ossmix_get_nodeinfo(pack->p1, i, &nodes[n]) < 0)
		{
	   		send_error("Cannot get mixer node info\n");
	   		return;
		}

		mixc_add_node(pack->p1, i, &nodes[n]);

		if (++n >= MAX_NODES)
		{
	  	  send_response_long(OSSMIX_CMD_GET_NODEINFO, n, i, 0, 0, 0,
				  (void*)&nodes, n*sizeof(oss_mixext));
		  n=0;
		}
	}

	if (n>0)
	    send_response_long(OSSMIX_CMD_GET_NODEINFO, n, pack->p3, 0, 0, 0,
			  (void*)&nodes, n*sizeof(oss_mixext));
}

static void
update_values(int mixernum)
{
	oss_mixext *ext;
	int i;
	int nrext;
	int value, prev_value;

	nrext=ossmix_get_nrext(mixernum);

	for (i=0;i<nrext;i++)
	{
		if ((ext=mixc_get_node(mixernum, i))==NULL)
		   continue;

		if (ext->type == MIXT_DEVROOT || ext->type == MIXT_GROUP || ext->type == MIXT_MARKER)
		   continue;

		prev_value=mixc_get_value(mixernum, i);

		if ((value=ossmix_get_value(mixernum, i, ext->timestamp))<0)
		   continue;

		if (value != prev_value)
		   mixc_set_value(mixernum, i, value);
	}
}

static void
serve_command(ossmix_commad_packet_t *pack)
{
	switch (pack->cmd)
	{
	case OSSMIX_CMD_INIT:
		if (pack->ack_rq)
		   send_ack();
		break;

	case OSSMIX_CMD_EXIT:
		//fprintf(stderr, "Exit\n");
		if (pack->ack_rq)
		   send_ack();
		break;

	case OSSMIX_CMD_GET_NMIXERS:
		return_value(ossmix_get_nmixers());
		break;

	case OSSMIX_CMD_GET_MIXERINFO:
		{
			oss_mixerinfo mi;

			if (ossmix_get_mixerinfo(pack->p1, &mi) < 0)
			   send_error("Cannot get mixer info\n");
			else
			   send_response_long(OSSMIX_CMD_OK, 0, 0, 0, 0, 0, (void*)&mi, sizeof(mi));
		}
		break;

	case OSSMIX_CMD_OPEN_MIXER:
		return_value(ossmix_open_mixer(pack->p1));
		break;

	case OSSMIX_CMD_CLOSE_MIXER:
		ossmix_close_mixer(pack->p1);
		break;

	case OSSMIX_CMD_GET_NREXT:
		return_value(ossmix_get_nrext(pack->p1));
		break;

	case OSSMIX_CMD_GET_NODEINFO:
		{
			oss_mixext ext;

			if (pack->p3 > pack->p2)
			{
				send_multiple_nodes(pack);
				break;
			}

			if (ossmix_get_nodeinfo(pack->p1, pack->p2, &ext) < 0)
			   send_error("Cannot get mixer node info\n");
			else
			{
			   mixc_add_node(pack->p1, pack->p2, &ext);
			   send_response_long(OSSMIX_CMD_OK, 0, 0, 0, 0, 0, (void*)&ext, sizeof(ext));
			}
		}
		break;

	case OSSMIX_CMD_GET_ENUMINFO:
		{
			oss_mixer_enuminfo desc;

			if (ossmix_get_enuminfo(pack->p1, pack->p2, &desc) < 0)
			   send_error("Cannot get mixer enum strings\n");
			else
			   send_response_long(OSSMIX_CMD_OK, 0, 0, 0, 0, 0, (void*)&desc, sizeof(desc));
		}
		break;

	case OSSMIX_CMD_GET_DESCRIPTION:
		{
			oss_mixer_enuminfo desc;

			if (ossmix_get_description(pack->p1, pack->p2, &desc) < 0)
			   send_error("Cannot get mixer description\n");
			else
			   send_response_long(OSSMIX_CMD_OK, 0, 0, 0, 0, 0, (void*)&desc, sizeof(desc));
		}
		break;

	case OSSMIX_CMD_GET_VALUE:
		return_value(ossmix_get_value(pack->p1, pack->p2, pack->p3));
		break;

	case OSSMIX_CMD_GET_ALL_VALUES:
		{
			int n;
			value_packet_t value_packet;

			update_values(pack->p1);
			n=mixc_get_all_values(pack->p1, value_packet);

	  	  send_response_long(OSSMIX_CMD_GET_ALL_VALUES, n, 0, 0, 0, 0,
				  (void*)&value_packet, n*sizeof(value_record_t));
		}
		break;

	case OSSMIX_CMD_SET_VALUE:
		ossmix_set_value(pack->p1, pack->p2, pack->p3, pack->p4);
		break;

	default:

		if (pack->ack_rq)
		   send_error("Unrecognized request");
	}
}

static void
handle_connection (int connfd)
{
	ossmix_commad_packet_t pack;

	send_response(OSSMIX_CMD_HALOO, OSSMIX_P1_MAGIC, 0, 0, 0, 0);

	while (read(connfd, &pack, sizeof(pack))==sizeof(pack))
	{
		serve_command(&pack);
	}

}

int
main (int argc, char *argv[])
{
  struct sockaddr_in servaddr;
  int port = 7777;
  int c;
  int err;
  extern int optind;

	if ((err=ossmix_init())<0)
	{
		fprintf(stderr, "ossmix_init() failed, err=%d\n");
		exit(EXIT_FAILURE);
	}

	if ((err=ossmix_connect(NULL, 0))<0) /* Force local connection */
	{
		fprintf(stderr, "ossmix_connect() failed, err=%d\n", err);
		exit(EXIT_FAILURE);
	}

  while ((c = getopt (argc, argv, "vp:")) != EOF)
    {
      switch (c)
	{
	case 'p':		/* TCP/IP port */
	  port = atoi (optarg);
	  if (port <= 0)
	    port = 9876;
	  break;

	case 'v':		/* Verbose */
	  verbose++;
	  break;
	}
    }

  printf ("Listening socket %d\n", port);

  if ((listenfd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("socket");
      exit (-1);
    }

  memset (&servaddr, 0, sizeof (servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl (INADDR_ANY);
  servaddr.sin_port = htons (port);

  if (bind (listenfd, (struct sockaddr *) &servaddr, sizeof (servaddr)) == -1)
    {
      perror ("bind");
      exit (-1);
    }

  while (1)
    {

      if (!wait_connect ())
	exit (-1);

      handle_connection (connfd);
      close (connfd);
    }

  close (listenfd);
  exit (0);
}
