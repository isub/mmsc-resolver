#include "resolver-auxiliary.h"
#include "utils/config/config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#ifdef _WIN32
#	include <curl.h>
#	include <io.h>
#	include <fcntl.h>
#	define open _open
#else
#	include <curl/curl.h>
#	include <sys/types.h>
#	include <sys/stat.h>
#	include <fcntl.h>
#	include <semaphore.h>
#	include <unistd.h>
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
#ifdef USE_GNUTLS
#include <gcrypt.h>
#include <errno.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;
void init_locks (void)
{
	gcry_control (GCRYCTL_SET_THREAD_CBS);
}
#define kill_locks()
#else
void init_locks (void)
{
	int i;

	g_lockarray = (pthread_mutex_t *)OPENSSL_malloc (CRYPTO_num_locks () * sizeof (pthread_mutex_t));
	for (i = 0; i < CRYPTO_num_locks (); i++) {
		pthread_mutex_init (&(g_lockarray[i]), NULL);
	}

	CRYPTO_set_id_callback ((unsigned long (*)()) thread_id);
	CRYPTO_set_locking_callback ((void (*)(int, int, const char*, int)) lock_callback);

	g_iLockInitialized = 1;
}
void kill_locks (void)
{
	int i;

	CRYPTO_set_locking_callback (NULL);

	if (g_iLockInitialized && g_lockarray) {
		for (i = 0; i < CRYPTO_num_locks (); i++) {
			pthread_mutex_destroy (&(g_lockarray[i]));
		}
	}

	if (g_lockarray) {
		OPENSSL_free (g_lockarray);
	}
}
#endif

bool operator < (const SOwnerData &p_soLeft, const SOwnerData &p_soRight)
{
	if (p_soLeft.m_uiCapacity < p_soRight.m_uiCapacity) {
		return true;
	} else {
		return false;
	}
}

int ParseNumberinPlanFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	int iRetVal = 0;
	int iFnRes;
	SOwnerData soResData = {0, 0, 0, "", 0, 0};
	char mcBuf[1024];
	FILE *psoFile;

	std::string strFileName;

	do {
		/* формируем имя файла */
		if (p_psoResData->m_soConf.m_strLocalDir.length ()) {
			strFileName = p_psoResData->m_soConf.m_strLocalDir;
			if (strFileName[strFileName.length () -1] != '/' && strFileName[strFileName.length () -1] != '\\') {
				strFileName += '/';
			}
		}
		strFileName += p_psoResData->m_soConf.m_strLocalNumPlanFile;

		psoFile = fopen (strFileName.c_str (), "r");
		if (NULL == psoFile) {
			iRetVal = errno;
			break;
		}

		char mcFrom[16];
		char mcTo[16];
		char mcRegionCode[16];
		char mcMNC[16];
		char *pszEndPtr;

		while (fgets (mcBuf, sizeof (mcBuf), psoFile)) {
			iFnRes= sscanf (
				mcBuf,
				"%[^,],%[^,],%[^,],%[^,],%[^,],",
				mcFrom,
				mcTo,
				soResData.m_mcOwner,
				mcRegionCode,
				mcMNC);
			/* если вознила ошибка завершаем обработку */
			if (5 != iFnRes) {
				iRetVal = -5;
				break;
			}
			soResData.m_uiRegionCode = strtoul (mcRegionCode, &pszEndPtr, 10);
			if (pszEndPtr == mcRegionCode) {
				continue;
			}
			soResData.m_uiMNC = strtoul (mcMNC, &pszEndPtr, 10);
			if (pszEndPtr == mcMNC) {
				continue;
			}
			iFnRes = InsertRange (
				p_mapCache,
				mcFrom,
				mcTo,
				soResData);
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
		}
	} while (0);

	if (psoFile) {
		fclose (psoFile);
		psoFile = NULL;
	}

	return iRetVal;
}

int ParsePortFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	int iRetVal = 0;
	int iFnRes;
	SOwnerData soResData = {0, 0, 0, "", 0, 0};
	char mcBuf[1024];
	FILE *psoFile;

	std::string strFileName;

	do {
		/* формируем имя файла */
		if (p_psoResData->m_soConf.m_strLocalDir.length ()) {
			strFileName = p_psoResData->m_soConf.m_strLocalDir;
			if (strFileName[strFileName.length () -1] != '/' && strFileName[strFileName.length () -1] != '\\') {
				strFileName += '/';
			}
		}
		strFileName += p_psoResData->m_soConf.m_strLocalPortFile;

		psoFile = fopen (strFileName.c_str (), "r");
		if (NULL == psoFile) {
			iRetVal = errno;
			break;
		}

		char mcABCDEFGHIJ[16];
		char mcRoute[16];
		char mcRegionCode[16];
		char mcMNC[16];
		char *pszEndPtr;

		while (fgets (mcBuf, sizeof (mcBuf), psoFile)) {
			iFnRes= sscanf (
				mcBuf,
				"%[^,],%[^,],%[^,],%[^,],%[^,],",
				mcABCDEFGHIJ,
				soResData.m_mcOwner,
				mcMNC,
				mcRoute,
				mcRegionCode);
			/* если вознила ошибка завершаем обработку */
			if (5 != iFnRes) {
				iRetVal = -5;
				break;
			}

			soResData.m_uiRegionCode = strtoul (mcRegionCode, &pszEndPtr, 10);
			if (pszEndPtr == mcRegionCode) {
				continue;
			}
			soResData.m_uiMNC = strtoul (mcMNC, &pszEndPtr, 10);
			if (pszEndPtr == mcMNC) {
				continue;
			}
			iFnRes = InsertRange (
				p_mapCache,
				mcABCDEFGHIJ,
				mcABCDEFGHIJ,
				soResData);
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
		}
	} while (0);

	if (psoFile) {
		fclose (psoFile);
		psoFile = NULL;
	}

	return iRetVal;
}

int resolver_cache (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	int iRetVal = 0;
	int iFnRes;

	do {
		/* парсинг файла, содержащего план нумерации */
		iFnRes = ParseNumberinPlanFile (p_psoResData, p_mapCache);
		if (iFnRes) {
			iRetVal = -2;
			break;
		}

		/* парсинг файла, содержащего список перенесенных номеров */
		iFnRes = ParsePortFile (p_psoResData, p_mapCache);
		if (iFnRes) {
			iRetVal = -1;
			break;
		}
	} while (0);

	return 0;
}

int resolver_recreate_cache (SResolverData *p_psoResData)
{
	int iRetVal = 0;
	int iFnRes;
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *pmapTmp =
		new std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >;
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *pmapTmpOld;

	do {
		/* ждем освобождения файлов numlex */
		iFnRes = sem_wait (p_psoResData->m_ptNumlexSem);
		if (iFnRes) {
			iFnRes = errno;
			break;
		}

		/* создаем временный экземпляр кэша */
		iFnRes = resolver_cache (p_psoResData, *pmapTmp);
		if (iFnRes || 0 == pmapTmp->size ()) {
			delete pmapTmp;
			iRetVal = -1;
			break;
		}

		/* освобождаем семафор */
		iFnRes = sem_post (p_psoResData->m_ptNumlexSem);
		if (iFnRes) {
			iFnRes = errno;
			break;
		}

		/* запоминаем прежний кэш */
		pmapTmpOld = p_psoResData->m_pmapResolverCache;

		/* ожидаем освобождения кэша всеми потоками */
		for (int i = 0; i < 256; i++) {
			if (sem_wait (&p_psoResData->m_tCacheSem)) {
				iRetVal = errno;
				break;
			}
		}
		if (iRetVal) {
			delete pmapTmp;
			pmapTmp = NULL;
			break;
		}

		/* сохраняем новый кэш */
		p_psoResData->m_pmapResolverCache = pmapTmp;

		/* освобождаем семафор */
		for (int i = 0; i < 256; i++) {
			if (sem_post (&p_psoResData->m_tCacheSem)) {
				iRetVal = errno;
				break;
			}
		}

		/* освобождаем память, занятую прежним кэшем */
		pmapTmpOld->clear ();
		delete pmapTmpOld;
	} while (0);

	return iRetVal;
}

