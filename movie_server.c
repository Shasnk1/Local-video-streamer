#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <process.h>
#include <stdint.h>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

#define PORT 8080
#define MAX_PATH_LEN 4096
#define MAX_MOVIES 2048
#define MAX_CLIENTS 64
#define BUFFER_SIZE 8192
#define CACHE_DIR "cache"
#define WWW_DIR "www"
#define MOVIES_DIR "movies"

typedef struct {
    char filename[MAX_PATH_LEN];
    char title[MAX_PATH_LEN];
    char year[16];
    long long size;
    time_t mtime;
    int has_thumb;
} Movie;

typedef struct {
    Movie movies[MAX_MOVIES];
    int count;
    CRITICAL_SECTION lock;
    bool dirty;
    HANDLE watcher_thread;
    HANDLE stop_event;
} MovieLibrary;

MovieLibrary g_library = {0};
char g_local_ip[64] = "127.0.0.1";
char BASE_DIR[MAX_PATH_LEN] = ".";

char* build_path(const char* suffix) {
    static char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", BASE_DIR, suffix);
    return path;
}

const char* VIDEO_EXTS[] = {".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".m4v", NULL};

void init_winsock() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
}

void get_local_ip() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        struct sockaddr_in local = {0};
        int len = sizeof(local);
        if (getsockname(s, (struct sockaddr*)&local, &len) == 0) {
            strncpy(g_local_ip, inet_ntoa(local.sin_addr), sizeof(g_local_ip) - 1);
            g_local_ip[sizeof(g_local_ip) - 1] = '\0';
        }
    }
    closesocket(s);
}

bool is_video_file(const char* filename) {
    char* ext = strrchr(filename, '.');
    if (!ext) return false;
    for (int i = 0; VIDEO_EXTS[i]; i++) {
        if (_stricmp(ext, VIDEO_EXTS[i]) == 0) return true;
    }
    return false;
}

void parse_title_year(const char* filename, char* title, char* year) {
    char name[MAX_PATH_LEN];
    strncpy(name, filename, MAX_PATH_LEN - 1);
    name[MAX_PATH_LEN - 1] = '\0';

    char* dot = strrchr(name, '.');
    if (dot) *dot = '\0';

    char* y = strstr(name, " (");
    if (y && strlen(y) >= 6 && y[2] >= '0' && y[2] <= '9') {
        *y = '\0';
        strncpy(year, y + 2, 15);
        year[15] = '\0';
        char* paren = strchr(year, ')');
        if (paren) *paren = '\0';
    } else {
        year[0] = '\0';
    }

    for (char* p = name; *p; p++) {
        if (*p == '.' || *p == '_') *p = ' ';
    }

    while (strstr(name, "  ")) {
        char* dbl = strstr(name, "  ");
        memmove(dbl, dbl + 1, strlen(dbl));
    }

    strncpy(title, name, MAX_PATH_LEN - 1);
    title[MAX_PATH_LEN - 1] = '\0';
}

bool generate_thumbnail(const char* movie_path, const char* thumb_path) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -ss 00:01:00 -vframes 1 -vf scale=320:-1 \"%s\" 2>nul",
        movie_path, thumb_path);
    int result = system(cmd);
    return result == 0 && _access(thumb_path, 0) == 0;
}

void ensure_cache_dir() {
    CreateDirectoryA(build_path(CACHE_DIR), NULL);
}

char* get_thumb_path(const char* filename) {
    static char path[MAX_PATH_LEN];
    char hash[65] = {0};
    for (int i = 0; filename[i] && i < 32; i++) {
        unsigned char c = (unsigned char)(filename[i] ^ (i * 31));
        snprintf(hash + i * 2, sizeof(hash) - i * 2, "%02x", c);
    }
    snprintf(path, sizeof(path), "%s/%s.jpg", build_path(CACHE_DIR), hash);
    return path;
}

void scan_movies() {
    EnterCriticalSection(&g_library.lock);
    g_library.count = 0;

    char search_path[MAX_PATH_LEN];
    snprintf(search_path, sizeof(search_path), "%s\\*", build_path(MOVIES_DIR));

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_library.lock);
        return;
    }

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && is_video_file(fd.cFileName)) {
            Movie* m = &g_library.movies[g_library.count];
            snprintf(m->filename, sizeof(m->filename), "%s", fd.cFileName);
            parse_title_year(fd.cFileName, m->title, m->year);

            char full_path[MAX_PATH_LEN];
            snprintf(full_path, sizeof(full_path), "%s\\%s", build_path(MOVIES_DIR), fd.cFileName);

            struct _stat st;
            if (_stat(full_path, &st) == 0) {
                m->size = st.st_size;
                m->mtime = st.st_mtime;
            }

            char* thumb = get_thumb_path(fd.cFileName);
            m->has_thumb = (_access(thumb, 0) == 0) || generate_thumbnail(full_path, thumb);

            g_library.count++;
            if (g_library.count >= MAX_MOVIES) break;
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    g_library.dirty = false;
    LeaveCriticalSection(&g_library.lock);
}

