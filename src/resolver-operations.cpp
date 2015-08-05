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
		psoResData->m_ptNumlexSem = SEM_FAILED;
		psoResData->m_pmapResolverCache = NULL;
		psoResData->m_iContinueUpdate = 1;

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

		/* создаем семафор доступа к файлам numlex */
		psoResData->m_ptNumlexSem = sem_open(SEM_NAME, O_CREAT, S_IRWXU, 1);
		/* если семафор уже создан открыаем его */
		if (SEM_FAILED == psoResData->m_ptNumlexSem && EACCES == errno) {
			psoResData->m_ptNumlexSem = sem_open (SEM_NAME, 0, S_IRWXU, 1);
		}
		if (SEM_FAILED == psoResData->m_ptNumlexSem) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}

		/* инициализируем семафор для ожидания потока обновления кэша, создаем его запертым */
		iFnRes = sem_init (&psoResData->m_tThreadSem, 0, 0);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}

		/* создаем семафор доступа к кэшу */
		iFnRes = sem_init (&psoResData->m_tCacheSem, 0, 256);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}

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

	/* отпускаем семафор ожидания потока обновления кэша */
	if (sem_post (&psoResData->m_tThreadSem)) {
		iRetVal = errno;
	}

	/* дожидаемся завершения работы потока обновления кэша */
	if ((pthread_t) -1 != psoResData->m_tThreadUpdateCache) {
		pthread_join (psoResData->m_tThreadUpdateCache, NULL);
	}

	/* ожидаем освобождение кэша всеми потоками */
	for (int i = 0; i < 256; i++) {
		sem_wait (&psoResData->m_tCacheSem);
	}

	/* очищаем кэш */
	if (psoResData->m_pmapResolverCache) {
		psoResData->m_pmapResolverCache->clear ();
		delete psoResData->m_pmapResolverCache;
		psoResData->m_pmapResolverCache = NULL;
	}

	/* сбрасываем все данные на диск */
	psoResData->m_coLog.Flush ();

	/* уничтожаем семафор доступа к кэшу */
	if (sem_destroy (&psoResData->m_tCacheSem)) {
		iRetVal = errno;
	}

	/* уничтожаем семафор данных numlex */
	if (SEM_FAILED != psoResData->m_ptNumlexSem) {
		if (sem_close (psoResData->m_ptNumlexSem)) {
			iRetVal = errno;
		}
	}

	/* уничтожаем семафор потока обнавления */
	if (sem_destroy (&psoResData->m_tThreadSem)) {
		iRetVal = errno;
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

		/* ожидание освобождения семафора доступа к кэшу */
		if (sem_wait (&psoResData->m_tCacheSem)) {
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

		/* освобождение семафора доступа к кэшу */
		if (sem_post (&psoResData->m_tCacheSem)) {
			break;
		}
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
	timespec tSemTime;
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
			tSemTime.tv_sec = tCurrentTime.tv_sec + psoResData->m_soConf.m_uiUpdateInterval;
			tSemTime.tv_nsec = tCurrentTime.tv_usec * 1000;
			/* ожидаем освобождения мьютекса или истечения таймаута */
			iFnRes = sem_timedwait (&psoResData->m_tThreadSem, &tSemTime);
			/* если произошла ошибка */
			if (iFnRes) {
				/* из ошибок нас устроит только таймаут, остальные ошибки считаем фатальными */
				iFnRes = errno;
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
