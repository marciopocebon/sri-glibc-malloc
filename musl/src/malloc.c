#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdatomic.h>

#include "internals.h"


struct chunk {
  size_t psize, csize;
  struct chunk *next, *prev;
};

struct bin {
  pthread_mutex_t  mutex;
  struct chunk *head;
  struct chunk *tail;
};

static struct {
  volatile uint64_t binmap;
  struct bin bins[64];
  pthread_mutex_t  mutex;
} mal;

typedef struct {
  atomic_size_t  bytes;
  atomic_size_t  inuse;
  atomic_size_t  mmapped;
  atomic_int     nmmaps;
} mal_info_t;

static mal_info_t mal_info;

static inline void mal_info_init(mal_info_t *mip){
  atomic_init(&mal_info.bytes, 0);
  atomic_init(&mal_info.inuse, 0);
  atomic_init(&mal_info.mmapped, 0);
  atomic_init(&mal_info.nmmaps, 0);
}

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool __inited = false;

static void init(void){
  int i;
  /* 
   * ian says: double checked locking! needs to be fixed.
   * http://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf
   * http://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/
   */
  if( ! __inited ){
    pthread_mutex_lock(&mutex);
    if(__inited){  return; }
    pthread_mutex_init(&mal.mutex, NULL);
    pthread_mutex_lock(&mal.mutex);
    for(i = 0; i < 64; i++){ 
      pthread_mutex_init(&mal.bins[i].mutex, NULL);
    }
    mal_info_init(&mal_info);  
    pthread_mutex_unlock(&mal.mutex);
    __inited = true;
    pthread_mutex_unlock(&mutex);
  }
}

#define SIZE_ALIGN (4*sizeof(size_t))
#define SIZE_MASK (-SIZE_ALIGN)
#define OVERHEAD (2*sizeof(size_t))
#define MMAP_THRESHOLD (0x1c00*SIZE_ALIGN)
#define DONTCARE 16
#define RECLAIM 163840

#define CHUNK_SIZE(c) ((c)->csize & -2)
#define CHUNK_PSIZE(c) ((c)->psize & -2)
#define PREV_CHUNK(c) ((struct chunk *)((char *)(c) - CHUNK_PSIZE(c)))
#define NEXT_CHUNK(c) ((struct chunk *)((char *)(c) + CHUNK_SIZE(c)))
#define MEM_TO_CHUNK(p) (struct chunk *)((char *)(p) - OVERHEAD)
#define CHUNK_TO_MEM(c) (void *)((char *)(c) + OVERHEAD)
#define BIN_TO_CHUNK(i) (MEM_TO_CHUNK(&mal.bins[i].head))

#define C_INUSE  ((size_t)1)

#define IS_MMAPPED(c) !((c)->csize & (C_INUSE))


static inline void lock_bin(int i)
{
  pthread_mutex_lock(&mal.bins[i].mutex);
  if (!mal.bins[i].head)
    mal.bins[i].head = mal.bins[i].tail = BIN_TO_CHUNK(i);
}

static inline void unlock_bin(int i)
{
  pthread_mutex_unlock(&mal.bins[i].mutex);
}

static int first_set(uint64_t x)
{
#if 1
  return a_ctz_64(x);
#else
  static const char debruijn64[64] = {
    0, 1, 2, 53, 3, 7, 54, 27, 4, 38, 41, 8, 34, 55, 48, 28,
    62, 5, 39, 46, 44, 42, 22, 9, 24, 35, 59, 56, 49, 18, 29, 11,
    63, 52, 6, 26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
    51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12
  };
  static const char debruijn32[32] = {
    0, 1, 23, 2, 29, 24, 19, 3, 30, 27, 25, 11, 20, 8, 4, 13,
    31, 22, 28, 18, 26, 10, 7, 12, 21, 17, 9, 6, 16, 5, 15, 14
  };
  if (sizeof(long) < 8) {
    uint32_t y = x;
    if (!y) {
      y = x>>32;
      return 32 + debruijn32[(y&-y)*0x076be629 >> 27];
    }
    return debruijn32[(y&-y)*0x076be629 >> 27];
  }
  return debruijn64[(x&-x)*0x022fdd63cc95386dull >> 58];
#endif
}

static int bin_index(size_t x)
{
  x = x / SIZE_ALIGN - 1;
  if (x <= 32) return x;
  if (x > 0x1c00) return 63;
  return ((union { float v; uint32_t r; }){(int)x}.r>>21) - 496;
}

