#include <stdlib.h>
#include <string.h>

#include "helper.h"
#include "macros.h"

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

void *choose_copy(shared_t shared, size_t segment_index, size_t word_index,
                  bool writeable, bool valid) {
  struct Region *reg = (struct Region *)shared;

  // Writeable/Valid   0     | 1
  //                0 first | second
  //                1 second| first
  if (valid == writeable) {
    return reg->segments[segment_index] + word_index;
  }

  return reg->segments_copy[segment_index] + word_index;
}

void read_word_at_index(shared_t shared, void *target, size_t segment_index,
                        size_t word_index, bool valid) {
  struct Region *reg = (struct Region *)shared;
  void *word = choose_copy(shared, segment_index, word_index, false, valid);
  size_t offset = word_index * reg->align;

  memcpy(target + offset, word, reg->align);
}

bool read_word(shared_t shared, tx_t tx, void *target, size_t segment_index,
               size_t word_index) {
  struct Region *reg = (struct Region *)shared;
  struct Control control = reg->controls[segment_index][word_index];

  if (tx == read_only_tx) {
    // TODO: read
    read_word_at_index(shared, target, segment_index, word_index,
                       control.valid);
    return true;
  }

  // the word has been written in the current epoch
  if (control.written) {
    // no transaction in access set
    if (!control.access_set) {
      read_word_at_index(shared, target, segment_index, word_index,
                         control.valid);
      return true;
    }

    return false;
  }

  read_word_at_index(shared, target, segment_index, word_index, control.valid);
  // add the transaction into the \access set" (if not already in);
  // what is a transaction, an id?
  control.access_set = tx;
  return true;
}

// bool read(shared_t shared, tx_t tx, void const *source, size_t size,
//           void *target) {
//   size_t word_index = WORD_INDEX(source);
//   size_t segment_index = SEGMENT_INDEX(source);

//   for (size_t i = word_index; i < word_index + size; ++i) {
//     bool result = read_word(shared, tx, target, segment_index, word_index);
//     if (!result) {
//       return false;
//     }
//   }
//   return true;
// }

void write_word_at_index(shared_t shared, void const *source,
                         size_t segment_index, size_t word_index, bool valid) {
  struct Region *reg = (struct Region *)shared;
  void *word = choose_copy(shared, segment_index, word_index, true, valid);
  uintptr_t offset = word_index * reg->align;

  memcpy(word, source + offset, reg->align);
}

bool write_word(shared_t shared, tx_t tx, void const *source,
                size_t segment_index, size_t word_index) {
  struct Region *reg = (struct Region *)shared;
  struct Control control = reg->controls[segment_index][word_index];

  if (control.written) {
    // if the transaction is already in the access set
    if (control.access_set == tx) {
      write_word_at_index(shared, source, segment_index, word_index,
                          control.valid);

      return true;
    }

    return false;
  }

  // if at least one other transaction is in the access set
  if (control.access_set != 0 && control.access_set != tx) {
    return false;
  }

  // write source into writable
  write_word_at_index(shared, source, segment_index, word_index, control.valid);

  // add the transaction into the \access set" (if not already in);
  control.access_set = tx;

  // mark that the word has been written in the current epoch;
  control.written = true;
  return true;
}

// bool write(shared_t shared, tx_t tx, void const *source, size_t size,
//            void *target) {
//   size_t word_index = WORD_INDEX(target);
//   size_t segment_index = SEGMENT_INDEX(target);

//   for (size_t i = word_index; i < word_index + size; ++i) {
//     bool result = write_word(shared, tx, source, segment_index, i);
//     if (!result) {
//       return false;
//     }
//   }
//   return true;
// }

// shared_t create(size_t size, size_t align) {
//   struct Region *reg = (struct Region *)malloc(sizeof(struct Region));

//   if (unlikely(!reg)) {
//     return invalid_shared;
//   }

//   init_batcher(&reg->batcher);

//   // Set the pointers to segments to NULL
//   memset(reg->segments, 0, MAX_SEGMENTS);
//   memset(reg->segments_copy, 0, MAX_SEGMENTS);

//   // Try to allocate the aligned memory for the first segment
//   if (posix_memalign(&(reg->segments[0]), align, size) != 0 ||
//       posix_memalign(&(reg->segments_copy[0]), align, size) != 0) {
//     free(reg);
//     return invalid_shared;
//   }

//   // Set the segment memory to 0
//   memset(reg->segments[0], 0, size);
//   memset(reg->segments_copy[0], 0, size);

//   // Initialize the control struct (set everything to 0)
//   reg->controls[0] = calloc(size / align, sizeof(struct Control));

//   // Initialize the region fields
//   reg->n_segments = 1; // the index of next segment to allocate
//   reg->size = size;
//   reg->align = align;

//   return reg;
// }

uintptr_t next_free(shared_t shared) {
  struct Region *reg = (struct Region *)shared;

  // Take the next available index O(1) time if the number of allocations is
  // less than the maximum. Index go from 0 to MAX_SEGMENTS - 1
  if (reg->n_segments < MAX_SEGMENTS) {
    return reg->n_segments + 1;
  }

  // Search the first index with non allocated segment. O(n) time.
  unsigned int i = 0;
  while (reg->segments[i] != NULL) {
    ++i;
  }

  return i;
}

// alloc_t alloc(shared_t shared, tx_t tx, size_t size, void **target) {
//   struct Region *reg = (struct Region *)shared;
//   uintptr_t index = next_free(shared);

//   if (posix_memalign((void **)&(reg->segments[index]), reg->align, size) != 0
//   ||
//       posix_memalign((void **)&(reg->segments_copy[index]), reg->align, size)
//       !=
//           0) {
//     return nomem_alloc;
//   }

//   memset(reg->segments[index], 0, size);
//   memset(reg->segments_copy[index], 0, size);

//   // Allocate control structre
//   reg->controls[index] = calloc(size / reg->align, sizeof(struct Control));

//   *target = (void *)(index << 48);

//   ++reg->n_segments;

//   return success_alloc;
// }

// bool tm_free(shared_t shared, tx_t tx, void *target) {
//   struct Region *reg = (struct Region *)shared;
//   uintptr_t index = SEGMENT_INDEX(target);

//   free(reg->segments[index]);
//   free(reg->segments_copy[index]);
//   free(reg->controls[index]);

//   // To indicate a free index
//   reg->segments[index] = NULL;

//   return false;
// }