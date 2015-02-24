#include "resolver-operations.h"
#include "resolver-auxiliary.h"

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
#endif

bool operator < (const SOwnerData &p_soLeft, const SOwnerData &p_soRight)
{
	if (p_soLeft.m_uiCapacity < p_soRight.m_uiCapacity) {
		return true;
	} else {
		return false;
	}
}

int LoadFileList (
	SResolverData *p_psoResData,
	std::string &p_strDir)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

	int iRetVal = 0;
	int iFnRes;
	CURL *pCurl = NULL;
	FILE *psoFile = NULL;

	do {
		/* инициализация дескриптора */
		pCurl = curl_easy_init ();
		if (NULL == pCurl) {
			iRetVal = CURLE_OUT_OF_MEMORY;
			p_psoResData->m_coLog.WriteLog ("error: '%s': curl_easy_init: out of memory", __FUNCTION__);
			break;
		}

		/* put transfer data into callbacks */
		std::string strFileName;
		if (p_psoResData->m_soConf.m_strLocalDir.length ()) {
			strFileName = p_psoResData->m_soConf.m_strLocalDir;
			if (strFileName[strFileName.length () - 1] != '/') {
				strFileName += '/';
			}
		}
		strFileName += p_psoResData->m_soConf.m_strLocalFileList;
		psoFile = fopen (strFileName.c_str (), "w");
		if (NULL == psoFile) {
			iRetVal = errno;
			p_psoResData->m_coLog.WriteLog ("error: '%s': fopen: error code: '%d'", __FUNCTION__, iFnRes);
			break;
		}
		iFnRes = curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, (void *) psoFile);
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
			break;
		}

		/* set an user name */
		iFnRes = curl_easy_setopt (pCurl, CURLOPT_USERNAME , p_psoResData->m_soConf.m_strUserName.c_str ());
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
			break;
		}

		/* set an password */
		iFnRes = curl_easy_setopt (pCurl, CURLOPT_PASSWORD , p_psoResData->m_soConf.m_strUserPswd.c_str ());
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
			break;
		}

		/* set an URL containing wildcard pattern (only in the last part) */
		std::string strURL;
		strURL = p_psoResData->m_soConf.m_strProto + "://" + p_psoResData->m_soConf.m_strHost + '/';
		if (p_strDir.length ()) {
			strURL += p_strDir + "/";
		}
		iFnRes = curl_easy_setopt (pCurl, CURLOPT_URL, strURL.c_str ());
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
			break;
		}

		/* если задан proxy host */
		if (p_psoResData->m_soConf.m_strProxyHost.length ()) {
			iFnRes = curl_easy_setopt (pCurl, CURLOPT_PROXY, p_psoResData->m_soConf.m_strProxyHost.c_str ());
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
				break;
			}
		}

		/* если задан proxy port */
		if (p_psoResData->m_soConf.m_strProxyPort.length ()) {
			long lPort = atol (p_psoResData->m_soConf.m_strProxyPort.c_str ());
			iFnRes = curl_easy_setopt (pCurl, CURLOPT_PROXYPORT, lPort);
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
				break;
			}
		}

		/* perform request */
		iFnRes = curl_easy_perform (pCurl);
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': curl_easy_perform: error code: '%d'", __FUNCTION__, iFnRes);
			break;
		}
	} while (0);

	/* освобождаем занятые ресурсы */
	if (NULL != pCurl) {
		curl_easy_cleanup (pCurl);
	}
	if (NULL != psoFile) {
		fclose (psoFile);
	}

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

