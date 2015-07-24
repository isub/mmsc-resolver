#include "resolver-auxiliary.h"

#include <string.h> /* strncmp */
#include <curl/curl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>

/* загружает данные с удаленного сервера */
int resolver_load_data(SResolverConf &p_soConf, CLog &p_coLog);
/* проверяет не существует ли файл. если файл существует функция возвращает '0', в противном случае функция возвращает '-1' */
int IsFileNotExists(
	std::string &p_strDir,
	std::string &p_strFileTitle);
/* загружает файл с удаленного сервера */
int DownloadFile(SResolverConf &p_soConf, std::string &p_strDir, std::string &p_strFileName, CLog &p_coLog);
/* получает имя актуального файла */
int GetLastFileName(SResolverConf &p_soConf, std::string &p_strDir, std::string &p_strFileName, CLog &p_coLog);
/* декомпрессия исходных данных */
int ExtractZipFile(SFileInfo &p_soUnZip, SFileInfo &p_soZipFile, SFileInfo &p_soOutput);
/* загрузка списка файлов */
int LoadFileList (SResolverConf &p_soConf, std::string &p_strDir, CLog &p_coLog);
/* парсинг списка файлов */
int ParseFileList (SResolverConf &p_soConf, std::set<std::string> &p_setFileList);

int main (int argc, char *argv[])
{
	int iFnRes;
	std::string strConfFileName;
	SResolverConf soConf;
	CLog coLog;

	for (int i = 0; i < argc; ++i) {
		if (0 == strncmp(argv[i], "conf=", 5)) {
			strConfFileName = argv[i] + 5;
			break;
		}
	}

	if (strConfFileName.length() == 0)
		return -1;
	/* загружаем конфигурацию модуля */
	iFnRes = resolver_apply_settings(strConfFileName.c_str(), soConf);
	if (iFnRes) {
		return -2;
	}

	/* инициализация логгера */
	iFnRes = coLog.Init(soConf.m_strLogFileMask.c_str());
	if (iFnRes){
		return -3;
	}

	sem_t *ptSem = NULL;

#ifdef DEBUG
	sem_unlink (SEM_NAME);
	if (iFnRes) {
		UTL_LOG_E (coLog, "can not unlink semaphore: error code: %d", errno);
	}
#endif

	/* инициализация семафора */
	ptSem = sem_open (SEM_NAME, O_CREAT, S_IRWXU, 256);
	/* если семафор уже создан */
	if (SEM_FAILED == ptSem && EACCES == errno) {
		ptSem = sem_open (SEM_NAME, 0, S_IRWXU, 256);
	}
	if (SEM_FAILED == ptSem) {
		iFnRes = errno;
		UTL_LOG_E (coLog, "can not initialize semaphore: error code: %d", iFnRes);
		goto exit;
	}

	/* инициализация объектов синхронизации openSSL */
	init_locks();

	/* инициализация библиотеки CURL */
	iFnRes = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (CURLE_OK != iFnRes) {
		UTL_LOG_E(coLog,"curl_global_init: error code: '%d'", iFnRes);
		goto clean_locks;
	}

	/* ожидаем освобождения необходимых файлов */
	for (int i = 0; i < 256; ++i) {
		if (sem_wait (ptSem)) {
			UTL_LOG_E (coLog, "sem_wait returned error code: %d", errno);
			goto clean_locks;
		}
	}

	/* загружаем данные с удаленного сервера */
	iFnRes = resolver_load_data (soConf, coLog);
	if (iFnRes) {
		UTL_LOG_E (coLog, "can not load data");
	}

	/* освобождаем семафор */
	for (int i = 0; i < 256; ++i) {
		if (sem_post (ptSem)) {
			UTL_LOG_E (coLog, "sem_post returned error code: %d", errno);
			goto clean_locks;
		}
	}

	/* очищаем CURL */
	curl_global_cleanup();

clean_locks:
	/* очищаем openSSL*/
	kill_locks();
	/* закрываем семафор */
#ifdef DEBUG
	iFnRes = sem_unlink (SEM_NAME);
	if (iFnRes) {
		UTL_LOG_E (coLog, "can not unlink semaphore: error code: %d", errno);
	}
#else
	iFnRes = sem_close (ptSem);
	if (iFnRes) {
		UTL_LOG_E (coLog, "can not close semaphore: error code: %d", errno);
	}
#endif
exit:
	coLog.Flush();

	return 0;
}

