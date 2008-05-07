/*
 * Purpose: OSS memory block allocation and management routines.
 */
#define COPYING7 Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.

typedef struct _oss_memblk_t oss_memblk_t;

extern oss_memblk_t *oss_global_memblk;

extern void *oss_memblk_malloc(oss_memblk_t **, int size);
extern void oss_memblk_free(oss_memblk_t **, void *addr);
extern void oss_memblk_unalloc(oss_memblk_t **);
