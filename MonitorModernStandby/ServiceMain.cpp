#include <Windows.h>
#include <tchar.h>
#include <string>
#include <ctime>
#include <PowrProf.h>
#pragma comment(lib,"PowrProf.lib")
#include <WinNT.h>
#include <Wtsapi32.h>
#pragma comment(lib,"Wtsapi32.lib")
#include "ServiceInstaller.h"

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
HANDLE				  g_MonitorPowerOnEvent = INVALID_HANDLE_VALUE;
HANDLE				  g_MonitorPowerOffEvent = INVALID_HANDLE_VALUE;
HANDLE				  g_MonitorShutDownEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv);
DWORD WINAPI ServiceCtrlHandlerEx(DWORD, DWORD, LPVOID, LPVOID);
DWORD WINAPI ServiceWorkerThread (LPVOID lpParam);
BOOL IsSupportConnectedStandby(void);

// Internal name of the service
#define SERVICE_NAME             L"MonitorSystemPowerState"
// Displayed name of the service
#define SERVICE_DISPLAY_NAME     L"Detect ModernStandby Service"
// Service start options.
#define SERVICE_START_TYPE       SERVICE_AUTO_START //SERVICE_DEMAND_START
// List of service dependencies - "dep1\0dep2\0\0"
#define SERVICE_DEPENDENCIES     L""
// The name of the account under which the service should run
#define SERVICE_ACCOUNT          NULL //L"NT AUTHORITY\\LocalService"
// The password to the service account name
#define SERVICE_PASSWORD         NULL

int _tmain (int argc, TCHAR *argv[])
{
	if ((argc > 1) && ((*argv[1] == L'-' || (*argv[1] == L'/'))))
	{
		if (_wcsicmp(L"install", argv[1] + 1) == 0)
		{
			// Install the service when the command is 
			// "-install" or "/install".
			InstallService(
				SERVICE_NAME,               // Name of service
				SERVICE_DISPLAY_NAME,       // Name to display
				SERVICE_START_TYPE,         // Service start type
				SERVICE_DEPENDENCIES,       // Dependencies
				SERVICE_ACCOUNT,            // Service running account
				SERVICE_PASSWORD            // Password of the account
			);
		}
		else if (_wcsicmp(L"remove", argv[1] + 1) == 0)
		{
			// Uninstall the service when the command is 
			// "-remove" or "/remove".
			UninstallService(SERVICE_NAME);
		}
	}
	else
	{
		OutputDebugString(_T("[Monitor ModernStandby] Main: Entry"));

		SERVICE_TABLE_ENTRY ServiceTable[] =
		{
			{SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
			{NULL, NULL}
		};

		if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
		{
			OutputDebugString(_T("[Monitor ModernStandby] Main: StartServiceCtrlDispatcher returned error"));
			return GetLastError();
		}
	}

    OutputDebugString(_T("[Monitor ModernStandby] Main: Exit"));
    return 0;
}

VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv)
{
    DWORD Status = E_FAIL;

    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: Entry"));

	// Create Monitor Power On/Off Event
	SECURITY_DESCRIPTOR    SD;
	SECURITY_ATTRIBUTES EventAttributes;
	EventAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	EventAttributes.lpSecurityDescriptor = &SD;
	EventAttributes.bInheritHandle = TRUE;
	InitializeSecurityDescriptor(&SD, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&SD, TRUE, (PACL)NULL, FALSE);

	g_MonitorPowerOnEvent = CreateEvent(&EventAttributes, TRUE, FALSE, L"Global\\MonitorPowerOnEvent");
	g_MonitorPowerOffEvent = CreateEvent(&EventAttributes, TRUE, FALSE, L"Global\\MonitorPowerOffEvent");
	g_MonitorShutDownEvent = CreateEvent(&EventAttributes, TRUE, FALSE, L"Global\\MonitorShutDownEvent");

#if 0
	BOOL bWTSNotify;
	WTS_SESSION_INFO *pInfos;
	DWORD count;
	WCHAR sInfo[256] = { 0 };
	bWTSNotify = WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, NULL, 1, &pInfos, &count);
	if (bWTSNotify == 0) {
		OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: WTSEnumerateSessions returned error"));
		goto EXIT;
	} else {
		for (DWORD i = 0; i < count; i++) {
			swprintf(sInfo, L"[Monitor ModernStandby] ServiceMain: Session %d (%d)\n", pInfos[i].SessionId, pInfos[i].State);
			OutputDebugString(sInfo);
			if (pInfos[i].State == WTSActive) {
				swprintf(sInfo, L"[Monitor ModernStandby] ServiceMain: pInfos[%d].SessionId = (%d)\n", i, pInfos[i].SessionId);
				OutputDebugString(sInfo);
			}
		}
		WTSFreeMemory(pInfos);
	}
