#include "batcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_batcher(struct Batcher *batcher)
{
  pthread_mutex_init(&batcher->lock_cond, NULL);
  pthread_mutex_init(&batcher->lock, NULL);
  pthread_mutex_init(&batcher->read_lock, NULL);
  pthread_mutex_init(&batcher->write_lock, NULL);
  pthread_mutex_init(&batcher->alloc_lock, NULL);
  pthread_mutex_init(&batcher->free_lock, NULL);
  pthread_cond_init(&batcher->empty, NULL);
  batcher->keep_waiting = true;
  batcher->epoch = 1;    // epoch starts at 1, because of written
  batcher->tx_count = 1; // Id starts at 1
  batcher->remaining = 0;
  batcher->n_blocked = 0;
}

void batcher_destroy(struct Batcher *batcher)
{
  pthread_mutex_destroy(&batcher->lock_cond);
  pthread_mutex_destroy(&batcher->lock);
  pthread_mutex_destroy(&batcher->alloc_lock);
  pthread_mutex_destroy(&batcher->free_lock);
  pthread_mutex_destroy(&batcher->read_lock);
  pthread_mutex_destroy(&batcher->write_lock);
  pthread_cond_destroy(&batcher->empty);
}

// size_t get_epoch(struct Batcher *batcher) { return batcher->epoch; }

// void enter(struct Batcher *batcher) {
//   printf("%ld wait lock\n", pthread_self());
//   pthread_mutex_lock(&batcher->lock_cond);
//   printf("%ld locked, remaining = %ld\n", pthread_self(),
//          atomic_load(&batcher->remaining));

//   if (atomic_load(&batcher->remaining) == 0) {
//     atomic_fetch_add(&batcher->remaining, 1);
//     pthread_mutex_unlock(&batcher->lock_cond);
//     return;
//   }

//   atomic_fetch_add(&batcher->n_blocked, 1);

//   printf("%ld cond wait\n", pthread_self());
//   while (atomic_load(&batcher->n_blocked) > 0) {

//     pthread_cond_wait(&batcher->empty, &batcher->lock_cond);
//   }

//   pthread_mutex_unlock(&batcher->lock_cond);
//   printf("%ld ------unlocked\n", pthread_self());
// }

// void leave(struct Batcher *batcher, void (*commit)(void *), void *shared) {

//   pthread_mutex_lock(&batcher->lock_cond);
//   atomic_fetch_sub(&batcher->remaining, 1);

//   printf("%ld before remaining\n", pthread_self());
//   if (atomic_load(&batcher->remaining) == 0) {

//     printf("%ld before commit\n", pthread_self());
//     commit(shared);
//     printf("%ld after commit\n", pthread_self());

//     atomic_fetch_add(&batcher->epoch, 1);
//     atomic_store(&batcher->remaining, batcher->n_blocked);
//     atomic_store(&batcher->n_blocked, 0);
//     pthread_cond_broadcast(&batcher->empty);

//     printf("%ld blocked: %ld, remaining \n", pthread_self(),
//            batcher->n_blocked);
//   }

//   pthread_mutex_unlock(&batcher->lock_cond);
// }

void enter(struct Batcher *batcher)
{
  pthread_mutex_lock(&batcher->lock_cond);

  if (batcher->remaining > 0)
  {
    batcher->keep_waiting = true;

    while (batcher->keep_waiting)
    {
      pthread_cond_wait(&batcher->empty, &batcher->lock_cond);
    }
  }
  // printf("%d\n", batcher->remaining);
  atomic_fetch_add(&batcher->remaining, 1);
  pthread_mutex_unlock(&batcher->lock_cond);
}

void leave(struct Batcher *batcher, void (*commit)(void *), void *shared)
{
  pthread_mutex_lock(&batcher->lock_cond);
  if (atomic_fetch_sub(&batcher->remaining, 1) == 1)
  {
    // Callback function to commit and free
    commit(shared);

    // Increment epoch after commit
    ++batcher->epoch;
    batcher->keep_waiting = false;
    pthread_cond_broadcast(&batcher->empty);
  }
  pthread_mutex_unlock(&batcher->lock_cond);
}
