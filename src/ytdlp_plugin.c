/*
 * Prism yt-dlp Plugin - Plugin Registration
 *
 * Implements the Prism plugin interface for the yt-dlp URL resolver.
 *
 * License: Unlicense (Public Domain)
 */

#include "prism_ytdlp_plugin.h"
#include <prism/prism_plugin.h>
#include <prism/prism_resolver.h>

#include <string.h>
#include <stdlib.h>

/* Forward declarations from ytdlp_resolver.c */
extern const PrismResolverFactory g_ytdlp_resolver_factory;

/* ============================================================================
 * Plugin Information
 * ========================================================================== */

static const char* s_supported_hosts[] = {
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

static const PrismResolverInfo s_resolver_info = {
    .name = "yt-dlp",
    .capabilities = PRISM_RESOLVER_CAP_VOD |
                    PRISM_RESOLVER_CAP_LIVE |
                    PRISM_RESOLVER_CAP_QUALITY |
                    PRISM_RESOLVER_CAP_ASYNC |
                    PRISM_RESOLVER_CAP_HEADERS,
    .hosts = s_supported_hosts
};

static const PrismPluginInfo s_plugin_info = {
    .api_version = PRISM_PLUGIN_API_VERSION,
    .type = PRISM_PLUGIN_TYPE_RESOLVER,
    .name = "yt-dlp URL Resolver",
    .identifier = PRISM_YTDLP_PLUGIN_ID,
    .version = PRISM_YTDLP_PLUGIN_VERSION,
    .description = "URL resolver for YouTube, Twitch, and 1000+ other sites using yt-dlp",
    .license = "Unlicense",
    .author = "Prism Video",
    .url = "https://github.com/apiedev/prism-ytdlp-plugin",
    .priority = PRISM_PLUGIN_PRIORITY_PREFERRED,
    .capabilities = 0
};

/* ============================================================================
 * Plugin Lifecycle
 * ========================================================================== */

static const PrismPluginInfo* ytdlp_plugin_get_info(void) {
    return &s_plugin_info;
}

static PrismError ytdlp_plugin_init(const char* config) {
    (void)config;
    /* Initialization handled lazily on first use */
    return PRISM_OK;
}

static void ytdlp_plugin_shutdown(void) {
    /* Nothing to clean up */
}

static PrismError ytdlp_plugin_register(PrismPluginRegistry* registry) {
    (void)registry;
    /* Registration is handled by the core when it loads this plugin.
     * The core calls prism_ytdlp_get_factory() to get the resolver factory.
     * Direct linking to prism_register_resolver is not needed since the
     * plugin DLL is loaded dynamically by the core.
     */
    return PRISM_OK;
}

/* ============================================================================
 * Plugin Exports
 * ========================================================================== */

PRISM_PLUGIN_EXPORT const PrismPluginInfo* PRISM_CALL prism_plugin_get_info(void) {
    return ytdlp_plugin_get_info();
}

PRISM_PLUGIN_EXPORT PrismError PRISM_CALL prism_plugin_init(const char* config) {
    return ytdlp_plugin_init(config);
}

PRISM_PLUGIN_EXPORT void PRISM_CALL prism_plugin_shutdown(void) {
    ytdlp_plugin_shutdown();
}

PRISM_PLUGIN_EXPORT PrismError PRISM_CALL prism_plugin_register(PrismPluginRegistry* registry) {
    return ytdlp_plugin_register(registry);
}

/* ============================================================================
 * Public API
 * ========================================================================== */

PRISM_YTDLP_API const PrismResolverFactory* prism_ytdlp_get_factory(void) {
    return &g_ytdlp_resolver_factory;
}
