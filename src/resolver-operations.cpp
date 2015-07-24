#include "resolver-operations.h"
#include "resolver-auxiliary.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#ifdef _WIN32
#	include <curl.h>
#else
#	include <curl/curl.h>
#	include <openssl/crypto.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>

/* процедура потока обновления кэша */
static void * resolver_update_cache (void *p_pParam);

void * resolver_init (const char *p_pszConfFile)
{
	int iFnRes;
	SResolverData *psoResData = NULL;

	do {
		psoResData = new SResolverData;

		/* инициализация параметров */
		psoResData->m_tThreadUpdateCache = (pthread_t) -1;
		psoResData->m_pmapResolverCache = NULL;
		psoResData->m_iContinueUpdate = 1;
		psoResData->m_iSemDataInitialized = 0;
		psoResData->m_iMutexInitialized = 0;

		/* загружаем конфигурацию модуля */
		iFnRes = resolver_apply_settings (p_pszConfFile, psoResData->m_soConf);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}

		/* инициализация лог-файла */
		iFnRes = psoResData->m_coLog.Init (psoResData->m_soConf.m_strLogFileMask.c_str ());
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}

		/* инициализируем мьютекс для ожидания потока обновления кэша */
		iFnRes = pthread_mutex_init (&(psoResData->m_tThreadMutex), NULL);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		psoResData->m_iMutexInitialized = 1;
		/* запираем мьютекс потока обновления */
		pthread_mutex_trylock (&(psoResData->m_tThreadMutex));

		/* создаем семафор кэша */
		psoResData->m_ptSemData = sem_open(SEM_NAME, O_CREAT, S_IRWXU, 256);
		/* если семафор уже создан открыаем его */
		if (SEM_FAILED == psoResData->m_ptSemData && EACCES == errno) {
			psoResData->m_ptSemData = sem_open (SEM_NAME, 0, S_IRWXU, 256);
		}
		if (SEM_FAILED == psoResData->m_ptSemData) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		psoResData->m_iSemDataInitialized = 1;

		/* первоначальная загрузка данных */
		/* выделяем память под кэш */
		psoResData->m_pmapResolverCache = new std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >;
		/* формируем кэш */
		iFnRes = resolver_cache (psoResData, *(psoResData->m_pmapResolverCache));
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}

		/* запуск потока обновления кэша */
		iFnRes = pthread_create (
			&(psoResData->m_tThreadUpdateCache),
			NULL,
			resolver_update_cache,
			psoResData);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		} 
	} while (0);

	if (psoResData) {
		psoResData->m_coLog.WriteLog ("%s: mms resovler module is initialized successfully", __FUNCTION__);
	}

	return psoResData;
}

int resolver_fini (void *p_pPtr)
{
	int iRetVal = 0;
	SResolverData *psoResData = (SResolverData *) p_pPtr;

	/* сообщаем потоку обновления кэша о необходимости завершения работы */
	psoResData->m_iContinueUpdate = 0;

	/* дожидаемся освобождения семафора данных всеми потоками */
	int iFnRes;
	if (psoResData->m_iSemDataInitialized) {
		for (int i = 0; i < 256; ++i) {
			iFnRes = sem_wait (psoResData->m_ptSemData);
			/* если произошла ошибка завершаем цикл */
			if (iFnRes) {
				iRetVal = errno;
				break;
			}
		}
	}

	/* отпускаем семафор ожидания потока */
	if (psoResData->m_iMutexInitialized) {
		pthread_mutex_unlock (&(psoResData->m_tThreadMutex));
	}

	/* дожидаемся завершения работы потока обновления кэша */
	if ((pthread_t) -1 != psoResData->m_tThreadUpdateCache) {
		pthread_join (psoResData->m_tThreadUpdateCache, NULL);
	}

	/* уничтожаем семафор потока обнавления */
	if (psoResData->m_iMutexInitialized) {
		iFnRes = pthread_mutex_destroy (&(psoResData->m_tThreadMutex));
		if (iFnRes) {
			iRetVal = errno;
		}
		psoResData->m_iMutexInitialized = 0;
	}

	/* сбрасываем все данные на диск */
	psoResData->m_coLog.Flush ();

	/* очищаем кэш */
	if (psoResData->m_pmapResolverCache) {
		psoResData->m_pmapResolverCache->clear ();
		delete psoResData->m_pmapResolverCache;
		psoResData->m_pmapResolverCache = NULL;
	}

	/* уничтожаем семафор данных */
	if (psoResData->m_iSemDataInitialized) {
		psoResData->m_iSemDataInitialized = 0;
		iFnRes = sem_destroy (psoResData->m_ptSemData);
		if (iFnRes) {
			iRetVal = errno;
		}
	}

	delete psoResData;

	return iRetVal;
}