static int bin_index_up(size_t x)
{
  x = x / SIZE_ALIGN - 1;
  if (x <= 32) return x;
  return (((union { float v; uint32_t r; }){(int)x}.r+0x1fffff)>>21) - 496;
}

#if 0
void __dump_heap(int x)
{
  struct chunk *c;
  int i;
  for (c = (void *)mal.heap; CHUNK_SIZE(c); c = NEXT_CHUNK(c))
    fprintf(stderr, "base %p size %zu (%d) flags %d/%d\n",
	    c, CHUNK_SIZE(c), bin_index(CHUNK_SIZE(c)),
	    c->csize & 15,
	    NEXT_CHUNK(c)->psize & 15);
  for (i=0; i<64; i++) {
    if (mal.bins[i].head != BIN_TO_CHUNK(i) && mal.bins[i].head) {
      fprintf(stderr, "bin %d: %p\n", i, mal.bins[i].head);
      if (!(mal.binmap & 1ULL<<i))
	fprintf(stderr, "missing from binmap!\n");
    } else if (mal.binmap & 1ULL<<i)
      fprintf(stderr, "binmap wrongly contains %d!\n", i);
  }
}
#endif

void *__expand_heap(size_t *);

static struct chunk *expand_heap(size_t n)
{
  static  pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;
  static void *end;
  void *p;
  struct chunk *w;

  /* The argument n already accounts for the caller's chunk
   * overhead needs, but if the heap can't be extended in-place,
   * we need room for an extra zero-sized sentinel chunk. */
  n += SIZE_ALIGN;

  pthread_mutex_lock(&heap_lock);

  p = __expand_heap(&n);
  if (!p) {
    pthread_mutex_unlock(&heap_lock);
    return 0;
  }

  /* If not just expanding existing space, we need to make a
   * new sentinel chunk below the allocated space. */
  if (p != end) {
    /* Valid/safe because of the prologue increment. */
    n -= SIZE_ALIGN;
    p = (char *)p + SIZE_ALIGN;
    w = MEM_TO_CHUNK(p);
    w->psize = 0 | C_INUSE;
  }

  /* Record new heap end and fill in footer. */
  end = (char *)p + n;
  w = MEM_TO_CHUNK(end);
  w->psize = n | C_INUSE;
  w->csize = 0 | C_INUSE;

  /* Fill in header, which may be new or may be replacing a
   * zero-size sentinel header at the old end-of-heap. */
  w = MEM_TO_CHUNK(p);
  w->csize = n | C_INUSE;

  pthread_mutex_unlock(&heap_lock);

  return w;
}

static int adjust_size(size_t *n)
{
  /* Result of pointer difference must fit in ptrdiff_t. */
  if (*n-1 > PTRDIFF_MAX - SIZE_ALIGN - PAGE_SIZE) {
    if (*n) {
      errno = ENOMEM;
      return -1;
    } else {
      *n = SIZE_ALIGN;
      return 0;
    }
  }
  *n = (*n + OVERHEAD + SIZE_ALIGN - 1) & SIZE_MASK;
  return 0;
}

static void unbin(struct chunk *c, int i)
{
  if (c->prev == c->next)
    a_and_64(&mal.binmap, ~(1ULL<<i));
  c->prev->next = c->next;
  c->next->prev = c->prev;
  c->csize |= C_INUSE;
  NEXT_CHUNK(c)->psize |= C_INUSE;
}

static int alloc_fwd(struct chunk *c)
{
  int i;
  size_t k;
  while (!((k=c->csize) & C_INUSE)) {
    i = bin_index(k);
    lock_bin(i);
    if (c->csize == k) {
      unbin(c, i);
      unlock_bin(i);
      return 1;
    }
    unlock_bin(i);
  }
  return 0;
}

static int alloc_rev(struct chunk *c)
{
  int i;
  size_t k;
  while (!((k=c->psize) & C_INUSE)) {
    i = bin_index(k);
    lock_bin(i);
    if (c->psize == k) {
      unbin(PREV_CHUNK(c), i);
      unlock_bin(i);
      return 1;
    }
    unlock_bin(i);
  }
  return 0;
}


/* pretrim - trims a chunk _prior_ to removing it from its bin.
 * Must be called with i as the ideal bin for size n, j the bin
 * for the _free_ chunk self, and bin j locked. */
