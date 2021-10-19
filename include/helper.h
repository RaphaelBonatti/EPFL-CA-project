#pragma once

#include "tm.h"
#include <stdbool.h>

static const tx_t read_only_tx = UINTPTR_MAX - 10;
static const tx_t read_write_tx = UINTPTR_MAX - 11;
static const tx_t success_tx = 0;
static const tx_t abort_tx = 1;

struct Control {
  bool whichReadable; // 0 if word is readable, 1 if word is writeable
  bool access_set;
  bool isWritten;
};

struct Region {
  void *word;
  void *word_copy;
  struct Control *control;
  size_t size;  // Size of the shared memory region (in bytes)
  size_t align; // Claimed alignment of the shared memory region (in bytes)
};