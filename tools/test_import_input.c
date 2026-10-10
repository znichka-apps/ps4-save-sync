#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "import_input.h"

static int callback_calls;
static int cancel(void *data)
{
	callback_calls++;
	return *(int *)data;
}

int main(void)
{
	import_input_result result={0};
	int cancelled=0;
	/* Recovery has a valid staged path and user even when account lookup failed. */
	assert(!import_input_valid("stage","CUSA12345","SAVE",42,42,0,cancel,&cancelled,&result));
	assert(!strcmp(result.call,"account_id") && result.native_result==0 && callback_calls==0);
	assert(import_input_valid("stage","CUSA12345","SAVE",42,42,123,cancel,&cancelled,&result));
	assert(callback_calls==1);
	cancelled=1;
	assert(!import_input_valid("stage","CUSA12345","SAVE",42,42,123,cancel,&cancelled,&result));
	assert(!strcmp(result.call,"cancelled") && result.native_result==1);
	assert(!import_input_valid("stage","CUSA12345","SAVE",42,43,123,cancel,&cancelled,&result));
	assert(!strcmp(result.call,"user_id") && result.native_result==42);
	puts("Staged import input diagnostics passed.");
	return 0;
}
