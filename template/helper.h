#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "batcher.h"
#include "tm.h"

#define MAX_SEGMENTS 65536
// We need the -1, because the first segment has address at 1 to avoid having
// NULL as an address
#define SEGMENT_INDEX(X) ((((uintptr_t)(X)) >> 48) - 1)
#define WORD_INDEX(X) (((uintptr_t)(X)) & 0xffffffffffff)

static const tx_t read_only_tx = UINTPTR_MAX - 10;
static const tx_t success_tx = 0;
static const tx_t abort_tx = 1;

struct Control {
  bool valid;      // index of the valid copy (0 or 1)
  tx_t access_set; // the transaction in the access set
  bool written;    // has been written in current epoch
};

struct Region {
  struct Batcher batcher;
  void *segments[MAX_SEGMENTS];           // Segment at index 0 is reserved
  void *segments_copy[MAX_SEGMENTS];      // Segment copy
  struct Control *controls[MAX_SEGMENTS]; // fixed array of array of control
  size_t n_segments;
  size_t size[MAX_SEGMENTS]; // Size of the first segment (in bytes), mult. of
                             // align
  size_t align; // Claimed alignment of the shared memory region (in bytes)
};

struct Transaction {
  size_t id;
  void *shared; // The shared memory region associated with this transaction.
};

uintptr_t next_free(shared_t shared);
void *choose_copy(shared_t shared, size_t segment_index, size_t index,
                  bool writeable, bool valid);
void read_word_at_index(shared_t shared, void *target, size_t segment_index,
                        size_t word_index, bool valid);
bool read_word(shared_t shared, tx_t tx, void *target, size_t segment_index,
               size_t word_index);
bool read(shared_t shared, tx_t tx, void const *source, size_t size,
          void *target);
bool write_word(shared_t shared, tx_t tx, void const *source,
                size_t segment_index, size_t index);
bool write(shared_t shared, tx_t tx, void const *source, size_t size,
           void *target);
shared_t create(size_t size, size_t align);
alloc_t alloc(shared_t shared, tx_t tx, size_t size, void **target);