static int pretrim(struct chunk *self, size_t n, int i, int j)
{
  size_t n1;
  struct chunk *next, *split;

  /* We cannot pretrim if it would require re-binning. */
  if (j < 40) return 0;
  if (j < i+3) {
    if (j != 63) return 0;
    n1 = CHUNK_SIZE(self);
    if (n1-n <= MMAP_THRESHOLD) return 0;
  } else {
    n1 = CHUNK_SIZE(self);
  }
  if (bin_index(n1-n) != j) return 0;

  next = NEXT_CHUNK(self);
  split = (void *)((char *)self + n);

  split->prev = self->prev;
  split->next = self->next;
  split->prev->next = split;
  split->next->prev = split;
  split->psize = n | C_INUSE;
  split->csize = n1-n;
  next->psize = n1-n;
  self->csize = n | C_INUSE;
  return 1;
}

static void trim(struct chunk *self, size_t n)
{
  size_t n1 = CHUNK_SIZE(self);
  struct chunk *next, *split;

  if (n >= n1 - DONTCARE) return;

  next = NEXT_CHUNK(self);
  split = (void *)((char *)self + n);

  split->psize = n | C_INUSE;
  split->csize = (n1-n) | C_INUSE;
  next->psize = (n1-n) | C_INUSE;
  self->csize = n | C_INUSE;

  free(CHUNK_TO_MEM(split));
}

