#include "batcher.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_batcher(struct Batcher *batcher) {
  batcher->counter = 0;
  batcher->remaining = 0;
  memset(batcher->blocked, 0, N_THREAD);
  batcher->n_blocked = 0;
}

unsigned int get_epoch(struct Batcher *batcher) { return batcher->counter; }

void enter(struct Batcher *batcher) {
  if (batcher->remaining == 0) {
    batcher->remaining++;
    return;
  }

  pthread_t tid = pthread_self();

  batcher->blocked[tid] = 1;
  batcher->n_blocked++;

  // TODO: wait until woken up, what is the best way: while loop vs
  // pthread_cond_signal
  while (batcher->blocked[tid] == 1) {
  }
}

void leave(struct Batcher *batcher) {
  batcher->remaining--;

  if (batcher->remaining == 0) {
    batcher->counter++;
    batcher->remaining = batcher->n_blocked;
    // TODO: wake up every thread in blocked
    memset(batcher->blocked, 0, N_THREAD);
    // TODO: empty list blocked
    batcher->n_blocked = 0;
  }
}
