/*
 * Purpose: Definition file for the oss_userdev driver
 *
 */
#define COPYING2 Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.
#define MAX_RATE 	192000
#define MAX_CHANNELS	64
#define SUPPORTED_FORMATS	(AFMT_S16_NE|AFMT_S32_NE)

typedef struct _userdev_devc_t userdev_devc_t;
typedef struct _userdev_portc_t userdev_portc_t;

struct _userdev_portc_t
{
  userdev_devc_t *devc;
  userdev_portc_t *peer;
  int audio_dev;
  int open_mode;
  int port_type;
#define PT_CLIENT	1
#define PT_SERVER	2

  /* State variables */
  int input_triggered, output_triggered;
  oss_wait_queue_t *wq;
};

struct _userdev_devc_t
{
  oss_device_t *osdev;
  oss_mutex_t mutex;

  userdev_devc_t *next_instance;

  int rate;
  int channels;
  unsigned int fmt, fmt_bytes;
  timeout_id_t timeout_id;

  userdev_portc_t client_portc;
  userdev_portc_t server_portc;
};

extern oss_device_t *userdev_osdev;
extern oss_mutex_t userdev_global_mutex;
extern userdev_devc_t *userdev_active_device_list;

extern int create_device_pair(void);
extern void delete_device_pair(userdev_devc_t *devc);
