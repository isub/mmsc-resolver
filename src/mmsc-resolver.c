#include "mmsc/mmsc_cfg.h"

#include "mmsc-resolver.h"
#include "resolver-operations.h"

static void *mms_resolvermodule_init (char *settings)
{
	return (void*) resolver_init (settings);
}

static int mms_resolvermodule_fini (void *module_data)
{
	return resolver_fini (module_data);
}

static Octstr *mms_resolve (
	Octstr * phonenum,
	char *src_int,
	char *src_id,
	void *module_data,
	void *settings_p,
	void *proxyrelays_p)
{
	/* Most custom implementations of this library will probably just ignore the two last arguments,
	 * but this one needs them
	 */

	List *proxyrelays = (List *) proxyrelays_p;
	int j, m;
	const char *pszPhoneNum;
	Octstr *postrOwner;
	struct SOwnerData *psoOwnerData;

	pszPhoneNum = octstr_get_cstr (phonenum);
	if (NULL == pszPhoneNum) {
		return NULL;
	}
	psoOwnerData = resolver_resolve (pszPhoneNum, module_data);
	if (NULL == psoOwnerData) {
		return NULL;
	}
	postrOwner = octstr_create (psoOwnerData->m_mcOwner);

	if (proxyrelays && gwlist_len (proxyrelays) > 0) {
		for (j = 0, m = gwlist_len (proxyrelays); j < m; j++) {
			MmsProxyRelay *mp = (MmsProxyRelay *) gwlist_get (proxyrelays, j);
			if (0 == octstr_compare (mp->name, postrOwner)) {
				return (Octstr *) octstr_duplicate (mp->host);
			}
		}
	}

	return 0;
}
