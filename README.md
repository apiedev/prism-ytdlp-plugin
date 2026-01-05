# Prism yt-dlp Plugin

URL resolver plugin for [Prism Video Player](https://github.com/apiedev/prism-video) using [yt-dlp](https://github.com/yt-dlp/yt-dlp).

## Features

- Resolves URLs from 1000+ video platforms to direct playback URLs
- Automatic yt-dlp binary download on first use
- Quality selection (360p to 4K)
- Live stream support
- Cross-platform (Windows, macOS, Linux)

## Supported Platforms

- YouTube (youtube.com, youtu.be)
- Twitch (twitch.tv, clips.twitch.tv)
- Vimeo (vimeo.com)
- Dailymotion (dailymotion.com)
- Facebook (facebook.com, fb.watch)
- Twitter/X (twitter.com, x.com)
- Instagram (instagram.com)
- TikTok (tiktok.com)
- Reddit (reddit.com, v.redd.it)
- Streamable (streamable.com)
- SoundCloud (soundcloud.com)
- Bandcamp (bandcamp.com)
- Bilibili (bilibili.com)
- Niconico (nicovideo.jp)
- Rumble (rumble.com)
- Odysee (odysee.com)
- Kick (kick.com)
- And many more...

## License

This plugin is released into the **Public Domain** under the [Unlicense](https://unlicense.org).

## Building

### Prerequisites

- CMake 3.16+
- C11 compiler
- [prism-video](https://github.com/apiedev/prism-video) core headers
- curl (for auto-download on Linux/macOS)

### Build Steps

```bash
mkdir build && cd build
cmake ..
cmake --build .
cmake --install .
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `PRISM_CORE_DIR` | `../prism-video/Native` | Path to Prism core headers |

## Usage

Place the built plugin (`prism_ytdlp.dll` / `libprism_ytdlp.so` / `libprism_ytdlp.dylib`) in the same directory as `prism_core` or in a `plugins/` subdirectory.

The plugin will automatically download yt-dlp on first use if not found on the system.

### Manual Configuration

```c
#include <prism_ytdlp_plugin.h>

PrismYtdlpConfig config = {
    .ytdlp_path = "/custom/path/to/yt-dlp",
    .auto_download = true,
    .process_timeout_ms = 30000
};
prism_ytdlp_configure(&config);
```

## Supported Capabilities

- `PRISM_RESOLVER_CAP_VOD` - Video on demand
- `PRISM_RESOLVER_CAP_LIVE` - Live streams
- `PRISM_RESOLVER_CAP_QUALITY` - Quality selection
- `PRISM_RESOLVER_CAP_ASYNC` - Async resolution (planned)
- `PRISM_RESOLVER_CAP_HEADERS` - Custom HTTP headers

## Dependencies

This plugin spawns the yt-dlp process and requires:
- yt-dlp binary (auto-downloaded if not found)
- Python 3.8+ (bundled in yt-dlp standalone binary)
