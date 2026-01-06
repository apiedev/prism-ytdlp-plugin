/*
 * Prism yt-dlp Plugin - Resolver Implementation
 *
 * URL resolution using yt-dlp process.
 *
 * License: Unlicense (Public Domain)
 */

#include "prism_ytdlp_plugin.h"
#include <prism/prism_resolver.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shlobj.h>
    #include <urlmon.h>
    #pragma comment(lib, "urlmon.lib")
    #pragma comment(lib, "shell32.lib")
    /* strtok_r is strtok_s on Windows */
    #define strtok_r strtok_s
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/stat.h>
    #include <errno.h>
    #include <pthread.h>
#endif

/* ============================================================================
 * Configuration
 * ========================================================================== */

#define YTDLP_PROCESS_TIMEOUT_MS 30000
#define YTDLP_OUTPUT_BUFFER_SIZE 8192
#define YTDLP_GITHUB_RELEASES "https://github.com/yt-dlp/yt-dlp/releases/latest/download/"

/* Known hosts that yt-dlp can resolve */
static const char* s_known_hosts[] = {
    "youtube.com", "youtu.be", "www.youtube.com", "m.youtube.com",
    "twitch.tv", "www.twitch.tv", "clips.twitch.tv",
    "vimeo.com", "www.vimeo.com", "player.vimeo.com",
    "dailymotion.com", "www.dailymotion.com",
    "facebook.com", "www.facebook.com", "fb.watch", "m.facebook.com",
    "twitter.com", "x.com", "mobile.twitter.com",
    "instagram.com", "www.instagram.com",
    "tiktok.com", "www.tiktok.com", "vm.tiktok.com",
    "reddit.com", "www.reddit.com", "v.redd.it",
    "streamable.com",
    "soundcloud.com", "www.soundcloud.com",
    "bandcamp.com",
    "bilibili.com", "www.bilibili.com",
    "nicovideo.jp", "www.nicovideo.jp",
    "rumble.com", "www.rumble.com",
    "odysee.com", "www.odysee.com",
    "kick.com", "www.kick.com",
    NULL
};

/* Global configuration */
static struct {
    char ytdlp_path[1024];
    char install_dir[1024];
    bool auto_download;
    int process_timeout_ms;
    bool initialized;
    bool download_attempted;
} g_config = {
    .ytdlp_path = {0},
    .install_dir = {0},
    .auto_download = true,
    .process_timeout_ms = YTDLP_PROCESS_TIMEOUT_MS,
    .initialized = false,
    .download_attempted = false
};

/* ============================================================================
 * Internal Types
 * ========================================================================== */

typedef struct YtdlpResolver {
    PrismResolver base;
    bool is_available;
} YtdlpResolver;

typedef struct ProcessResult {
    char* output;
    char* error;
    int exit_code;
} ProcessResult;

/* ============================================================================
 * String Utilities
 * ========================================================================== */

static char* str_dup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static void str_to_lower(char* s) {
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

static char* str_trim(char* s) {
    if (!s) return NULL;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*s)) s++;

    if (*s == 0) return s;

    /* Trim trailing whitespace */
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return s;
}

static bool str_contains(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != NULL;
}

/* ============================================================================
 * URL Parsing
 * ========================================================================== */

static bool extract_host(const char* url, char* host, size_t host_size) {
    if (!url || !host || host_size == 0) return false;

    const char* start = url;

    /* Skip protocol */
    const char* proto = strstr(url, "://");
    if (proto) {
        start = proto + 3;
    }

    /* Skip user:pass@ */
    const char* at = strchr(start, '@');
    if (at) {
        start = at + 1;
    }

    /* Find end of host (port, path, query, or end) */
    const char* end = start;
    while (*end && *end != ':' && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }

    size_t len = (size_t)(end - start);
    if (len >= host_size) len = host_size - 1;

    memcpy(host, start, len);
    host[len] = '\0';
    str_to_lower(host);

    return len > 0;
}

/* ============================================================================
 * Process Execution (Platform-specific)
 * ========================================================================== */

#ifdef _WIN32

