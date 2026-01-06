/*
 * Prism yt-dlp Plugin - Test URL Configuration
 * Platform URL Resolution Tests (YouTube, Twitch, Vimeo, etc.)
 *
 * This file contains test URLs for platforms that require yt-dlp resolution.
 * These URLs are resolved to direct stream URLs before being passed to a decoder.
 *
 * Note: Platform URLs may change or become unavailable. Update as needed.
 */

#ifndef PRISM_YTDLP_TEST_URLS_H
#define PRISM_YTDLP_TEST_URLS_H

/* ============================================================================
 * YouTube Test URLs
 * ============================================================================ */

/* YouTube 24/7 lofi hip hop radio - live stream */
#ifndef PRISM_TEST_YOUTUBE_LIVE
#define PRISM_TEST_YOUTUBE_LIVE "https://www.youtube.com/watch?v=jfKfPfyJRdk"
#endif

/* YouTube short video - Big Buck Bunny (public domain, always available) */
#ifndef PRISM_TEST_YOUTUBE_VOD_SHORT
#define PRISM_TEST_YOUTUBE_VOD_SHORT "https://www.youtube.com/watch?v=aqz-KE-bpKQ"
#endif

/* YouTube standard video - Blender Foundation Sintel */
#ifndef PRISM_TEST_YOUTUBE_VOD
#define PRISM_TEST_YOUTUBE_VOD "https://www.youtube.com/watch?v=eRsGyueVLvQ"
#endif

/* YouTube Shorts format */
#ifndef PRISM_TEST_YOUTUBE_SHORTS
#define PRISM_TEST_YOUTUBE_SHORTS "https://www.youtube.com/shorts/example"
#endif

/* ============================================================================
 * Twitch Test URLs
 * ============================================================================
 * Note: Twitch VODs may expire. Live streams depend on streamer being online.
 */

/* Twitch channel - popular streamer (may or may not be live) */
#ifndef PRISM_TEST_TWITCH_CHANNEL
#define PRISM_TEST_TWITCH_CHANNEL "https://www.twitch.tv/shroud"
#endif

/* Twitch alternate channel */
#ifndef PRISM_TEST_TWITCH_CHANNEL_ALT
#define PRISM_TEST_TWITCH_CHANNEL_ALT "https://www.twitch.tv/xqc"
#endif

/* Twitch VOD - replace with a current VOD ID as needed */
#ifndef PRISM_TEST_TWITCH_VOD
#define PRISM_TEST_TWITCH_VOD "https://www.twitch.tv/videos/2341234567"
#endif

/* Twitch Clip */
#ifndef PRISM_TEST_TWITCH_CLIP
#define PRISM_TEST_TWITCH_CLIP "https://clips.twitch.tv/example"
#endif

/* ============================================================================
 * Vimeo Test URLs
 * ============================================================================ */

/* Vimeo public video - Big Buck Bunny */
#ifndef PRISM_TEST_VIMEO_VOD
#define PRISM_TEST_VIMEO_VOD "https://vimeo.com/1084537"
#endif

/* ============================================================================
 * Dailymotion Test URLs
 * ============================================================================ */

/* Dailymotion public video */
#ifndef PRISM_TEST_DAILYMOTION_VOD
#define PRISM_TEST_DAILYMOTION_VOD "https://www.dailymotion.com/video/x2bu1a8"
#endif

/* ============================================================================
 * Quality Settings for Testing
 * ============================================================================ */

/* Test resolutions */
#define PRISM_TEST_QUALITY_360P  360
#define PRISM_TEST_QUALITY_480P  480
#define PRISM_TEST_QUALITY_720P  720
#define PRISM_TEST_QUALITY_1080P 1080

#endif /* PRISM_YTDLP_TEST_URLS_H */
