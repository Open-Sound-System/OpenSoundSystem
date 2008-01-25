#ifndef UART401_H
#define UART401_H
/*
 * Purpose: Definitions for the MPU-401 (UART) support library
 */

#define COPYING18 Copyright (C) Hannu Savolainen and Dev Mazumdar 1996-2005. All rights reserved.

#ifndef MIDI_CORE_H
#include "midi_core.h"
#endif

/*
 * Purpose: Definitions for the MPU-401 UART driver
 */

typedef struct uart401_devc
{
  oss_native_word base;
  int irq;
  oss_device_t *osdev;
  int running;
  oss_mutex_t mutex;
  oss_midi_inputbuf_t save_input_buffer;
  int opened, disabled;
  volatile unsigned char input_byte;
  int my_dev;
  int share_irq;
}
uart401_devc;

extern int uart401_init (uart401_devc * devc, oss_device_t * osdev, int base,
			 char *name);
extern void uart401_irq (uart401_devc * devc);
extern void uart401_disable (uart401_devc * devc);
#endif
