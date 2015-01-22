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

/* ��������� ��� �������� ������������ ������ */
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
	/* ������ ���������� ���� � ��������. �� ��������� 3600 */
	unsigned int m_uiUpdateInterval;
#ifdef _WIN32
	SFileInfo m_soCURLInfo;
#endif
};

/* ��������� ��� �������� ������ ������ */
struct SResolverData {
	/* ��� */
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *m_pmapResolverCache;
	/* ������������ ������ */
	SResolverConf m_soConf;
#ifdef _WIN32
	/* ���������� �������� */
	HANDLE m_hSem;
	/* ���������� ������ ���������� ���� */
	HANDLE m_hThreadUpdateCache;
	/* ���������� �������� ������ ���������� ����, ������������ ���� ������� */
	HANDLE m_hSemTimer;
#else
	/* ������ �������� */
	sem_t m_tSem;
	int m_iSemInitialized;
	int m_iSemTimerInitialized;
	/* ������������� ������ ���������� ���� */
	pthread_t m_tThreadUpdateCache;
	/* ���������� �������� ������ ���������� ����, ������������ ���� ������� */
	sem_t m_tSemTimer;
#endif
	volatile int m_iContinueUpdate;
};

/* �������� ��� ����������� ����� */
int GetLastFileName (
	SResolverConf &p_soConf,
	std::string &p_strDir,
	std::string &p_strFileName);
/* ��������� ������ � ���������� ������� */
int resolver_load_data (
	SResolverConf &p_soConf,
	int *p_piUpdated);
/* ��������� ���� � ���������� ������� */
int DownloadFile (
	SResolverConf &p_soConf,
	std::string &p_strDir,
	std::string &p_strFileName);
/* ��������� ����, ���������� ���� ��������� */
int ParseNumberinPlanFile (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* ��������� ����, ���������� ���� ��������� */
int ParsePortFile (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* ��������� �� ���������� �� ����. ���� ���� ���������� ������� ���������� '0', � ��������� ������ ������� ���������� '-1' */
int IsFileNotExists (
	std::string &p_strDir,
	std::string &p_strFileTitle);
/* ��������� �� ����� ������ � �������� �� � ��� */
int resolver_cache (
	SResolverConf &p_soResConf,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* ��������� ��� */
int resolver_recreate_cache (SResolverData *p_psoResData);
/* ��������� ������ �� ����� ������������ ������ */
int resolver_load_conf (const char *p_pszConfFileName, SResolverConf &p_soResConf);
/* ��������� �������� � ��� */
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
/* ������������ �������� ������ */
int ExtractZipFile (
	SFileInfo &p_soUnZip,
	SFileInfo &p_soZipFile,
	SFileInfo &p_soOutput);