static ProcessResult run_process(const char* command, const char* args, int timeout_ms) {
    ProcessResult result = {0};
    result.exit_code = -1;

    HANDLE stdout_read = NULL, stdout_write = NULL;
    HANDLE stderr_read = NULL, stderr_write = NULL;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    /* Create pipes for stdout and stderr */
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
        result.error = str_dup("Failed to create pipes");
        goto cleanup;
    }

    /* Don't inherit read handles */
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    /* Build command line */
    char cmdline[4096];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", command, args);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = stdout_write;
    si.hStdError = stderr_write;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        result.error = str_dup("Failed to create process");
        goto cleanup;
    }

    /* Close write ends in parent */
    CloseHandle(stdout_write); stdout_write = NULL;
    CloseHandle(stderr_write); stderr_write = NULL;

    /* Wait for process with timeout */
    DWORD wait_result = WaitForSingleObject(pi.hProcess, (DWORD)timeout_ms);

    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.error = str_dup("Process timed out");
        goto cleanup_process;
    }

    /* Get exit code */
    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = (int)exit_code;

    /* Read stdout */
    char buffer[YTDLP_OUTPUT_BUFFER_SIZE];
    DWORD bytes_read;
    size_t total_stdout = 0;
    result.output = (char*)malloc(YTDLP_OUTPUT_BUFFER_SIZE);
    if (result.output) {
        result.output[0] = '\0';
        while (ReadFile(stdout_read, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
            buffer[bytes_read] = '\0';
            size_t new_len = total_stdout + bytes_read;
            char* new_buf = (char*)realloc(result.output, new_len + 1);
            if (new_buf) {
                result.output = new_buf;
                memcpy(result.output + total_stdout, buffer, bytes_read + 1);
                total_stdout = new_len;
            }
        }
    }

    /* Read stderr */
    size_t total_stderr = 0;
    result.error = (char*)malloc(YTDLP_OUTPUT_BUFFER_SIZE);
    if (result.error) {
        result.error[0] = '\0';
        while (ReadFile(stderr_read, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
            buffer[bytes_read] = '\0';
            size_t new_len = total_stderr + bytes_read;
            char* new_buf = (char*)realloc(result.error, new_len + 1);
            if (new_buf) {
                result.error = new_buf;
                memcpy(result.error + total_stderr, buffer, bytes_read + 1);
                total_stderr = new_len;
            }
        }
    }

    /* If exit code is 0 and error is empty, set error to NULL */
    if (result.exit_code == 0 && result.error && result.error[0] == '\0') {
        free(result.error);
        result.error = NULL;
    }

cleanup_process:
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

cleanup:
    if (stdout_read) CloseHandle(stdout_read);
    if (stdout_write) CloseHandle(stdout_write);
    if (stderr_read) CloseHandle(stderr_read);
    if (stderr_write) CloseHandle(stderr_write);

    return result;
}

static bool file_exists(const char* path) {
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static const char* get_platform_binary_name(void) {
    return "yt-dlp.exe";
}

static void get_default_install_dir(char* path, size_t size) {
    char appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata))) {
        snprintf(path, size, "%s\\Prism", appdata);
    } else {
        snprintf(path, size, "C:\\Prism");
    }
}

#else /* POSIX */

static ProcessResult run_process(const char* command, const char* args, int timeout_ms) {
    ProcessResult result = {0};
    result.exit_code = -1;

    int stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        result.error = str_dup("Failed to create pipes");
        return result;
    }

    pid_t pid = fork();

    if (pid < 0) {
        result.error = str_dup("Failed to fork process");
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        /* Child process */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        /* Parse args into argv array (simple tokenization) */
        char* args_copy = str_dup(args);
        char* argv[64];
        int argc = 0;

        argv[argc++] = (char*)command;

        if (args_copy) {
            char* token = strtok(args_copy, " ");
            while (token && argc < 62) {
                /* Handle quoted strings */
                if (token[0] == '"') {
                    token++;
                    char* end = strchr(token, '"');
                    if (end) *end = '\0';
                }
                argv[argc++] = token;
                token = strtok(NULL, " ");
            }
        }
        argv[argc] = NULL;

        execvp(command, argv);
        _exit(127);
    }

    /* Parent process */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Wait with timeout */
    int status;
    int elapsed = 0;
    const int interval = 100;

    while (elapsed < timeout_ms) {
        pid_t wpid = waitpid(pid, &status, WNOHANG);
        if (wpid == pid) break;
        if (wpid < 0) {
            result.error = str_dup("waitpid failed");
            goto cleanup;
        }
        usleep(interval * 1000);
        elapsed += interval;
    }

    if (elapsed >= timeout_ms) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        result.error = str_dup("Process timed out");
        goto cleanup;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }

    /* Read stdout */
    char buffer[YTDLP_OUTPUT_BUFFER_SIZE];
    ssize_t bytes_read;
    size_t total_stdout = 0;
    result.output = (char*)malloc(YTDLP_OUTPUT_BUFFER_SIZE);
    if (result.output) {
        result.output[0] = '\0';
        while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            size_t new_len = total_stdout + (size_t)bytes_read;
            char* new_buf = (char*)realloc(result.output, new_len + 1);
            if (new_buf) {
                result.output = new_buf;
                memcpy(result.output + total_stdout, buffer, (size_t)bytes_read + 1);
                total_stdout = new_len;
            }
        }
    }

    /* Read stderr */
    size_t total_stderr = 0;
    result.error = (char*)malloc(YTDLP_OUTPUT_BUFFER_SIZE);
    if (result.error) {
        result.error[0] = '\0';
        while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            size_t new_len = total_stderr + (size_t)bytes_read;
            char* new_buf = (char*)realloc(result.error, new_len + 1);
            if (new_buf) {
                result.error = new_buf;
                memcpy(result.error + total_stderr, buffer, (size_t)bytes_read + 1);
                total_stderr = new_len;
            }
        }
    }

    if (result.exit_code == 0 && result.error && result.error[0] == '\0') {
        free(result.error);
        result.error = NULL;
    }