void *malloc(size_t n)
{
  struct chunk *c;
  int i, j;

  init();

  if (adjust_size(&n) < 0) return 0;

  if (n > MMAP_THRESHOLD) {
    size_t len = (n + OVERHEAD + PAGE_SIZE - 1) & -PAGE_SIZE;
    char *base = mmap(0, len, PROT_READ|PROT_WRITE,
			MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (base == (void *)-1) return 0;
    c = (void *)(base + SIZE_ALIGN - OVERHEAD);
    c->csize = len - (SIZE_ALIGN - OVERHEAD);
    c->psize = SIZE_ALIGN - OVERHEAD;

    atomic_fetch_add(&mal_info.mmapped, len);
    atomic_fetch_add(&mal_info.nmmaps, 1);

    
    return CHUNK_TO_MEM(c);
  }
  
  i = bin_index_up(n);
  for (;;) {
    uint64_t mask = mal.binmap & -(1ULL<<i);
    if (!mask) {
      c = expand_heap(n);
      if (!c) return 0;
      if (alloc_rev(c)) {
	struct chunk *x = c;
	c = PREV_CHUNK(c);
	NEXT_CHUNK(x)->psize = c->csize =
	  x->csize + CHUNK_SIZE(c);
      }
      break;
    }
    j = first_set(mask);
    lock_bin(j);
    c = mal.bins[j].head;
    if (c != BIN_TO_CHUNK(j)) {
      if (!pretrim(c, n, i, j)) unbin(c, j);
      unlock_bin(j);
      break;
    }
    unlock_bin(j);
  }
  
  /* Now patch up in case we over-allocated */
  trim(c, n);
  
  return CHUNK_TO_MEM(c);
}

void *__malloc0(size_t n)
{
  void *p = malloc(n);
  if (p && !IS_MMAPPED(MEM_TO_CHUNK(p))) {
    size_t *z;
    n = (n + sizeof *z - 1)/sizeof *z;
    for (z=p; n; n--, z++) if (*z) *z=0;
  }
  return p;
}

void *realloc(void *p, size_t n)
{
  struct chunk *self, *next;
  size_t n0, n1;
  void *new;
  
  if (!p) return malloc(n);

  if(! __inited ) return 0;
  
  if (adjust_size(&n) < 0) return 0;

  self = MEM_TO_CHUNK(p);
  n1 = n0 = CHUNK_SIZE(self);
  
  if (IS_MMAPPED(self)) {
    size_t extra = self->psize;
    char *base = (char *)self - extra;
    size_t oldlen = n0 + extra;
    size_t newlen = n + extra;
    /* Crash on realloc of freed chunk */
    if (extra & 1) a_crash();
    if (newlen < PAGE_SIZE && (new = malloc(n))) {
      memcpy(new, p, n-OVERHEAD);
      free(p);
      return new;
    }
    newlen = (newlen + PAGE_SIZE-1) & -PAGE_SIZE;
    if (oldlen == newlen) return p;
    base = mremap(base, oldlen, newlen, MREMAP_MAYMOVE);
    if (base == (void *)-1)
      return newlen < oldlen ? p : 0;
    self = (void *)(base + extra);
    self->csize = newlen - extra;

    atomic_fetch_add(&mal_info.mmapped, newlen - oldlen);
    
    return CHUNK_TO_MEM(self);
  }
  
  next = NEXT_CHUNK(self);
  
  /* Crash on corrupted footer (likely from buffer overflow) */
  if (next->psize != self->csize) a_crash();
  
  /* Merge adjacent chunks if we need more space. This is not
   * a waste of time even if we fail to get enough space, because our
   * subsequent call to free would otherwise have to do the merge. */
  if (n > n1 && alloc_fwd(next)) {
    n1 += CHUNK_SIZE(next);
    next = NEXT_CHUNK(next);
  }
  /* FIXME: find what's wrong here and reenable it..? */
  if (0 && n > n1 && alloc_rev(self)) {
    self = PREV_CHUNK(self);
    n1 += CHUNK_SIZE(self);
  }
  self->csize = n1 | C_INUSE;
  next->psize = n1 | C_INUSE;

  /* If we got enough space, split off the excess and return */
  if (n <= n1) {
    //memmove(CHUNK_TO_MEM(self), p, n0-OVERHEAD);
    trim(self, n);
    return CHUNK_TO_MEM(self);
  }

  /* As a last resort, allocate a new chunk and copy to it. */
  new = malloc(n-OVERHEAD);
  if (!new) return 0;
  memcpy(new, p, n0-OVERHEAD);
  free(CHUNK_TO_MEM(self));
  return new;
}

void free(void *p)
{
  struct chunk *self = MEM_TO_CHUNK(p);
  struct chunk *next;
  size_t final_size, new_size, size;
  int reclaim=0;
  int i;

  if (!p) return;

  if(! __inited ) return;

  if (IS_MMAPPED(self)) {
    size_t extra = self->psize;
    char *base = (char *)self - extra;
    size_t len = CHUNK_SIZE(self) + extra;
    /* Crash on double free */
    if (extra & 1) a_crash();
    munmap(base, len);

    atomic_fetch_sub(&mal_info.mmapped, len);
    atomic_fetch_sub(&mal_info.nmmaps, 1);

    return;
  }

  final_size = new_size = CHUNK_SIZE(self);
  next = NEXT_CHUNK(self);

  /* Crash on corrupted footer (likely from buffer overflow) */
  if (next->psize != self->csize) a_crash();

  for (;;) {
    if (self->psize & next->csize & C_INUSE) {
      self->csize = final_size | C_INUSE;
      next->psize = final_size | C_INUSE;
      i = bin_index(final_size);
      lock_bin(i);
      pthread_mutex_lock(&mal.mutex);
      if (self->psize & next->csize & C_INUSE)
	break;
      pthread_mutex_unlock(&mal.mutex);
      unlock_bin(i);
    }

    if (alloc_rev(self)) {
      self = PREV_CHUNK(self);
      size = CHUNK_SIZE(self);
      final_size += size;
      if (new_size+size > RECLAIM && ((new_size+size)^size) > size)
	reclaim = 1;
    }

    if (alloc_fwd(next)) {
      size = CHUNK_SIZE(next);
      final_size += size;
      if (new_size+size > RECLAIM && ((new_size+size)^size) > size)
	reclaim = 1;
      next = NEXT_CHUNK(next);
    }
  }

  if (!(mal.binmap & 1ULL<<i))
    a_or_64(&mal.binmap, 1ULL<<i);

  self->csize = final_size;
  next->psize = final_size;
  pthread_mutex_unlock(&mal.mutex);

  self->next = BIN_TO_CHUNK(i);
  self->prev = mal.bins[i].tail;
  self->next->prev = self;
  self->prev->next = self;

  /* Replace middle of large chunks with fresh zero pages */
  if (reclaim) {
    uintptr_t a = ((uintptr_t)self + SIZE_ALIGN+PAGE_SIZE-1) & -PAGE_SIZE;
    uintptr_t b = ((uintptr_t)next - SIZE_ALIGN) & -PAGE_SIZE;
#if 1
    madvise((void *)a, b-a, MADV_DONTNEED);
#else
    mmap((void *)a, b-a, PROT_READ|PROT_WRITE,
	   MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
#endif
  }

  unlock_bin(i);
}

void malloc_stats(void){
  fprintf(stderr,
	  "total bytes: %zu\n"
	  "inuse bytes: %zu\n"
	  "mmapped bytes: %zu\n"
	  "mmap count: %d\n",
	  mal_info.bytes, mal_info.inuse,
	  mal_info.mmapped, mal_info.nmmaps);
}