int replaceFile(SFileInfo &p_soFileInfoNew, SFileInfo &p_soFileInfoOld, CLog &p_coLog)
{
	int iRetVal = 0;
	int iFnRes;
	std::string strFileName;

	do {
		/* удаляем старый файл */
		if (p_soFileInfoOld.m_strDir.length()) {
			strFileName = p_soFileInfoOld.m_strDir;
			if (strFileName[strFileName.length() - 1] != '/')
				strFileName += '/';
		}
		strFileName += p_soFileInfoOld.m_strTitle;
		iFnRes = unlink(strFileName.c_str());
		if (iFnRes && ENOENT != errno) {
			iFnRes = errno;
			if (0 == iFnRes)
				iFnRes = -1;
			UTL_LOG_E(p_coLog, "can not to delete file '%s'; error code: '%s'", strFileName.c_str(), iFnRes);
			iRetVal = iFnRes;
			break;
		}

		/* переименовываем новый файл */
		std::string strFileName2;
		if (p_soFileInfoNew.m_strDir.length()) {
			strFileName2 = p_soFileInfoNew.m_strDir;
			if (strFileName2[strFileName2.length() - 1] != '/')
				strFileName2 += '/';
		}
		strFileName2 += p_soFileInfoNew.m_strTitle;
		iFnRes = rename(strFileName2.c_str(), strFileName.c_str());
		if (iFnRes) {
			iFnRes = errno;
			if (0 == iFnRes)
				iFnRes = -2;
			UTL_LOG_E (p_coLog, "can not to rename file '%s' to '%s'; error code: '%s'", strFileName2.c_str (), strFileName.c_str (), iFnRes);
			iRetVal = iFnRes;
			break;
		}
	} while (0);

	return iRetVal;
}

int resolver_load_data (SResolverConf &p_soConf, CLog &p_coLog)
{
	int iRetVal = 0;
	int iFnRes;
	std::string strFileName;

	do {
		/* запрашиваем имя актуального файла, содержащего список перенесенных номеров */
		iFnRes = GetLastFileName(p_soConf, p_soConf.m_strPortDir, strFileName, p_coLog);
		if (iFnRes) {
			iRetVal = -1;
			break;
		}

		/* проверяем не существует ли такой файл на нашем диске */
		iFnRes = IsFileNotExists(p_soConf.m_strLocalDir, strFileName);
		if (iFnRes) {
			/* если на диске такого файла нет загружаем с удаленного сервера */
			iFnRes = DownloadFile(p_soConf, p_soConf.m_strPortDir, strFileName, p_coLog);
			if (iFnRes) {
				iRetVal = -2;
				break;
			}

			/* распаковываем файл, содержащий список перенесенных номеров */
			SFileInfo
				soUnZip = { "unzip", "", 0 },
				soZipFile = { strFileName, p_soConf.m_strLocalDir, 0 },
				soCSVFile = { p_soConf.m_strLocalPortFile, p_soConf.m_strLocalDir, 0 },
				soCSVFileTmp = { p_soConf.m_strLocalPortFile + ".tmp", p_soConf.m_strLocalDir, 0 };
			iFnRes = ExtractZipFile(soUnZip, soZipFile, soCSVFileTmp);
			if (iFnRes) {
				iRetVal = -3;
				break;
			}
			/* если распаковка завершилась успешно заменяем старый файл */
			iFnRes = replaceFile(soCSVFileTmp, soCSVFile, p_coLog);
			if (iFnRes) {
				iRetVal = -4;
				break;
			}
		}

		/* запрашиваем имя актуального файла, содержащего план нумерации */
		iFnRes = GetLastFileName(p_soConf, p_soConf.m_strNumPlanDir, strFileName, p_coLog);
		if (iFnRes) {
			iRetVal = -5;
			break;
		}

		/* проверяем существует ли такой файл */
		iFnRes = IsFileNotExists(p_soConf.m_strLocalDir, strFileName);
		/* если файл не существует */
		if (iFnRes) {
			/* загужаем с удаленного сервера файл, содеражащий план нумерации */
			iFnRes = DownloadFile(p_soConf, p_soConf.m_strNumPlanDir, strFileName, p_coLog);
			if (iFnRes) {
				iRetVal = -6;
				break;
			}

			/* распаковываем файл, содержащий план нумерации */
			SFileInfo
				soUnZip = { "unzip", "", 0 },
				soZipFile = { strFileName, p_soConf.m_strLocalDir, 0 },
				soCSVFile = { p_soConf.m_strLocalNumPlanFile, p_soConf.m_strLocalDir, 0 },
				soCSVFileTmp = { p_soConf.m_strLocalNumPlanFile + ".tmp", p_soConf.m_strLocalDir, 0 };
			iFnRes = ExtractZipFile(soUnZip, soZipFile, soCSVFileTmp);
			if (iFnRes) {
				iRetVal = -7;
				break;
			}
			/* если распаковка завершилась успешно заменяем старый файл */
			iFnRes = replaceFile(soCSVFileTmp, soCSVFile, p_coLog);
			if (iFnRes) {
				iRetVal = -8;
				break;
			}
		}
	} while (0);

	return iRetVal;
}