DWORD WINAPI watcher_thread(LPVOID arg) {
    HANDLE hDir = CreateFileA(build_path(MOVIES_DIR), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (hDir == INVALID_HANDLE_VALUE) return 0;

    BYTE buffer[4096];
    DWORD bytes;
    while (WaitForSingleObject(g_library.stop_event, 0) != WAIT_OBJECT_0) {
        if (ReadDirectoryChangesW(hDir, buffer, sizeof(buffer), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
            &bytes, NULL, NULL)) {
            EnterCriticalSection(&g_library.lock);
            g_library.dirty = true;
            LeaveCriticalSection(&g_library.lock);
        }
        Sleep(500);
    }
    CloseHandle(hDir);
    return 0;
}

void start_watcher() {
    g_library.stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_library.watcher_thread = CreateThread(NULL, 0, watcher_thread, NULL, 0, NULL);
}

void stop_watcher() {
    if (g_library.stop_event) SetEvent(g_library.stop_event);
    if (g_library.watcher_thread) WaitForSingleObject(g_library.watcher_thread, 1000);
}

const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (_stricmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (_stricmp(ext, ".css") == 0) return "text/css";
    if (_stricmp(ext, ".js") == 0) return "application/javascript";
    if (_stricmp(ext, ".jpg") == 0 || _stricmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(ext, ".png") == 0) return "image/png";
    if (_stricmp(ext, ".svg") == 0) return "image/svg+xml";
    if (_stricmp(ext, ".mp4") == 0) return "video/mp4";
    if (_stricmp(ext, ".mkv") == 0) return "video/x-matroska";
    if (_stricmp(ext, ".webm") == 0) return "video/webm";
    return "application/octet-stream";
}

void send_response(SOCKET client, int status, const char* content_type, const void* body, int body_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n", status, content_type, body_len);
    send(client, header, header_len, 0);
    if (body_len > 0) send(client, body, body_len, 0);
}

void send_file(SOCKET client, const char* filepath, const char* range_header) {
    HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        send_response(client, 404, "text/plain", "Not Found", 9);
        return;
    }

    LARGE_INTEGER size;
    GetFileSizeEx(hFile, &size);
    long long file_size = size.QuadPart;

    long long start = 0, end = file_size - 1;
    bool has_range = false;

    if (range_header) {
        char* range = strstr(range_header, "bytes=");
        if (range) {
            range += 6;
            char* dash = strchr(range, '-');
            if (dash) {
                *dash = '\0';
                start = atoll(range);
                if (dash[1]) end = atoll(dash + 1);
                has_range = true;
            }
        }
    }

    if (start >= file_size) start = 0;
    if (end >= file_size) end = file_size - 1;
    long long content_len = end - start + 1;

    char header[512];
    int status = has_range ? 206 : 200;
    char range_hdr[128] = "";
    if (has_range) {
        snprintf(range_hdr, sizeof(range_hdr), "Content-Range: bytes %I64d-%I64d/%I64d\r\n", start, end, file_size);
    }
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %I64d\r\n"
        "Accept-Ranges: bytes\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        status, has_range ? "Partial Content" : "OK",
        get_mime_type(filepath),
        content_len,
        range_hdr
    );

    send(client, header, header_len, 0);

    LARGE_INTEGER li;
    li.QuadPart = start;
    SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);

    char buf[BUFFER_SIZE];
    DWORD read;
    long long remaining = content_len;
    while (remaining > 0) {
        DWORD to_read = (DWORD)min(BUFFER_SIZE, remaining);
        if (!ReadFile(hFile, buf, to_read, &read, NULL) || read == 0) break;
        send(client, buf, read, 0);
        remaining -= read;
    }
    CloseHandle(hFile);
}

void handle_api_movies(SOCKET client) {
    if (g_library.dirty) scan_movies();

    char* json = malloc(MAX_MOVIES * 512);
    char* p = json;
    p += sprintf(p, "{\"movies\":[");

    EnterCriticalSection(&g_library.lock);
    for (int i = 0; i < g_library.count; i++) {
        Movie* m = &g_library.movies[i];
        char thumb_url[256];
        if (m->has_thumb) {
            snprintf(thumb_url, sizeof(thumb_url), "/thumb/%s", m->filename);
        } else {
            thumb_url[0] = '\0';
        }

        p += sprintf(p, "%s{\"id\":%d,\"filename\":\"%s\",\"title\":\"%s\",\"year\":\"%s\",\"size\":%I64d,\"thumb\":\"%s\"}",
            i == 0 ? "" : ",", i, m->filename, m->title, m->year, m->size, thumb_url);
    }
    LeaveCriticalSection(&g_library.lock);

    p += sprintf(p, "],\"ip\":\"%s\"}", g_local_ip);
    send_response(client, 200, "application/json", json, p - json);
    free(json);
}

