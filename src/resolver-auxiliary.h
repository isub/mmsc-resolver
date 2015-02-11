#include "utils/log/log.h"

#define ENTER_ROUT(__coLog__)			__coLog__.WriteLog ("enter '%s'", __FUNCTION__)
#define LEAVE_ROUT(__coLog__,res_code)	__coLog__.WriteLog ("leave '%s'; result code: '%x'", __FUNCTION__, res_code)
#define CHECKPOINT(__coLog__)			__coLog__.WriteLog ("check point: thread: %X; function: '%s'; line: '%u';", pthread_self (), __FUNCTION__, __LINE__)

#include <string>
#include <map>
#include <set>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h> /* access */

/* ���������� � ����� */
struct SFileInfo {
	std::string m_strTitle; 
	std::string m_strDir;
	size_t m_stFileSize;
};

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
	std::string m_strLocalFileList;
	std::string m_strLogFileMask;
	std::string m_strProxyHost;
	std::string m_strProxyPort;
	/* ������ ���������� ���� � ��������. �� ��������� 3600 */
	unsigned int m_uiUpdateInterval;
	int m_iDebug;
};

/* ��������� ��� �������� ������ ������ */
struct SResolverData {
	/* ��� */
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *m_pmapResolverCache;
	/* ������ */
	CLog m_coLog;
	/* ������������ ������ */
	SResolverConf m_soConf;
	/* ������ �������� */
	sem_t m_tSemData;
	int m_iSemDataInitialized;
	/* ������������� ������ ���������� ���� */
	pthread_t m_tThreadUpdateCache;
	/* ���������� �������� ������ ���������� ����, ������������ ���� ������� */
	pthread_mutex_t m_tThreadMutex;
	int m_iMutexInitialized;
	volatile int m_iContinueUpdate;
	int m_iDebug;
};

/* �������� ��� ����������� ����� */
int GetLastFileName (
	SResolverConf &p_soConf,
	std::string &p_strDir,
	std::string &p_strFileName);
/* ��������� ������ � ���������� ������� */
int resolver_load_data (
	SResolverData *p_psoResData,
	int *p_piUpdated);
/* ��������� ���� � ���������� ������� */
int DownloadFile (
	SResolverData *p_psoResData,
	std::string &p_strDir,
	std::string &p_strFileName);
/* ��������� ����, ���������� ���� ��������� */
int ParseNumberinPlanFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* ��������� ����, ���������� ���� ��������� */
int ParsePortFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* ��������� �� ���������� �� ����. ���� ���� ���������� ������� ���������� '0', � ��������� ������ ������� ���������� '-1' */
int IsFileNotExists (
	std::string &p_strDir,
	std::string &p_strFileTitle);
/* ��������� �� ����� ������ � �������� �� � ��� */
int resolver_cache (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* ��������� ��� */
int resolver_recreate_cache (SResolverData *p_psoResData);
/* ��������� ������ �� ����� ������������ ������ */
int resolver_apply_settings (
	const char *p_pszSettings,
	SResolverConf &p_soResConf);
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
