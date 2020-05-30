#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include "sample.h" //REMOVE

#include "Svc.h"

#pragma comment(lib, "advapi32.lib")

#define SVCNAME TEXT("AAA_TCB_HELPER")

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;

VOID SvcInstall(void);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR*);
VOID LogError(LPCTSTR szFunction);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR*);
VOID SvcReportEvent(LPTSTR);

VOID WriteToLog(const char* str);

HANDLE hThread = nullptr;
HANDLE hThreadStartedEvent = nullptr;
HANDLE hStopThreadEvent = nullptr;

FILE*  gLog;


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


DWORD WINAPI myThreadProc(_In_ LPVOID lpParameter) {
    HANDLE hEvent = (HANDLE)lpParameter;
    SetEvent(hEvent);
    //service();
    return 0;
}

bool myInit() {
    DWORD tid;

    // TODO: create hStopThreadEvent

    hThreadStartedEvent = CreateEvent(nullptr, true, false, nullptr);
    if (nullptr == hThreadStartedEvent) {
        //error
        return false;
    }

    hThread = CreateThread(nullptr, 0, myThreadProc, hThreadStartedEvent, 0, &tid);
    if (nullptr == hThread) {
        //error
        return false;
    }

    HANDLE handles[2] = { hThreadStartedEvent , hThread };
    DWORD res = WaitForMultipleObjects(2, handles, false, 60 * 1000);
    switch (res)
    {
    case 0:
        return true;
    case 1:
    case WAIT_TIMEOUT:
        // error/cleanup
        return false;
    }


    return true;
}

void myDone() {
    //check if handles initialized
    SetEvent(hStopThreadEvent);
    DWORD res = WaitForSingleObject(hThread, 60 * 1000);
    switch (res)
    {
    case 0:
        return;
    default:
        // error/cleanup
        // TerminateThread
        // CloseHandles
        return;
    }
}


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

    fopen_s(&gLog, "C:\\Users\\Danya\\source\\repos\\TCBhelper\\TCBhelper\\TCBhelper.log", "a+");
    if (lstrcmpi(argv[1], TEXT("install")) == 0)
    {
        printf("installing:\n");
        WriteToLog("installing:\n");
        SvcInstall(); //REMOVE
        return;
    }
    //SvcInstall(); //REMOVE


    // TO_DO: Add any additional services for the process to this table.
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { (LPWSTR)(SVCNAME), (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { NULL, NULL }
    };

    // This call returns when the service has stopped. 
    // The process should simply terminate when the call returns.

    printf("trying to run Main\n");
    WriteToLog("trying to run Main\n");

    if (!StartServiceCtrlDispatcher(DispatchTable))
    {
        LogError(TEXT("StartServiceCtrlDispatcher"));
        return;
    }
    else {
        LogError(TEXT("StartServiceCtrlDispatcher not"));
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
}//REMOVE

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
    printf("Main function is here\n");
    WriteToLog("Main function is here\n");
    // Register the handler function for the service

    gSvcStatusHandle = RegisterServiceCtrlHandler(
        SVCNAME,
        SvcCtrlHandler);

    if (!gSvcStatusHandle)
    {
        SvcReportEvent((LPTSTR)(TEXT("RegisterServiceCtrlHandler")));
        return;
    }
    else {
        SvcReportEvent((LPTSTR)(TEXT("RegisterServiceCtrlHandler STARTED!")));
    }

    // These SERVICE_STATUS members remain as set here

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;

    // Report initial status to the SCM

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    SvcReportEvent((LPTSTR)(TEXT("STARTED and working!")));

    // Perform service-specific initialization and work.

    SvcInit(dwArgc, lpszArgv);
}

//
// Purpose: 
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None
//
VOID SvcInit(DWORD dwArgc, LPTSTR* lpszArgv)
{
    printf("SvcInit is here\n");
    WriteToLog("SvcInit is here\n");
    // TO_DO: Declare and set any required variables.
    //   Be sure to periodically call ReportSvcStatus() with 
    //   SERVICE_START_PENDING. If initialization fails, call
    //   ReportSvcStatus with SERVICE_STOPPED.

    /*if (!myInit()) {
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }*/

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

    // Report running status when initialization is complete.

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);


    // TO_DO: Perform work until service stops.
    LogError(TEXT("Service will start process here..."));

    HANDLE hProcess = GetCurrentProcess();
    HANDLE hToken = GetCurrentProcessToken();
    DWORD bSuccess = OpenProcessToken(hProcess, TOKEN_ALL_ACCESS, &hToken);
    if (!bSuccess) {
        LogError(TEXT("OpenProcessToken"));
        return;
    }
    
    HANDLE hTokenCopy;
    bSuccess = DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &hTokenCopy);
    if (!bSuccess) {
        LogError(TEXT("DuplicateTokenEx"));
        return;
    }
    
    // EnableTCB Privilege SE_TCB_NAME
    bSuccess = SetPrivilege(hTokenCopy, SE_TCB_NAME, true);
    if (!bSuccess) {
        LogError(TEXT("SetPrivilege"));
        return;
    }

    DWORD sessionId = 1;
    bSuccess = SetTokenInformation(hTokenCopy, _TOKEN_INFORMATION_CLASS::TokenSessionId, &sessionId, sizeof(DWORD));
    if (!bSuccess) {
        LogError(TEXT("SetTokenInformation"));
        return;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    printf("Process creation:\n");
    WriteToLog("Process creation:\n");
    bSuccess = CreateProcessAsUserA(hTokenCopy, NULL, (LPSTR)("C:\\Windows\\System32\\cmd.exe"), NULL, NULL, false, 0, NULL, NULL, &si, &pi);
    
    if (!bSuccess) {
        LogError(TEXT("CreateProcessAsUser"));
        return;
    }


    while (1)
    {
        // Check whether to stop the service.

        // put server_step() here

        WaitForSingleObject(ghSvcStopEvent, INFINITE);
        printf("Service was asked to stop\n");

        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }
}

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
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        myDone();

        // Signal the service to stop.

        SetEvent(ghSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

        return;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    default:
        break;
    }

}

//
// Purpose: 
//   Logs messages to the event log
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
VOID SvcReportEvent(LPTSTR szFunction) //REMOVE
{
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    TCHAR Buffer[80];

    hEventSource = RegisterEventSource(NULL, SVCNAME);

    if (NULL != hEventSource)
    {
        StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

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

VOID WriteToLog(const char* str) {
    fprintf(gLog, "%ls \n", str);
}

// TODO: Good way: add message to eventLog "Service started, path t its log: ..."