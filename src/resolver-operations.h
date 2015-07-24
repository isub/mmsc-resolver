#ifndef _RESOLVER_OPERATIONS_H_
#define _RESOLVER_OPERATIONS_H_

struct SOwnerData {
	unsigned int m_uiFromGHIJ;
	unsigned int m_uiToGHIJ;
	unsigned int m_uiCapacity;
	char m_mcOwner[128];
	unsigned int m_uiRegionCode;
	unsigned int m_uiMNC;
};

#ifdef __cplusplus
extern "C" {
#endif

/* инициализация резольвера */
void * resolver_init (const char *p_pszConfFile);

/* деинициализация резольвера */
int resolver_fini (void *p_pPtr);

/* поиск номера телефона в кэше */
struct SOwnerData * resolver_resolve (
	const char *p_pszPhoneNum,
	const void *p_pModuleData);

#ifdef __cplusplus
}
#endif

#endif /* _RESOLVER_OPERATIONS_H_ */