#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include "sample.h" //REMOVE

#include "Svc.h"

#pragma comment(lib, "advapi32.lib")

#define SVCNAME TEXT("AAA_TCB_HELPER")
#define LOGPATH "C:\\Users\\Danya\\source\\repos\\TCBhelper\\TCBhelper\\TCBhelper.log"
#define CMDPATH "C:\\Windows\\System32\\cmd.exe"

VOID SvcInstall(void);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR*);
BOOL SetPrivilege(HANDLE hToken, LPCTSTR lpszPrivilege, BOOL bEnablePrivilege);
FILE* gLog;
VOID LogError(LPCTSTR szFunction);

class TCBService {
private:
    HANDLE hThread = nullptr;
    HANDLE hThreadStartedEvent = nullptr;
    HANDLE hStopThreadEvent = nullptr;

    HANDLE hProcess, hProcessToken, hProcessTokenCopy;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    HANDLE hEventSource;

    //
    // Purpose: 
    //   Sets the current service status and reports it to the SCM.
    //
    // Parameters:
    //   dwCurrentState - The current state (see SERVICE_STATUS)
    //   dwWin32ExitCode - The system error code
    //   dwWaitHint - Estimated time for pending operation, 
    //     in milliseconds
    // 
    // Return value:
    //   None
    //
    VOID ReportSvcStatus(DWORD dwCurrentState,
        DWORD dwWin32ExitCode,
        DWORD dwWaitHint)
    {
        static DWORD dwCheckPoint = 1;

        // Fill in the SERVICE_STATUS structure.

        gSvcStatus.dwCurrentState = dwCurrentState;
        gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
        gSvcStatus.dwWaitHint = dwWaitHint;

        if (dwCurrentState == SERVICE_START_PENDING)
            gSvcStatus.dwControlsAccepted = 0;
        else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

        if ((dwCurrentState == SERVICE_RUNNING) ||
            (dwCurrentState == SERVICE_STOPPED))
            gSvcStatus.dwCheckPoint = 0;
        else gSvcStatus.dwCheckPoint = dwCheckPoint++;

        // Report the status of the service to the SCM.
        SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
    }

    //
    // Purpose: 
    //   Logs messages about Errors into the event log with Error severity
    //
    // Parameters:
    //   szFunction - name of function that failed
    // 
    // Return value:
    //   None
    //
    // Remarks:
    //   The service must have an entry in the Application event log.
    //
    VOID SvcReportError(LPTSTR szFunction)
    {
        LPCTSTR lpszStrings[2];
        TCHAR Buffer[80];

        hEventSource = RegisterEventSource(NULL, SVCNAME);

        if (NULL != hEventSource)
        {
            StringCchPrintf(Buffer, 80, TEXT("%s failed with %s"), szFunction, GetLastError());

            lpszStrings[0] = SVCNAME;
            lpszStrings[1] = Buffer;

            ReportEvent(hEventSource,        // event log handle
                EVENTLOG_ERROR_TYPE, // event type
                0,                   // event category
                SVC_ERROR,           // event identifier
                NULL,                // no security identifier
                2,                   // size of lpszStrings array
                0,                   // no binary data
                lpszStrings,         // array of strings
                NULL);               // no binary data

            DeregisterEventSource(hEventSource);
        }
    }

    //
    // Purpose: 
    //   Logs information to the event log with Notice severity
    //
    // Parameters:
    //   message - information to place into the log
    // 
    // Return value:
    //   None
    //
    // Remarks:
    //   The service must have an entry in the Application event log.
    //
    VOID SvcReportLogInfo()
    {
        LPCTSTR lpszStrings[2];
        TCHAR Buffer[80];

        hEventSource = RegisterEventSource(NULL, SVCNAME);

        if (NULL != hEventSource)
        {
            StringCchPrintf(Buffer, 80, TEXT("The log is placed at: %s"), TEXT(LOGPATH));

            lpszStrings[0] = SVCNAME;
            lpszStrings[1] = Buffer;

            ReportEvent(hEventSource,        // event log handle
                EVENTLOG_SUCCESS, // event type
                0,                   // event category
                SVC_NOTICE,           // event identifier
                NULL,                // no security identifier
                2,                   // size of lpszStrings array
                0,                   // no binary data
                lpszStrings,         // array of strings
                NULL);               // no binary data

            DeregisterEventSource(hEventSource);
        }
    }

    // Closes all the handles that service could use
    void CloseServiceHandles() {
        CloseHandle(hThread);
        CloseHandle(hThreadStartedEvent);
        CloseHandle(hStopThreadEvent);
        CloseHandle(hProcess);
        CloseHandle(hProcessToken);
        CloseHandle(hProcessTokenCopy);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(si.hStdError);
        CloseHandle(si.hStdInput);
        CloseHandle(si.hStdOutput);

        CloseHandle(hEventSource);
        CloseHandle(ghSvcStopEvent);
    }

public:
    SERVICE_STATUS          gSvcStatus;
    SERVICE_STATUS_HANDLE   gSvcStatusHandle;
    HANDLE                  ghSvcStopEvent = NULL;


