# SceHttp/SceSsl Transport (net_http) — Design

**Date:** 2026-07-17
**Target:** OpenOrbis `samples/net_http` runs end-to-end in Kyty (real HTTPS download to `/data/net_http.txt`), establishing the SceHttp HLE transport that commercial titles use.

## Current state

Kyty (@ `b16679c`) has the full SceHttp *object model* (Network.cpp: `m_templates`/`m_connections`/`m_requests`, `HttpBase` with headers/timeouts/redirect/SSL-callback state, type-tagged `Network::Id`) and ~20 registered Http NIDs — but `HttpSendRequest` is a print-stub returning `HTTP_ERROR_TIMEOUT`, and the response path doesn't exist.

First boot of the sample (permissive) confirmed the missing surface. NIDs computed with the validated Sony hash (`base64_sony(reverse(sha1(name+suffix)[:8]))`, cross-checked against 5 registered knowns):

| Function | NID | Notes |
|---|---|---|
| `sceHttpCreateRequestWithURL` | `Aeu5wVKkF9w` | int-method variant; maps 0..6 → GET/POST/HEAD/OPTIONS/PUT/DELETE/TRACE, then reuses the WithURL2 path |
| `sceHttpGetStatusCode` | `0a2TBNfE3BU` | new |
| `sceHttpGetResponseContentLength` | `yuO2H2Uvnos` | new; type 0=EXIST, 1=NOT_FOUND, 2=CHUNK_ENC |
| `sceHttpReadData` | `P5pdoykPYTk` | new |
| `sceHttpTerm` | `Ik-KpLTlf7Q` | impl exists, register only |
| `sceSslTerm` | `0K1yQ6Lv-Yc` | impl exists, register only |
| `sceNetPoolDestroy` | `K7RlrTkI-mw` | impl exists, register only |

## Decision: libcurl transport

Host libcurl 8.18.0 (OpenSSL flavour) with dev headers is installed. libcurl over hand-rolled sockets+TLS: gets HTTPS, redirects, chunked decoding, and proxy semantics for free; the alternative would re-implement half of it badly. Easy API, one handle per perform, `curl_global_init` via `std::call_once`.

## Request lifecycle

1. **`sceHttpSendRequest(req, post_data, size)`** — synchronous perform:
   - Under `m_mutex`: snapshot the request's config (url, method, UA, http_ver, headers, timeouts, redirect, ssl_cbfunc) into a plain struct. No lock held during network I/O.
   - SSL callback (https only): verification stays ON by default. On an SSL failure with a callback installed, the callback is consulted (`cb(ssl_ctx_id, verifyErr=0x02, certs=null, certNum=0, user_arg)` — a `KYTY_SYSV_ABI` pointer called on the guest's own thread, so a direct call is ABI-safe; certNum=0 means a well-behaved guest won't deref the array); a negative return rejects, otherwise the transfer retries with host verification off — the guest accepted the peer. On success the callback is still consulted once with verifyErr=0 (Sony always invokes it during the handshake). libcurl exposes no per-cert hook, so the cert list is always empty — documented limitation.
   - curl mapping: `USERAGENT`, `HTTP_VERSION` (1→1.0, 2→1.1), `FOLLOWLOCATION`=auto_redirect, `CONNECTTIMEOUT_MS`=connect_timeout/1000, `TIMEOUT_MS`=send+recv budget (recv_timeout/1000), request headers → `curl_slist`, `post_data`+method → `POSTFIELDS` when present.
   - Response streamed into an in-memory body buffer on the `HttpRequest` object (v1 limitation: whole body buffered; fine for the API's typical payloads).
   - Under `m_mutex`: store `status`, content-length type/value (`CURLINFO_CONTENT_LENGTH_DOWNLOAD_T`, −1 ⇒ CHUNK_ENC if chunked else NOT_FOUND), body, `read_pos=0`, `performed=true`.
2. **`sceHttpGetStatusCode` / `sceHttpGetResponseContentLength`** — read stored values; error if not performed.
3. **`sceHttpReadData(req, buf, size)`** — copy from `body[read_pos:]`, advance cursor, return bytes copied; 0 at EOF (the sample's loop contract).
4. Delete/Term paths unchanged (already implemented).

**Error mapping (curl→SCE):** `OPERATION_TIMEDOUT`→`HTTP_ERROR_TIMEOUT`; `COULDNT_RESOLVE_HOST/PROXY`→resolver error; `SSL_*`/`PEER_FAILED_VERIFICATION`→SSL error; `COULDNT_CONNECT`→connect error; default→`HTTP_ERROR_TIMEOUT`. (Exact SCE constants: reuse/extend the existing `HTTP_ERROR_*` set in Network.cpp.)

**Out of scope (documented, not silently wrong):** nonblock mode + Http epoll (sample doesn't use them; `SetNonblock(true)` requests still perform synchronously with a warning log), cookie APIs, auth, real per-cert callback data.

## Build

`source/emulator/CMakeLists.txt`: `find_package(CURL REQUIRED)` + link (same pattern as the existing Freetype dependency). New code lives in Network.cpp (existing convention — the whole Net/Ssl/Http surface is there), curl specifics in an anonymous namespace.

## Verification

1. `net_http` sample: guest prints status 200 + Content-Length + `Download completed.`; `/data/net_http.txt` exists host-side and matches a host `curl https://www.google.com/robots.txt` fetch (modulo Google A/B variance — compare structure/first lines).
2. Sanity: rerun to confirm repeat requests on fresh template/conn/request ids.
3. Regression matrix: ClassiCube singleplayer (renders + audio), ClassiCube multiplayer (classic_server.py join), trophies (`TROPHY UNLOCKED`, exit 0), Doom boot.
