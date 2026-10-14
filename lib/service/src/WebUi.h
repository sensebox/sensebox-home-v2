#pragma once

#include <stddef.h>

// Service-mode web UI: web/index.html, embedded into the firmware at build time
// (board_build.embed_txtfiles in platformio.ini). One self-contained page: no external fonts,
// scripts or CDNs, it must work without internet. Talks to the REST API described in ServiceMode.h.

extern const char webIndexHtmlStart[] asm("_binary_web_index_html_start");
extern const char webIndexHtmlEnd[] asm("_binary_web_index_html_end");

namespace webui {

inline const char* indexHtml() { return webIndexHtmlStart; }
// embed_txtfiles appends a terminating 0 byte, which is not part of the page.
inline size_t indexHtmlLen() { return static_cast<size_t>(webIndexHtmlEnd - webIndexHtmlStart) - 1; }

}  // namespace webui