int IsFileNotExists(std::string &p_strDir, std::string &p_strFileTitle)
{
	int iRetVal = 0;

	std::string strFileName;
	if (p_strDir.length()) {
		strFileName = p_strDir;
		if (strFileName[strFileName.length() - 1] != '/' && strFileName[strFileName.length() - 1] != '\\') {
			strFileName += '/';
		}
	}
	strFileName += p_strFileTitle;

	iRetVal = access(strFileName.c_str(), 0);

	return iRetVal;
}

int DownloadFile (SResolverConf &p_soConf, std::string &p_strDir, std::string &p_strFileName, CLog &p_coLog)
{
	int iRetVal = 0;
	int iFnRes;
	CURLcode curlRes;
	CURL *pvCurl = NULL;
	std::string strURL;
	FILE *psoLocalFile = NULL;

	do {
		/* инициализация экземпляра CURL */
		pvCurl = curl_easy_init();
		if (NULL == pvCurl) {
			iRetVal = -1;
			UTL_LOG_E(p_coLog,"curl_easy_init: out of memory");
			break;
		}

		/* user name */
		if (p_soConf.m_strUserName.length()) {
			curlRes = curl_easy_setopt(pvCurl, CURLOPT_USERNAME, p_soConf.m_strUserName.c_str());
			if (curlRes) {
				iRetVal = curlRes;
				UTL_LOG_E(p_coLog,"curl_easy_setopt: error code: '%d'", curlRes);
				break;
			}
		}

		/* user password */
		if (p_soConf.m_strUserPswd.length()) {
			curlRes = curl_easy_setopt(pvCurl, CURLOPT_PASSWORD, p_soConf.m_strUserPswd.c_str());
			if (curlRes) {
				iRetVal = curlRes;
				UTL_LOG_E(p_coLog,"curl_easy_setopt: error code: '%d'", curlRes);
				break;
			}
		}

		/* set write data */
		std::string strLocalFileName;
		if (p_soConf.m_strLocalDir.length()) {
			strLocalFileName = p_soConf.m_strLocalDir;
			if (strLocalFileName[strLocalFileName.length() - 1] != '/' && strLocalFileName[strLocalFileName.length() - 1] != '\\') {
				strLocalFileName += '/';
			}
		}
		strLocalFileName += p_strFileName;
		psoLocalFile = fopen(strLocalFileName.c_str(), "w");
		if (NULL == psoLocalFile) {
			iRetVal = errno;
			UTL_LOG_E(p_coLog,"fopen: error code: '%d'", iRetVal);
			break;
		}
		curlRes = curl_easy_setopt(pvCurl, CURLOPT_WRITEDATA, (void *)psoLocalFile);
		if (curlRes) {
			iRetVal = curlRes;
			UTL_LOG_E(p_coLog,"curl_easy_setopt: error code: '%d'", curlRes);
			break;
		}

		/* URL */
		strURL = p_soConf.m_strProto;
		strURL += "://";
		strURL += p_soConf.m_strHost;
		strURL += '/';
		strURL += p_strDir;
		if (strURL[strURL.length() - 1] != '/') {
			strURL += '/';
		}
		strURL += p_strFileName;
		curlRes = curl_easy_setopt(pvCurl, CURLOPT_URL, strURL.c_str());
		if (curlRes) {
			iRetVal = curlRes;
			UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", curlRes);
			break;
		}

		/* если задан proxy host */
		if (p_soConf.m_strProxyHost.length()) {
			iFnRes = curl_easy_setopt(pvCurl, CURLOPT_PROXY, p_soConf.m_strProxyHost.c_str());
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
				break;
			}
		}

		/* если задан proxy port */
		if (p_soConf.m_strProxyPort.length()) {
			long lPort = atol(p_soConf.m_strProxyPort.c_str());
			iFnRes = curl_easy_setopt(pvCurl, CURLOPT_PROXYPORT, lPort);
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
				break;
			}
		}

		/* execute */
		curlRes = curl_easy_perform(pvCurl);

		/* если при выполнении запроса произошла ошибка */
		if (curlRes) {
			iRetVal = curlRes;
			UTL_LOG_E(p_coLog, "curl_easy_perform: error code: '%d'", curlRes);
			break;
		}

	} while (0);

	if (pvCurl) {
		curl_easy_cleanup(pvCurl);
		pvCurl = NULL;
	}
	if (psoLocalFile) {
		fclose(psoLocalFile);
	}

	return iRetVal;
}

