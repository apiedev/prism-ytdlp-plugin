/*
 * Prism yt-dlp Plugin - URL Resolution Tests
 *
 * Test suite for validating yt-dlp resolver with various platforms:
 * - YouTube (live streams, VODs)
 * - Twitch (channels, VODs, clips)
 * - Vimeo
 * - Dailymotion
 *
 * Usage:
 *   ytdlp_resolver_tests [options] [test-name | url]
 *
 * Options:
 *   --list             List all available tests
 *   --all              Run all tests
 *   --url <url>        Test a specific URL directly
 *   --quality <height> Set quality (360, 720, 1080, etc.)
 *   --timeout <sec>    Set test timeout in seconds (default: 60)
 *   --verbose          Enable verbose logging
 *   --json             Output results as JSON
 */

#include "prism_ytdlp_plugin.h"
#include "test_urls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #define sleep_ms(ms) Sleep(ms)

    static double get_time_ms(void) {
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);
        return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
    }
#else
    #include <unistd.h>
    #include <sys/time.h>
    #define sleep_ms(ms) usleep((ms) * 1000)

    static double get_time_ms(void) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
    }
#endif

/* ============================================================================
 * Test Configuration
 * ========================================================================== */

#define MAX_TESTS 32
#define DEFAULT_TIMEOUT_SEC 60
#define PRISM_DEFAULT_QUALITY PRISM_QUALITY_AUTO

/* Stub for stream cleanup since prism_core isn't built yet */
static void free_resolved_stream(PrismResolvedStream* stream) {
    if (!stream) return;

    free((void*)stream->direct_url);
    free((void*)stream->audio_url);
    free((void*)stream->title);
    free((void*)stream->channel);
    free((void*)stream->thumbnail_url);
    free((void*)stream->description);
    free((void*)stream->video_codec);
    free((void*)stream->audio_codec);
    free((void*)stream->cookies);
    free((void*)stream->error);
    free((void*)stream->warning);
    free((void*)stream->original_url);

    if (stream->header_names) {
        for (int i = 0; i < stream->header_count; i++) {
            free((void*)stream->header_names[i]);
        }
        free((void*)stream->header_names);
    }
    if (stream->header_values) {
        for (int i = 0; i < stream->header_count; i++) {
            free((void*)stream->header_values[i]);
        }
        free((void*)stream->header_values);
    }
    if (stream->available_heights) {
        free(stream->available_heights);
    }

    free(stream);
}

typedef enum TestCategory {
    TEST_CATEGORY_YOUTUBE,
    TEST_CATEGORY_TWITCH,
    TEST_CATEGORY_VIMEO,
    TEST_CATEGORY_OTHER,
    TEST_CATEGORY_COUNT
} TestCategory;

typedef enum TestResult {
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP,
    TEST_RESULT_TIMEOUT,
    TEST_RESULT_ERROR
} TestResult;

typedef struct TestCase {
    const char* name;
    const char* description;
    const char* url;
    TestCategory category;
    bool expect_live;
    bool skip_by_default;  /* Skip unless explicitly requested */
} TestCase;

typedef struct TestResults {
    const char* name;
    TestResult result;
    double resolve_time_ms;
    char resolved_url[1024];
    char title[256];
    int width;
    int height;
    bool is_live;
    bool is_hls;
    char error_message[512];
} TestResults;

typedef struct Config {
    bool list_tests;
    bool run_all;
    bool verbose;
    bool json_output;
    int timeout_sec;
    int quality;
    const char* category_filter;
    const char* test_filter;
    const char* direct_url;
} Config;

/* ============================================================================
 * Test Cases
 * ========================================================================== */

