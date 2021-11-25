#include "batcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_batcher(struct Batcher *batcher) {
  pthread_mutex_init(&batcher->lock_cond, NULL);
  pthread_mutex_init(&batcher->lock, NULL);
  pthread_cond_init(&batcher->empty, NULL);
  batcher->epoch = 0;
  batcher->tx_count = 1; // Id starts at 1
  batcher->remaining = 0;
  batcher->n_blocked = 0;
}

unsigned int get_epoch(struct Batcher *batcher) { return batcher->epoch; }

void enter(struct Batcher *batcher) {
  unsigned int expected = 0;

  if (!atomic_compare_exchange_strong(&batcher->remaining, &expected, 1)) {
    // Wait for the blocked threads to enter, in case the last thread just
    // leaved.
    pthread_mutex_lock(&batcher->lock);
    atomic_fetch_add(&batcher->n_blocked, 1);
    pthread_mutex_unlock(&batcher->lock);

    // Wait until last thread exits
    pthread_cond_wait(&batcher->empty, &batcher->lock_cond);
  }
}

void leave(struct Batcher *batcher) {
  unsigned int expected = 0;

  atomic_fetch_sub(&batcher->remaining, 1);

  if (atomic_compare_exchange_strong(&batcher->remaining, &expected, 1)) {
    atomic_fetch_add(&batcher->epoch, 1); // necessary?

    // Lock to avoid situation where "new" threads are blocked just after
    // setting remaining to n_blocked, those new threads would be unblocked but
    // not taken into account.
    pthread_mutex_lock(&batcher->lock);
    atomic_store(&batcher->remaining, batcher->n_blocked); // necessary?

    // Will wake up in tm_end
    // Wake up every thread waiting in cond_wait

    // Will unlock after the the swap for written words in function tm_end
    // pthread_mutex_unlock(&batcher->lock);
  }
}