cleanup:
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    return result;
}

static bool file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

static const char* get_platform_binary_name(void) {
#ifdef __APPLE__
    return "yt-dlp_macos";
#else
    return "yt-dlp";
#endif
}

static void get_default_install_dir(char* path, size_t size) {
    const char* home = getenv("HOME");
    if (home) {
        snprintf(path, size, "%s/.local/bin", home);
    } else {
        snprintf(path, size, "/tmp/prism");
    }
}

#endif

static void free_process_result(ProcessResult* result) {
    if (result->output) {
        free(result->output);
        result->output = NULL;
    }
    if (result->error) {
        free(result->error);
        result->error = NULL;
    }
}

/* ============================================================================
 * yt-dlp Detection and Download
 * ========================================================================== */

static bool find_ytdlp(char* path, size_t path_size) {
    /* Check common locations */
    const char* candidates[] = {
#ifdef _WIN32
        "C:\\Program Files\\yt-dlp\\yt-dlp.exe",
        "C:\\yt-dlp\\yt-dlp.exe",
        "C:\\ProgramData\\Prism\\yt-dlp.exe",
#else
        "/usr/local/bin/yt-dlp",
        "/usr/bin/yt-dlp",
        "/opt/homebrew/bin/yt-dlp",
#endif
        NULL
    };

    /* Check install directory first */
    if (g_config.install_dir[0]) {
        char install_path[1024];
        snprintf(install_path, sizeof(install_path), "%s/%s",
                 g_config.install_dir, get_platform_binary_name());
        if (file_exists(install_path)) {
            strncpy(path, install_path, path_size - 1);
            path[path_size - 1] = '\0';
            return true;
        }
    }

    /* Check default install directory */
    char default_dir[1024];
    get_default_install_dir(default_dir, sizeof(default_dir));
    char default_path[1024];
    snprintf(default_path, sizeof(default_path), "%s/%s",
             default_dir, get_platform_binary_name());
    if (file_exists(default_path)) {
        strncpy(path, default_path, path_size - 1);
        path[path_size - 1] = '\0';
        return true;
    }

    /* Check system paths */
    for (int i = 0; candidates[i]; i++) {
        if (file_exists(candidates[i])) {
            strncpy(path, candidates[i], path_size - 1);
            path[path_size - 1] = '\0';
            return true;
        }
    }

    /* Check PATH environment */
#ifdef _WIN32
    char env_path[4096];
    if (GetEnvironmentVariableA("PATH", env_path, sizeof(env_path))) {
        char* saveptr = NULL;
        char* dir = strtok_s(env_path, ";", &saveptr);
        while (dir) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s\\yt-dlp.exe", dir);
            if (file_exists(full_path)) {
                strncpy(path, full_path, path_size - 1);
                path[path_size - 1] = '\0';
                return true;
            }
            dir = strtok_s(NULL, ";", &saveptr);
        }
    }