/******************************************************************************
  Образец строки, содержащей информацию о элементе удаленной файловой системы
  drwxr-x---   6 hpiumusr   iumusers        96 Nov  1 00:00 2013
******************************************************************************/
static const char g_mcFormat [] = "%s %u %s %s %u %s %u %u:%u %s";
struct SFTPFileInfo {
	char m_mcMode[16];
	unsigned int m_uiUID;
	char m_mcFileOwner[32];
	char m_mcFileGroup[32];
	unsigned int m_uiFileSize;
	char m_mcMon[8];
	unsigned int m_uiDay;
	unsigned int m_uiHour;
	unsigned int m_uiMin;
	char m_mcFileName[256];
};
int ParseFileList (
	SResolverData *p_psoResData,
	std::set<std::string> &p_setFileList)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

	int iRetVal = 0;
	int iFnRes;
	FILE *psoFile = NULL;

	do {
		std::string strFileName;
		char mcBuf[1024];
		SFTPFileInfo soInfo;

		/* формируем имя файла, содержащего список файлов */
		if (p_psoResData->m_soConf.m_strLocalDir.length ()) {
			strFileName = p_psoResData->m_soConf.m_strLocalDir;
			if (strFileName[strFileName.length () - 1] != '/') {
				strFileName += '/';
			}
		}
		strFileName += p_psoResData->m_soConf.m_strLocalFileList;
		/* открываем файл, содержащий список файлов, на чтение */
		psoFile = fopen (strFileName.c_str (), "r");
		if (NULL == psoFile) {
			iRetVal = errno;
			break;
		}
		/* читаем файл построчно */
		while (fgets (mcBuf, sizeof (mcBuf), psoFile)) {
			iFnRes = sscanf (
				mcBuf,
				g_mcFormat,
				soInfo.m_mcMode,
				&soInfo.m_uiUID,
				soInfo.m_mcFileOwner,
				soInfo.m_mcFileGroup,
				&soInfo.m_uiFileSize,
				soInfo.m_mcMon,
				&soInfo.m_uiDay,
				&soInfo.m_uiHour,
				&soInfo.m_uiMin,
				soInfo.m_mcFileName);
			if (10 != iFnRes) {
				continue;
			}
			switch (soInfo.m_mcMode[0]) {
			case 'd': /* если файл является директорией */
				break;
			default: /* во всех остальных случаях */
				p_setFileList.insert (soInfo.m_mcFileName);
				break;
			}
		}
	} while (0);

	if (NULL != psoFile) {
		fclose (psoFile);
	}

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

int GetLastFileName (
	SResolverData *p_psoResData,
	std::string &p_strDir,
	std::string &p_strFileName)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

	int iRetVal = 0;
	int iFnRes;

	do {
		/* загружаем список файлов */
		iFnRes = LoadFileList (
			p_psoResData,
			p_strDir);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		/* список файлов на удаленном сервере */
		std::set<std::string> setFileList;
		/* парсим список файлов */
		iFnRes = ParseFileList (
			p_psoResData,
			setFileList);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		/* проверка результата выполнения операции */
		if (iFnRes || 0 == setFileList.size ()) {
			if (iFnRes) {
				iRetVal = iFnRes;
			} else {
				iRetVal = -1;
			}
			break;
		}

		/* выбираем последний файл из списка */
		std::set<std::string>::reverse_iterator iterSet;
		iterSet = setFileList.rbegin ();
		p_strFileName = *(iterSet);

		/* освобождаем ресурсы, занятые списком */
		setFileList.clear ();
	} while (0);

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

int resolver_load_data (
	SResolverData *p_psoResData,
	int *p_piUpdated)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

	int iRetVal = 0;
	int iFnRes;
	std::string strFileName;
	if (p_piUpdated) {
		*p_piUpdated = 0;
	}

	do {
		/* запрашиваем имя актуального файла, содержащего список перенесенных номеров */
		iFnRes = GetLastFileName (p_psoResData, p_psoResData->m_soConf.m_strPortDir, strFileName);
		if (iFnRes) {
			iRetVal = -1;
			break;
		}

		/* проверяем не существует ли такой файл */
		iFnRes = IsFileNotExists (p_psoResData->m_soConf.m_strLocalDir, strFileName);
		if (iFnRes) {
			/* загужаем с удаленного сервера файл, содеражащий список перенесенных номеров */
			iFnRes = DownloadFile (p_psoResData, p_psoResData->m_soConf.m_strPortDir, strFileName);
			if (iFnRes) {
				iRetVal = -2;
				break;
			}

			/* распаковываем файл, содержащий список перенесенных номеров */
			SFileInfo
				soUnZip = { "unzip", "", 0 },
				soZipFile = { strFileName, p_psoResData->m_soConf.m_strLocalDir, 0 },
				soCSVFile = { p_psoResData->m_soConf.m_strLocalPortFile, p_psoResData->m_soConf.m_strLocalDir, 0 };
			iFnRes = ExtractZipFile (soUnZip, soZipFile, soCSVFile);
			if (iFnRes) {
				iRetVal = -3;
				break;
			}
			if (p_piUpdated) {
				++ (*p_piUpdated);
			}
		}

		/* запрашиваем имя актуального файла, содержащего план нумерации */
		iFnRes = GetLastFileName (p_psoResData, p_psoResData->m_soConf.m_strNumPlanDir, strFileName);
		if (iFnRes) {
			iRetVal = -4;
			break;
		}

		/* проверяем существует ли такой файл */
		iFnRes = IsFileNotExists (p_psoResData->m_soConf.m_strLocalDir, strFileName);
		/* если файл не существует */
		if (iFnRes) {
			/* загужаем с удаленного сервера файл, содеражащий план нумерации */
			iFnRes = DownloadFile (p_psoResData, p_psoResData->m_soConf.m_strNumPlanDir, strFileName);
			if (iFnRes) {
				iRetVal = -5;
				break;
			}

			/* распаковываем файл, содержащий план нумерации */
			SFileInfo
				soUnZip = { "unzip", "", 0 },
				soZipFile = { strFileName, p_psoResData->m_soConf.m_strLocalDir, 0 },
				soCSVFile = { p_psoResData->m_soConf.m_strLocalNumPlanFile, p_psoResData->m_soConf.m_strLocalDir, 0 };
			iFnRes = ExtractZipFile (soUnZip, soZipFile, soCSVFile);
			if (iFnRes) {
				iRetVal = -6;
				break;
			}
			if (p_piUpdated) {
				++ (*p_piUpdated);
			}
		}
	} while (0);

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

