Windows platform implementation units belong here. Keep the public game
interfaces in `include/` stable and implement them here instead of spreading
Windows conditionals through Rasterfall gameplay code.

Planned units:

* `window_sdl.c` — window, software surface, keyboard and relative mouse
* `audio_sdl.c` — PCM callback and audio queue
* `socket_winsock.c` — Winsock startup and error translation
* `time_win32.c` — monotonic clock and sleep
* `assets_embedded.c` — generated single-file asset table
