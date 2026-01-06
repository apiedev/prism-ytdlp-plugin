/*
 * Prism yt-dlp Plugin - Public Header
 *
 * URL resolver plugin using yt-dlp for YouTube, Twitch, and other platforms.
 *
 * License: Unlicense (Public Domain)
 */

#ifndef PRISM_YTDLP_PLUGIN_H
#define PRISM_YTDLP_PLUGIN_H

#include <prism/prism_resolver.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Plugin export macro */
#ifdef _WIN32
    #ifdef PRISM_YTDLP_EXPORTS
        #define PRISM_YTDLP_API __declspec(dllexport)
    #else
        #define PRISM_YTDLP_API __declspec(dllimport)
    #endif
#else
    #define PRISM_YTDLP_API __attribute__((visibility("default")))
#endif

/* Plugin version */
#define PRISM_YTDLP_PLUGIN_VERSION "1.0.0"

/* Plugin identifier */
#define PRISM_YTDLP_PLUGIN_ID "com.prism.ytdlp"

/* Configuration options */
typedef struct PrismYtdlpConfig {
    const char* ytdlp_path;       /* Custom path to yt-dlp binary (NULL for auto-detect) */
    const char* install_dir;      /* Directory to install yt-dlp if not found (NULL = temp dir) */
    bool auto_download;           /* Automatically download yt-dlp if not found (default: true) */
    int process_timeout_ms;       /* Timeout for yt-dlp process in milliseconds (default: 30000) */
} PrismYtdlpConfig;

/*
 * Get the yt-dlp resolver factory.
 * Can be used to manually create resolvers without going through the plugin system.
 */
PRISM_YTDLP_API const PrismResolverFactory* prism_ytdlp_get_factory(void);

/*
 * Check if yt-dlp is available (installed or downloadable).
 */
PRISM_YTDLP_API bool prism_ytdlp_is_available(void);

/*
 * Get the path to the yt-dlp binary.
 * Returns NULL if not found.
 */
PRISM_YTDLP_API const char* prism_ytdlp_get_path(void);

/*
 * Download yt-dlp binary to the specified directory.
 * If install_dir is NULL, uses a platform-specific default.
 * progress_callback is called with values 0.0-1.0 during download.
 * Returns PRISM_OK on success.
 */
PRISM_YTDLP_API PrismError prism_ytdlp_download(
    const char* install_dir,
    void (*progress_callback)(float progress, void* user_data),
    void* user_data
);

/*
 * Set configuration options.
 * Call before any resolve operations.
 */
PRISM_YTDLP_API void prism_ytdlp_configure(const PrismYtdlpConfig* config);

#ifdef __cplusplus
}
#endif

#endif /* PRISM_YTDLP_PLUGIN_H */
