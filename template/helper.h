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
#define WORD_INDEX(X) \
  ((((uintptr_t)(X)) & 0xffffffffffff) / (((struct Region *)shared)->align))

// static const tx_t success_tx = 0;
// static const tx_t abort_tx = 1;

struct Control
{
  bool valid;            // index of the valid copy (0 or 1)
  size_t access_set;     // the transaction in the access set
  size_t accessed_epoch; // Epoch in which the word was accessed, so we don't
                         // need to reset the access set
  size_t written;        // has been written in current epoch
  size_t aborted;        // If the thread that has written aborted in the current epoch
  void *write_word;
  void *read_word;
};

struct Region
{
  struct Batcher batcher;
  void *segments_write[MAX_SEGMENTS];     // Segment at index 0 is reserved
  void *segments_read[MAX_SEGMENTS];      // Segment copy
  struct Control *controls[MAX_SEGMENTS]; // Fixed array of array of control
  atomic_size_t n_segments;
  struct List modified_controls; // ptr to modified control
  pthread_mutex_t modified_controls_lock;

  // bool to_free[MAX_SEGMENTS]; // To free at commit

  struct List freed_segments;
  pthread_mutex_t freed_segments_lock;
  size_t size[MAX_SEGMENTS]; // Size of the first segment (in bytes), mult.
                             // of align
  size_t align;              // Claimed alignment of the shared memory region (in bytes)
};

struct Transaction
{
  size_t id;       // Part of unique id.
  void *shared;    // Part of unique id. The shared memory region associated with
                   // this transaction.
  bool is_ro;      // Is read only
  bool is_aborted; // Is aborted
                   // struct List modified_controls; // ptr to modified control
  struct List written_words;
  struct List read_words;
  struct List accessed_words;
  struct List alloced_segments; // index of segment (uintptr_t)
  struct List freed_segments;   // index of segment (uintptr_t)
};

uintptr_t next_free(shared_t shared);
void *choose_copy(shared_t shared, size_t segment_index, size_t index,
                  bool writeable, bool valid);
void read_word_at_index(shared_t shared, void *target, size_t segment_index,
                        size_t word_index, bool writeable, bool valid);
bool read_word(shared_t shared, tx_t tx, void *target, size_t segment_index,
               size_t word_index);
void write_word_at_index(shared_t shared, void const *source,
                         size_t segment_index, size_t word_index, bool valid);
bool write_word(shared_t shared, tx_t tx, void const *source,
                size_t segment_index, size_t index);
void seg_free(shared_t shared, uintptr_t index);
void realloc_size_t_array(size_t **array, size_t *size);
void insert_segment_array(size_t **array, size_t *size, size_t *n,
                          size_t index);
void commit(shared_t shared);
