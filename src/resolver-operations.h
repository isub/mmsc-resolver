#ifdef __cplusplus
extern "C" {
#endif

/* ������������� ���������� */
void * resolver_init (const char *p_pszConfFile);

/* ��������������� ���������� */
int resolver_fini (void *p_pPtr);

struct SOwnerData {
	unsigned int m_uiFromGHIJ;
	unsigned int m_uiToGHIJ;
	unsigned int m_uiCapacity;
	char m_mcOwner[128];
	unsigned int m_uiRegionCode;
	unsigned int m_uiMNC;
};

/* ����� ������ �������� � ���� */
struct SOwnerData * resolver_resolve (
	const char *p_pszPhoneNum,
	const void *p_pModuleData);

#ifdef __cplusplus
}
#endif
