#include "resolver-operations.h"
#include "resolver-auxiliary.h"

#ifdef _WIN32
#else
#	include <errno.h>
#	include <string.h>
#	include <stdlib.h>
#	include <sys/time.h>
#endif

/* ��������� ������ ���������� ���� */
#ifdef _WIN32
DWORD WINAPI resolver_update_cache (LPVOID p_pParam);
#else
static void * resolver_update_cache (void *p_pParam);
#endif

void * resolver_init (const char *p_pszConfFile)
{
	ENTER_ROUT;

	int iFnRes;
	SResolverData *psoResData = NULL;

	do {
		psoResData = new SResolverData;

		/* ������������� ���������� */
		psoResData->m_pmapResolverCache = NULL;
		psoResData->m_iContinueUpdate = 1;

		/* ��������� ������������ ������ */
		iFnRes = resolver_load_conf (p_pszConfFile, psoResData->m_soConf);
		if (iFnRes) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}

		std::string strLogFileMask;
#ifdef _DEBUG
		g_coLog.Init (psoResData->m_soConf.m_strLogFileMask.c_str ());
#endif

#ifdef _WIN32
		psoResData->m_hSemTimer = CreateSemaphore (NULL, 0, 1, NULL);
		if (NULL == psoResData->m_hSem) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}
#else
		/* �������������, ��� ������� �� ��������������� */
		psoResData->m_iSemInitialized = 0;
		psoResData->m_iSemTimerInitialized = 0;
		/* ������� ������� ��� �������� ������ ���������� ���� */
		iFnRes = sem_init (&(psoResData->m_tSemTimer), 0, 1);
		if (iFnRes) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}
		/* �������� ������� */
		sem_wait (&(psoResData->m_tSemTimer));
		psoResData->m_iSemTimerInitialized = 1;
#endif

#ifdef _WIN32
		/* ������ ������ ���������� ���� */
		psoResData->m_hThreadUpdateCache = CreateThread (
			NULL,
			0,
			resolver_update_cache,
			psoResData,
			0,
			NULL);
		if (NULL == psoResData->m_hThreadUpdateCache) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}
#else
		/* ������ ������ ���������� ���� */
		iFnRes = pthread_create (
			&(psoResData->m_tThreadUpdateCache),
			NULL,
			resolver_update_cache,
			psoResData);
		if (iFnRes) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}
#endif

		/* ��������� ������ � ���������� ������� */
		iFnRes = resolver_load_data (psoResData->m_soConf, NULL);
		if (iFnRes) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}

		/* ��������� ��� */
		psoResData->m_pmapResolverCache = new std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >;
		iFnRes = resolver_cache (psoResData->m_soConf, *(psoResData->m_pmapResolverCache));
		if (iFnRes) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}

#ifdef _WIN32
		psoResData->m_hSem = CreateSemaphore (NULL, 256, 256, NULL);
		if (NULL == psoResData->m_hSem) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}
#else
		iFnRes = sem_init (&(psoResData->m_tSem), 0, 256);
		if (iFnRes) {
			resolver_fini (psoResData);
			delete psoResData;
			psoResData = NULL;
			break;
		}
		psoResData->m_iSemInitialized = 1;
#endif
	} while (0);

	LEAVE_ROUT (psoResData);

	return psoResData;
}