#else
    const char* env_path = getenv("PATH");
    if (env_path) {
        char* path_copy = str_dup(env_path);
        if (path_copy) {
            char* dir = strtok(path_copy, ":");
            while (dir) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/yt-dlp", dir);
                if (file_exists(full_path)) {
                    strncpy(path, full_path, path_size - 1);
                    path[path_size - 1] = '\0';
                    free(path_copy);
                    return true;
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
    }
#endif

    return false;
}

static void ensure_directory_exists(const char* dir) {
#ifdef _WIN32
    CreateDirectoryA(dir, NULL);
#else
    mkdir(dir, 0755);
#endif
}

/* ============================================================================
 * Download Implementation
 * ========================================================================== */

#ifdef _WIN32

PrismError prism_ytdlp_download(
    const char* install_dir,
    void (*progress_callback)(float progress, void* user_data),
    void* user_data
) {
    char target_dir[1024];
    if (install_dir && install_dir[0]) {
        strncpy(target_dir, install_dir, sizeof(target_dir) - 1);
    } else {
        get_default_install_dir(target_dir, sizeof(target_dir));
    }
    target_dir[sizeof(target_dir) - 1] = '\0';

    ensure_directory_exists(target_dir);

    char target_path[1024];
    snprintf(target_path, sizeof(target_path), "%s\\%s",
             target_dir, get_platform_binary_name());

    char url[512];
    snprintf(url, sizeof(url), "%s%s", YTDLP_GITHUB_RELEASES, get_platform_binary_name());

    /* Use URLDownloadToFile */
    HRESULT hr = URLDownloadToFileA(NULL, url, target_path, 0, NULL);

    if (progress_callback) {
        progress_callback(1.0f, user_data);
    }

    if (SUCCEEDED(hr) && file_exists(target_path)) {
        /* Update global path */
        strncpy(g_config.ytdlp_path, target_path, sizeof(g_config.ytdlp_path) - 1);
        return PRISM_OK;
    }

    return PRISM_ERROR_NETWORK;
}

#else /* POSIX */

PrismError prism_ytdlp_download(
    const char* install_dir,
    void (*progress_callback)(float progress, void* user_data),
    void* user_data
) {
    char target_dir[1024];
    if (install_dir && install_dir[0]) {
        strncpy(target_dir, install_dir, sizeof(target_dir) - 1);
    } else {
        get_default_install_dir(target_dir, sizeof(target_dir));
    }
    target_dir[sizeof(target_dir) - 1] = '\0';

    ensure_directory_exists(target_dir);

    char target_path[1024];
    snprintf(target_path, sizeof(target_path), "%s/%s",
             target_dir, get_platform_binary_name());

    char url[512];
    snprintf(url, sizeof(url), "%s%s", YTDLP_GITHUB_RELEASES, get_platform_binary_name());

    /* Use curl to download */
    char curl_args[1024];
    snprintf(curl_args, sizeof(curl_args), "-L -o \"%s\" \"%s\"", target_path, url);

    ProcessResult result = run_process("curl", curl_args, 120000); /* 2 minute timeout */

    if (progress_callback) {
        progress_callback(1.0f, user_data);
    }

    free_process_result(&result);

    if (!file_exists(target_path)) {
        return PRISM_ERROR_NETWORK;
    }

    /* Make executable */
    chmod(target_path, 0755);

    /* Update global path */
    strncpy(g_config.ytdlp_path, target_path, sizeof(g_config.ytdlp_path) - 1);

    return PRISM_OK;
}

#endif

/* ============================================================================
 * Public API
 * ========================================================================== */

PRISM_YTDLP_API bool prism_ytdlp_is_available(void) {
    if (g_config.ytdlp_path[0]) {
        return file_exists(g_config.ytdlp_path);
    }

    char path[1024];
    if (find_ytdlp(path, sizeof(path))) {
        strncpy(g_config.ytdlp_path, path, sizeof(g_config.ytdlp_path) - 1);
        return true;
    }

    return false;
}

PRISM_YTDLP_API const char* prism_ytdlp_get_path(void) {
    if (!prism_ytdlp_is_available()) {
        return NULL;
    }
    return g_config.ytdlp_path;
}

PRISM_YTDLP_API void prism_ytdlp_configure(const PrismYtdlpConfig* config) {
    if (!config) return;

    if (config->ytdlp_path) {
        strncpy(g_config.ytdlp_path, config->ytdlp_path, sizeof(g_config.ytdlp_path) - 1);
    }

    if (config->install_dir) {
        strncpy(g_config.install_dir, config->install_dir, sizeof(g_config.install_dir) - 1);
    }

    g_config.auto_download = config->auto_download;

    if (config->process_timeout_ms > 0) {
        g_config.process_timeout_ms = config->process_timeout_ms;
    }
}

/* ============================================================================
 * Resolver Implementation
 * ========================================================================== */

static bool ensure_ytdlp_available(void) {
    if (prism_ytdlp_is_available()) {
        return true;
    }

    if (!g_config.auto_download || g_config.download_attempted) {
        return false;
    }

    g_config.download_attempted = true;
    return prism_ytdlp_download(NULL, NULL, NULL) == PRISM_OK;
}

static bool ytdlp_can_resolve(PrismResolver* resolver, const char* url) {
    (void)resolver;

    if (!url || !url[0]) return false;

    char host[256];
    if (!extract_host(url, host, sizeof(host))) {
        return false;
    }

    for (int i = 0; s_known_hosts[i]; i++) {
        if (str_contains(host, s_known_hosts[i]) || str_contains(s_known_hosts[i], host)) {
            return true;
        }
    }

    return false;
}

static PrismResolvedStream* ytdlp_resolve(
    PrismResolver* resolver,
    const char* url,
    const PrismResolverOptions* options
) {
    (void)resolver;

    PrismResolvedStream* stream = (PrismResolvedStream*)calloc(1, sizeof(PrismResolvedStream));
    if (!stream) return NULL;

    if (!url) {
        stream->success = false;
        stream->error = str_dup("URL is NULL");
        return stream;
    }

    stream->original_url = str_dup(url);

    if (!ensure_ytdlp_available()) {
        stream->success = false;
        stream->error = str_dup("yt-dlp not available");
        return stream;
    }

    /* Get quality from options */
    PrismStreamQuality quality = options ? options->quality : PRISM_QUALITY_AUTO;
    stream->requested_quality = quality;

    /* Build format argument based on quality */
    char format_arg[256];
    int height = 0;

    switch (quality) {
        case PRISM_QUALITY_LOW:    height = 360; break;
        case PRISM_QUALITY_MEDIUM: height = 480; break;
        case PRISM_QUALITY_HIGH:   height = 720; break;
        case PRISM_QUALITY_FULL:   height = 1080; break;
        case PRISM_QUALITY_QHD:    height = 1440; break;
        case PRISM_QUALITY_4K:     height = 2160; break;
        default:
            /* For numeric values like 360, 720, etc., use directly */
            if (quality > 0 && quality <= 4320) {
                height = (int)quality;
            }
            break;
    }

    /* Check if live stream first */
    char args[1024];
    snprintf(args, sizeof(args), "--no-warnings --no-check-certificate --print is_live \"%s\"", url);

    ProcessResult live_check = run_process(g_config.ytdlp_path, args, g_config.process_timeout_ms);
    bool is_live = false;
    if (live_check.output) {
        char* trimmed = str_trim(live_check.output);
        str_to_lower(trimmed);
        is_live = strcmp(trimmed, "true") == 0;
    }
    free_process_result(&live_check);

    stream->is_live = is_live;

    /* Build format string */
    if (is_live) {
        if (height > 0) {
            snprintf(format_arg, sizeof(format_arg),
                "best[height<=%d][protocol!=m3u8]/best[height<=%d][protocol!=m3u8_native]/best[height<=%d]",
                height, height, height);
        } else {
            snprintf(format_arg, sizeof(format_arg),
                "best[protocol!=m3u8]/best[protocol!=m3u8_native]/best");
        }
    } else {
        if (height > 0) {
            snprintf(format_arg, sizeof(format_arg),
                "bestvideo[height<=%d][ext=mp4][protocol!=m3u8]+bestaudio[ext=m4a]/best[height<=%d][ext=mp4][protocol!=m3u8]/best[height<=%d][ext=mp4]/best[ext=mp4]/best",
                height, height, height);
        } else {
            snprintf(format_arg, sizeof(format_arg),
                "bestvideo[ext=mp4][protocol!=m3u8]+bestaudio[ext=m4a]/best[ext=mp4][protocol!=m3u8]/best[ext=mp4]/best");
        }
    }

    /* Get direct URL */
    snprintf(args, sizeof(args),
        "--no-warnings --no-check-certificate -f \"%s\" --get-url \"%s\"",
        format_arg, url);

    ProcessResult url_result = run_process(g_config.ytdlp_path, args, g_config.process_timeout_ms);

    if (url_result.exit_code != 0 || !url_result.output || url_result.output[0] == '\0') {
        const char* error_msg = url_result.error ? url_result.error : "Failed to resolve URL";
        stream->success = false;
        stream->error = str_dup(error_msg);
        free_process_result(&url_result);
        return stream;
    }

    char* direct_url = str_trim(url_result.output);
    stream->direct_url = str_dup(direct_url);
    free_process_result(&url_result);

    if (!stream->direct_url) {
        stream->success = false;
        stream->error = str_dup("Out of memory");
        return stream;
    }

    /* Check if HLS */
    stream->is_hls = str_contains(stream->direct_url, ".m3u8") ||
                     str_contains(stream->direct_url, "m3u8");

    /* Get additional info (title, resolution) */
    snprintf(args, sizeof(args),
        "--no-warnings --no-check-certificate --print title --print width --print height \"%s\"",
        url);

    ProcessResult info_result = run_process(g_config.ytdlp_path, args, g_config.process_timeout_ms);

    if (info_result.output) {
        char* lines = info_result.output;
        char* saveptr = NULL;

        /* Title */
        char* line = strtok_r(lines, "\r\n", &saveptr);
        if (line) {
            stream->title = str_dup(str_trim(line));
        }

        /* Width */
        line = strtok_r(NULL, "\r\n", &saveptr);
        if (line) {
            stream->width = atoi(str_trim(line));
        }

        /* Height */
        line = strtok_r(NULL, "\r\n", &saveptr);
        if (line) {
            stream->height = atoi(str_trim(line));
        }
    }
    free_process_result(&info_result);

    stream->success = true;
    stream->has_video = true;
    stream->has_audio = true;

    return stream;
}

static void ytdlp_destroy(PrismResolver* resolver) {
    if (resolver) {
        free(resolver);
    }
}

static PrismError ytdlp_ensure_available(
    PrismResolver* resolver,
    PrismResolverProgressCallback progress,
    void* user_data
) {
    (void)resolver;

    if (prism_ytdlp_is_available()) {
        if (progress) progress(user_data, 1.0f, "yt-dlp available");
        return PRISM_OK;
    }

    if (progress) progress(user_data, 0.0f, "Downloading yt-dlp...");

    PrismError err = prism_ytdlp_download(NULL, NULL, NULL);

    if (err == PRISM_OK) {
        if (progress) progress(user_data, 1.0f, "yt-dlp downloaded");
    }

    return err;
}

static PrismError ytdlp_update_tool(
    PrismResolver* resolver,
    PrismResolverProgressCallback progress,
    void* user_data
) {
    (void)resolver;

    if (!prism_ytdlp_is_available()) {
        return ytdlp_ensure_available(resolver, progress, user_data);
    }

    if (progress) progress(user_data, 0.0f, "Updating yt-dlp...");

    /* Run yt-dlp -U to self-update */
    ProcessResult result = run_process(g_config.ytdlp_path, "-U", g_config.process_timeout_ms);

    PrismError err = (result.exit_code == 0) ? PRISM_OK : PRISM_ERROR_NETWORK;
    free_process_result(&result);

    if (progress) progress(user_data, 1.0f, err == PRISM_OK ? "Updated" : "Update failed");

    return err;
}

static PrismResolvedStream* ytdlp_probe(PrismResolver* resolver, const char* url) {
    (void)resolver;

    PrismResolvedStream* stream = (PrismResolvedStream*)calloc(1, sizeof(PrismResolvedStream));
    if (!stream) return NULL;

    if (!url) {
        stream->success = false;
        stream->error = str_dup("URL is NULL");
        return stream;
    }

    stream->original_url = str_dup(url);

    if (!ensure_ytdlp_available()) {
        stream->success = false;
        stream->error = str_dup("yt-dlp not available");
        return stream;
    }

    /* Get basic info without resolving URL */
    char args[1024];
    snprintf(args, sizeof(args),
        "--no-warnings --no-check-certificate --print title --print is_live --print duration \"%s\"",
        url);

    ProcessResult result = run_process(g_config.ytdlp_path, args, g_config.process_timeout_ms);

    if (result.exit_code != 0) {
        stream->success = false;
        stream->error = str_dup(result.error ? result.error : "Probe failed");
        free_process_result(&result);
        return stream;
    }

    if (result.output) {
        char* lines = result.output;
        char* saveptr = NULL;

        /* Title */
        char* line = strtok_r(lines, "\r\n", &saveptr);
        if (line) {
            stream->title = str_dup(str_trim(line));
        }

        /* is_live */
        line = strtok_r(NULL, "\r\n", &saveptr);
        if (line) {
            char* trimmed = str_trim(line);
            str_to_lower(trimmed);
            stream->is_live = strcmp(trimmed, "true") == 0;
        }

        /* Duration */
        line = strtok_r(NULL, "\r\n", &saveptr);
        if (line) {
            stream->duration = atof(str_trim(line));
        }
    }

    free_process_result(&result);

    stream->success = true;
    return stream;
}

static const char* ytdlp_get_tool_version(PrismResolver* resolver) {
    (void)resolver;

    static char version[64] = {0};

    if (!prism_ytdlp_is_available()) {
        return NULL;
    }

    ProcessResult result = run_process(g_config.ytdlp_path, "--version", 5000);

    if (result.exit_code == 0 && result.output) {
        char* trimmed = str_trim(result.output);
        strncpy(version, trimmed, sizeof(version) - 1);
        version[sizeof(version) - 1] = '\0';
    }

    free_process_result(&result);

    return version[0] ? version : NULL;
}

static void ytdlp_set_tool_path(PrismResolver* resolver, const char* path) {
    (void)resolver;

    if (path) {
        strncpy(g_config.ytdlp_path, path, sizeof(g_config.ytdlp_path) - 1);
        g_config.ytdlp_path[sizeof(g_config.ytdlp_path) - 1] = '\0';
    } else {
        g_config.ytdlp_path[0] = '\0';
    }
}

static bool ytdlp_is_available(PrismResolver* resolver) {
    (void)resolver;
    return prism_ytdlp_is_available() || g_config.auto_download;
}

/* ============================================================================
 * Resolver VTable and Factory
 * ========================================================================== */

static const PrismResolverVTable s_ytdlp_vtable = {
    .destroy = ytdlp_destroy,
    .can_resolve = ytdlp_can_resolve,
    .is_available = ytdlp_is_available,
    .ensure_available = ytdlp_ensure_available,
    .update_tool = ytdlp_update_tool,
    .resolve = ytdlp_resolve,
    .resolve_async = NULL,  /* Not implemented yet */
    .cancel = NULL,
    .probe = ytdlp_probe,
    .get_tool_version = ytdlp_get_tool_version,
    .set_tool_path = ytdlp_set_tool_path
};

static PrismResolver* ytdlp_factory_create(void) {
    YtdlpResolver* resolver = (YtdlpResolver*)calloc(1, sizeof(YtdlpResolver));
    if (!resolver) return NULL;

    resolver->base.vtable = &s_ytdlp_vtable;
    resolver->base.identifier = PRISM_YTDLP_PLUGIN_ID;
    resolver->is_available = prism_ytdlp_is_available();

    return &resolver->base;
}

static const PrismResolverInfo* ytdlp_factory_get_info(void) {
    static const PrismResolverInfo info = {
        .name = "yt-dlp",
        .capabilities = PRISM_RESOLVER_CAP_VOD |
                        PRISM_RESOLVER_CAP_LIVE |
                        PRISM_RESOLVER_CAP_QUALITY |
                        PRISM_RESOLVER_CAP_HEADERS |
                        PRISM_RESOLVER_CAP_DOWNLOAD |
                        PRISM_RESOLVER_CAP_UPDATE,
        .hosts = s_known_hosts,
        .tool_version = NULL  /* Set dynamically */
    };
    return &info;
}

static bool ytdlp_factory_can_handle(const char* url) {
    if (!url || !url[0]) return false;

    char host[256];
    if (!extract_host(url, host, sizeof(host))) {
        return false;
    }

    for (int i = 0; s_known_hosts[i]; i++) {
        if (str_contains(host, s_known_hosts[i]) || str_contains(s_known_hosts[i], host)) {
            return true;
        }
    }

    return false;
}

const PrismResolverFactory g_ytdlp_resolver_factory = {
    .get_info = ytdlp_factory_get_info,
    .can_handle = ytdlp_factory_can_handle,
    .create = ytdlp_factory_create
};