int GetLastFileName (SResolverConf &p_soConf, std::string &p_strDir, std::string &p_strFileName, CLog &p_coLog)
{
	int iRetVal = 0;
	int iFnRes;

	do {
		/* загружаем список файлов */
		iFnRes = LoadFileList(p_soConf, p_strDir, p_coLog);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		/* список файлов на удаленном сервере */
		std::set<std::string> setFileList;
		/* парсим список файлов */
		iFnRes = ParseFileList(
			p_soConf,
			setFileList);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		/* проверка результата выполнения операции */
		if (iFnRes || 0 == setFileList.size()) {
			if (iFnRes) {
				iRetVal = iFnRes;
			} else {
				iRetVal = -1;
			}
			break;
		}

		/* выбираем последний файл из списка */
		std::set<std::string>::reverse_iterator iterSet;
		iterSet = setFileList.rbegin();
		p_strFileName = *(iterSet);

		/* освобождаем ресурсы, занятые списком */
		setFileList.clear();
	} while (0);

	return iRetVal;
}

int ExtractZipFile(SFileInfo &p_soUnZip, SFileInfo &p_soZipFile, SFileInfo &p_soOutput)
{
	int iRetVal = 0;

	/* формируем командную строку */
	std::string strCmdLine;
	/* задаем директорию, если необходимо */
	if (p_soUnZip.m_strDir.length()) {
		strCmdLine = p_soUnZip.m_strDir;
		if (strCmdLine[strCmdLine.length() - 1] != '/' && strCmdLine[strCmdLine.length() - 1] != '\\') {
			strCmdLine += '/';
		}
	}
	/* задаем имя файла */
	strCmdLine += p_soUnZip.m_strTitle;

	/* формируем имя исходного файла */
	std::string strZipFile;
	/* задаем директорию, если необходимо */
	if (p_soZipFile.m_strDir.length()) {
		strZipFile += p_soZipFile.m_strDir;
		if (strZipFile[strZipFile.length() - 1] != '/' && strZipFile[strZipFile.length() - 1] != '\\') {
			strZipFile += '/';
		}
	}
	/* задаем имя файла */
	strZipFile += p_soZipFile.m_strTitle;

	/* формируем имя файла для записи результата */
	std::string strOutputFile;
	/* задаем директорию, если необходимо */
	if (p_soOutput.m_strDir.length()) {
		strOutputFile += p_soOutput.m_strDir;
		if (strOutputFile[strOutputFile.length() - 1] != '/' && strOutputFile[strOutputFile.length() - 1] != '\\') {
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
	iRetVal = system(strCmdLine.c_str());

	return iRetVal;
}

int LoadFileList (SResolverConf &p_soConf, std::string &p_strDir, CLog &p_coLog)
{
	int iRetVal = 0;
	int iFnRes;
	CURL *pCurl = NULL;
	FILE *psoFile = NULL;

	do {
		/* инициализация дескриптора */
		pCurl = curl_easy_init();
		if (NULL == pCurl) {
			iRetVal = CURLE_OUT_OF_MEMORY;
			UTL_LOG_E(p_coLog, "curl_easy_init: out of memory");
			break;
		}

		/* put transfer data into callbacks */
		std::string strFileName;
		if (p_soConf.m_strLocalDir.length()) {
			strFileName = p_soConf.m_strLocalDir;
			if (strFileName[strFileName.length() - 1] != '/') {
				strFileName += '/';
			}
		}
		strFileName += p_soConf.m_strLocalFileList;
		psoFile = fopen(strFileName.c_str(), "w");
		if (NULL == psoFile) {
			iRetVal = errno;
			UTL_LOG_E(p_coLog, "fopen: error code: '%d'", iFnRes);
			break;
		}
		iFnRes = curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, (void *)psoFile);
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
			break;
		}

		/* set an user name */
		iFnRes = curl_easy_setopt(pCurl, CURLOPT_USERNAME, p_soConf.m_strUserName.c_str());
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
			break;
		}

		/* set an password */
		iFnRes = curl_easy_setopt(pCurl, CURLOPT_PASSWORD, p_soConf.m_strUserPswd.c_str());
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
			break;
		}

		/* set an URL containing wildcard pattern (only in the last part) */
		std::string strURL;
		strURL = p_soConf.m_strProto + "://" + p_soConf.m_strHost + '/';
		if (p_strDir.length()) {
			strURL += p_strDir + "/";
		}
		iFnRes = curl_easy_setopt(pCurl, CURLOPT_URL, strURL.c_str());
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
			break;
		}

		/* если задан proxy host */
		if (p_soConf.m_strProxyHost.length()) {
			iFnRes = curl_easy_setopt(pCurl, CURLOPT_PROXY, p_soConf.m_strProxyHost.c_str());
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
				break;
			}
		}

		/* если задан proxy port */
		if (p_soConf.m_strProxyPort.length()) {
			long lPort = atol(p_soConf.m_strProxyPort.c_str());
			iFnRes = curl_easy_setopt(pCurl, CURLOPT_PROXYPORT, lPort);
			if (CURLE_OK != iFnRes) {
				iRetVal = iFnRes;
				UTL_LOG_E(p_coLog, "curl_easy_setopt: error code: '%d'", iFnRes);
				break;
			}
		}

		/* perform request */
		iFnRes = curl_easy_perform(pCurl);
		if (CURLE_OK != iFnRes) {
			iRetVal = iFnRes;
			UTL_LOG_E(p_coLog, "curl_easy_perform: error code: '%d'", iFnRes);
			break;
		}
	} while (0);

	/* освобождаем занятые ресурсы */
	if (NULL != pCurl) {
		curl_easy_cleanup(pCurl);
	}
	if (NULL != psoFile) {
		fclose(psoFile);
	}

	return iRetVal;
}