static const TestCase g_test_cases[] = {
    /* ===== YouTube Tests ===== */
    {
        .name = "youtube_live",
        .description = "YouTube 24/7 lofi hip hop live stream",
        .url = PRISM_TEST_YOUTUBE_LIVE,
        .category = TEST_CATEGORY_YOUTUBE,
        .expect_live = true
    },
    {
        .name = "youtube_vod_short",
        .description = "YouTube Big Buck Bunny (short)",
        .url = PRISM_TEST_YOUTUBE_VOD_SHORT,
        .category = TEST_CATEGORY_YOUTUBE,
        .expect_live = false
    },
    {
        .name = "youtube_vod",
        .description = "YouTube Sintel trailer",
        .url = PRISM_TEST_YOUTUBE_VOD,
        .category = TEST_CATEGORY_YOUTUBE,
        .expect_live = false
    },

    /* ===== Twitch Tests ===== */
    {
        .name = "twitch_channel",
        .description = "Twitch channel (may be offline)",
        .url = PRISM_TEST_TWITCH_CHANNEL,
        .category = TEST_CATEGORY_TWITCH,
        .expect_live = true,
        .skip_by_default = true  /* Depends on streamer being live */
    },
    {
        .name = "twitch_channel_alt",
        .description = "Twitch alternate channel (may be offline)",
        .url = PRISM_TEST_TWITCH_CHANNEL_ALT,
        .category = TEST_CATEGORY_TWITCH,
        .expect_live = true,
        .skip_by_default = true  /* Depends on streamer being live */
    },

    /* ===== Vimeo Tests ===== */
    {
        .name = "vimeo_vod",
        .description = "Vimeo video (may require login)",
        .url = PRISM_TEST_VIMEO_VOD,
        .category = TEST_CATEGORY_VIMEO,
        .expect_live = false,
        .skip_by_default = true  /* Vimeo now requires login for most videos */
    },

    /* ===== Dailymotion Tests ===== */
    {
        .name = "dailymotion_vod",
        .description = "Dailymotion video (may be unavailable)",
        .url = PRISM_TEST_DAILYMOTION_VOD,
        .category = TEST_CATEGORY_OTHER,
        .expect_live = false,
        .skip_by_default = true  /* Videos may be removed */
    },

    /* Sentinel */
    { .name = NULL }
};

static const char* g_category_names[] = {
    "youtube",
    "twitch",
    "vimeo",
    "other"
};

/* ============================================================================
 * Test Execution
 * ========================================================================== */

static const char* result_to_string(TestResult result) {
    switch (result) {
        case TEST_RESULT_PASS:    return "PASS";
        case TEST_RESULT_FAIL:    return "FAIL";
        case TEST_RESULT_SKIP:    return "SKIP";
        case TEST_RESULT_TIMEOUT: return "TIMEOUT";
        case TEST_RESULT_ERROR:   return "ERROR";
        default:                  return "UNKNOWN";
    }
}