struct SOwnerData * resolver_resolve (
	const char *p_pszPhoneNum,
	const void *p_pModuleData)
{
	SOwnerData * psoRetVal = NULL;
	SResolverData *psoResData = (SResolverData *) p_pModuleData;

	do {
		if (10 > strlen (p_pszPhoneNum)) {
			break;
		}

		unsigned int uiABC;
		unsigned int uiDEF;
		unsigned int uiGHIJ;
		char mcTmp[5];
		char *pszEndPtr;

		/* получаем ABC */
		memcpy (mcTmp, &(p_pszPhoneNum[2]), 3);
		mcTmp[3] = '\0';
		uiABC = strtoul (mcTmp, &pszEndPtr, 10);
		if (&(mcTmp[3]) != pszEndPtr) {
			break;
		}

		/* получаем DEF */
		memcpy (mcTmp, &(p_pszPhoneNum[5]), 3);
		mcTmp[3] = '\0';
		uiDEF = strtoul (mcTmp, &pszEndPtr, 10);
		if (&(mcTmp[3]) != pszEndPtr) {
			break;
		}

		/* получаем GHIJ */
		memcpy (mcTmp, &(p_pszPhoneNum[8]), 4);
		mcTmp[4] = '\0';
		uiGHIJ = strtoul (mcTmp, &pszEndPtr, 10);
		if (&(mcTmp[4]) != pszEndPtr) {
			break;
		}

		std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >::iterator iterMapABC;

		int iFnRes;
		iFnRes = sem_wait (psoResData->m_ptSemData);
		if (iFnRes) {
			break;
		}

		do {
			/* ищем ABC */
			iterMapABC = psoResData->m_pmapResolverCache->find (uiABC);
			if (iterMapABC == psoResData->m_pmapResolverCache->end ()) {
				break;
			}

			std::map<unsigned int,std::multiset<SOwnerData> >::iterator iterMapDEF;
			std::multiset<SOwnerData>::iterator iterResData;

			/* ищем DEF */
			iterMapDEF = iterMapABC->second.find (uiDEF);
			if (iterMapDEF == iterMapABC->second.end ()) {
				break;;
			}

			/* ищем GHIJ */
			iterResData = iterMapDEF->second.begin ();
			for (; iterResData != iterMapDEF->second.end (); ++iterResData) {
				if (uiGHIJ >= iterResData->m_uiFromGHIJ && uiGHIJ <= iterResData->m_uiToGHIJ) {
					psoRetVal = (struct SOwnerData *) &(*iterResData);
					break;
				}
			}
		} while (0);

		sem_post (psoResData->m_ptSemData);
	} while (0);

	if (psoRetVal) {
		psoResData->m_coLog.WriteLog ("'%s': owner: '%s'; region: '%u'; MNC: '%u';", p_pszPhoneNum, psoRetVal->m_mcOwner, psoRetVal->m_uiRegionCode, psoRetVal->m_uiMNC);
	} else {
		psoResData->m_coLog.WriteLog ("'%s;: owner not found", p_pszPhoneNum);
	}

	return psoRetVal;
}

static void * resolver_update_cache (void *p_pParam)
{
	timeval tCurrentTime;
	timespec tMutexTime;
	SResolverData *psoResData = (SResolverData *) p_pParam;

	int iDataUpdated;
	int iFnRes;
	int iCURLInitialized = 0;
	struct stat soStat;
	time_t tLastTime = 0;

	do {
		while (psoResData->m_iContinueUpdate) {
			/* получаем текущее время */
			iFnRes = gettimeofday (&tCurrentTime, NULL);
			if (iFnRes) {
				break;
			}
			/* вычисляем время таймаута семафора */
			tMutexTime.tv_sec = tCurrentTime.tv_sec + psoResData->m_soConf.m_uiUpdateInterval;
			tMutexTime.tv_nsec = tCurrentTime.tv_usec * 1000;
			/* ожидаем освобождения семафора или истечения таймаута */
			iFnRes = pthread_mutex_timedlock (&(psoResData->m_tThreadMutex), &tMutexTime);
			/* если произошла ошибка */
			if (iFnRes) {
				/* из ошибок нас устроит только таймаут, остальные ошибки считаем фатальными */
				if (ETIMEDOUT != iFnRes) {
					/* завершаем цикл */
					break;
				}
			}

			/* если сброшен флаг продолжения работы */
			if (0 == psoResData->m_iContinueUpdate) {
				/* завершаем цикл */
				break;
			} else {
			}

			/* проверяем не обновились ли файлы данных */
			iDataUpdated = 0;
			/* проверяем время изменения файла плана нумерации */
			std::string strFileName;
			if (psoResData->m_soConf.m_strLocalDir.length()) {
				strFileName = psoResData->m_soConf.m_strLocalDir;
				if (strFileName[strFileName.length() - 1] != '/')
					strFileName += '/';
			}
			strFileName += psoResData->m_soConf.m_strLocalNumPlanFile;
			iFnRes = stat(strFileName.c_str(), &soStat);
			if (iFnRes) {
				iFnRes = errno;
				if (0 == iFnRes)
					iFnRes = -1;
				UTL_LOG_E (psoResData->m_coLog, "can not retrieve file '%s' info; error code: '%d'", strFileName.c_str (), iFnRes);
				continue;
			}
			if (tLastTime < soStat.st_ctime) {
				++iDataUpdated;
				tLastTime = soStat.st_ctime;
			}
			/* преверяем время изменения файла портированных номеров */
			if (psoResData->m_soConf.m_strLocalDir.length()) {
				strFileName = psoResData->m_soConf.m_strLocalDir;
				if (strFileName[strFileName.length() - 1] != '/')
					strFileName += '/';
			}
			strFileName += psoResData->m_soConf.m_strLocalPortFile;
			iFnRes = stat(strFileName.c_str(), &soStat);
			if (iFnRes) {
				iFnRes = errno;
				if (0 == iFnRes)
					iFnRes = -1;
				UTL_LOG_E (psoResData->m_coLog, "can not retrieve file '%s' info; error code: '%d'", strFileName.c_str (), iFnRes);
				continue;
			}
			if (tLastTime < soStat.st_ctime) {
				++iDataUpdated;
				tLastTime = soStat.st_ctime;
			}
			/* если данные обновились */
			if (iDataUpdated) {
				resolver_recreate_cache (psoResData);
			}
		}
	} while (0);

	pthread_exit (0);
}
