#pragma once

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

#define N_THREAD 256

struct Batcher {
  pthread_cond_t empty; // broadcast signal when remaining is 0 (i.e. empty)
  pthread_mutex_t lock_cond;
  pthread_mutex_t lock;
  atomic_uint epoch; // 0 to 2^{32} - 1, 4B
  atomic_uint tx_count;
  atomic_uint remaining;
  atomic_uint n_blocked;
};

void init_batcher(struct Batcher *batcher);
unsigned int get_epoch(struct Batcher *batcher);
void enter(struct Batcher *batcher);
void leave(struct Batcher *batcher);