int resolver_apply_settings (
	const char *p_pszSettings,
	SResolverConf &p_soResConf)
{
	int iRetVal = 0;
	int iFnRes;
	CConfig coConf;
	const char *pszParamName;
	std::string strParamVal;
	unsigned int uiParamMask = 0;

	do {
		iFnRes = coConf.LoadConf(p_pszSettings, 0);
		if (iFnRes) {
			iRetVal = 0;
			break;
		}

		pszParamName = "numlex_host";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strHost = strParamVal;
		uiParamMask |= 1;

		pszParamName = "numlex_user_name";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strUserName = strParamVal;
		uiParamMask |= 2;

		pszParamName = "numlex_user_pswd";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strUserPswd = strParamVal;
		uiParamMask |= 4;

		pszParamName = "numlex_proto_name";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strProto = strParamVal;
		uiParamMask |= 8;

		pszParamName = "numlex_numplan_dir";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strNumPlanDir = strParamVal;
		uiParamMask |= 16;

		pszParamName = "numlex_portnum_dir";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strPortDir = strParamVal;
		uiParamMask |= 32;

		pszParamName = "local_cache_dir";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strLocalDir = strParamVal;
		uiParamMask |= 64;

		pszParamName = "local_numplan_file";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strLocalNumPlanFile = strParamVal;
		uiParamMask |= 128;

		pszParamName = "local_portnum_file";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strLocalPortFile = strParamVal;
		uiParamMask |= 256;

		pszParamName = "local_file_list";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strLocalFileList = strParamVal;
		uiParamMask |= 512;

		pszParamName = "update_interval";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_uiUpdateInterval = atol (strParamVal.c_str());
		if (0 == p_soResConf.m_uiUpdateInterval) {
			p_soResConf.m_uiUpdateInterval = 3600;
		}
		uiParamMask |= 1024;

		pszParamName = "log_file_mask";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (iFnRes)
			break;
		p_soResConf.m_strLogFileMask = strParamVal;
		uiParamMask |= 2048;

		/* далее разбираются опциональные параметры */
		pszParamName = "proxy_host";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (!iFnRes)
			p_soResConf.m_strProxyHost = strParamVal;
		pszParamName = "proxy_port";
		iFnRes = coConf.GetParamValue(pszParamName, strParamVal);
		if (!iFnRes)
			p_soResConf.m_strProxyPort = strParamVal;
	} while (0);

	/* проверяем, все ли нужные параметры мы получили */
	if (! (uiParamMask & (unsigned int) (4096 - 1))) {
		iRetVal = uiParamMask;
	}

	return iRetVal;
}