/******************************************************************************
Образец строки, содержащей информацию о элементе удаленной файловой системы
drwxr-x---   6 hpiumusr   iumusers        96 Nov  1 00:00 2013
******************************************************************************/
static const char g_mcFormat[] = "%s %u %s %s %u %s %u %u:%u %s";
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
int ParseFileList(SResolverConf &p_soConf, std::set<std::string> &p_setFileList)
{
	int iRetVal = 0;
	int iFnRes;
	FILE *psoFile = NULL;

	do {
		std::string strFileName;
		char mcBuf[1024];
		SFTPFileInfo soInfo;

		/* формируем имя файла, содержащего список файлов */
		if (p_soConf.m_strLocalDir.length()) {
			strFileName = p_soConf.m_strLocalDir;
			if (strFileName[strFileName.length() - 1] != '/') {
				strFileName += '/';
			}
		}
		strFileName += p_soConf.m_strLocalFileList;
		/* открываем файл, содержащий список файлов, на чтение */
		psoFile = fopen(strFileName.c_str(), "r");
		if (NULL == psoFile) {
			iRetVal = errno;
			break;
		}
		/* читаем файл построчно */
		while (fgets(mcBuf, sizeof(mcBuf), psoFile)) {
			iFnRes = sscanf(
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
				p_setFileList.insert(soInfo.m_mcFileName);
				break;
			}
		}
	} while (0);

	if (NULL != psoFile) {
		fclose(psoFile);
	}

	return iRetVal;
}
