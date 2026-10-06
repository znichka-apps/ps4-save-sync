#include <stddef.h>
#include "import_input.h"

int import_input_valid(const char *stage, const char *title, const char *directory,
	uint32_t user, uint32_t current_user, uint64_t account_id,
	int (*cancelled)(void *), void *data, import_input_result *result)
{
	if (!result) return 0;
	result->call="none";
	result->native_result=0;
	if (!stage) result->call="stage";
	else if (!title) result->call="title";
	else if (!directory) result->call="directory";
	else if (!cancelled) result->call="cancel_callback";
	else if (!user || user!=current_user) {
		result->call="user_id";
		result->native_result=(int)user;
	}
	else if (!account_id) result->call="account_id";
	else {
		int cancel_result=cancelled(data);
		if (!cancel_result) return 1;
		result->call="cancelled";
		result->native_result=cancel_result;
	}
	return 0;
}
