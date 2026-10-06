#ifndef IMPORT_INPUT_H
#define IMPORT_INPUT_H

#include <stdint.h>

typedef struct {
	const char *call;
	int native_result;
} import_input_result;

int import_input_valid(const char *stage, const char *title, const char *directory,
	uint32_t user, uint32_t current_user, uint64_t account_id,
	int (*cancelled)(void *), void *data, import_input_result *result);

#endif
