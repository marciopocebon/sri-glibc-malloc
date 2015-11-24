#ifndef _METADATA_H
#define _METADATA_H

/*
 * Dynamic hashing, after CACM April 1988 pp 446-457, by Per-Ake Larson.
 * [{ X }] refers to the concept X in Larson's paper.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <stdio.h>

#include "types.h"
#include "memcxt.h"


#define SEGMENT_LENGTH 256

#define DIRECTORY_LENGTH 1024


/*
 *
 * The directory is an expandable array of segments.  Each segment is
 * essentially a fixed size array of bins.  Each bin is essentially a
 * linked list of buckets.  Each bin is "addressed" by its hash.  The
 * hash is currently uint32_t. So the maximum number of bins is also
 * uint32_t.
 *
 * We either we run out of memory prior to achieving the maximum number
 * of bins, or else we achieve the maximum.
 *
 * Once the maximum number of bins has been reached, we should still
 * be able to function as a hash table, the load will just start to
 * increase, as the linked lists of buckets get longer.  The only real
 * *catastrophic* failure should be when we can no longer create
 * buckets. 
 *
 */

#ifndef INTERNAL_SIZE_T
#define INTERNAL_SIZE_T size_t
#endif

typedef void * mchunkptr;

typedef struct bucket_s {
  INTERNAL_SIZE_T   prev_size;     /* Size of previous in bytes          */
  INTERNAL_SIZE_T   size;          /* Size in bytes, including overhead. */
  INTERNAL_SIZE_T   req;           /* Original request size, for guard.  */
  struct bucket_s*  fd;	           /* double links -- used only if free. */
  struct bucket_s*  bk;            /* double links -- used only if free. */
  struct bucket_s*  next_bucket;   /* next bucket in the bin             */
  mchunkptr key;                  
  bucket_pool_t* bucket_pool_ptr;  //BD's optimization #1.
} bucket_t;


typedef struct segment_s {
  bucket_t* segment[SEGMENT_LENGTH];
  segment_pool_t *segment_pool_ptr;  
} segment_t;


typedef struct metadata_cfg_s {

  memcxt_t *memcxt;                 /* Where we get our memory from                                            */
 
  size_t segment_length;            /* segment length; larsen uses 256; we could use  4096 or 2^18 = 262144    */

  size_t initial_directory_length;  /* Larsen's directory is static ( also 256), ours will have to be dynamic  */

  size_t directory_length_max;      /* Currently don't get this big (see note following this)                  */

  size_t bincount_max;              /* the maximum number of bins: directory_length_max * segment_length       */
  
  uint16_t min_load;                /* Not sure if Larsen ever specifies his value for this                    */

  uint16_t max_load;                /* Larsen uses 5 we could use  4 or 8                                      */

  bool multithreaded;               /* are we going to protect against contention                              */


} metadata_cfg_t;

/*

  Notes (ddean, iam):

  2^44 bytes on a godzilla machine
  
  minimum malloc chunk  16 <=  x  <=  64 

  Thus 2^38 <  # of chunks  < 2^40

  If we settle on a segment_length of between 4096 = 2^12  and 262144 = 2^18

  Then  [2^20,  2^26] <= directory_length_max <= [2^22,  2^28]

 */



typedef struct metadata_s {

  metadata_cfg_t cfg;             /* configuration constants                                                */

  pthread_mutex_t mutex;	 /* lock for resolving contention    (only when cfg->multithreaded)        */

  segment_t** directory;         /* the array of segment pointers                                          */

  size_t directory_length;       /* the size of the directory (must be a power of two)                     */

  size_t directory_current;      /* the number of segments in the directory                                */

  size_t N;                      /* mininum number of buckets    [{ N }]                                   */
  
  size_t L;                      /* the number of times the table has doubled in size  [{ L }]             */

  size_t p;                      /* index of the next bin due to be split  [{ p :  0 <= p < N * 2^L }]     */

  size_t count;                  /* the total number of records in the table                               */
 
  size_t maxp;                   /* the current limit on the bin count  [{ maxp = N * 2^L }]               */

  size_t bincount;               /* the current number of bins                                             */
  
} metadata_t;




/* 
 * Initializes a metadata_t object; returns true if successful; false if not.
 * If it returns false it sets errno to explain the error.
 * It can fail due to:
 *   -- lack of memory   errno = ENOMEM.
 *   -- bad arguments    errno = EINVAL.
 */
extern bool init_metadata(metadata_t* lhash, memcxt_t* memcxt);

extern void delete_metadata(metadata_t* htbl);

/* 
 * Adds the bucket to the table according to the key. Returns true if successful; false if not.
 * If it returns false it sets errno to explain the error.
 * It can fail due to:
 *   -- lack of memory   errno = ENOMEM.
 *   -- bad arguments    errno = EINVAL.
 *   BD: also wants it to fail if the key is already in the table.
 */

extern bool metadata_add(metadata_t* htbl, bucket_t* bucket);

extern bucket_t* metadata_lookup(metadata_t* htbl, const void *key);

/* deletes the first bucket keyed by key; returns true if such a bucket was found; false otherwise */
extern bool metadata_delete(metadata_t* htbl, const void *key);

/* deletes all buckets keyed by key; returns the number of buckets deleted */
extern size_t metadata_delete_all(metadata_t* htbl, const void *key);

extern void dump_metadata(FILE* fp, metadata_t* lhash, bool showloads);

static inline bucket_t* new_bucket(metadata_t* htbl){
  return htbl->cfg.memcxt->allocate(BUCKET, sizeof(bucket_t));
}

static inline bool metadata_insert (metadata_t* htbl, bucket_t* ci_orig, bucket_t* ci_insert){
  return metadata_add(htbl, ci_insert);
}

static inline bool metadata_skiprm (metadata_t* htbl, bucket_t* ci_orig, bucket_t* ci_todelete){
  return metadata_delete(htbl, ci_todelete->key);
}

static inline bool metadata_insert_key(metadata_t* htbl, void * key){
  bucket_t* newb = new_bucket(htbl);
  if(newb != NULL){
    newb->key = key;
    return metadata_add(htbl, newb);
  }
  return false;
}

#endif


