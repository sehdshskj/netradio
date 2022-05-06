#ifndef MYTBF_H_
#define MTDBF_H_
#include <pthread.h>
#include <stdlib.h>
#include "thr_msgcntl.h"
#define MYTBF_MAX       1024

typedef void mytbf_t;

mytbf_t *mytbf_init(chnid_t chnid, int cps, int burst);

int mytbf_fetchtoken(mytbf_t *, int);

int mytbf_returntoken(mytbf_t *, int);

int mytbf_destroy(mytbf_t *);

#endif