int InsertRange (
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_pmapCache,
	const char *p_pszFrom,
	const char *p_pszTo,
	SOwnerData &p_soOwnData)
{
	int iRetVal = 0;
	unsigned int
		uiFromABC,
		uiFromDEF,
		uiFromGHIJ,
		uiToABC,
		uiToDEF,
		uiToGHIJ;
	char mcTmp[5];
	char *pszEndPtr;

	do {
		/* получаем fromABC */
		memcpy (mcTmp, p_pszFrom, 3);
		mcTmp[3] = '\0';
		uiFromABC = strtoul (mcTmp, &pszEndPtr, 10);
		if (pszEndPtr != &(mcTmp[3])) {
			iRetVal = -1;
			break;
		}

		/* получаем toABC */
		memcpy (mcTmp, p_pszTo, 3);
		mcTmp[3] = '\0';
		uiToABC = strtoul (mcTmp, &pszEndPtr, 10);
		if (pszEndPtr != &(mcTmp[3])) {
			iRetVal = -2;
			break;
		}

		/* получаем fromDEF */
		memcpy (mcTmp, &(p_pszFrom[3]), 3);
		mcTmp[3] = '\0';
		uiFromDEF = strtoul (mcTmp, &pszEndPtr, 10);
		if (pszEndPtr != &(mcTmp[3])) {
			iRetVal = -3;
			break;
		}

		/* получаем toDEF */
		memcpy (mcTmp, &(p_pszTo[3]), 3);
		mcTmp[3] = '\0';
		uiToDEF = strtoul (mcTmp, &pszEndPtr, 10);
		if (pszEndPtr != &(mcTmp[3])) {
			iRetVal = -4;
			break;
		}

		/* получаем fromGHIJ */
		memcpy (mcTmp, &(p_pszFrom[6]), 4);
		mcTmp[4] = '\0';
		uiFromGHIJ = strtoul (mcTmp, &pszEndPtr, 10);
		if (pszEndPtr != &(mcTmp[4])) {
			iRetVal = -5;
			break;
		}

		/* получаем toGHIJ */
		memcpy (mcTmp, &(p_pszTo[6]), 4);
		mcTmp[4] = '\0';
		uiToGHIJ = strtoul (mcTmp, &pszEndPtr, 10);
		if (pszEndPtr != &(mcTmp[4])) {
			iRetVal = -6;
			break;
		}

		/* проверка на всякий случай */
		if (uiFromABC > uiToABC) {
			iRetVal = -7;
			break;
		}

		unsigned int
			uiABC,
			uiDEF,
			uiGHIJ;
		int iFnRes;
		std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >::iterator iterMapABC;
		std::map<unsigned int,std::multiset<SOwnerData> > mapTmpDEF;
		std::map<unsigned int,std::multiset<SOwnerData> > *pmapDEF;

		/* объодим все ABC */
		uiDEF = uiFromDEF;
		uiGHIJ = uiFromGHIJ;
		for (uiABC = uiFromABC; uiABC <= uiToABC; ++uiABC) {
			/* ищем в кэше ABC-ключ */
			iterMapABC = p_pmapCache.find (uiABC);
			/* если ключ не найден нам придется создать его позже */
			if (iterMapABC == p_pmapCache.end ()) {
				/* используем временное хранилище. позже с его помощью мы созданим недостающий ключ ABC */
				mapTmpDEF.clear ();
				pmapDEF = &mapTmpDEF;
			} else {
				pmapDEF = &(iterMapABC->second);
			}
			/* добавляем диапазон DEF в кэш */
			if (uiABC < uiToABC) {
				iFnRes = InsertRangeDEF (
					pmapDEF,
					uiDEF,
					uiGHIJ,
					999,
					9999,
					p_soOwnData);
				if (iFnRes) {
					iRetVal = -7;
				}
				/* в дальшейшем нам не понадобится исходное значение fromDEF */
				uiDEF = 000;
				/* в дальнейшем нам не понадобится исходное значение fromGHIJ */
				uiGHIJ = 0000;
			} else {
				iFnRes = InsertRangeDEF (
					pmapDEF,
					uiDEF,
					uiGHIJ,
					uiToDEF,
					uiToGHIJ,
					p_soOwnData);
				if (iFnRes) {
					iRetVal = -8;
					break;
				}
			}
			if (iterMapABC == p_pmapCache.end ()) {
				p_pmapCache.insert (std::make_pair (uiABC, *pmapDEF));
			}
		}
	} while (0);

	return iRetVal;
}

int InsertRangeDEF (
	std::map<unsigned int, std::multiset<SOwnerData> > *p_pmapCacheDEF,
	unsigned int p_uiFromDEF,
	unsigned int p_uiFromGHIJ,
	unsigned int p_uiToDEF,
	unsigned int p_uiToGHIJ,
	SOwnerData &p_soResData)
{
	int iRetVal = 0;

	do {
		/* на всякий случай проверяем параметры */
		if (p_uiFromDEF > p_uiToDEF) {
			iRetVal = -1;
			break;
		}

		int iRetVal = 0;
		unsigned int uiDEF;
		unsigned int uiGHIJ;
		std::map<unsigned int, std::multiset<SOwnerData> >::iterator iterMapDEF;
		std::multiset<SOwnerData> msetTmp;
		std::multiset<SOwnerData> *pmsetSResData;
		SOwnerData soResData;

		uiGHIJ = p_uiFromGHIJ;
		for (uiDEF = p_uiFromDEF; uiDEF <= p_uiToDEF; ++ uiDEF) {
			soResData = p_soResData;
			iterMapDEF = p_pmapCacheDEF->find (uiDEF);
			if (iterMapDEF == p_pmapCacheDEF->end ()) {
				msetTmp.clear ();
				pmsetSResData = &msetTmp;
			} else {
				pmsetSResData = &(iterMapDEF->second);
			}
			soResData.m_uiFromGHIJ = uiGHIJ;
			if (uiDEF < p_uiToDEF) {
				soResData.m_uiToGHIJ = 9999;
				uiGHIJ = 0000;
			} else {
				soResData.m_uiToGHIJ = p_uiToGHIJ;
			}
			soResData.m_uiCapacity = soResData.m_uiToGHIJ - soResData.m_uiFromGHIJ + 1;
			pmsetSResData->insert (soResData);
			if (iterMapDEF == p_pmapCacheDEF->end ()) {
				p_pmapCacheDEF->insert (std::make_pair (uiDEF, *pmsetSResData));
			}
		}
	} while (0);

	return iRetVal;
}
