
#include "simple_list.h"

#define INIT_NMEMB 128

void init_list(struct List *list, size_t size_object)
{
  list->array = calloc(INIT_NMEMB, sizeof(size_object));
  list->nmemb = INIT_NMEMB;
  list->n = 0;
}

void destroy_list(struct List *list) { free(list->array); }

void realloc_list(struct List *list, size_t size_object)
{
  size_t new_size = list->nmemb * 2;
  size_t *new_array = realloc(list->array, new_size * size_object);

  list->array = new_array;
  list->nmemb = new_size;
}