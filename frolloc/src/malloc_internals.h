#ifndef __MAGED_INTERNALS_H__
#define __MAGED_INTERNALS_H__

#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "atomic.h"
#include "lf_lifo_queue.h"
#include "lf_fifo_queue.h"

struct Descriptor;
typedef struct Descriptor descriptor;
struct Procheap;
typedef struct Procheap procheap;

#define	PAGESIZE	4096
#define SBSIZE		(16 * PAGESIZE)
#define DESCSBSIZE	(1024 * sizeof(descriptor))

#define ACTIVE		0
#define FULL		1
#define PARTIAL		2
#define EMPTY		3

#define	MAXCREDITS	64    // 2^(bits for credits in active)
#define GRANULARITY	16    // sri: for x86_64 alignment we require 16 NOT 8

// sri: bigger than this we mmap. 
// glibc's default is 128 * 1024 
// but since our SBSIZE is 16 * 4 * 1024
// we can't get much bigger than this unless we increase SBIZE too.
#ifdef DARWIN
#define MAX_BLOCK_SIZE  32 * 1024
#else
#define MAX_BLOCK_SIZE  32 * 1024
#endif
/* 
   Seems like XCode 7.3 has a bug: 

   ld: section __DATA/__thread_bss extends beyond end of file, file
   'obj/malloc.o' for architecture x86_64

   So we have to be no bigger than 32 * 1024 for the Mac
*/  
                              

/* 
 * Global "free" descriptor list (with ABA tag)
 */
typedef struct {
  uintptr_t 	DescAvail;
  uint64_t      tag;
} descriptor_queue;

/* Superblock descriptor structure. We bumped avail and count 
 * to 24 bits to support larger superblock sizes. */
typedef struct {
	unsigned long long 	avail:24,count:24, state:2, tag:14;
} anchor;

/*
  Note Bene: the first element of the descriptor struct allows it to
  be stored in the lf_lifo_queue, also with a slight change the same is true 
  for the lf_fifo_queue. This means that a descrriptor cannot be stored
  in more than one queue, and should not be enqueued more than once.
  This same remark holds true for lfpa.
*/

struct Descriptor {
	struct queue_elem_t	lf_lifo_queue_padding;
	volatile anchor		Anchor;
	descriptor*		Next;
	void*			sb;		// pointer to superblock
	procheap*		heap;		// pointer to owner procheap
	unsigned int		sz;		// block size
	unsigned int		maxcount;	// superblock size / sz
};

typedef struct {
	lf_lifo_queue_t		Partial;	// initially empty
	unsigned int		sz;		// block size
	unsigned int		sbsize;		// superblock size
} sizeclass;

typedef struct {
  uintptr_t	ptr;
  uint64_t      credits;
} active;

struct Procheap {
	volatile active		Active;		// initially NULL
	volatile descriptor*	Partial;	// initially NULL
	sizeclass*		sc;		// pointer to parent sizeclass
};

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#endif	/* __MAGED_INTERNALS_H__ */
