#ifndef _RESOLVER_AUXILIARY_H_
#define _RESOLVER_AUXILIARY_H_

#include "utils/log/log.h"
#include "resolver-operations.h"

#include <string>
#include <map>
#include <set>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h> /* access */

#define SEM_NAME "/mmsc-resolver.update.locer"

/* ��������� ������������� �������� ������������� openSSL */
void init_locks (void);
/* ��������� ������������ �������� �������� ������������� openSSL */
void kill_locks (void);

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
};

/* ��������� ��� �������� ������ ������ */
struct SResolverData {
	/* ��� */
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > *m_pmapResolverCache;
	/* ������ */
	CLog m_coLog;
	/* ������������ ������ */
	SResolverConf m_soConf;
	/* ������ �������� ��� ������� � ������ numlex */
	sem_t *m_ptNumlexSem;
	/* ������ �������� ��� ������� � ���� */
	sem_t m_tCacheSem;
	/* ������������� ������ ���������� ���� */
	pthread_t m_tThreadUpdateCache;
	/* ���������� �������� ������ ���������� ����, ������������ ���� ������� */
	sem_t m_tThreadSem;
	volatile int m_iContinueUpdate;
};

/* ��������� ����, ���������� ���� ��������� */
int ParseNumberinPlanFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
/* ��������� ����, ���������� ���� ��������� */
int ParsePortFile (
	SResolverData *p_psoResData,
	std::map<unsigned int,std::map<unsigned int,std::multiset<SOwnerData> > > &p_mapCache);
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

#endif /* _RESOLVER_AUXILIARY_H_ */