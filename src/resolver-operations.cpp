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

static pthread_mutex_t *g_lockarray = NULL;
static int g_iLockInitialized = 0;

/* процедура блокировки дл openSSL */
static void lock_callback (int mode, int type, const char *file, int line)
{
	(void) file;
	(void) line;
	if (mode & CRYPTO_LOCK) {
		pthread_mutex_lock (&(g_lockarray[type]));
	} else {
		pthread_mutex_unlock (&(g_lockarray[type]));
	}
}

/* процедура определения идентификатора потока */
static unsigned long thread_id (void)
{
	unsigned long ret;

	ret = (unsigned long) pthread_self ();

	return (ret);
}

/* процедура инициализации объектов синхронизации openSSL */
static void init_locks (void)
{
	int i;

	g_lockarray = (pthread_mutex_t *) OPENSSL_malloc (CRYPTO_num_locks () * sizeof (pthread_mutex_t));
	for (i=0; i < CRYPTO_num_locks (); i++) {
		pthread_mutex_init (&(g_lockarray[i]), NULL);
	}

	CRYPTO_set_id_callback ((unsigned long (*)()) thread_id);
	CRYPTO_set_locking_callback ((void (*)(int, int, const char*, int)) lock_callback);

	g_iLockInitialized = 1;
}

/* функция освобождения ресурсов объектов синхронизации openSSL */
static void kill_locks (void)
{
	int i;

	CRYPTO_set_locking_callback (NULL);

	if (g_iLockInitialized && g_lockarray) {
		for (i=0; i < CRYPTO_num_locks (); i++) {
			pthread_mutex_destroy (&(g_lockarray[i]));
		}
	}

	if (g_lockarray) {
		OPENSSL_free (g_lockarray);
	}
}

#ifdef USE_GNUTLS
#include <gcrypt.h>
#include <errno.h>

GCRY_THREAD_OPTION_PTHREAD_IMPL;

void init_locks (void)
{
	gcry_control (GCRYCTL_SET_THREAD_CBS);
}

#define kill_locks()
#endif

/* процедура потока обновления кэша */
static void * resolver_update_cache (void *p_pParam);

void * resolver_init (const char *p_pszConfFile)
{
	int iFnRes;
	SResolverData *psoResData = NULL;

	do {
		psoResData = new SResolverData;

		CHECKPOINT (psoResData->m_coLog);

		/* инициализация параметров */
		psoResData->m_tThreadUpdateCache = (pthread_t) -1;
		psoResData->m_pmapResolverCache = NULL;
		psoResData->m_iContinueUpdate = 1;
		psoResData->m_iDebug = 0;
		psoResData->m_iSemDataInitialized = 0;
		psoResData->m_iMutexInitialized = 0;

		/* загружаем конфигурацию модуля */
		iFnRes = resolver_apply_settings (p_pszConfFile, psoResData->m_soConf);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		CHECKPOINT (psoResData->m_coLog);

		psoResData->m_iDebug = psoResData->m_soConf.m_iDebug;

		/* инициализация лог-файла */
		iFnRes = psoResData->m_coLog.Init (psoResData->m_soConf.m_strLogFileMask.c_str ());
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);

		/* инициализируем мьютекс для ожидания потока обновления кэша */
		iFnRes = pthread_mutex_init (&(psoResData->m_tThreadMutex), NULL);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
		psoResData->m_iMutexInitialized = 1;
		/* запираем мьютекс потока обновления */
		pthread_mutex_trylock (&(psoResData->m_tThreadMutex));

		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);

		/* инициализируем семафор кэша */
		iFnRes = sem_init (&(psoResData->m_tSemData), 0, 256);
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
		psoResData->m_iSemDataInitialized = 1;

		/* инициализация объектов синхронизации openSSL */
		init_locks ();

		/* инициализация библиотеки CURL */
		iFnRes = curl_global_init (CURL_GLOBAL_DEFAULT);
		if (CURLE_OK != iFnRes) {
			psoResData->m_coLog.WriteLog ("error: '%s': curl_global_init: error code: '%d'", __FUNCTION__, iFnRes);
			break;
		}
		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);

		/* первоначальная загрузка данных */
		/* выделяем память под кэш */
		psoResData->m_pmapResolverCache = new std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >;
		/* загружаем данные с удаленного сервера */
		iFnRes = resolver_load_data (psoResData, NULL);
		if (iFnRes) {
			psoResData->m_coLog.WriteLog ("error: '%s': resolver_load_data: error code: '%d'", __FUNCTION__, iFnRes);
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
		/* формируем кэш */
		iFnRes = resolver_cache (psoResData, *(psoResData->m_pmapResolverCache));
		if (iFnRes) {
			resolver_fini (psoResData);
			psoResData = NULL;
			break;
		}
		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);

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
		if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
	} while (0);

	if (psoResData && psoResData->m_iDebug > 1) LEAVE_ROUT (psoResData->m_coLog, psoResData);

	return psoResData;
}

