/*
 * Purpose: OSS memory block allocation and management routines.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.

#include <oss_config.h>
 
struct _oss_memblk_t
{
	oss_memblk_t *next;
	void *addr;
};

oss_memblk_t *oss_global_memblk=NULL;

void
*oss_memblk_malloc(oss_memblk_t **blk, int size)
{
	oss_memblk_t *newblk;

	newblk = KERNEL_MALLOC (sizeof(oss_memblk_t) + size);

	newblk->addr = newblk +1;
	newblk->next = NULL;

	if (*blk == NULL)
	   {
		/*
		 * No earlier memory blocks in the chain.
		 */
		*blk = newblk;
		return newblk->addr;
	   }

/*
 * Add this block to the chain.
 */
	newblk->next = *blk;
	*blk = newblk;

	return newblk->addr;
}

void
oss_memblk_free(oss_memblk_t **blk, void *addr)
{
	oss_memblk_t *this_one = *blk, *prev = NULL;

	while (this_one != NULL)
	{
		if (this_one->addr == addr)
		   {
			   if (prev == NULL) /* First one in the chain */
			      {
				      *blk = this_one->next;
				      KERNEL_FREE (this_one);
			      }
			   else
			      {
				      prev->next = this_one->next;
				      KERNEL_FREE (this_one);
			      }

			   return;
		   }

		this_one = this_one->next;
	}
}

void
oss_memblk_unalloc(oss_memblk_t **blk)
{
/*
 * Free all memory allocations on the chain.
 */
	oss_memblk_t *this_one = *blk;

   while (this_one != NULL)
   {
   	   oss_memblk_t *next_one;

	   next_one = this_one->next;

	   KERNEL_FREE(this_one);
	   this_one = next_one;
   }

   *blk = NULL;
}