    TCBService() {
        fopen_s(&gLog, LOGPATH, "a+");
        SvcReportLogInfo(); // Send path of a log file
        
        // Register the handler function for the service

        gSvcStatusHandle = RegisterServiceCtrlHandler(
            SVCNAME,
            SvcCtrlHandler);

        if (!gSvcStatusHandle)
        {
            SvcReportError((LPTSTR)(TEXT("RegisterServiceCtrlHandler")));
            return;
        }
        
        // Create an event. The control handler function, SvcCtrlHandler,
        // signals this event when it receives the stop control code.

        ghSvcStopEvent = CreateEvent(
            NULL,    // default security attributes
            TRUE,    // manual reset event
            FALSE,   // not signaled
            NULL);   // no name

        if (ghSvcStopEvent == NULL)
        {
            ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
            return;
        }

        // These SERVICE_STATUS members remain as set here

        gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        gSvcStatus.dwServiceSpecificExitCode = 0;

        // Report initial status to the SCM

        ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    }

    void run() {
        ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

        hProcess = GetCurrentProcess();
        hProcessToken = GetCurrentProcessToken();
        DWORD bSuccess = OpenProcessToken(hProcess, TOKEN_ALL_ACCESS, &hProcessToken);
        if (!bSuccess) {
            LogError(TEXT("OpenProcessToken"));
            return;
        }

        bSuccess = DuplicateTokenEx(hProcessToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &hProcessTokenCopy);
        if (!bSuccess) {
            LogError(TEXT("DuplicateTokenEx"));
            return;
        }

        // Enable TCB Privilege SE_TCB_NAME
        bSuccess = SetPrivilege(hProcessTokenCopy, SE_TCB_NAME, true);
        if (!bSuccess) {
            LogError(TEXT("SetPrivilege"));
            return;
        }

        DWORD sessionId = 1;
        bSuccess = SetTokenInformation(hProcessTokenCopy, _TOKEN_INFORMATION_CLASS::TokenSessionId, &sessionId, sizeof(DWORD));
        if (!bSuccess) {
            LogError(TEXT("SetTokenInformation"));
            return;
        }

        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));

        bSuccess = CreateProcessAsUserA(hProcessTokenCopy, NULL, (LPSTR)(CMDPATH), NULL, NULL, false, 0, NULL, NULL, &si, &pi);

        if (!bSuccess) {
            LogError(TEXT("CreateProcessAsUser"));
            return;
        }

        HANDLE handles[2];
        handles[0] = pi.hProcess; // The cmd was closed by the user
        handles[1] = ghSvcStopEvent; // Service was asked to stop

        WaitForMultipleObjects(2, handles, false, INFINITE);
        TerminateProcess(pi.hProcess, 0);

        StopService();
        return;
    }

    void StopService() {
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        SetEvent(gpService->ghSvcStopEvent); // signal service to stop
        
        DWORD res = WaitForSingleObject(pi.hProcess, 1000);

        if (res == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 0);
            LogError(TEXT("timeout, killing"));
        }
        else {
            LogError(TEXT("killed in time"));
        }

        CloseServiceHandles();
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
    }

    ~TCBService() {
        CloseServiceHandles();
    }
} *gpService;


//
// Purpose: 
//   Entry point for the process
//
// Parameters:
//   None
// 
// Return value:
//   None
//
void __cdecl service(int argc, TCHAR* argv[])
{
    // If command-line parameter is "install", install the service. 
    // Otherwise, the service is probably being started by the SCM.
    if (lstrcmpi(argv[1], TEXT("install")) == 0)
    {
        SvcInstall();
        return;
    }


    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { (LPWSTR)(SVCNAME), (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcher(DispatchTable))
    {
        LogError(TEXT("StartServiceCtrlDispatcher"));
        return;
    }
}

BOOL SetPrivilege(
    HANDLE hToken,          // access token handle
    LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
    BOOL bEnablePrivilege   // to enable or disable privilege
)
{
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!LookupPrivilegeValue(
        NULL,            // lookup privilege on local system
        lpszPrivilege,   // privilege to lookup 
        &luid))        // receives LUID of privilege
    {
        printf("LookupPrivilegeValue error: %u\n", GetLastError());
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    if (bEnablePrivilege)
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    else
        tp.Privileges[0].Attributes = 0;

    // Enable the privilege or disable all privileges.

    if (!AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tp,
        sizeof(TOKEN_PRIVILEGES),
        (PTOKEN_PRIVILEGES)NULL,
        (PDWORD)NULL))
    {
        printf("AdjustTokenPrivileges error: %u\n", GetLastError());
        return FALSE;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)

    {
        printf("The token does not have the specified privilege. \n");
        return FALSE;
    }

    return TRUE;
}

//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    gpService = new TCBService();
    gpService->run();
}

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
// 
// Return value:
//   None
//
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    // Handle the requested control code. 

    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        gpService->StopService();
        //ReportSvcStatus(gpService->gSvcStatus.dwCurrentState, NO_ERROR, 0);
        return;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    default:
        break;
    }
}


//
// Purpose: 
//   Installs a service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szPath[MAX_PATH];

    if (!GetModuleFileName(nullptr, szPath, MAX_PATH))
    {
        printf("Cannot install service (%d)\n", GetLastError());
        return;
    }

    // Get a handle to the SCM database. 

    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    // Create the service

    schService = CreateService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SVCNAME,                   // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_DEMAND_START,      // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 

    if (schService == NULL)
    {
        printf("CreateService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    else printf("Service installed successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

VOID LogError(LPCTSTR szFunction)
{
#define buffsize 160
    wchar_t buffer[buffsize];
    StringCchPrintf(buffer, buffsize, L"%s failed with %d", szFunction, GetLastError());
    if (gLog)
    {
        fprintf(gLog, "%ls \n", buffer);
    }
}