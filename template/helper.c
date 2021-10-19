#include <string.h>

#include "helper.h"

void read_word_at_index(struct Region *region, void *target, uint32_t index,
                        bool isReadable) {
  if (region->control[index].whichReadable == isReadable) {
    // TODO: test memcpy(target, region->word + region->align * index), 1);
    memcpy(target, &(region->word_copy[index]), region->align);
  } else {
    memcpy(target, &(region->word[index]), region->align);
  }
}

void read_word(struct Region *region, tx_t tx, void *target, uint32_t index) {
  if (tx == read_only_tx) {
    // TODO: read
    read_word_at_index(region, target, index, true);
    return success_tx;
  }

  // the word has been written in the current epoch
  if (region->control[index].isWritten) {
    // no transaction in access set
    if (!region->control[index].access_set) {
      read_word_at_index(region, target, index, false);
      return success_tx;
    }

    return abort_tx;
  }

  read_word_at_index(region, target, index, true);
  // add the transaction into the \access set" (if not already in);
  // what is a transaction, an id?
  region->control[index].access_set = 1;
  return success_tx;
}