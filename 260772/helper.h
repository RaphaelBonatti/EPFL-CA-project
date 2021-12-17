#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "batcher.h"
#include "simple_list.h"
#include "tm.h"

#define MAX_SEGMENTS 65536
// We need the -1, because the first segment has address at 1 to avoid having
// NULL as an address
#define SEGMENT_INDEX(X) ((((uintptr_t)(X)) >> 48) - 1)
#define WORD_INDEX(X)                                                          \
  ((((uintptr_t)(X)) & 0xffffffffffff) / (((struct Region *)shared)->align))

// For the whole encoding
#define ACS_NULL 0

// For the accessed bit field
#define ACS_NOT_ACCESSED 0
#define ACS_ACCESSED 1

// For the type bit field
#define ACS_READ 0
#define ACS_WRITE 1

// For write ability
#define ACS_CAN 0
#define ACS_CANNOT 1

// Helpers
#define ACS_WA(x) ((x)&0b100)
#define ACS_TYPE(x) ((x)&0b10)
#define ACS_ACCESS(x) ((x)&0b1)
#define ACS_ID(x) ((x) >> 3) // The tx that can write

#define ACS_ID_ACCESSED(x) ACS_ID((x)) << 3 | ACS_ACCESSED // Do not care if r/w

// Masks
#define ACS_MASK_ID_TYPE(x) ACS_ID((x)) << 3 | ACS_TYPE((x))
#define ACS_MASK_ID_WA(x) ACS_ID((x)) << 3 | ACS_WA((x))

#define ACS_CREATE(id, write_ability, type, accessed)                          \
  (((id) << 3) | ((write_ability) << 2) | ((type) << 1) | (accessed))

#define ACS_FIRST_READ 0b001
#define ACS_MORE_READ 0b101



typedef atomic_size_t atomic_acs;
typedef size_t acs;

// static const tx_t success_tx = 0;
// static const tx_t abort_tx = 1;
static const tx_t read_only_tx = UINTPTR_MAX - 10;

struct Control {
  // bool valid; // index of the valid copy (0 or 1)
  // atomic_size_t access_set;     // the transaction in the access set
  atomic_size_t accessed_epoch; // Epoch in which the word was accessed, so we
                                // don't need to reset the access set
  // atomic_size_t written;        // has been written in current epoch
  // size_t aborted; // If the thread that has written aborted in the current
  // epoch atomic_short accessed;
  bool saved;            // was the action save on the tx list
  bool accessed_write;   // Was the write accessed
  atomic_size_t tx_read; // first tx that read the
  void *write_word;
  void *read_word;
  atomic_acs access_type_id;
};

struct Region {
  struct Batcher batcher;
  void *segments_write[MAX_SEGMENTS];     // Segment at index 0 is reserved
  void *segments_read[MAX_SEGMENTS];      // Segment copy
  struct Control *controls[MAX_SEGMENTS]; // Fixed array of array of control
  atomic_size_t n_segments;
  struct List modified_controls; // ptr to modified control
  pthread_mutex_t modified_controls_lock;

  struct List freed_segments;
  pthread_mutex_t freed_segments_lock;
  size_t size[MAX_SEGMENTS]; // Size of the segments (in bytes), mult.
                             // of align
  size_t align; // Claimed alignment of the shared memory region (in bytes)
};

struct Transaction {
  size_t id;    // Part of unique id.
  void *shared; // Part of unique id. The shared memory region associated with
                // this transaction.
  bool is_ro;   // Is read only
  bool is_aborted; // Is aborted
                   // struct List modified_controls; // ptr to modified control
  struct List written_words;
  struct List read_words;
  struct List accessed_words;
  struct List alloced_segments; // index of segment (uintptr_t)
  struct List freed_segments;   // index of segment (uintptr_t)
};

uintptr_t next_free(struct Region *reg);
// void *choose_copy(shared_t shared, size_t segment_index, size_t index,
//                   bool writeable, bool valid);
// void read_word_at_index(shared_t shared, void *target, size_t segment_index,
//                         size_t word_index, bool writeable, bool valid);
bool read_word(struct Region *reg, tx_t tx, void *target, size_t segment_index,
               size_t word_index, acs access_type_id);
// void write_word_at_index(shared_t shared, void const *source,
//                          size_t segment_index, size_t word_index, bool
//                          valid);
bool write_word(struct Region *reg, tx_t tx, void const *source,
                size_t segment_index, size_t index, acs access_type_id);
void seg_free(shared_t shared, uintptr_t index);
// void realloc_size_t_array(size_t **array, size_t *size);
// void insert_segment_array(size_t **array, size_t *size, size_t *n,
//                           size_t index);
void commit(shared_t shared);

void printBits(unsigned int num);