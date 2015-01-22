#include "resolver-operations.h"
#include "resolver-auxiliary.h"

#ifdef _WIN32
#else
#	include <stdlib.h>
#	include <stdio.h>
#	include <errno.h>
#	include <string.h>
#endif

#ifdef _DEBUG
	CLog g_coLog;
#endif

bool operator < (const SOwnerData &p_soLeft, const SOwnerData &p_soRight)
{
	if (p_soLeft.m_uiCapacity < p_soRight.m_uiCapacity) {
		return true;
	} else {
		return false;
	}
}

int GetLastFileName (
	SResolverConf &p_soConf,
	std::string &p_strDir,
	std::string &p_strFileName)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;

	do {
		/* список файлов на удаленном сервере */
		std::multimap<std::string, SFileInfo> mmapFileList;
		/* инициализация структуры, необходимой для формирования списка файлов */
		SFileListInfo soFileListInfo = { p_soConf.m_strProto, p_soConf.m_strHost, p_soConf.m_strUserName, p_soConf.m_strUserPswd, p_strDir, &mmapFileList, 0 };

		/* объект класса для формирования списка файлов */
		CFileList coFileList;
		/* формирование списка файлов */
#ifdef _WIN32
		coFileList.CURL_Init (p_soConf.m_soCURLInfo);
#endif
		iFnRes = coFileList.CreateFileList (soFileListInfo);
#ifdef _WIN32
		coFileList.CURL_Cleanup ();
#endif
		/* проверка результата выполнения операции */
		if (iFnRes || 0 == mmapFileList.size ()) {
			if (iFnRes) {
				iRetVal = iFnRes;
			} else {
				iRetVal = -1;
			}
			break;
		}

		std::multimap<std::string, SFileInfo>::reverse_iterator iterMMap;

		iterMMap = mmapFileList.rbegin ();
		p_strFileName = iterMMap->second.m_strTitle;

		/* освобождаем ресурсы, занятые списком */
		mmapFileList.clear ();
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int resolver_load_data (
	SResolverConf &p_soConf,
	int *p_piUpdated)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;
	std::string strFileName;
	if (p_piUpdated) {
		*p_piUpdated = 0;
	}

	do {
		/* запрашиваем имя актуального файла, содержащего список перенесенных номеров */
		iFnRes = GetLastFileName (p_soConf, p_soConf.m_strPortDir, strFileName);
		if (iFnRes) {
			iRetVal = -1;
			break;
		}

		/* проверяем не существует ли такой файл */
		iFnRes = IsFileNotExists (p_soConf.m_strLocalDir, strFileName);
		if (iFnRes) {
			/* загужаем с удаленного сервера файл, содеражащий список перенесенных номеров */
			iFnRes = DownloadFile (p_soConf, p_soConf.m_strPortDir, strFileName);
			if (iFnRes) {
				iRetVal = -2;
				break;
			}

			/* распаковываем файл, содержащий список перенесенных номеров */
			SFileInfo
				soUnZip = { "unzip", "", 0 },
				soZipFile = { strFileName, p_soConf.m_strLocalDir, 0 },
				soCSVFile = { p_soConf.m_strLocalPortFile, p_soConf.m_strLocalDir, 0 };
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
		iFnRes = GetLastFileName (p_soConf, p_soConf.m_strNumPlanDir, strFileName);
		if (iFnRes) {
			iRetVal = -4;
			break;
		}

		/* проверяем существует ли такой файл */
		iFnRes = IsFileNotExists (p_soConf.m_strLocalDir, strFileName);
		/* если файл не существует */
		if (iFnRes) {
			/* загужаем с удаленного сервера файл, содеражащий план нумерации */
			iFnRes = DownloadFile (p_soConf, p_soConf.m_strNumPlanDir, strFileName);
			if (iFnRes) {
				iRetVal = -5;
				break;
			}

			/* распаковываем файл, содержащий план нумерации */
			SFileInfo
				soUnZip = { "unzip", "", 0 },
				soZipFile = { strFileName, p_soConf.m_strLocalDir, 0 },
				soCSVFile = { p_soConf.m_strLocalNumPlanFile, p_soConf.m_strLocalDir, 0 };
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

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int DownloadFile (
	SResolverConf &p_soConf,
	std::string &p_strDir,
	std::string &p_strFileName)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;
	SFileInfo soFileInfo = { p_strFileName, p_strDir, 0 };
	CFileReader coFileReader;

	std::string strType (p_soConf.m_strProto);
	std::string strHost (p_soConf.m_strHost);
	std::string strUser (p_soConf.m_strUserName);
	std::string strPasswd (p_soConf.m_strUserPswd);
	int iDataSize = 65536;
	unsigned char *pucmBuf = (unsigned char*) malloc (iDataSize);

	do {
		/* создаем класс для сохранения исходного файла на диске */
		CFileWriter coFileWriter;
		/* инициализация класса */
		iFnRes = coFileWriter.Init ();
		if (iFnRes) {
			iRetVal = iFnRes;
			if (pucmBuf) {
				free (pucmBuf);
				pucmBuf = NULL;
			}
			break;
		}

		/* открываем файл */
		iFnRes = coFileReader.OpenDataFile (
			strType,
#ifdef _WIN32
			p_soConf.m_soCURLInfo,
#endif
			soFileInfo,
			&strHost,
			&strUser,
			&strPasswd);
		/* если файл успешно открыт */
		if (0 == iFnRes) {
			/* формируем имя файла */
			std::string strOutputFile;
			if (p_soConf.m_strLocalDir.length ()) {
				/* если задана директрия */
				strOutputFile = p_soConf.m_strLocalDir;
				if (strOutputFile[strOutputFile.length () - 1] != '/' && strOutputFile[strOutputFile.length () - 1] != '\\') {
					strOutputFile += '/';
				}
			}
			strOutputFile += p_strFileName;
			/* создаем файл для сохранения исходных данных */
			iFnRes = coFileWriter.CreateOutputFile (strOutputFile.c_str ());
			/* проверяем результат открытия файла */
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
			int iDataRead;
			iDataRead = iDataSize;
			while (0 == coFileReader.ReadData (pucmBuf, iDataRead)) {
				iFnRes = coFileWriter.WriteData (pucmBuf, iDataRead);
				if (iFnRes) {
					iRetVal = iFnRes;
					break;
				}
				iDataRead = iDataSize;
			}
			coFileWriter.Finalise ();
			iFnRes = coFileReader.CloseDataFile ();
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
		}
	} while (0);

	if (pucmBuf) {
		free (pucmBuf);
		pucmBuf = NULL;
	}

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int ParseNumberinPlanFile (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;
	SOwnerData soResData = {0, 0, 0, "", 0, 0};
	char mcBuf[1024];
	FILE *psoFile;

	std::string strFileName;

	do {
		/* формируем имя файла */
		if (p_soResConf.m_strLocalDir.length ()) {
			strFileName = p_soResConf.m_strLocalDir;
			if (strFileName[strFileName.length () -1] != '/' && strFileName[strFileName.length () -1] != '\\') {
				strFileName += '/';
			}
		}
		strFileName += p_soResConf.m_strLocalNumPlanFile;

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

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int ParsePortFile (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;
	SOwnerData soResData = {0, 0, 0, "", 0, 0};
	char mcBuf[1024];
	FILE *psoFile;

	std::string strFileName;

	do {
		/* формируем имя файла */
		if (p_soResConf.m_strLocalDir.length ()) {
			strFileName = p_soResConf.m_strLocalDir;
			if (strFileName[strFileName.length () -1] != '/' && strFileName[strFileName.length () -1] != '\\') {
				strFileName += '/';
			}
		}
		strFileName += p_soResConf.m_strLocalPortFile;

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

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int IsFileNotExists (
	std::string &p_strDir,
	std::string &p_strFileTitle)
{
	ENTER_ROUT;

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
	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int resolver_cache (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;

	do {
		/* парсинг файла, содержащего план нумерации */
		iFnRes = ParseNumberinPlanFile (p_soResConf, p_mapCache);
		if (iFnRes) {
			iRetVal = -2;
			break;
		}

		/* парсинг файла, содержащего список перенесенных номеров */
		iFnRes = ParsePortFile (p_soResConf, p_mapCache);
		if (iFnRes) {
			iRetVal = -1;
			break;
		}
	} while (0);

	LEAVE_ROUT (iRetVal);

	return 0;
}

int resolver_recreate_cache (SResolverData *p_psoResData)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *pmapTmp =
		new std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > >;
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *pmapTmpOld;

	do {
		/* создаем временный экземпляр кэша */
		iFnRes = resolver_cache (p_psoResData->m_soConf, *pmapTmp);
		if (iFnRes || 0 == pmapTmp->size ()) {
			delete pmapTmp;
			iRetVal = -1;
			break;
		}

		/* ждем освобождения кэша всеми потоками */
		for (int i = 0; i < 256; ++i) {
#ifdef _WIN32
			WaitForSingleObject (p_psoResData->m_hSem, INFINITE);
#else
			sem_wait (&(p_psoResData->m_tSem));
#endif
		}

		/* запоминаем прежний кэш */
		pmapTmpOld = p_psoResData->m_pmapResolverCache;
		/* сохраняем новый кэш */
		p_psoResData->m_pmapResolverCache = pmapTmp;

		/* освобождаем семафор */
#ifdef _WIN32
		ReleaseSemaphore (p_psoResData->m_hSem, 256, NULL);
#else
		for (int i = 0; i < 256; ++i) {
			sem_post (&(p_psoResData->m_tSem));
		}
#endif

		/* освобождаем память, занятую прежним кэшем */
		pmapTmpOld->clear ();
		delete pmapTmpOld;
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int resolver_load_conf (const char *p_pszConfFileName, SResolverConf &p_soResConf)
{
	ENTER_ROUT;

	int iRetVal = 0;
	int iFnRes;
	CConfig coConf;
	std::string strParamValue;

	do {
		iFnRes = coConf.LoadConf (p_pszConfFileName);
		if (iFnRes) {
			iRetVal = -1;
			break;
		}

		iFnRes = coConf.GetParamValue ("numlex_host", strParamValue);
		if (iFnRes) {
			iRetVal = -2;
			break;
		}
		p_soResConf.m_strHost = strParamValue;

		iFnRes = coConf.GetParamValue ("numlex_user_name", strParamValue);
		if (iFnRes) {
			iRetVal = -3;
			break;
		}
		p_soResConf.m_strUserName = strParamValue;

		iFnRes = coConf.GetParamValue ("numlex_user_pswd", strParamValue);
		if (iFnRes) {
			iRetVal = -4;
			break;
		}
		p_soResConf.m_strUserPswd = strParamValue;

		iFnRes = coConf.GetParamValue ("numlex_proto_name", strParamValue);
		if (iFnRes) {
			iRetVal = -5;
			break;
		}
		p_soResConf.m_strProto = strParamValue;

		iFnRes = coConf.GetParamValue ("numlex_numplan_dir", strParamValue);
		if (iFnRes) {
			iRetVal = -7;
			break;
		}
		p_soResConf.m_strNumPlanDir = strParamValue;

		iFnRes = coConf.GetParamValue ("numlex_portnum_dir", strParamValue);
		if (iFnRes) {
			iRetVal = -8;
			break;
		}
		p_soResConf.m_strPortDir = strParamValue;

		iFnRes = coConf.GetParamValue ("local_cache_dir", strParamValue);
		if (iFnRes) {
			iRetVal = -9;
			break;
		}
		p_soResConf.m_strLocalDir = strParamValue;

		iFnRes = coConf.GetParamValue ("local_numplan_file", strParamValue);
		if (iFnRes) {
			iRetVal = -10;
			break;
		}
		p_soResConf.m_strLocalNumPlanFile = strParamValue;

		iFnRes = coConf.GetParamValue ("local_portnum_file", strParamValue);
		if (iFnRes) {
			iRetVal = -11;
			break;
		}
		p_soResConf.m_strLocalPortFile = strParamValue;

		iFnRes = coConf.GetParamValue ("update_interval", strParamValue);
		if (iFnRes) {
			iRetVal = -12;
			break;
		}
		p_soResConf.m_uiUpdateInterval = atol (strParamValue.c_str ());
		if (0 == p_soResConf.m_uiUpdateInterval) {
			p_soResConf.m_uiUpdateInterval = 3600;
		}

		iFnRes = coConf.GetParamValue ("log_file_mask", strParamValue);
		if (iFnRes) {
			iRetVal = -13;
			break;
		}
		p_soResConf.m_strLogFileMask = strParamValue;

#ifdef _WIN32
		p_soResConf.m_soCURLInfo.m_strDir = "C:/LIB/cURL";
		p_soResConf.m_soCURLInfo.m_strTitle = "libcurl.dll";
		p_soResConf.m_soCURLInfo.m_stFileSize = 0;
#endif
	} while (0);

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}

int InsertRange (
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_pmapCache,
	const char *p_pszFrom,
	const char *p_pszTo,
	SOwnerData &p_soOwnData)
{
	/*ENTER_ROUT;*/

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

	/*LEAVE_ROUT (iRetVal);*/

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
	/*ENTER_ROUT;*/

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

	/*LEAVE_ROUT (iRetVal);*/

	return iRetVal;
}

int ExtractZipFile (
	SFileInfo &p_soUnZip,
	SFileInfo &p_soZipFile,
	SFileInfo &p_soOutput)
{
	ENTER_ROUT;

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

#ifdef _WIN32
	/* инициализация структуры параметров процесса */
	STARTUPINFOA soStrartUpInfo;
	memset (&soStrartUpInfo, 0, sizeof (soStrartUpInfo));
	soStrartUpInfo.cb = sizeof (soStrartUpInfo);
	soStrartUpInfo.dwFlags = STARTF_USESTDHANDLES;
	soStrartUpInfo.hStdError = INVALID_HANDLE_VALUE;
	soStrartUpInfo.hStdInput = INVALID_HANDLE_VALUE;
	soStrartUpInfo.hStdOutput = INVALID_HANDLE_VALUE;

	do {
		PROCESS_INFORMATION soProcessInfo;

		soStrartUpInfo.hStdError = GetStdHandle (STD_ERROR_HANDLE);
		if (INVALID_HANDLE_VALUE == soStrartUpInfo.hStdError) {
			break;
		}
		soStrartUpInfo.hStdInput = GetStdHandle (STD_INPUT_HANDLE);
		if (INVALID_HANDLE_VALUE == soStrartUpInfo.hStdInput) {
			break;
		}
		SECURITY_ATTRIBUTES soSA;
		soSA.nLength = sizeof (soSA);
		soSA.bInheritHandle = TRUE;
		soSA.lpSecurityDescriptor = NULL;
		soStrartUpInfo.hStdOutput = CreateFileA (strOutputFile.c_str (), GENERIC_WRITE, FILE_SHARE_READ, &soSA, CREATE_ALWAYS, 0, NULL);
		if (INVALID_HANDLE_VALUE == soStrartUpInfo.hStdOutput) {
			break;
		}

		/* инициализация структуры информации о процессе */
		memset (&soProcessInfo, 0, sizeof (soProcessInfo));

		if (! CreateProcessA (
				NULL,
				(LPSTR) strCmdLine.c_str (),
				NULL,
				NULL,
				TRUE,
				0,
				NULL,
				NULL,
				&soStrartUpInfo,
				&soProcessInfo)) {
			iRetVal = GetLastError ();
			break;
		}
		WaitForSingleObject (soProcessInfo.hProcess, INFINITE);
		/* проверяем результат выполнения процесса */
		DWORD dwExitCode;
		GetExitCodeProcess (soProcessInfo.hProcess, &dwExitCode);
		/* освобождаем ресурсы, занятые дескрипторами */
		CloseHandle (soProcessInfo.hThread);
		CloseHandle (soProcessInfo.hProcess);
	} while (0);
	if (INVALID_HANDLE_VALUE != soStrartUpInfo.hStdError) {
		CloseHandle (soStrartUpInfo.hStdError);
	}
	if (INVALID_HANDLE_VALUE != soStrartUpInfo.hStdInput) {
		CloseHandle (soStrartUpInfo.hStdInput);
	}
	if (INVALID_HANDLE_VALUE != soStrartUpInfo.hStdOutput) {
		CloseHandle (soStrartUpInfo.hStdOutput);
	}
#else
	strCmdLine += " > \"";
	strCmdLine += strOutputFile;
	strCmdLine += '"';
	iRetVal = system (strCmdLine.c_str ());
#endif

	LEAVE_ROUT (iRetVal);

	return iRetVal;
}
