#ifndef _MMSC_RESOLVER_H_
#define _MMSC_RESOLVER_H_

#include "mmsc/mms_resolve.h"
#include "mmlib/mms_util.h"
#include "gwlib/gwlib.h"

static void *mms_resolvermodule_init (char *settings);

static int mms_resolvermodule_fini (void *module_data);

static Octstr *mms_resolve (
	Octstr * phonenum,
	char *src_int,
	char *src_id,
	void *module_data,
	void *settings_p,
	void *proxyrelays_p);

MmsResolverFuncStruct mms_resolvefuncs = {
     mms_resolvermodule_init,
     mms_resolve,
     mms_resolvermodule_fini
};


#endif /* _MMSC_RESOLVER_H_ */