static TestResults run_single_test(const TestCase* test, const Config* config) {
    TestResults results = {0};
    results.name = test->name;

    double start_time = get_time_ms();

    /* Get yt-dlp resolver factory */
    const PrismResolverFactory* factory = prism_ytdlp_get_factory();
    if (!factory) {
        results.result = TEST_RESULT_ERROR;
        snprintf(results.error_message, sizeof(results.error_message),
                 "Failed to get yt-dlp resolver factory");
        return results;
    }

    /* Check if factory can handle this URL */
    if (!factory->can_handle(test->url)) {
        results.result = TEST_RESULT_SKIP;
        snprintf(results.error_message, sizeof(results.error_message),
                 "Resolver cannot handle this URL");
        return results;
    }

    /* Create resolver instance */
    PrismResolver* resolver = factory->create();
    if (!resolver) {
        results.result = TEST_RESULT_ERROR;
        snprintf(results.error_message, sizeof(results.error_message),
                 "Failed to create resolver instance");
        return results;
    }

    if (config->verbose) {
        printf("  [DEBUG] Resolver created\n");
    }

    /* Check if yt-dlp is available */
    if (!resolver->vtable->is_available(resolver)) {
        if (config->verbose) {
            printf("  [DEBUG] yt-dlp not available, attempting to ensure availability...\n");
        }

        PrismError err = resolver->vtable->ensure_available(resolver, NULL, NULL);
        if (err != PRISM_OK) {
            results.result = TEST_RESULT_ERROR;
            snprintf(results.error_message, sizeof(results.error_message),
                     "yt-dlp not available and could not be downloaded");
            resolver->vtable->destroy(resolver);
            return results;
        }
    }

    if (config->verbose) {
        const char* version = resolver->vtable->get_tool_version(resolver);
        printf("  [DEBUG] yt-dlp version: %s\n", version ? version : "unknown");
    }

    /* Set up options */
    PrismResolverOptions options;
    prism_resolver_options_init(&options);
    options.timeout_ms = config->timeout_sec * 1000;
    options.quality = (PrismStreamQuality)config->quality;
    options.include_metadata = true;

    if (config->verbose) {
        printf("  [DEBUG] Resolving URL: %s\n", test->url);
        printf("  [DEBUG] Quality: %d\n", config->quality);
    }

    /* Resolve URL */
    PrismResolvedStream* stream = resolver->vtable->resolve(resolver, test->url, &options);

    results.resolve_time_ms = get_time_ms() - start_time;

    if (!stream) {
        results.result = TEST_RESULT_FAIL;
        snprintf(results.error_message, sizeof(results.error_message),
                 "Resolution returned NULL");
        resolver->vtable->destroy(resolver);
        return results;
    }

    if (!stream->success) {
        results.result = TEST_RESULT_FAIL;
        snprintf(results.error_message, sizeof(results.error_message),
                 "Resolution failed: %s", stream->error ? stream->error : "Unknown error");
        free_resolved_stream(stream);
        resolver->vtable->destroy(resolver);
        return results;
    }

    /* Copy results */
    if (stream->direct_url) {
        strncpy(results.resolved_url, stream->direct_url, sizeof(results.resolved_url) - 1);
    }
    if (stream->title) {
        strncpy(results.title, stream->title, sizeof(results.title) - 1);
    }
    results.width = stream->width;
    results.height = stream->height;
    results.is_live = stream->is_live;
    results.is_hls = stream->is_hls;

    if (config->verbose) {
        printf("  [DEBUG] Resolution successful (%.1fms)\n", results.resolve_time_ms);
        printf("  [DEBUG] Title: %s\n", stream->title ? stream->title : "(none)");
        printf("  [DEBUG] Resolution: %dx%d\n", stream->width, stream->height);
        printf("  [DEBUG] Live: %s, HLS: %s\n",
               stream->is_live ? "yes" : "no",
               stream->is_hls ? "yes" : "no");
        if (stream->direct_url) {
            /* Only show first 100 chars of URL */
            printf("  [DEBUG] URL: %.100s%s\n",
                   stream->direct_url,
                   strlen(stream->direct_url) > 100 ? "..." : "");
        }
    }

    /* Validate results */
    if (!stream->direct_url || strlen(stream->direct_url) == 0) {
        results.result = TEST_RESULT_FAIL;
        snprintf(results.error_message, sizeof(results.error_message),
                 "No direct URL returned");
    } else {
        results.result = TEST_RESULT_PASS;
    }

    /* Cleanup */
    free_resolved_stream(stream);
    resolver->vtable->destroy(resolver);

    if (config->verbose) {
        printf("  [DEBUG] Resolver destroyed\n");
    }

    return results;
}

/* ============================================================================
 * Output Formatting
 * ========================================================================== */

static void print_results_text(const TestResults* results) {
    printf("  [%s] %s", result_to_string(results->result), results->name);

    if (results->result == TEST_RESULT_PASS) {
        printf(" (%.1fms, %dx%d, %s%s)\n",
               results->resolve_time_ms,
               results->width,
               results->height,
               results->is_live ? "LIVE" : "VOD",
               results->is_hls ? ", HLS" : "");
    } else {
        printf("\n");
        if (results->error_message[0]) {
            printf("    Error: %s\n", results->error_message);
        }
    }
}

static void print_results_json(const TestResults* results, bool last) {
    printf("    {\n");
    printf("      \"name\": \"%s\",\n", results->name);
    printf("      \"result\": \"%s\",\n", result_to_string(results->result));
    printf("      \"resolve_time_ms\": %.2f,\n", results->resolve_time_ms);
    printf("      \"width\": %d,\n", results->width);
    printf("      \"height\": %d,\n", results->height);
    printf("      \"is_live\": %s,\n", results->is_live ? "true" : "false");
    printf("      \"is_hls\": %s,\n", results->is_hls ? "true" : "false");
    printf("      \"title\": \"%s\",\n", results->title);
    printf("      \"error\": \"%s\"\n", results->error_message);
    printf("    }%s\n", last ? "" : ",");
}

