/**
 * @file   tm.c
 * @author Raphael Bonatti
 *
 * @section LICENSE
 *
 * [...]
 *
 * @section DESCRIPTION
 *
 * Implementation of your own transaction manager.
 * You can completely rewrite this file (and create more files) as you wish.
 * Only the interface (i.e. exported symbols and semantic) must be preserved.
 **/

// Requested features
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#ifdef __STDC_NO_ATOMICS__
#error Current C11 compiler does not support atomic operations
#endif

// External headers
#include <stdio.h> // for debug purposes
#include <stdlib.h>
#include <string.h>

// Internal headers
#include <tm.h>

#include "helper.h"
#include "macros.h"

/** Create (i.e. allocate + init) a new shared memory region, with one first
 *non-free-able allocated segment of the requested size and alignment.
 * @param size  Size of the first shared segment of memory to allocate (in
 *bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the shared
 *memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
 **/
shared_t tm_create(size_t size, size_t align) {
  struct Region *reg = (struct Region *)calloc(1, sizeof(struct Region));

  if (unlikely(!reg)) {
    printf("Error: Could not allocate memory.\n");
    free(reg);
    return invalid_shared;
  }

  init_batcher(&reg->batcher);

  // Try to allocate the aligned memory for the first segment
  if (unlikely(posix_memalign(&(reg->segments_write[0]), align, size) != 0 ||
               posix_memalign(&(reg->segments_read[0]), align, size) != 0)) {
    free(reg);
    return invalid_shared;
  }

  // Set the segment memory to 0
  memset(reg->segments_write[0], 0, size);
  memset(reg->segments_read[0], 0, size);

  // Initialize the control struct (set everything to 0)
  reg->controls[0] =
      (struct Control *)calloc(size / align, sizeof(struct Control));

  init_list(&reg->modified_controls, sizeof(struct Control *));
  init_list(&reg->freed_segments, sizeof(uintptr_t));

  pthread_mutex_init(&reg->modified_controls_lock, NULL);
  pthread_mutex_init(&reg->freed_segments_lock, NULL);

  // Initialize the region fields
  reg->n_segments = 1; // the index of next segment to allocate
  // memset(reg->size, 0, MAX_SEGMENTS * sizeof(size_t));
  // memset(reg->to_free, 0, MAX_SEGMENTS);
  reg->size[0] = size;
  reg->align = align;

  return reg;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
 **/
void tm_destroy(shared_t shared) {
  struct Region *reg = (struct Region *)shared;

  batcher_destroy(&reg->batcher);

  size_t seg = 1;
  size_t i = 0;

  while (seg < MAX_SEGMENTS && i < reg->n_segments) {
    if (reg->controls[seg] != NULL) {
      free(reg->segments_write[seg]);
      free(reg->segments_read[seg]);
      free(reg->controls[seg]);
      ++i;
    }

    ++seg;
  }

  free(reg->segments_write[0]);
  free(reg->segments_read[0]);
  free(reg->controls[0]);

  destroy_list(&reg->freed_segments);
  destroy_list(&reg->modified_controls);

  pthread_mutex_destroy(&reg->modified_controls_lock);
  pthread_mutex_destroy(&reg->freed_segments_lock);

  free(reg);
}

/** [thread-safe] Return the start address of the first allocated segment in the
 *shared memory region.
 * @param shared Shared memory region to query
 * @return Start address of the first allocated segment
 **/
void *tm_start(shared_t unused(shared)) { return (void *)((uintptr_t)1 << 48); }

/** [thread-safe] Return the size (in bytes) of the first allocated segment of
 *the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
 **/
size_t tm_size(shared_t shared) { return ((struct Region *)shared)->size[0]; }

/** [thread-safe] Return the alignment (in bytes) of the memory accesses on the
 *given shared memory region.
 * @param shared Shared memory region to query
 * @return Alignment used globally
 **/
size_t tm_align(shared_t shared) { return ((struct Region *)shared)->align; }

/** [thread-safe] Begin a new transaction on the given shared memory region.
 * @param shared Shared memory region to start a transaction on
 * @param is_ro  Whether the transaction is read-only
 * @return Opaque transaction ID, 'invalid_tx' on failure
 **/
tx_t tm_begin(shared_t shared, bool is_ro) {
  struct Region *reg = (struct Region *)shared;

  if (is_ro) {
    enter(&reg->batcher);
    return read_only_tx;
  }

  struct Transaction *tr =
      (struct Transaction *)calloc(1, sizeof(struct Transaction));

  if (unlikely(tr == NULL)) {
    free(tr);
    return invalid_tx;
  }

  // Unique transaction defined by id and shared
  tr->id = atomic_fetch_add(&reg->batcher.tx_count, 1);
  // tr->is_ro = is_ro;
  tr->is_aborted = 0;
  // tr->shared = shared;
  init_list(&tr->alloced_segments, sizeof(uintptr_t));
  init_list(&tr->freed_segments, sizeof(uintptr_t));
  init_list(&tr->accessed_words, sizeof(struct Control *));

  enter(&reg->batcher);

  return (uintptr_t)tr;
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
 **/
bool tm_end(shared_t shared, tx_t tx) {
  struct Region *reg = (struct Region *)shared;

  if (tx == read_only_tx) {
    leave(&reg->batcher, commit, shared);

    return true;
  }

  struct Transaction *tr = (struct Transaction *)tx;
  struct Control *control;
  uintptr_t index;
  bool correct = true;

  if (unlikely(tr->is_aborted)) {
    // Must undo writes and reads
    for (size_t i = 0; i < tr->accessed_words.n; ++i) {
      control = get_list(&tr->accessed_words, i, struct Control *);
      control->saved = 0;
      control->tx_read = 0;
      control->accessed_write = 0;
      control->accessed_epoch = 0;
      control->access_type_id = ACS_NULL;
    }

    // Must free allocated segments
    for (size_t i = 0; i < tr->alloced_segments.n; ++i) {
      index = get_list(&tr->alloced_segments, i, uintptr_t);
      atomic_fetch_sub(&reg->n_segments, 1);
      seg_free(shared, index);
    }

    correct = false;
  } else {
    if (tr->freed_segments.n > 0) {
      pthread_mutex_lock(&reg->freed_segments_lock);
      for (size_t i = 0; i < tr->freed_segments.n; ++i) {
        index = get_list(&tr->freed_segments, i, uintptr_t);
        insert_list(&reg->freed_segments, index, uintptr_t);
      }
      pthread_mutex_unlock(&reg->freed_segments_lock);
    }

    // Commit your acessed words
    pthread_mutex_lock(&reg->modified_controls_lock);
    for (size_t i = 0; i < tr->accessed_words.n; ++i) {
      control = get_list(&tr->accessed_words, i, struct Control *);

      insert_list(&reg->modified_controls, control, struct Control *);
    }
    pthread_mutex_unlock(&reg->modified_controls_lock);

    // Mark to  free segments
  }

  leave(&reg->batcher, commit, shared);

  // Clean up tx
  destroy_list(&tr->alloced_segments);
  destroy_list(&tr->freed_segments);
  destroy_list(&tr->accessed_words);
  free(tr);

  return correct;
}

/** [thread-safe] Read operation in the given transaction, source in the shared
 *region and target in a private region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in the shared region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the
 *alignment
 * @param target Target start address (in a private region)
 * @return Whether the whole transaction can continue
 **/
bool tm_read(shared_t shared, tx_t tx, void const *source, size_t size,
             void *target) {
  struct Region *reg = (struct Region *)shared;
  struct Transaction *tr = (struct Transaction *)tx;
  size_t word_index = WORD_INDEX(source);
  size_t segment_index = SEGMENT_INDEX(source);
  acs acs_read = ACS_NULL;

  if (tx != read_only_tx) {
    acs_read = ACS_CREATE(tr->id, 0, 1, 1); // supposing we've written
  }

  for (size_t i = 0; i < size / reg->align; ++i) {
    bool result = read_word(shared, tx, ((char *)target + i * reg->align),
                            segment_index, word_index + i, acs_read);
    if (!result) {
      ((struct Transaction *)tx)->is_aborted = 1;
      tm_end(shared, tx);
      return false;
    }
  }

  return true;
}

/** [thread-safe] Write operation in the given transaction, source in a private
 *region and target in the shared region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in a private region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the
 *alignment
 * @param target Target start address (in the shared region)
 * @return Whether the whole transaction can continue
 **/
bool tm_write(shared_t shared, tx_t tx, void const *source, size_t size,
              void *target) {
  struct Region *reg = (struct Region *)shared;
  struct Transaction *tr = (struct Transaction *)tx;
  size_t word_index = WORD_INDEX(target);
  size_t segment_index = SEGMENT_INDEX(target);
  acs acs_write = ACS_CREATE(tr->id, ACS_CAN, ACS_WRITE, ACS_ACCESSED);

  for (size_t i = 0; i < size / reg->align; ++i) {
    bool result = write_word(shared, tx, ((char *)source + i * reg->align),
                             segment_index, word_index + i, acs_write);
    if (!result) {
      ((struct Transaction *)tx)->is_aborted = 1;
      tm_end(shared, tx);

      return false;
    }
  }

  return true;
}

/** [thread-safe] Memory allocation in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param size   Allocation requested size (in bytes), must be a positive
 *multiple of the alignment
 * @param target Pointer in private memory receiving the address of the first
 *byte of the newly allocated, aligned segment
 * @return Whether the whole transaction can continue (success/nomem), or not
 *(abort_alloc)
 **/
alloc_t tm_alloc(shared_t shared, tx_t tx, size_t size, void **target) {
  struct Region *reg = (struct Region *)shared;
  struct Transaction *tr = (struct Transaction *)tx;
  // printf("Tx: %ld Alloc\n", tr->id);

  // pthread_mutex_lock(&reg->batcher.alloc_lock);
  uintptr_t index = next_free(reg);
  // ++reg->n_segments;
  // pthread_mutex_unlock(&reg->batcher.alloc_lock);

  // if (unlikely(tr->is_aborted))
  // {
  //   atomic_fetch_sub(&reg->n_segments, 1);
  //   return abort_alloc;
  // }

  if (unlikely(posix_memalign((void **)&(reg->segments_write[index]),
                              reg->align, size) != 0 ||
               posix_memalign((void **)&(reg->segments_read[index]), reg->align,
                              size) != 0)) {
    atomic_fetch_sub(&reg->n_segments, 1);
    return nomem_alloc;
  }

  memset(reg->segments_write[index], 0, size);
  memset(reg->segments_read[index], 0, size);

  // Allocate control structre
  reg->controls[index] =
      (struct Control *)calloc(size / reg->align, sizeof(struct Control));

  // We add one because we start a 1, the first segment was allocated at
  // creation of the shared memory.
  *target = (void *)((index + 1) << 48);
  reg->size[index] = size;

  insert_list(&tr->alloced_segments, index, uintptr_t);

  return success_alloc;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment
 *to deallocate
 * @return Whether the whole transaction can continue
 **/
bool tm_free(shared_t unused(shared), tx_t tx, void *target) {
  struct Transaction *tr = (struct Transaction *)tx;
  uintptr_t index = SEGMENT_INDEX(target);

  insert_list(&tr->freed_segments, index, uintptr_t);

  return true;
}
