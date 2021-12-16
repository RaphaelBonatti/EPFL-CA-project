#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helper.h"
#include "macros.h"

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

bool read_word(shared_t shared, tx_t tx, void *target, size_t segment_index,
               size_t word_index)
{
  struct Region *reg = (struct Region *)shared;
  struct Control *control = &reg->controls[segment_index][word_index];
  struct Transaction *tr = (struct Transaction *)tx;

  if (tr->is_ro)
  {
    void *word =
        (char *)reg->segments_read[segment_index] + word_index * reg->align;
    memcpy(target, word, reg->align);

    return true;
  }

  pthread_mutex_lock(&reg->batcher.read_lock);

  if (control->written == reg->batcher.epoch)
  {
    if ((control->accessed_epoch == reg->batcher.epoch) &&
        (control->access_set == tr->id))
    {
      memcpy(target, control->write_word, reg->align);
      pthread_mutex_unlock(&reg->batcher.read_lock);
      return true;
    }
    pthread_mutex_unlock(&reg->batcher.read_lock);
    return false;
  }

  if (control->accessed_epoch != reg->batcher.epoch)
  {
    control->accessed_epoch = reg->batcher.epoch;
    pthread_mutex_unlock(&reg->batcher.read_lock);
    control->access_set = tr->id;

    control->write_word =
        (char *)reg->segments_write[segment_index] + word_index * reg->align;
    control->read_word =
        (char *)reg->segments_read[segment_index] + word_index * reg->align;
    insert_list(&tr->accessed_words, control, struct Control *);
  }
  else
  {
    pthread_mutex_unlock(&reg->batcher.read_lock);
  }

  memcpy(target, control->read_word, reg->align);

  return true;
}

bool write_word(shared_t shared, tx_t tx, void const *source,
                size_t segment_index, size_t word_index)
{
  struct Region *reg = (struct Region *)shared;
  struct Control *control = &reg->controls[segment_index][word_index];
  struct Transaction *tr = (struct Transaction *)tx;

  pthread_mutex_lock(&reg->batcher.write_lock);
  if (control->written == reg->batcher.epoch)
  {
    // Check if can be accessed by current tx
    if ((control->accessed_epoch == reg->batcher.epoch) &&
        (control->access_set == tr->id))
    {
      pthread_mutex_unlock(&reg->batcher.write_lock);
      memcpy(control->write_word, source, reg->align);
      return true;
    }
    pthread_mutex_unlock(&reg->batcher.write_lock);
    return false;
  }

  // if at least one other transaction is in the access set
  if ((control->accessed_epoch == reg->batcher.epoch) &&
      (control->access_set != tr->id))
  {
    pthread_mutex_unlock(&reg->batcher.write_lock);
    return false;
  }

  if (control->accessed_epoch != reg->batcher.epoch)
  {
    control->accessed_epoch = reg->batcher.epoch;
    control->access_set = tr->id;

    control->write_word =
        (char *)reg->segments_write[segment_index] + word_index * reg->align;
    control->read_word =
        (char *)reg->segments_read[segment_index] + word_index * reg->align;

    insert_list(&tr->accessed_words, control, struct Control *);
  }
  control->written = reg->batcher.epoch;
  pthread_mutex_unlock(&reg->batcher.write_lock);
  memcpy(control->write_word, source, reg->align);

  return true;
}

uintptr_t next_free(shared_t shared)
{
  struct Region *reg = (struct Region *)shared;

  // Take the next available index O(1) time if the number of allocations is
  // less than the maximum. Index go from 0 to MAX_SEGMENTS - 1
  if (reg->n_segments < MAX_SEGMENTS)
  {
    return atomic_fetch_add(&reg->n_segments, 1);
  }

  // Search the first index with non allocated segment. O(n) time.
  unsigned int i = 0;
  while (reg->segments_write[i] != NULL)
  {
    ++i;
  }

  return i;
}

void seg_free(shared_t shared, uintptr_t index)
{
  struct Region *reg = (struct Region *)shared;

  free(reg->segments_write[index]);
  free(reg->segments_read[index]);
  free(reg->controls[index]);

  // To indicate a free index
  reg->segments_write[index] = NULL;
  reg->segments_read[index] = NULL;
  reg->controls[index] = NULL;
}

void commit(shared_t shared)
{
  struct Region *reg = (struct Region *)shared;
  struct Control *control = NULL;

  for (size_t i = 0; i < reg->modified_controls.n; ++i)
  {
    control = get_list(&reg->modified_controls, i, struct Control *);

    if (control->written == reg->batcher.epoch)
    {
      memcpy(control->read_word, control->write_word, reg->align);
    }
  }

  (&reg->modified_controls)->n = 0;

  for (size_t i = 0; i < reg->freed_segments.n; ++i)
  {
    uintptr_t index = get_list(&reg->freed_segments, i, uintptr_t);
    seg_free(shared, index);
  }
}