int DownloadFile (
	SResolverData *p_psoResData,
	std::string &p_strDir,
	std::string &p_strFileName)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

	int iRetVal = 0;
	int iFnRes;
	CURLcode curlRes;
	CURL *pvCurl = NULL;
	std::string strURL;
	FILE *psoLocalFile = NULL;

	do {
		/* инициализация экземпляра CURL */
		pvCurl = curl_easy_init ();
		if (NULL == pvCurl) {
			iRetVal = -1;
			p_psoResData->m_coLog.WriteLog ("error: '%s': curl_easy_init: out of memory", __FUNCTION__);
			break;
		}

		/* user name */
		if (p_psoResData->m_soConf.m_strUserName.length ()) {
			curlRes = curl_easy_setopt (pvCurl, CURLOPT_USERNAME, p_psoResData->m_soConf.m_strUserName.c_str ());
			if (curlRes) {
				iRetVal = curlRes;
				p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, curlRes);
				break;
			}
		}

		/* user password */
		if (p_psoResData->m_soConf.m_strUserPswd.length ()) {
			curlRes = curl_easy_setopt (pvCurl, CURLOPT_PASSWORD, p_psoResData->m_soConf.m_strUserPswd.c_str ());
			if (curlRes) {
				iRetVal = curlRes;
				p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, curlRes);
				break;
			}
		}

		/* set write data */
		std::string strLocalFileName;
		if (p_psoResData->m_soConf.m_strLocalDir.length ()) {
			strLocalFileName = p_psoResData->m_soConf.m_strLocalDir;
			if (strLocalFileName[strLocalFileName.length () - 1] != '/' && strLocalFileName[strLocalFileName.length () - 1] != '\\') {
				strLocalFileName += '/';
			}
		}
		strLocalFileName += p_strFileName;
		psoLocalFile = fopen (strLocalFileName.c_str (), "w");
		if (NULL == psoLocalFile) {
			iRetVal = errno;
			p_psoResData->m_coLog.WriteLog ("error: '%s': fopen: error code: '%d'", __FUNCTION__, iRetVal);
			break;
		}
		curlRes = curl_easy_setopt (pvCurl, CURLOPT_WRITEDATA, (void *) psoLocalFile);
		if (curlRes) {
			iRetVal = curlRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, curlRes);
			break;
		}

		/* URL */
		strURL = p_psoResData->m_soConf.m_strProto;
		strURL += "://";
		strURL += p_psoResData->m_soConf.m_strHost;
		strURL += '/';
		strURL += p_strDir;
		if (strURL[strURL.length () - 1] != '/') {
			strURL += '/';
		}
		strURL += p_strFileName;
		curlRes = curl_easy_setopt (pvCurl, CURLOPT_URL, strURL.c_str ());
		if (curlRes) {
			iRetVal = curlRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, curlRes);
			break;
		}

		/* если задан proxy host */
		if (p_psoResData->m_soConf.m_strProxyHost.length ()) {
			iFnRes = curl_easy_setopt (pvCurl, CURLOPT_PROXY, p_psoResData->m_soConf.m_strProxyHost.c_str ());
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
				break;
			}
		}

		/* если задан proxy port */
		if (p_psoResData->m_soConf.m_strProxyPort.length ()) {
			long lPort = atol (p_psoResData->m_soConf.m_strProxyPort.c_str ());
			iFnRes = curl_easy_setopt (pvCurl, CURLOPT_PROXYPORT, lPort);
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				p_psoResData->m_coLog.WriteLog ("error: '%s': line: '%d'; curl_easy_setopt: error code: '%d'", __FUNCTION__, __LINE__, iFnRes);
				break;
			}
		}

		/* execute */
		curlRes = curl_easy_perform (pvCurl);

		/* если при выполнении запроса произошла ошибка */
		if (curlRes) {
			iRetVal = curlRes;
			p_psoResData->m_coLog.WriteLog ("error: '%s': curl_easy_perform: error code: '%d'", __FUNCTION__, curlRes);
			break;
		}

	} while (0);

	if (pvCurl) {
		curl_easy_cleanup (pvCurl);
		pvCurl = NULL;
	}
	if (psoLocalFile) {
		fclose (psoLocalFile);
	}

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

int ParseNumberinPlanFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

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

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

int ParsePortFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

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

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

int IsFileNotExists (
	std::string &p_strDir,
	std::string &p_strFileTitle)
{
	int iRetVal = 0;

	std::string strFileName;
	if (p_strDir.length ()) {
		strFileName = p_strDir;
		if (strFileName[strFileName.length () - 1] != '/' && strFileName[strFileName.length () - 1] != '\\') {
			strFileName += '/';
		}
	}
	strFileName += p_strFileTitle;

	iRetVal = access (strFileName.c_str (), 0);

	return iRetVal;
}

int resolver_cache (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

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

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return 0;
}

int resolver_recreate_cache (SResolverData *p_psoResData)
{
	if (p_psoResData->m_iDebug > 1) ENTER_ROUT (p_psoResData->m_coLog);

	int iRetVal = 0;
	int iFnRes;
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *pmapTmp =
		new std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >;
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *pmapTmpOld;

	do {
		/* создаем временный экземпляр кэша */
		iFnRes = resolver_cache (p_psoResData, *pmapTmp);
		if (iFnRes || 0 == pmapTmp->size ()) {
			delete pmapTmp;
			iRetVal = -1;
			break;
		}

		/* ждем освобождения кэша всеми потоками */
		for (int i = 0; i < 256; ++i) {
			sem_wait (&(p_psoResData->m_tSemData));
		}

		/* запоминаем прежний кэш */
		pmapTmpOld = p_psoResData->m_pmapResolverCache;
		/* сохраняем новый кэш */
		p_psoResData->m_pmapResolverCache = pmapTmp;

		/* освобождаем семафор */
		for (int i = 0; i < 256; ++i) {
			sem_post (&(p_psoResData->m_tSemData));
		}

		/* освобождаем память, занятую прежним кэшем */
		pmapTmpOld->clear ();
		delete pmapTmpOld;
	} while (0);

	if (p_psoResData->m_iDebug > 1) LEAVE_ROUT (p_psoResData->m_coLog, iRetVal);

	return iRetVal;
}

