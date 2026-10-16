/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* update.h -- is there a newer release than the one running?  One HTTPS request to GitHub. */
#ifndef UPDATE_H
#define UPDATE_H

#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <wininet.h>
#include "version.h"

#define CUBOLT_REPO     "nbwzx/cubolt"
#define CUBOLT_RELEASES "https://github.com/" CUBOLT_REPO "/releases"
#define CUBOLT_LATEST   "https://api.github.com/repos/" CUBOLT_REPO "/releases/latest"

/* <0, 0, >0 as `a` is older, equal, newer; "0.0.10" beats "0.0.9", which strcmp gets backwards */
static int ver_cmp(const char *a, const char *b)
{
    if (*b == 'v' || *b == 'V') b++;
    for (;;) {
        long long x = 0, y = 0;
        while (*a >= '0' && *a <= '9') { x = x * 10 + (*a - '0'); a++; }
        while (*b >= '0' && *b <= '9') { y = y * 10 + (*b - '0'); b++; }
        if (x != y) return x < y ? -1 : 1;
        if (!*a && !*b) return 0;
        /* only the tag can carry a '-': a prerelease is older than the release it precedes */
        if (*a != '.' && *b != '.') return *b == '-' ? 1 : 0;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
}

#define UPDATE_NO_TAG "the release API answered no tag"

/* Copies the value the key names into `dst`; -1 with none, -2 ended early, else the stopper. */
static int json_str_after(const char *key_at, char *dst, size_t cap)
{
    const char *p = strchr(key_at, ':');
    if (!p) { dst[0] = 0; return -1; }
    p++;
    while (*p == ':' || *p == ' ') p++;
    if (*p != '"') { dst[0] = 0; return -1; }
    size_t i = 0;
    for (p++; p[i] && p[i] != '"' && i + 1 < cap; i++) dst[i] = p[i];
    dst[i] = 0;
    /* 0 is the end of the answer, not a stopper: a tag cut there compares as its own prefix */
    return p[i] ? (unsigned char)p[i] : -2;
}

/* 0 on success, else 1 with a short reason in `why`. */
static int update_latest(char *tag, size_t n, char *why, size_t wn)
{
    char buf[16384];
    DWORD got = 0, total = 0, status = 0, statuslen = sizeof status;
    tag[0] = 0;
    why[0] = 0;

    /* the User-Agent header, which the GitHub API refuses to answer without */
    HINTERNET h = InternetOpenA(CUBOLT_NAME "/" CUBOLT_VERSION,
                                INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!h) {
        snprintf(why, wn, "the network layer could not start (error %lu)",
                 (unsigned long)GetLastError());
        return 1;
    }
    /* A request with no timeout can hang: refuse to send one */
    DWORD to = 10000;
    if (!InternetSetOptionA(h, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to) ||
        !InternetSetOptionA(h, INTERNET_OPTION_SEND_TIMEOUT,    &to, sizeof to) ||
        !InternetSetOptionA(h, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to)) {
        snprintf(why, wn, "the network layer would not take its timeouts (error %lu)",
                 (unsigned long)GetLastError());
        InternetCloseHandle(h);
        return 1;
    }

    HINTERNET u = InternetOpenUrlA(h, CUBOLT_LATEST, NULL, 0,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                   INTERNET_FLAG_NO_UI, 0);
    /* 407, no such name and a bad certificate all read alike without the code */
    if (!u) {
        snprintf(why, wn, "cannot reach the release API (error %lu)",
                 (unsigned long)GetLastError());
        InternetCloseHandle(h);
        return 1;
    }
    /* 404 and a rate-limit 403 carry bodies too: the status is what tells them apart */
    if (!HttpQueryInfoA(u, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                        &status, &statuslen, NULL)) {
        snprintf(why, wn, "the release API's status could not be read (error %lu)",
                 (unsigned long)GetLastError());
        InternetCloseHandle(u);
        InternetCloseHandle(h);
        return 1;
    }
    /* A failed read and the reply's end leave the loop the same way, so which it was is kept. */
    int read_ok = 1;
    while (total + 1 < sizeof buf) {
        if (!InternetReadFile(u, buf + total, (DWORD)(sizeof buf - 1 - total), &got)) {
            read_ok = 0;
            break;
        }
        if (!got) break;
        total += got;
    }
    buf[total] = 0;

    /* A full buffer is not proof of truncation: one more byte is the question. */
    int cut = 0;
    if (total + 1 >= sizeof buf) {
        char extra;
        DWORD more = 0;
        if (InternetReadFile(u, &extra, 1, &more) && more > 0) cut = 1;
    }
    InternetCloseHandle(u);
    InternetCloseHandle(h);
    char msg[128] = "";
    const char *m = strstr(buf, "\"message\"");
    if (m) json_str_after(m, msg, sizeof msg);
    if (status != 200) {
        snprintf(why, wn, "the release API answered HTTP %lu%s%s",
                 (unsigned long)status, *msg ? " -- " : "", msg);
        return 1;
    }
    if (cut) {
        snprintf(why, wn, "the release API answered more than %u bytes", (unsigned)sizeof buf);
        return 1;
    }
    if (!total) { snprintf(why, wn, "the release API answered nothing"); return 1; }
    if (!read_ok) {
        snprintf(why, wn, "the release API's answer stopped part way through");
        return 1;
    }
    const char *tagfield = strstr(buf, "\"tag_name\"");
    if (!tagfield) {
        snprintf(why, wn, "no release to compare with -- %s", *msg ? msg : UPDATE_NO_TAG);
        return 1;
    }
    int stopped = json_str_after(tagfield, tag, n);
    if (stopped < 0) {
        snprintf(why, wn, stopped == -2 ? "the release API's answer stopped part way through"
                                        : UPDATE_NO_TAG);
        return 1;
    }
    if (stopped && stopped != '"') {
        snprintf(why, wn, "the release tag is longer than %zu bytes", n - 1);
        return 1;
    }
    if (!*tag) { snprintf(why, wn, "the release API answered an empty tag"); return 1; }
    return 0;
}

#endif /* UPDATE_H */
