#ifndef LMC_H
#define LMC_H

#include <stdio.h>

typedef unsigned short lmc_int;

struct lmc_state;

struct lmc_state* lmc_alloc(lmc_int memory_size, void* (*allocate)(size_t));

void lmc_destroy(struct lmc_state* s, void (*release)(void*));

int lmc_execute_instruction(struct lmc_state* s, FILE* input, FILE* output);

void lmc_debugprint(struct lmc_state* s, FILE* stream);

int lmc_load_file(struct lmc_state* s, const char* filename, FILE* stream);

#endif /* LMC_H */