int resolver_fini (void *p_pPtr)
{
	ENTER_ROUT;

	int iRetVal = 0;
	SResolverData *psoResData = (SResolverData *) p_pPtr;

	/* �������� ������ ���������� ���� � ������������� ���������� ������ */
	psoResData->m_iContinueUpdate = 0;

	/* ���������� ������������ �������� ����� �������� */
#ifdef _WIN32
	DWORD dwFnRes;
	if (NULL != psoResData->m_hSem) {
		for (int i = 0; i < 256; ++i) {
			dwFnRes = WaitForSingleObject (psoResData->m_hSem, INFINITE);
			if (WAIT_FAILED == dwFnRes) {
				iRetVal = GetLastError ();
			} else {
				iRetVal = dwFnRes;
			}
			/* ���� ��������� ������ ��������� ���� */
			if (iRetVal) {
				break;
			}
		}
	}
#else
	int iFnRes;
	if (psoResData->m_iSemInitialized) {
		for (int i = 0; i < 256; ++i) {
			iFnRes = sem_wait (&(psoResData->m_tSem));
			/* ���� ��������� ������ ��������� ���� */
			if (iFnRes) {
				iRetVal = errno;
				break;
			}
		}
	}
#endif

	/* ��������� �������� ������ ���������� ���� */
#ifdef _WIN32
	ReleaseSemaphore (psoResData->m_hSemTimer, 1, NULL);
#else
	sem_post (&(psoResData->m_tSem));
#endif

	/* ���������� ���������� ������ ������ ���������� ���� */
#ifdef _WIN32
	WaitForSingleObject (psoResData->m_hThreadUpdateCache, INFINITE);
	CloseHandle (psoResData->m_hThreadUpdateCache);
	psoResData->m_hThreadUpdateCache = NULL;
#else
	pthread_join (psoResData->m_tThreadUpdateCache, NULL);
#endif

	/* ������� ��� */
	if (psoResData->m_pmapResolverCache) {
		psoResData->m_pmapResolverCache->clear ();
		delete psoResData->m_pmapResolverCache;
		psoResData->m_pmapResolverCache = NULL;
	}

	/* ���������� ������� */
#ifdef _WIN32
	if (NULL != psoResData->m_hSem) {
		if (! CloseHandle (psoResData->m_hSem)) {
			iRetVal = GetLastError ();
		}
		psoResData->m_hSem = NULL;
	}
#else
	if (psoResData->m_iSemInitialized) {
		psoResData->m_iSemInitialized = 0;
		iFnRes = sem_destroy (&(psoResData->m_tSem));
		if (iFnRes) {
			iRetVal = errno;
		}
	}
#endif

	delete psoResData;

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

struct SOwnerData * resolver_resolve (
	const char *p_pszPhoneNum,
	const void *p_pModuleData)
{
	ENTER_ROUT;

	SOwnerData * psoRetVal = NULL;

	do {
		SResolverData *psoResData = (SResolverData *) p_pModuleData;
		if (10 > strlen (p_pszPhoneNum)) {
			break;
		}

		unsigned int uiABC;
		unsigned int uiDEF;
		unsigned int uiGHIJ;
		char mcTmp[5];
		char *pszEndPtr;

		/* �������� ABC */
		memcpy (mcTmp, &(p_pszPhoneNum[2]), 3);
		mcTmp[3] = '\0';
		uiABC = strtoul (mcTmp, &pszEndPtr, 10);
		if (&(mcTmp[3]) != pszEndPtr) {
			break;
		}

		/* �������� DEF */
		memcpy (mcTmp, &(p_pszPhoneNum[5]), 3);
		mcTmp[3] = '\0';
		uiDEF = strtoul (mcTmp, &pszEndPtr, 10);
		if (&(mcTmp[3]) != pszEndPtr) {
			break;
		}

		/* �������� GHIJ */
		memcpy (mcTmp, &(p_pszPhoneNum[8]), 4);
		mcTmp[4] = '\0';
		uiGHIJ = strtoul (mcTmp, &pszEndPtr, 10);
		if (&(mcTmp[4]) != pszEndPtr) {
			break;
		}

		std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >::iterator iterMapABC;

#ifdef _WIN32
		DWORD dwFnRes;
		dwFnRes = WaitForSingleObject (psoResData->m_hSem, INFINITE);
		if (0 != dwFnRes) {
			break;
		}
#else
		int iFnRes;
		iFnRes = sem_wait (&(psoResData->m_tSem));
		if (iFnRes) {
			break;
		}
#endif

		do {
			/* ���� ABC */
			iterMapABC = psoResData->m_pmapResolverCache->find (uiABC);
			if (iterMapABC == psoResData->m_pmapResolverCache->end ()) {
				break;
			}

			std::map<unsigned int,std::multiset<SOwnerData> >::iterator iterMapDEF;
			std::multiset<SOwnerData>::iterator iterResData;

			/* ���� DEF */
			iterMapDEF = iterMapABC->second.find (uiDEF);
			if (iterMapDEF == iterMapABC->second.end ()) {
				break;;
			}

			/* ���� GHIJ */
			iterResData = iterMapDEF->second.begin ();
			for (; iterResData != iterMapDEF->second.end (); ++iterResData) {
				if (uiGHIJ >= iterResData->m_uiFromGHIJ && uiGHIJ <= iterResData->m_uiToGHIJ) {
					psoRetVal = (struct SOwnerData *) &(*iterResData);
				}
			}
		} while (0);

#ifdef _WIN32
		ReleaseSemaphore (psoResData->m_hSem, 1, NULL);
#else
		sem_post (&(psoResData->m_tSem));
#endif
	} while (0);

	LEAVE_ROUT (psoRetVal);

	return psoRetVal;
}

#ifdef _WIN32
DWORD WINAPI resolver_update_cache (LPVOID p_pParam)
#else
static void * resolver_update_cache (void *p_pParam)
#endif
{
	ENTER_ROUT;

#ifdef _WIN32
	DWORD dwRetVal = 0;
	DWORD dwFnRes;
#else
	timeval tCurrentTime;
	timespec tSemTime;
#endif
	SResolverData *psoResData = (SResolverData *) p_pParam;
	int iDataUpdated;
	int iFnRes;

	do {
		while (psoResData->m_iContinueUpdate) {
#ifdef _WIN32
			/* ���� ��������� ����� */
			dwFnRes = WaitForSingleObject (psoResData->m_hSemTimer, psoResData->m_soConf.m_uiUpdateInterval * 1000);
			/* ���� �������� ����������� ������� */
			if (WAIT_FAILED == dwFnRes) {
				dwRetVal = GetLastError ();
				/* ��������� ���� */
				break;
			}
#else
			/* �������� ������� ����� */
			iFnRes = gettimeofday (&tCurrentTime, NULL);
			if (iFnRes) {
				break;
			}
			/* ��������� ����� �������� �������� */
			tSemTime.tv_sec = tCurrentTime.tv_sec + psoResData->m_soConf.m_uiUpdateInterval;
			tSemTime.tv_nsec = tCurrentTime.tv_usec * 1000;
			/* ������� ������������ �������� ��� ��������� �������� */
			iFnRes = sem_timedwait (&(psoResData->m_tSemTimer), &tSemTime);
			/* ���� ��������� ������ */
			if (iFnRes) {
				iFnRes = errno;
				/* �� ������ ��� ������� ������ �������, ��������� ������ ������� ���������� */
				if (ETIMEDOUT != iFnRes) {
					/* ��������� ���� */
					break;
				}
			}
#endif
			/* ���� ������� ���� ����������� ������ */
			if (0 == psoResData->m_iContinueUpdate) {
				/* ��������� ���� */
				break;
			}

			/* ��������� ������ � ���������� ������� */
			iFnRes = resolver_load_data (psoResData->m_soConf, &iDataUpdated);
			if (iFnRes) {
				continue;
			}
			/* ���� ������ ���������� */
			if (iDataUpdated) {
				resolver_recreate_cache (psoResData);
			}
		}
	} while (0);

	LEAVE_ROUT (dwRetVal);

#ifdef _WIN32
	ExitThread (dwRetVal);
#else
	pthread_exit (0);
#endif
}