int resolver_apply_settings (
	const char *p_pszSettings,
	SResolverConf &p_soResConf)
{
	int iRetVal = 0;
	int iFnRes;
	const char *pszParamName;
	char *pszNextParam;
	char *pszParamVal;
	unsigned int uiParamMask = 0;

	pszParamName = p_pszSettings;

	while (pszParamName) {
		/* запоминаем следующий параметр */
		pszNextParam = (char *) strstr (pszParamName, ";");
		if (pszNextParam) {
			*pszNextParam = '\0';
			++pszNextParam;
		}
		/* получаем указатель на значение параметра */
		pszParamVal = (char *) strstr (pszParamName, "=");
		if (! pszParamVal) {
			goto mk_continue;
		}
		*pszParamVal = '\0';
		++pszParamVal;

		/* приступаем к разбору параметра */
		if (0 == strcmp ("numlex_host", pszParamName)) {
			p_soResConf.m_strHost = pszParamVal;
			uiParamMask |= 1;
		} else if (0 == strcmp ("numlex_user_name", pszParamName)) {
			p_soResConf.m_strUserName = pszParamVal;
			uiParamMask |= 2;
		} else  if (0 == strcmp ("numlex_user_pswd", pszParamName)) {
			p_soResConf.m_strUserPswd = pszParamVal;
			uiParamMask |= 4;
		} else if (0 == strcmp ("numlex_proto_name", pszParamName)) {
			p_soResConf.m_strProto = pszParamVal;
			uiParamMask |= 8;
		} else if (0 == strcmp ("numlex_numplan_dir", pszParamName)) {
			p_soResConf.m_strNumPlanDir = pszParamVal;
			uiParamMask |= 16;
		} else if (0 == strcmp ("numlex_portnum_dir", pszParamName)) {
			p_soResConf.m_strPortDir = pszParamVal;
			uiParamMask |= 32;
		} else if (0 == strcmp ("local_cache_dir", pszParamName)) {
			p_soResConf.m_strLocalDir = pszParamVal;
			uiParamMask |= 64;
		} else if (0 == strcmp ("local_numplan_file", pszParamName)) {
			p_soResConf.m_strLocalNumPlanFile = pszParamVal;
			uiParamMask |= 128;
		} else if (0 == strcmp ("local_portnum_file", pszParamName)) {
			p_soResConf.m_strLocalPortFile = pszParamVal;
			uiParamMask |= 256;
		} else if (0 == strcmp ("local_file_list", pszParamName))  {
			p_soResConf.m_strLocalFileList = pszParamVal;
			uiParamMask |= 512;
		} else if (0 == strcmp ("update_interval", pszParamName)) {
			p_soResConf.m_uiUpdateInterval = atol (pszParamVal);
			if (0 == p_soResConf.m_uiUpdateInterval) {
				p_soResConf.m_uiUpdateInterval = 3600;
			}
			uiParamMask |= 1024;
		} else if (0 == strcmp ("log_file_mask", pszParamName)) {
			p_soResConf.m_strLogFileMask = pszParamVal;
			uiParamMask |= 2048;
		}
		/* далее разбираются опциональные параметры */
		else if (0 == strcmp ("debug", pszParamName)) {
			p_soResConf.m_iDebug = atol (pszParamVal);
		} else if (0 == strcmp ("proxy_host", pszParamName)) {
			p_soResConf.m_strProxyHost = pszParamVal;
		} else if (0 == strcmp ("proxy_port", pszParamName)) {
			p_soResConf.m_strProxyPort = pszParamVal;
		}
		/* завершили разбор параметра */

mk_continue:
		/* переходим к следующему параметру */
		pszParamName = pszNextParam;
	}

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

int ExtractZipFile (
	SFileInfo &p_soUnZip,
	SFileInfo &p_soZipFile,
	SFileInfo &p_soOutput)
{
	int iRetVal = 0;

	/* формируем командную строку */
	std::string strCmdLine;
	/* задаем директорию, если необходимо */
	if (p_soUnZip.m_strDir.length ()) {
		strCmdLine = p_soUnZip.m_strDir;
		if (strCmdLine[strCmdLine.length () - 1] != '/' && strCmdLine[strCmdLine.length () - 1] != '\\') {
			strCmdLine += '/';
		}
	}
	/* задаем имя файла */
	strCmdLine += p_soUnZip.m_strTitle;

	/* формируем имя исходного файла */
	std::string strZipFile;
	/* задаем директорию, если необходимо */
	if (p_soZipFile.m_strDir.length ()) {
		strZipFile += p_soZipFile.m_strDir;
		if (strZipFile[strZipFile.length () - 1] != '/' && strZipFile[strZipFile.length () - 1] != '\\') {
			strZipFile += '/';
		}
	}
	/* задаем имя файла */
	strZipFile += p_soZipFile.m_strTitle;

	/* формируем имя файла для записи результата */
	std::string strOutputFile;
	/* задаем директорию, если необходимо */
	if (p_soOutput.m_strDir.length ()) {
		strOutputFile += p_soOutput.m_strDir;
		if (strOutputFile[strOutputFile.length () - 1] != '/' && strOutputFile[strOutputFile.length () - 1] != '\\') {
			strOutputFile += '/';
		}
	}
	/* задаем имя файла */
	strOutputFile += p_soOutput.m_strTitle;

	/* завершаем формирование командной строки */
	strCmdLine += " -p \"";
	strCmdLine += strZipFile;
	strCmdLine += '"';
	strCmdLine += " > \"";
	strCmdLine += strOutputFile;
	strCmdLine += '"';

	/* выполняем операцию */
	iRetVal = system (strCmdLine.c_str ());

	return iRetVal;
}
