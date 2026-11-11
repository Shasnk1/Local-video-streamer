#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

#define PORT 8080
#define IDC_STATUS 100
#define IDC_URL    101
#define IDC_START  102
#define IDC_STOP   103
#define IDC_OPEN   104

static HWND g_hMain = NULL;
static HANDLE g_hServer = NULL;
static DWORD g_serverPid = 0;
static BOOL g_running = FALSE;
static char g_exePath[MAX_PATH];

static HWND g_hStatus = NULL;
static HWND g_hUrl = NULL;
static HWND g_hStart = NULL;
static HWND g_hStop = NULL;
static HWND g_hOpen = NULL;

/* True if something is listening on localhost:PORT. */
static BOOL port_open(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return FALSE;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { WSACleanup(); return FALSE; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);
    int r = connect(s, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(s);
    WSACleanup();
    return r == 0;
}

/* Resolve the movie_server.exe path next to this exe (or from %1). */
static void FindServerExe(void) {
    if (__argc >= 2) {
        strncpy(g_exePath, __argv[1], MAX_PATH - 1);
        g_exePath[MAX_PATH - 1] = '\0';
        return;
    }
    GetModuleFileNameA(NULL, g_exePath, MAX_PATH);
    char* last = strrchr(g_exePath, '\\');
    if (last) *(last + 1) = '\0';
    else g_exePath[0] = '\0';
    strcat(g_exePath, "movie_server.exe");
}

static void UpdateStatus(void) {
    if (g_running) {
        SetWindowTextA(g_hStatus, "Status: Running");
        SetWindowTextA(g_hStart, "Start Server");
        EnableWindow(g_hStart, FALSE);
        EnableWindow(g_hStop, TRUE);
        EnableWindow(g_hOpen, TRUE);
    } else {
        SetWindowTextA(g_hStatus, "Status: Stopped");
        SetWindowTextA(g_hStart, "Start Server");
        EnableWindow(g_hStart, TRUE);
        EnableWindow(g_hStop, FALSE);
        EnableWindow(g_hOpen, FALSE);
    }
}

static void StartServer(void) {
    if (g_running) return;
    if (port_open()) {
        g_running = TRUE;
        UpdateStatus();
        return;
    }
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessA(g_exePath, NULL, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        g_hServer = pi.hProcess;
        g_serverPid = pi.dwProcessId;
        for (int i = 0; i < 40 && !g_running; i++) {
            if (port_open()) g_running = TRUE;
            Sleep(100);
        }
        if (!g_running) {
            TerminateProcess(g_hServer, 1);
            WaitForSingleObject(g_hServer, 2000);
            CloseHandle(g_hServer);
            g_hServer = NULL;
            g_serverPid = 0;
            MessageBoxA(g_hMain,
                "The server failed to start.\n\n"
                "Check that movie_server.exe exists next to this program\n"
                "and that port 8080 is not already in use.",
                "Movie Server", MB_OK | MB_ICONWARNING);
        }
        UpdateStatus();
    } else {
        DWORD err = GetLastError();
        char msg[256];
        snprintf(msg, sizeof(msg),
            "Could not start the server.\n\nError %d\nFile: %s",
            err, g_exePath);
        MessageBoxA(g_hMain, msg, "Movie Server", MB_OK | MB_ICONERROR);
        UpdateStatus();
    }
}

static void StopServer(void) {
    if (!g_running) return;
    if (g_hServer) {
        TerminateProcess(g_hServer, 1);
        WaitForSingleObject(g_hServer, 3000);
        CloseHandle(g_hServer);
        g_hServer = NULL;
    }
    g_serverPid = 0;
    g_running = FALSE;
    UpdateStatus();
}

static void OpenBrowser(void) {
    ShellExecuteA(NULL, "open",
        "http://localhost:8080", NULL, NULL, SW_SHOWDEFAULT);
}

static LRESULT CALLBACK WndProcTrader...