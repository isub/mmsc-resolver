#include "resolver-operations.h"
#include "resolver-auxiliary.h"
#include "utils/ipconnector/ipconnector.h"
#include "utils/tcp_listener/tcp_listener.h"

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

static struct STCPListener *g_psoTCPLsnr;
static int mmsc_resolver_tcp_cb( const struct SAcceptedSock *p_psoAcceptedSocket );

static CLog *g_pcoLog;
static SResolverData *g_psoResolvData;

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

    psoResData->m_soConf.m_bIsMainService = mmsc_resolver_is_main_service( psoResData );
    if ( psoResData->m_soConf.m_bIsMainService ) {
		  /* создаем семафор доступа к файлам numlex */
		  psoResData->m_ptNumlexSem = sem_open(SEM_NAME, O_CREAT, S_IRWXU, 1);
		  /* если семафор уже создан открыаем его */
		  if (SEM_FAILED == psoResData->m_ptNumlexSem && EACCES == errno) {
			  psoResData->m_ptNumlexSem = sem_open (SEM_NAME, 0, S_IRWXU, 1);
		  }
		  if (SEM_FAILED == psoResData->m_ptNumlexSem) {
			  resolver_fini (psoResData);
			  psoResData = NULL;
        UTL_LOG_E( psoResData->m_coLog, "error: %s:%u", __FUNCTION__, __LINE__ );
			  break;
		  }

		  /* инициализируем семафор для ожидания потока обновления кэша, создаем его запертым */
		  iFnRes = sem_init (&psoResData->m_tThreadSem, 0, 0);
		  if (iFnRes) {
			  resolver_fini (psoResData);
			  psoResData = NULL;
        UTL_LOG_E( psoResData->m_coLog, "error: %s:%u", __FUNCTION__, __LINE__ );
        break;
		  }

		  /* создаем семафор доступа к кэшу */
		  iFnRes = sem_init (&psoResData->m_tCacheSem, 0, 256);
		  if (iFnRes) {
			  resolver_fini (psoResData);
			  psoResData = NULL;
        UTL_LOG_E( psoResData->m_coLog, "error: %s:%u", __FUNCTION__, __LINE__ );
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
        UTL_LOG_E( psoResData->m_coLog, "error: %s:%u", __FUNCTION__, __LINE__ );
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
        UTL_LOG_E( psoResData->m_coLog, "error: %s:%u", __FUNCTION__, __LINE__ );
        break;
		  }
     /* сервис работает в качестве главного сервиса - сервера */
      int iErrLine;
      g_psoTCPLsnr = tcp_listener_init( "127.0.0.1", 9999, 32, 32, mmsc_resolver_tcp_cb, &iErrLine );
      if ( NULL != g_psoTCPLsnr ) {
      } else {
        UTL_LOG_E( psoResData->m_coLog, "can not initialize tcp_listener: error in line: %d", iErrLine );
        psoResData = NULL;
        break;
      }
    } else {
      /* сервис работает в качестве клиента */
    }

    g_pcoLog = &psoResData->m_coLog;
    g_psoResolvData = psoResData;
	} while (0);

	if (psoResData) {
    psoResData->m_coLog.WriteLog(
      "%s: mms resovler module is initialized successfully as %s",
      __FUNCTION__,
      psoResData->m_soConf.m_bIsMainService ? "server" : "client" );
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

const char * resolver_resolve (
	const char *p_pszPhoneNum,
	const void *p_pModuleData)
{
  const char *pszRetVal;
  SOwnerData * psoRetVal = NULL;
  SResolverData *psoResData = ( SResolverData * )p_pModuleData;
  static char mcResult[ 32 ];

  if ( NULL != psoResData ) {
  } else {
    return NULL;
  }

  if ( psoResData->m_soConf.m_bIsMainService ) {
    pszRetVal = mmsc_resolver_resolve( psoResData, p_pszPhoneNum );
  } else {
    CIPConnector coIPConn( 10 );
    int iFnRes;

    if ( 0 == coIPConn.Connect( "127.0.0.1", 9999, IPPROTO_TCP ) ) {
    } else {
      return NULL;
    }
    iFnRes = coIPConn.Send( p_pszPhoneNum, strlen( p_pszPhoneNum ) );
    if ( 0 == iFnRes ) {
    } else {
      coIPConn.DisConnect();
      return NULL;
    }
    iFnRes = coIPConn.Recv( mcResult, sizeof( mcResult ) );
    if ( 0 < iFnRes ) {
      if ( iFnRes < sizeof( mcResult ) - 1 ) {
        mcResult[ iFnRes ] = '\0';
        pszRetVal = mcResult;
      } else {
        return NULL;
      }
    } else {
      coIPConn.DisConnect();
      return NULL;
    }
    coIPConn.DisConnect();
  }

	return pszRetVal;
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

static int mmsc_resolver_tcp_cb( const struct SAcceptedSock *p_psoAcceptedSocket )
{
  int iRetVal = 0;
  int iFnRes;
  char mcData[ 32 ];
  const char *pszResult;

  if ( NULL != g_pcoLog ) {
    g_pcoLog->WriteLog( "connection is accepted (%s:%hu)", p_psoAcceptedSocket->m_mcIPAddress, p_psoAcceptedSocket->m_usPort );
  }

  iFnRes = recv( p_psoAcceptedSocket->m_iAcceptedSock, mcData, sizeof( mcData ), 0 );
  if ( 0 < iFnRes ) {
    if ( iFnRes < sizeof( mcData ) - 1 ) {
      mcData[ iFnRes ] = '\0';
    } else {
      mcData[ sizeof( mcData ) - 1 ] = '\0';
    }
  } else if ( 0 == iFnRes ) {
    if ( NULL != g_pcoLog ) {
      g_pcoLog->WriteLog( "connection is closed (%s:%hu)", p_psoAcceptedSocket->m_mcIPAddress, p_psoAcceptedSocket->m_usPort );
      return iRetVal;
    }
  } else {
    g_pcoLog->WriteLog( "connection error code %d (%s:%hu)", errno, p_psoAcceptedSocket->m_mcIPAddress, p_psoAcceptedSocket->m_usPort );
    return iRetVal;
  }
  pszResult = mmsc_resolver_resolve( g_psoResolvData, mcData );
  if ( NULL != pszResult ) {
    iFnRes = send( p_psoAcceptedSocket->m_iAcceptedSock, pszResult, strlen( pszResult ) + 1, 0 );
  } else {
    iFnRes = send( p_psoAcceptedSocket->m_iAcceptedSock, "", 1, 0 );
  }

  if ( NULL != g_pcoLog ) {
    if ( 0 < iFnRes ) {
      g_pcoLog->WriteLog( "%d bytes is sent to %s:%hu", iFnRes, p_psoAcceptedSocket->m_mcIPAddress, p_psoAcceptedSocket->m_usPort );
    } else {
      g_pcoLog->WriteLog( "connection error code %d (%s:%hu)", errno, p_psoAcceptedSocket->m_mcIPAddress, p_psoAcceptedSocket->m_usPort );
    }
  }

  return iRetVal;
}
