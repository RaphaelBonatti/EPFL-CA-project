#pragma once

#define N_THREAD 256

struct Batcher {
  unsigned int counter; // 0 to 2^{32} - 1, 4B
  unsigned int remaining;
  unsigned int blocked[N_THREAD];
  unsigned int n_blocked;
};

void init_batcher(struct Batcher *batcher);
unsigned int get_epoch(struct Batcher *batcher);
void enter(struct Batcher *batcher);
void leave(struct Batcher *batcher);