#endif

    g_StatusHandle = RegisterServiceCtrlHandlerEx(SERVICE_NAME, ServiceCtrlHandlerEx, NULL);
    if (g_StatusHandle == NULL) 
    {
        OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: RegisterServiceCtrlHandlerEx returned error"));
        goto EXIT;
    }

    // Tell the service controller we are starting
    ZeroMemory (&g_ServiceStatus, sizeof (g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE) 
    {
        OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: SetServiceStatus returned error"));
    }

	/* Connected Standby Process */
	HPOWERNOTIFY hNotify = NULL;
	HPOWERNOTIFY hConsoleDisplayNotify = NULL;
	if (IsSupportConnectedStandby())
	{
		hNotify = RegisterPowerSettingNotification(g_StatusHandle, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_SERVICE_HANDLE);
		if (!hNotify)
			OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: GUID_MONITOR_POWER_ON PowerSettingNotification register err!!!!"));
		else
			OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: GUID_MONITOR_POWER_ON PowerSettingNotification register success!!!!"));

		hConsoleDisplayNotify = RegisterPowerSettingNotification(g_StatusHandle, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_SERVICE_HANDLE);
		if (!hConsoleDisplayNotify)
			OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: GUID_CONSOLE_DISPLAY_STATE PowerSettingNotification register err!!!!"));
		else
			OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: GUID_CONSOLE_DISPLAY_STATE PowerSettingNotification register success!!!!"));

#if 0
		BOOL bWTSSessionNotify;
		bWTSSessionNotify = WTSRegisterSessionNotification((HWND)g_StatusHandle, NOTIFY_FOR_ALL_SESSIONS);
		if (!bWTSSessionNotify)
			OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: WTSRegisterSessionNotificationEx register err!!!!"));
		else
			OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: WTSRegisterSessionNotificationEx register success!!!!"));
#endif
	}

    /* 
     * Perform tasks neccesary to start the service here
     */
    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: Performing Service Start Operations"));

    // Create stop event to wait on later.
    g_ServiceStopEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) 
    {
        OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: CreateEvent(g_ServiceStopEvent) returned error"));

        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;

        if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
	    {
		    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: SetServiceStatus returned error"));
	    }
        goto EXIT; 
    }    

    // Tell the service controller we are started
    //g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;//KennyKang 20200927
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE /*| SERVICE_ACCEPT_SHUTDOWN*/ | SERVICE_ACCEPT_PRESHUTDOWN;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
    {
	    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: SetServiceStatus returned error"));
    }

    // Start the thread that will perform the main task of the service
    HANDLE hThread = CreateThread (NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: Waiting for Worker Thread to complete"));

    // Wait until our worker thread exits effectively signaling that the service needs to stop
    WaitForSingleObject (hThread, INFINITE);
    
    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: Worker Thread Stop Event signaled"));
    
    
    /* 
     * Perform any cleanup tasks
     */
    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: Performing Cleanup Operations"));

    CloseHandle (g_ServiceStopEvent);
	CloseHandle (g_MonitorPowerOnEvent);
	CloseHandle (g_MonitorPowerOffEvent);
	CloseHandle (g_MonitorShutDownEvent);

    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;

    if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
    {
	    OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: SetServiceStatus returned error"));
    }
    
EXIT:
	if (hNotify)
		UnregisterPowerSettingNotification(hNotify);
	if (hConsoleDisplayNotify)
		UnregisterPowerSettingNotification(hConsoleDisplayNotify);
	OutputDebugString(_T("[Monitor ModernStandby] ServiceMain: Exit"));
    return;
}

//Converting a Ansi string to WChar string   
std::wstring Ansi2WChar(LPCSTR pszSrc, int nLen)
{
	int nSize = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, 0, 0);
	if (nSize <= 0) return NULL;
	WCHAR *pwszDst = new WCHAR[nSize + 1];
	if (NULL == pwszDst) return NULL;
	MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, pwszDst, nSize);
	pwszDst[nSize] = 0;
	if (pwszDst[0] == 0xFEFF) // skip Oxfeff   
		for (int i = 0; i < nSize; i++)
			pwszDst[i] = pwszDst[i + 1];
	std::wstring wcharString(pwszDst);
	delete pwszDst;
	return wcharString;
}

std::wstring s2ws(const std::string& s)
{
	return Ansi2WChar(s.c_str(), (int) s.size());
}

DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
	std::wstring stemp;
	std::string str;
	PPOWERBROADCAST_SETTING setting = (PPOWERBROADCAST_SETTING)lpEventData;
	PWTSSESSION_NOTIFICATION Notification = (PWTSSESSION_NOTIFICATION)lpEventData;

    OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: Entry"));
	switch (dwControl)
	{
		 case SERVICE_CONTROL_STOP :
			OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: SERVICE_CONTROL_STOP Request"));

			if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			   break;

			/* 
			 * Perform tasks neccesary to stop the service here 
			*/
			g_ServiceStatus.dwControlsAccepted = 0;
			g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
			g_ServiceStatus.dwWin32ExitCode = 0;
			g_ServiceStatus.dwCheckPoint = 4;

			if (SetServiceStatus (g_StatusHandle, &g_ServiceStatus) == FALSE)
			{
				OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: SetServiceStatus returned error"));
			}

			// This will signal the worker thread to start shutting down
			SetEvent (g_ServiceStopEvent);
			break;

		 //case SERVICE_CONTROL_SHUTDOWN:
		 //	 OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: SERVICE_CONTROL_SHUTDOWN Request"));
		 //	 SetEvent(g_MonitorShutDownEvent);
		 //	 break;

		 case SERVICE_CONTROL_PRESHUTDOWN:
			OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: SERVICE_CONTROL_PRESHUTDOWN Request"));
			SetEvent(g_MonitorShutDownEvent);
			break;

		 case SERVICE_CONTROL_POWEREVENT:
			 switch (dwEventType)
			 {
				 case PBT_APMRESUMEAUTOMATIC:
				 {
					 OutputDebugString(_T("[Monitor ModernStandby] PBT_APMRESUMEAUTOMATIC"));
					 break;
				 }

				 case PBT_APMRESUMESUSPEND:
				 {
					 OutputDebugString(_T("[Monitor ModernStandby] PBT_APMRESUMESUSPEND"));
					 break;
				 }

				 case PBT_APMSUSPEND:
				 {
					 OutputDebugString(_T("[Monitor ModernStandby] PBT_APMSUSPEND"));
					 break;
				 }

				 case PBT_APMPOWERSTATUSCHANGE: // 交流變電池 或者電池變交流
				 {
					 OutputDebugString(_T("[Monitor ModernStandby] PBT_APMPOWERSTATUSCHANGE"));
					 break;
				 }

				 case PBT_POWERSETTINGCHANGE:
				 {
					 OutputDebugString(_T("[Monitor ModernStandby] PBT_POWERSETTINGCHANGE"));
					 ResetEvent(g_MonitorPowerOnEvent);
					 ResetEvent(g_MonitorPowerOffEvent);

					 if (setting->PowerSetting == GUID_MONITOR_POWER_ON)
					 {
						 str = "GUID_MONITOR_POWER_ON ";
						 if (setting->DataLength == 4)
						 {
							 // MONITOR_POWER_OFF(data ==0)-->進入 connected standby狀態
							 // MONITOR_POWER_ON(data ==1)-->進入 active狀態
							 // MONITOR_POWER_OFF對應於設置裏面睡眠進入的時間, 而不是根據字面意思對應設置裏面的屏幕關閉時間, 這個要註意!!!
							 // 剛剛註冊GUID_MONITOR_POWER_ON成功後會立刻收到一次該事件, 報告data ==1
							 DWORD data = *(DWORD*)(setting->Data);
							 str += std::to_string(data);
							 if (data == 0) {
								 //MONITOR_POWER_OFF(data ==0)-->進入 connected standby狀態
								 SetEvent(g_MonitorPowerOffEvent);
								 OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: MONITOR_POWER_OFF"));
							 }
							 else {
								 //MONITOR_POWER_ON(data ==1)-->進入 active狀態
								 SetEvent(g_MonitorPowerOnEvent);
								 OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: MONITOR_POWER_ON"));
							 }
						 }
						 else
						 {
							 str += "len: ";
							 str += std::to_string(setting->DataLength);
						 }
						 OutputDebugString(s2ws(str).c_str());

						 // current date/time based on current system
						 time_t now = time(0);
						 tm *ltm = localtime(&now);

						 WCHAR sInfo[256] = { 0 };
						 swprintf(sInfo, L"[Monitor ModernStandby] [%02d/%02d %02d:%02d:%02d] %s", 1 + ltm->tm_mon, ltm->tm_mday, ltm->tm_hour, ltm->tm_min, ltm->tm_sec, s2ws(str).c_str());
						 OutputDebugString(sInfo);	 
					 }
					 else if (setting->PowerSetting == GUID_CONSOLE_DISPLAY_STATE) 
					 {
						 str = "GUID_CONSOLE_DISPLAY_STATE ";
						 DWORD data = *(DWORD*)(setting->Data);
						 str += std::to_string(data);

						 if (data == 0) 
							 OutputDebugString(_T("[Monitor ModernStandby] Display off"));
						 else if (data == 1)
							 OutputDebugString(_T("[Monitor ModernStandby] Display on"));
						 else if (data == 2) 
							 OutputDebugString(_T("[Monitor ModernStandby] Display dimmed"));

						 OutputDebugString(s2ws(str).c_str());
					 }
					 else
						 OutputDebugString(_T("[Monitor ModernStandby] Unknown GUID"));
					 break;
				 }

				 default:
					 break;
			 }
			 break;

		 case SERVICE_CONTROL_SESSIONCHANGE:
			 OutputDebugString(_T("[Monitor ModernStandby] SERVICE_CONTROL_SESSIONCHANGE"));
			 str = "SERVICE_CONTROL_SESSIONCHANGE: ";
			 str += std::to_string(dwEventType);

			 str += " ";
			 str += std::to_string(Notification->dwSessionId);
			 stemp = s2ws(str);
			 OutputDebugString(stemp.c_str());

			 switch (dwEventType) 
			 {
				 case WTS_CONSOLE_CONNECT:
					 OutputDebugString(_T("[Monitor ModernStandby] WTS_CONSOLE_CONNECT"));
					 break;
				 case WTS_CONSOLE_DISCONNECT:
					 OutputDebugString(_T("[Monitor ModernStandby] WTS_CONSOLE_DISCONNECT"));
					 break;
				 case WTS_SESSION_LOGON:
					 OutputDebugString(_T("[Monitor ModernStandby] WTS_SESSION_LOGON"));
					 break;
				 case WTS_SESSION_LOGOFF:
					 OutputDebugString(_T("[Monitor ModernStandby] WTS_SESSION_LOGOFF"));
					 break;
				 case WTS_SESSION_LOCK:
					 OutputDebugString(_T("[Monitor ModernStandby] WTS_SESSION_LOCK"));
					 break;
				 case WTS_SESSION_UNLOCK:
					 OutputDebugString(_T("[Monitor ModernStandby] WTS_SESSION_UNLOCK"));
					 break;
				 default:
					 break;
			 }
			 break;

		 default:
			 break;
    }

    OutputDebugString(_T("[Monitor ModernStandby] ServiceCtrlHandlerEx: Exit"));
	return NO_ERROR;
}

