#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define insert_list(listptr, item, type)                   \
  (                                                        \
      {                                                    \
        if ((listptr)->n >= (listptr)->nmemb)              \
        {                                                  \
          realloc_list(listptr, sizeof(type));             \
        }                                                  \
        ((type *)((listptr)->array))[(listptr)->n] = item; \
        ++(listptr)->n;                                    \
      })

#define get_list(listptr, index, type) ((type *)((listptr)->array))[(index)]

struct List
{
  void *array;
  size_t nmemb;
  size_t n;
};

void init_list(struct List *list, size_t size_object);
void destroy_list(struct List *list);
void realloc_list(struct List *list, size_t size_object);