int resolver_fini (void *p_pPtr)
{
	int iRetVal = 0;
	SResolverData *psoResData = (SResolverData *) p_pPtr;

	if (psoResData->m_iDebug > 1) ENTER_ROUT (psoResData->m_coLog);
	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }

	/* сообщаем потоку обновления кэша о необходимости завершения работы */
	psoResData->m_iContinueUpdate = 0;

	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }

	/* дожидаемся освобождения семафора данных всеми потоками */
	int iFnRes;
	if (psoResData->m_iSemDataInitialized) {
		for (int i = 0; i < 256; ++i) {
			iFnRes = sem_wait (&(psoResData->m_tSemData));
			/* если произошла ошибка завершаем цикл */
			if (iFnRes) {
				iRetVal = errno;
				break;
			}
		}
	}

	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }

	/* отпускаем семафор ожидания потока */
	if (psoResData->m_iMutexInitialized) {
		pthread_mutex_unlock (&(psoResData->m_tThreadMutex));
	}

	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }

	/* дожидаемся завершения работы потока обновления кэша */
	if ((pthread_t) -1 != psoResData->m_tThreadUpdateCache) {
		pthread_join (psoResData->m_tThreadUpdateCache, NULL);
	}

	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }

	/* уничтожаем семафор потока обнавления */
	if (psoResData->m_iMutexInitialized) {
		iFnRes = pthread_mutex_destroy (&(psoResData->m_tThreadMutex));
		if (iFnRes) {
			iRetVal = errno;
		}
		psoResData->m_iMutexInitialized = 0;
	}

	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }

	/* сбрасываем все данные на диск */
	psoResData->m_coLog.Flush ();

	/* очищаем кэш */
	if (psoResData->m_pmapResolverCache) {
		psoResData->m_pmapResolverCache->clear ();
		delete psoResData->m_pmapResolverCache;
		psoResData->m_pmapResolverCache = NULL;
	}

	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }

	/* уничтожаем семафор данных */
	if (psoResData->m_iSemDataInitialized) {
		psoResData->m_iSemDataInitialized = 0;
		iFnRes = sem_destroy (&(psoResData->m_tSemData));
		if (iFnRes) {
			iRetVal = errno;
		}
	}

	if (psoResData->m_iDebug > 1) LEAVE_ROUT (psoResData->m_coLog, iRetVal);

	delete psoResData;

	curl_global_cleanup ();

	kill_locks ();

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
		iFnRes = sem_wait (&(psoResData->m_tSemData));
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

		sem_post (&(psoResData->m_tSemData));
	} while (0);

	if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }
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

	if (psoResData->m_iDebug > 1) ENTER_ROUT (psoResData->m_coLog);

	int iDataUpdated;
	int iFnRes;
	int iCURLInitialized = 0;

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
			if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }
			/* ожидаем освобождения семафора или истечения таймаута */
			iFnRes = pthread_mutex_timedlock (&(psoResData->m_tThreadMutex), &tMutexTime);
			if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }
			/* если произошла ошибка */
			if (iFnRes) {
				/* из ошибок нас устроит только таймаут, остальные ошибки считаем фатальными */
				if (ETIMEDOUT != iFnRes) {
					if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
					/* завершаем цикл */
					break;
				}
			}

			/* если сброшен флаг продолжения работы */
			if (0 == psoResData->m_iContinueUpdate) {
				if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
				/* завершаем цикл */
				break;
			} else {
				if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }
			}

			/* загружаем данные с удаленного сервера */
			iFnRes = resolver_load_data (psoResData, &iDataUpdated);
			if (iFnRes) {
				if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
				continue;
			} else {
				if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }
			}
			/* если данные обновились */
			if (iDataUpdated) {
				if (psoResData->m_iDebug) CHECKPOINT (psoResData->m_coLog);
				resolver_recreate_cache (psoResData);
			} else {
				if (psoResData->m_iDebug) { CHECKPOINT (psoResData->m_coLog); }
			}
		}
	} while (0);

	if (psoResData->m_iDebug > 1) LEAVE_ROUT (psoResData->m_coLog, iFnRes);

	pthread_exit (0);
}