static void print_summary(int total, int passed, int failed, int skipped, int timeout, double total_time) {
    printf("\n");
    printf("=== Test Summary ===\n");
    printf("  Total:   %d\n", total);
    printf("  Passed:  %d\n", passed);
    printf("  Failed:  %d\n", failed);
    printf("  Skipped: %d\n", skipped);
    printf("  Timeout: %d\n", timeout);
    printf("  Time:    %.2f seconds\n", total_time / 1000.0);
    printf("\n");
}

/* ============================================================================
 * Command Line Parsing
 * ========================================================================== */

static void print_usage(const char* program) {
    printf("\n");
    printf("Prism yt-dlp Plugin - URL Resolution Tests\n");
    printf("Tests YouTube, Twitch, Vimeo URL resolution using yt-dlp.\n");
    printf("\n");
    printf("Usage: %s [options] [test-name | url]\n", program);
    printf("\n");
    printf("Options:\n");
    printf("  --list             List all available tests\n");
    printf("  --all              Run all tests (except skipped)\n");
    printf("  --category <name>  Run tests in category: youtube, twitch, vimeo, other\n");
    printf("  --url <url>        Test a specific URL directly\n");
    printf("  --quality <height> Set quality (360, 720, 1080, etc. Default: auto)\n");
    printf("  --timeout <sec>    Set test timeout (default: %d)\n", DEFAULT_TIMEOUT_SEC);
    printf("  --verbose          Enable verbose logging\n");
    printf("  --json             Output results as JSON\n");
    printf("  --help             Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --list\n", program);
    printf("  %s --all --verbose\n", program);
    printf("  %s --category youtube\n", program);
    printf("  %s youtube_live --quality 720\n", program);
    printf("  %s --url \"https://www.youtube.com/watch?v=dQw4w9WgXcQ\"\n", program);
    printf("  %s --url \"https://www.twitch.tv/shroud\" --verbose\n", program);
    printf("\n");
}

static void list_all_tests(void) {
    printf("\n");
    printf("=== Available Tests ===\n");
    printf("\n");

    TestCategory current_category = -1;

    for (int i = 0; g_test_cases[i].name; i++) {
        if (g_test_cases[i].category != current_category) {
            current_category = g_test_cases[i].category;
            printf("\n[%s]\n", g_category_names[current_category]);
        }

        printf("  %-20s %s%s%s\n",
               g_test_cases[i].name,
               g_test_cases[i].description,
               g_test_cases[i].expect_live ? " [LIVE]" : "",
               g_test_cases[i].skip_by_default ? " (skipped by default)" : "");
    }

    printf("\n");
}

static Config parse_args(int argc, char* argv[]) {
    Config config = {
        .timeout_sec = DEFAULT_TIMEOUT_SEC,
        .quality = PRISM_DEFAULT_QUALITY
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            config.list_tests = true;
        } else if (strcmp(argv[i], "--all") == 0) {
            config.run_all = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            config.verbose = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            config.json_output = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--category") == 0) {
            if (i + 1 < argc) {
                config.category_filter = argv[++i];
            }
        } else if (strcmp(argv[i], "--url") == 0) {
            if (i + 1 < argc) {
                config.direct_url = argv[++i];
            }
        } else if (strcmp(argv[i], "--quality") == 0) {
            if (i + 1 < argc) {
                config.quality = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 < argc) {
                config.timeout_sec = atoi(argv[++i]);
            }
        } else if (argv[i][0] != '-') {
            if (strstr(argv[i], "://") != NULL) {
                config.direct_url = argv[i];
            } else {
                config.test_filter = argv[i];
            }
        }
    }

    return config;
}

static bool should_run_test(const TestCase* test, const Config* config) {
    /* Skip tests marked as skip_by_default unless explicitly requested */
    if (test->skip_by_default && config->run_all && !config->test_filter && !config->category_filter) {
        return false;
    }

    if (config->run_all) return true;

    if (config->category_filter) {
        const char* cat_name = g_category_names[test->category];
        if (strcmp(config->category_filter, cat_name) != 0) {
            return false;
        }
        return true;
    }

    if (config->test_filter) {
        return strcmp(config->test_filter, test->name) == 0;
    }

    return false;
}

/* ============================================================================
 * Main
 * ========================================================================== */

