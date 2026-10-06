#ifndef ACCOUNT_IDENTITY_H
#define ACCOUNT_IDENTITY_H

#include <stdint.h>
#include <limits.h>

/* UserService returns ORBIS_OK (0) and writes an NP account ID on success.
   Never retain an old identity after an SDK error or an unassigned ID. */
static inline int account_identity_refresh(uint32_t user, uint64_t *account_id,
	int32_t (*lookup)(int32_t, uint64_t *), int32_t *native_status)
{
	if (native_status) *native_status=-1;
	if (account_id) *account_id=0;
	if (!account_id || !lookup || !user || user>INT32_MAX) return 0;
	uint64_t candidate=0;
	int32_t status=lookup((int32_t)user,&candidate);
	if (native_status) *native_status=status;
	if (status!=0 || !candidate) return 0;
	*account_id=candidate;
	return 1;
}

#endif