//S0 Low Power Idle
BOOL IsSupportConnectedStandby(void)                                                
{
	WCHAR sInfo[256] = { 0 };
	bool result = false;
	do {
		SYSTEM_POWER_CAPABILITIES info = { 0 };
		NTSTATUS ret = CallNtPowerInformation(SystemPowerCapabilities, NULL, 0, &info, sizeof(info));
		if (ret != 0)                      
		{
			swprintf(sInfo, L"[Monitor ModernStandby] Get Info Error: 0x%x\n", ret);
			OutputDebugString(sInfo);
			break;
		}
		if (info.AoAc == TRUE)
			result = true;
	} while (false);
	return result;
}

DWORD WINAPI ServiceWorkerThread (LPVOID lpParam)
{
    OutputDebugString(_T("[Monitor ModernStandby] ServiceWorkerThread: Entry"));
	DWORD WaitResult;

	HANDLE hWaitAry[4];
	hWaitAry[0] = g_ServiceStopEvent;
	hWaitAry[1] = g_MonitorPowerOnEvent;
	hWaitAry[2] = g_MonitorPowerOffEvent;
	hWaitAry[3] = g_MonitorShutDownEvent;

	while (1)
    {   
		//  Periodically check if the service has been requested to stop
		//WaitResult = WaitForSingleObject(g_ServiceStopEvent, INFINITE);
		WaitResult = ::WaitForMultipleObjects(4, hWaitAry, FALSE, INFINITE);
		if (WaitResult == WAIT_OBJECT_0) {
			OutputDebugString(_T("[Monitor ModernStandby] ServiceWorkerThread: g_ServiceStopEvent Trigger"));
			break;
		}
		else if (WaitResult == (WAIT_OBJECT_0 + 1)) {
			ResetEvent(g_MonitorPowerOnEvent);
			OutputDebugString(_T("[Monitor ModernStandby] ServiceWorkerThread: g_MonitorPowerOnEvent Trigger"));
		}
		else if (WaitResult == (WAIT_OBJECT_0 + 2)) {
			ResetEvent(g_MonitorPowerOffEvent);
			OutputDebugString(_T("[Monitor ModernStandby] ServiceWorkerThread: g_MonitorPowerOffEvent Trigger"));
		}
		if (WaitResult == WAIT_OBJECT_0 + 3) {
			OutputDebugString(_T("[Monitor ModernStandby] ServiceWorkerThread: g_MonitorShutDownEvent Trigger"));
			break;
		}
	}
	OutputDebugString(_T("[Monitor ModernStandby] ServiceWorkerThread: Exit"));
    return ERROR_SUCCESS;
}