int main(int argc, char* argv[]) {
    Config config = parse_args(argc, argv);

    if (config.list_tests) {
        list_all_tests();
        return 0;
    }

    if (!config.run_all && !config.category_filter && !config.test_filter && !config.direct_url) {
        print_usage(argv[0]);
        return 2;
    }

    /* Check yt-dlp availability */
    printf("\n");
    printf("Prism yt-dlp Plugin Tests\n");
    printf("yt-dlp:   %s\n", prism_ytdlp_is_available() ? "available" : "not found (will attempt download)");
    if (prism_ytdlp_is_available()) {
        printf("Path:     %s\n", prism_ytdlp_get_path());
    }
    printf("Timeout:  %d seconds\n", config.timeout_sec);
    printf("Quality:  %s\n", config.quality == 0 ? "auto" :
           (config.quality == 360 ? "360p" :
            config.quality == 480 ? "480p" :
            config.quality == 720 ? "720p" :
            config.quality == 1080 ? "1080p" : "custom"));
    printf("\n");

    TestResults all_results[MAX_TESTS];
    int test_count = 0;
    int passed = 0, failed = 0, skipped = 0, timeout = 0;
    double total_start = get_time_ms();

    if (config.json_output) {
        printf("{\n");
        printf("  \"plugin\": \"yt-dlp\",\n");
        printf("  \"available\": %s,\n", prism_ytdlp_is_available() ? "true" : "false");
        printf("  \"tests\": [\n");
    } else {
        printf("=== Running Tests ===\n\n");
    }

    /* Handle direct URL testing */
    if (config.direct_url) {
        TestCase direct_test = {
            .name = "direct_url",
            .description = "Direct URL test",
            .url = config.direct_url,
            .category = TEST_CATEGORY_OTHER,
            .expect_live = false
        };

        if (!config.json_output) {
            printf("Testing: direct URL\n");
            printf("  URL: %s\n", config.direct_url);
        }

        TestResults results = run_single_test(&direct_test, &config);
        all_results[test_count++] = results;

        switch (results.result) {
            case TEST_RESULT_PASS:    passed++; break;
            case TEST_RESULT_FAIL:    failed++; break;
            case TEST_RESULT_SKIP:    skipped++; break;
            case TEST_RESULT_TIMEOUT: timeout++; break;
            default:                  failed++; break;
        }

        if (config.json_output) {
            print_results_json(&results, true);
        } else {
            print_results_text(&results);
            printf("\n");
        }
    } else {
        /* Run predefined tests */
        for (int i = 0; g_test_cases[i].name && test_count < MAX_TESTS; i++) {
            if (!should_run_test(&g_test_cases[i], &config)) {
                continue;
            }

            if (!config.json_output) {
                printf("Testing: %s\n", g_test_cases[i].name);
                if (config.verbose) {
                    printf("  URL: %s\n", g_test_cases[i].url);
                }
            }

            TestResults results = run_single_test(&g_test_cases[i], &config);
            all_results[test_count++] = results;

            switch (results.result) {
                case TEST_RESULT_PASS:    passed++; break;
                case TEST_RESULT_FAIL:    failed++; break;
                case TEST_RESULT_SKIP:    skipped++; break;
                case TEST_RESULT_TIMEOUT: timeout++; break;
                default:                  failed++; break;
            }

            if (config.json_output) {
                bool is_last = !g_test_cases[i + 1].name ||
                              !should_run_test(&g_test_cases[i + 1], &config);
                print_results_json(&results, is_last);
            } else {
                print_results_text(&results);
                printf("\n");
            }
        }
    }

    double total_time = get_time_ms() - total_start;

    if (config.json_output) {
        printf("  ],\n");
        printf("  \"summary\": {\n");
        printf("    \"total\": %d,\n", test_count);
        printf("    \"passed\": %d,\n", passed);
        printf("    \"failed\": %d,\n", failed);
        printf("    \"skipped\": %d,\n", skipped);
        printf("    \"timeout\": %d,\n", timeout);
        printf("    \"total_time_ms\": %.2f\n", total_time);
        printf("  }\n");
        printf("}\n");
    } else {
        print_summary(test_count, passed, failed, skipped, timeout, total_time);
    }

    return (failed > 0 || timeout > 0) ? 1 : 0;
}
