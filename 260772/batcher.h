#pragma once

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

#define N_THREAD 256

struct Batcher
{
  pthread_cond_t empty; // broadcast signal when remaining is 0 (i.e. empty)
  pthread_mutex_t lock_cond;
  pthread_mutex_t lock;
  pthread_mutex_t read_lock;
  pthread_mutex_t write_lock;
  pthread_mutex_t alloc_lock;
  pthread_mutex_t free_lock;
  atomic_bool keep_waiting;
  atomic_size_t epoch;
  atomic_uint tx_count;
  atomic_uint remaining;
  atomic_uint n_blocked;
};

void init_batcher(struct Batcher *batcher);
void batcher_destroy(struct Batcher *batcher);
size_t get_epoch(struct Batcher *batcher);
void enter(struct Batcher *batcher);
void leave(struct Batcher *batcher, void (*commit)(void *), void *shared);