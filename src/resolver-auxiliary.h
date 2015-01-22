#include "utils/filereader/filereader.h"
#include "utils/filewriter/FileWriter.h"
#include "utils/filelist/FileList.h"
#include "utils/config/Config.h"
#include "utils/log/Log.h"


#ifdef _DEBUG
	extern CLog g_coLog;
#	define ENTER_ROUT	g_coLog.WriteLog ("enter '%s'", __FUNCTION__)
#	define LEAVE_ROUT(res_code)	g_coLog.WriteLog ("leave '%s'; result code: '%x'", __FUNCTION__, res_code)
#else
#	define ENTER_ROUT
#	define LEAVE_ROUT(res_code)
#endif

#include <string>
#include <map>
#include <set>
#ifdef _WIN32
#	include <Windows.h>
#	include <io.h> /* access */
#else
#	include <semaphore.h>
#	include <unistd.h> /* access */
#endif

/* структура для хранения конфигурации модуля */
struct SResolverConf {
	std::string m_strHost;
	std::string m_strUserName;
	std::string m_strUserPswd;
	std::string m_strProto;
	std::string m_strNumPlanDir;
	std::string m_strPortDir;
	std::string m_strLocalDir;
	std::string m_strLocalNumPlanFile;
	std::string m_strLocalPortFile;
	std::string m_strLogFileMask;
	/* период обновления кэша в секундах. по умолчанию 3600 */
	unsigned int m_uiUpdateInterval;
#ifdef _WIN32
	SFileInfo m_soCURLInfo;
#endif
};

/* структура для хранения данных модуля */
struct SResolverData {
	/* кэш */
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *m_pmapResolverCache;
	/* конфигурация модуля */
	SResolverConf m_soConf;
#ifdef _WIN32
	/* дескриптор семафора */
	HANDLE m_hSem;
	/* дескриптор потока обновления кэша */
	HANDLE m_hThreadUpdateCache;
	/* дескриптом семафора потока обновления кэша, выполняющего роль таймера */
	HANDLE m_hSemTimer;
#else
	/* объект семафора */
	sem_t m_tSem;
	int m_iSemInitialized;
	int m_iSemTimerInitialized;
	/* идентификатор потока обновления кэша */
	pthread_t m_tThreadUpdateCache;
	/* дескриптом семафора потока обновления кэша, выполняющего роль таймера */
	sem_t m_tSemTimer;
#endif
	volatile int m_iContinueUpdate;
};

/* получает имя актуального файла */
int GetLastFileName (
	SResolverConf &p_soConf,
	std::string &p_strDir,
	std::string &p_strFileName);
/* загружает данные с удаленного сервера */
int resolver_load_data (
	SResolverConf &p_soConf,
	int *p_piUpdated);
/* загружает файл с удаленного сервера */
int DownloadFile (
	SResolverConf &p_soConf,
	std::string &p_strDir,
	std::string &p_strFileName);
/* разбирает файл, содержащий план нумерации */
int ParseNumberinPlanFile (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* разбирает файл, содержащий план нумерации */
int ParsePortFile (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* проверяет не существует ли файл. если файл существует функция возвращает '0', в противном случае функция возвращает '-1' */
int IsFileNotExists (
	std::string &p_strDir,
	std::string &p_strFileTitle);
/* считывает из файла данные и помещает их в кэш */
int resolver_cache (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* обновляет кэш */
int resolver_recreate_cache (SResolverData *p_psoResData);
/* считывает данные из файла конфигурации модуля */
int resolver_load_conf (const char *p_pszConfFileName, SResolverConf &p_soResConf);
/* добавляет диапазон в кэш */
int InsertRange (
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_pmapCache,
	const char *p_pszFrom,
	const char *p_pszTo,
	SOwnerData &p_soResData);
int InsertRangeDEF (
	std::map<unsigned int, std::multiset<SOwnerData> > *p_pmapCacheDEF,
	unsigned int p_uiFromDEF,
	unsigned int p_uiFromGHIJ,
	unsigned int p_uiToDEF,
	unsigned int p_uiToGHIJ,
	SOwnerData &p_soResData);
/* декомпрессия исходных данных */
int ExtractZipFile (
	SFileInfo &p_soUnZip,
	SFileInfo &p_soZipFile,
	SFileInfo &p_soOutput);
