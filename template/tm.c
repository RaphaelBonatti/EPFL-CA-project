/**
 * @file   tm.c
 * @author [...]
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

#define CHECK_TX(X, VAL)                                                       \
  if (((X) != read_only_tx) &&                                                 \
      unlikely(((struct Transaction *)(X))->shared != shared)) {               \
    return (VAL);                                                              \
  }

// External headers
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
  struct Region *reg = (struct Region *)malloc(sizeof(struct Region));

  if (unlikely(!reg)) {
    return invalid_shared;
  }

  init_batcher(&reg->batcher);

  // Set the pointers to segments to NULL
  memset(reg->segments, 0, MAX_SEGMENTS * sizeof(void *));
  memset(reg->segments_copy, 0, MAX_SEGMENTS * sizeof(void *));

  // Try to allocate the aligned memory for the first segment
  if (posix_memalign(&(reg->segments[0]), align, size) != 0 ||
      posix_memalign(&(reg->segments_copy[0]), align, size) != 0) {
    free(reg);
    return invalid_shared;
  }

  // Set the segment memory to 0
  memset(reg->segments[0], 0, size);
  memset(reg->segments_copy[0], 0, size);

  // Initialize the control struct (set everything to 0)
  reg->controls[0] = calloc(size / align, sizeof(struct Control));

  // Initialize the region fields
  reg->n_segments = 1; // the index of next segment to allocate
  memset(reg->size, 0, MAX_SEGMENTS * sizeof(size_t));
  reg->size[0] = size;
  reg->align = align;

  return reg;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
 **/
void tm_destroy(shared_t unused(shared)) {
  // TODO: tm_destroy(shared_t)
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

  enter(&reg->batcher);

  if (is_ro) {
    return read_only_tx;
  } else {
    struct Transaction *tr = malloc(sizeof(struct Transaction));

    if (tr == NULL) {
      return invalid_tx;
    }

    // Unique transaction defined by id and shared
    tr->id = atomic_fetch_add(&reg->batcher.tx_count, 1);
    tr->shared = shared;

    return (uintptr_t)tr;
  }
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
 **/
bool tm_end(shared_t shared, tx_t tx) {
  CHECK_TX(tx, false);

  struct Region *reg = (struct Region *)shared;

  // FIXME: What if an adversary uses this tx several times
  // should I store the tx somewhere?
  if (tx == read_only_tx) {
    leave(&reg->batcher);
    return true;
  }

  leave(&reg->batcher);

  // Last transaction, then must swap written words valid status
  // TODO: fix this bottleneck: Could save the address of control variables
  // or keep track of accessed segments
  if (reg->batcher.remaining == 0) {
    size_t remaining_to_check = reg->n_segments;
    size_t seg = 0;
    while (remaining_to_check > 0) {
      while (reg->controls[seg] == NULL) {
        ++seg;
      }

      for (size_t word = 0; word < reg->size[seg]; ++word) {
        reg->controls[seg][word].access_set = 0;

        if (reg->controls[seg][word].written) {
          reg->controls[seg][word].valid = !reg->controls[seg][word].valid;
          reg->controls[seg][word].written = 0;
        }
      }

      --remaining_to_check;
    }

    pthread_cond_broadcast(&reg->batcher.empty);

    pthread_mutex_unlock(&reg->batcher.lock);
  }

  free((struct Transaction *)tx);

  return true;
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
  CHECK_TX(tx, false);

  size_t word_index = WORD_INDEX(source);
  size_t segment_index = SEGMENT_INDEX(source);

  for (size_t i = word_index; i < word_index + size; ++i) {
    bool result = read_word(shared, tx, target, segment_index, word_index);
    if (!result) {
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
  CHECK_TX(tx, false);

  size_t word_index = WORD_INDEX(target);
  size_t segment_index = SEGMENT_INDEX(target);

  for (size_t i = word_index; i < word_index + size; ++i) {
    bool result = write_word(shared, tx, source, segment_index, i);
    if (!result) {
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
  CHECK_TX(tx, abort_alloc);

  struct Region *reg = (struct Region *)shared;
  uintptr_t index = next_free(shared);

  if (posix_memalign((void **)&(reg->segments[index]), reg->align, size) != 0 ||
      posix_memalign((void **)&(reg->segments_copy[index]), reg->align, size) !=
          0) {
    return nomem_alloc;
  }

  memset(reg->segments[index], 0, size);
  memset(reg->segments_copy[index], 0, size);

  // Allocate control structre
  reg->controls[index] = calloc(size / reg->align, sizeof(struct Control));

  *target = (void *)(index << 48);

  ++reg->n_segments;

  return success_alloc;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment
 *to deallocate
 * @return Whether the whole transaction can continue
 **/
bool tm_free(shared_t shared, tx_t tx, void *target) {
  CHECK_TX(tx, false);

  struct Region *reg = (struct Region *)shared;
  uintptr_t index = SEGMENT_INDEX(target);

  free(reg->segments[index]);
  free(reg->segments_copy[index]);
  free(reg->controls[index]);

  // To indicate a free index
  reg->segments[index] = NULL;
  reg->segments_copy[index] = NULL;
  reg->controls[index] = NULL;

  return true;
}
