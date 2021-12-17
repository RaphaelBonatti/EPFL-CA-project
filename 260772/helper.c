#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helper.h"
#include "macros.h"

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

void printBits(unsigned int num)
{
   for(int bit=0;bit<(sizeof(unsigned int) * 8); bit++)
   {
      printf("%i ", num & 0x01);
      num = num >> 1;
   }
}

bool read_word(struct Region *reg, tx_t tx, void *target, size_t segment_index,
               size_t word_index, acs access_type_id) {
  struct Control *control = &reg->controls[segment_index][word_index];
  struct Transaction *tr = (struct Transaction *)tx;

  if (tx == read_only_tx) {
    void *word =
        (char *)reg->segments_read[segment_index] + word_index * reg->align;
    memcpy(target, word, reg->align);

    return true;
  }

  if (atomic_load(&control->access_type_id) == access_type_id) {
    memcpy(target, control->write_word, reg->align);

    return true;
  }

  acs expected_acs = ACS_NULL;
  acs expected_acs_2 = 0b1;

  if (atomic_compare_exchange_strong(&control->access_type_id, &expected_acs,
                                     ACS_FIRST_READ)) {

    control->tx_read = tr->id;

    control->write_word =
        (char *)reg->segments_write[segment_index] + word_index * reg->align;
    control->read_word =
        (char *)reg->segments_read[segment_index] + word_index * reg->align;
    control->saved = 1;

    insert_list(&tr->accessed_words, control, struct Control *);

    memcpy(target, control->read_word, reg->align);

    return true;
  }

  if (atomic_load(&control->tx_read) == tr->id) {
    memcpy(target, control->read_word, reg->align);

    return true;
  }

  if (atomic_compare_exchange_strong(&control->access_type_id, &expected_acs_2,
                                     ACS_MORE_READ) ||
      atomic_load(&control->access_type_id) == ACS_MORE_READ) {
    // control->accessed_epoch = reg->batcher.epoch;
    // control->access_set = tr->id;

    memcpy(target, control->read_word, reg->align);

    return true;
  }

  return false;
}

bool write_word(struct Region *reg, tx_t tx, void const *source,
                size_t segment_index, size_t word_index, acs access_type_id) {
  struct Control *control = &reg->controls[segment_index][word_index];
  struct Transaction *tr = (struct Transaction *)tx;

  if (atomic_load(&control->access_type_id) == access_type_id) {
    memcpy(control->write_word, source, reg->align);

    return true;
  }

  acs expected_acs = ACS_NULL;
  acs expected_acs_2 = 0b1;

  if (atomic_compare_exchange_strong(&control->access_type_id, &expected_acs,
                                     access_type_id) ||
      atomic_compare_exchange_strong(&control->access_type_id, &expected_acs_2,
                                     access_type_id)) {
    if (!control->saved) {
      control->write_word =
          (char *)reg->segments_write[segment_index] + word_index * reg->align;
      control->read_word =
          (char *)reg->segments_read[segment_index] + word_index * reg->align;
      control->saved = 1;
    }

    if (!control->accessed_write) {
      insert_list(&tr->accessed_words, control, struct Control *);
      control->accessed_write = 1;
    }

    memcpy(control->write_word, source, reg->align);

    return true;
  }

  return false;
}

uintptr_t next_free(struct Region *reg) {
  // Take the next available index O(1) time if the number of allocations is
  // less than the maximum. Index go from 0 to MAX_SEGMENTS - 1
  if (reg->n_segments < MAX_SEGMENTS) {
    return atomic_fetch_add(&reg->n_segments, 1);
  }

  // Search the first index with non allocated segment. O(n) time.
  unsigned int i = 0;
  while (reg->segments_write[i] != NULL) {
    ++i;
  }

  return i;
}

void seg_free(shared_t shared, uintptr_t index) {
  struct Region *reg = (struct Region *)shared;

  // assert(reg->segments_write[index] != NULL);
  // assert(reg->segments_read[index] != NULL);
  // assert(reg->controls[index] != NULL);

  free(reg->segments_write[index]);
  free(reg->segments_read[index]);
  free(reg->controls[index]);

  // To indicate a free index
  reg->segments_write[index] = NULL;
  reg->segments_read[index] = NULL;
  reg->controls[index] = NULL;
}

void commit(shared_t shared) {
  struct Region *reg = (struct Region *)shared;
  struct Control *control = NULL;

  for (size_t i = 0; i < reg->modified_controls.n; ++i) {
    control = get_list(&reg->modified_controls, i, struct Control *);

    if (ACS_TYPE(control->access_type_id)) {
      memcpy(control->read_word, control->write_word, reg->align);
    }

    control->access_type_id = ACS_NULL;
    control->accessed_epoch = 0;
    control->accessed_write = 0;
    control->saved = 0;
    control->tx_read = 0;
  }

  reg->modified_controls.n = 0;

  for (size_t i = 0; i < reg->freed_segments.n; ++i) {
    uintptr_t index = get_list(&reg->freed_segments, i, uintptr_t);
    seg_free(shared, index);
  }

  reg->freed_segments.n = 0;
}