void handle_api_ip(SOCKET client) {
    char json[128];
    int len = snprintf(json, sizeof(json), "{\"ip\":\"%s\"}", g_local_ip);
    send_response(client, 200, "application/json", json, len);
}

void handle_thumb(SOCKET client, const char* path) {
    const char* filename = path + 7;
    char thumb_path[MAX_PATH_LEN];
    snprintf(thumb_path, sizeof(thumb_path), "%s", get_thumb_path(filename));

    if (_access(thumb_path, 0) == 0) {
        send_file(client, thumb_path, NULL);
    } else {
        char svg[512];
        int len = snprintf(svg, sizeof(svg),
            "<svg xmlns='http://www.w3.org/2000/svg' width='320' height='180' viewBox='0 0 320 180'>"
            "<rect fill='#1a1a2e' width='320' height='180'/>"
            "<text x='160' y='90' font-family='system-ui' font-size='14' fill='#888' text-anchor='middle' dominant-baseline='middle'>%s</text>"
            "</svg>", filename);
        send_response(client, 200, "image/svg+xml", svg, len);
    }
}

void handle_request(SOCKET client, const char* request) {
    char method[16], path[256], version[16];
    sscanf(request, "%15s %255s %15s", method, path, version);

    char* range_header = strstr(request, "Range:");
    if (range_header) {
        char* end = strstr(range_header, "\r\n");
        if (end) *end = '\0';
    }

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/index.html", build_path(WWW_DIR));
        send_file(client, filepath, NULL);
    }
    else if (strcmp(path, "/api/movies") == 0) {
        handle_api_movies(client);
    }
    else if (strcmp(path, "/api/ip") == 0) {
        handle_api_ip(client);
    }
    else if (strncmp(path, "/thumb/", 7) == 0) {
        handle_thumb(client, path);
    }
    else if (strncmp(path, "/movie/", 7) == 0) {
        const char* filename = path + 7;
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s\\%s", build_path(MOVIES_DIR), filename);
        send_file(client, filepath, range_header);
    }
    else {
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s%s", build_path(WWW_DIR), path);
        if (_access(filepath, 0) == 0) {
            send_file(client, filepath, NULL);
        } else {
            send_response(client, 404, "text/plain", "Not Found", 9);
        }
    }
}

unsigned __stdcall client_thread(void* arg) {
    SOCKET client = (SOCKET)(uintptr_t)arg;
    char buf[BUFFER_SIZE];
    int received = recv(client, buf, BUFFER_SIZE - 1, 0);
    if (received > 0) {
        buf[received] = '\0';
        handle_request(client, buf);
    }
    closesocket(client);
    _endthreadex(0);
    return 0;
}

int main() {
    init_winsock();
    get_local_ip();

    // Resolve paths relative to the executable's directory so the server
    // works no matter where it is launched from.
    char exe[MAX_PATH_LEN];
    if (GetModuleFileNameA(NULL, exe, sizeof(exe)) > 0) {
        strncpy(BASE_DIR, exe, sizeof(BASE_DIR) - 1);
        BASE_DIR[sizeof(BASE_DIR) - 1] = '\0';
        char* last = strrchr(BASE_DIR, '\\');
        if (last) *last = '\0';
        last = strrchr(BASE_DIR, '/');
        if (last) *last = '\0';
    }

    InitializeCriticalSection(&g_library.lock);
    ensure_cache_dir();
    CreateDirectoryA(MOVIES_DIR, NULL);
    CreateDirectoryA(WWW_DIR, NULL);

    scan_movies();
    start_watcher();

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server, (struct sockaddr*)&addr, sizeof(addr));
    listen(server, MAX_CLIENTS);

    printf("\n========================================\n");
    printf("  Movie Server Running\n");
    printf("  Local:   http://localhost:%d\n", PORT);
    printf("  Network: http://%s:%d\n", g_local_ip, PORT);
    printf("  Movies folder: %s\n", MOVIES_DIR);
    printf("  UI files in: %s (edit freely)\n", WWW_DIR);
    printf("========================================\n\n");
    printf("Waiting for connections...\n\n");

    while (1) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client = accept(server, (struct sockaddr*)&client_addr, &addr_len);
        if (client != INVALID_SOCKET) {
            _beginthreadex(NULL, 0, client_thread, (void*)(uintptr_t)client, 0, NULL);
        }
    }

    stop_watcher();
    closesocket(server);
    WSACleanup();
    DeleteCriticalSection(&g_library.lock);
    return 0;
}