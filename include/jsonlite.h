/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* jsonlite.h -- a minimal JSON reader for the batch case files: --json and File > Batch solve. */
#ifndef JSONLITE_H
#define JSONLITE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
    JType   type;
    double  num;      /* J_NUM / J_BOOL */
    char   *str;
    JVal  **items;    /* J_ARR elements / J_OBJ values */
    char  **keys;     /* J_OBJ member names, parallel to items */
    int     n;
    int     cap;      /* item slots, and key slots for J_OBJ; 0 until the first append */
};

static __thread char j_err[128];

#define JERR(...) do { snprintf(j_err, sizeof j_err, __VA_ARGS__); return NULL; } while (0)

#define J_OOM     "out of memory"
#define J_UNTERM  "unterminated string"
#define J_BADNUM  "bad number"

/* NULL on failure with `j_err` set: no message can ride back on a NULL. */
static JVal *j_new(JType t)
{
    JVal *v = (JVal *)calloc(1, sizeof *v);
    if (!v) JERR(J_OOM);
    v->type = t;
    return v;
}

static void j_free(JVal *v)
{
    if (!v) return;
    free(v->str);
    for (int i = 0; i < v->n; i++) {
        if (v->keys) free(v->keys[i]);
        j_free(v->items[i]);
    }
    free(v->items);
    free(v->keys);
    free(v);
}

static void j_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

/* exactly four hex digits -> value; 0 if any character is not hex */
static int j_hex4(const char *s, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char h = s[i];
        v <<= 4;
        if (h >= '0' && h <= '9')      v |= (unsigned)(h - '0');
        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/* *p is at the opening quote: the caller has not stepped past it */
static char *j_string(const char **p)
{
    if (**p != '"') JERR("expected a string");
    (*p)++;
    size_t cap = 32, len = 0;
    char *s = (char *)malloc(cap);
    if (!s) JERR(J_OOM);
#define JPUSH(ch) do {                                                  \
        if (len + 1 >= cap) {                                           \
            cap *= 2;                                                   \
            char *ns = (char *)realloc(s, cap);                         \
            if (!ns) { free(s); JERR(J_OOM); }                \
            s = ns;                                                     \
        }                                                               \
        s[len++] = (char)(ch);                                          \
    } while (0)
    while (**p && **p != '"') {
        char c = **p;
        if (c != '\\') { JPUSH(c); (*p)++; continue; }
        (*p)++;
        char e = **p;
        if (!e) { free(s); JERR(J_UNTERM); }
        if (e == 'u') {
            unsigned cp;
            if (!j_hex4(*p + 1, &cp)) { free(s); JERR("bad \\u escape"); }
            (*p) += 5;                       /* past 'u' and the four hex digits */
            if (cp >= 0xD800 && cp <= 0xDBFF && (*p)[0] == '\\' && (*p)[1] == 'u') {
                unsigned lo;
                if (j_hex4(*p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    (*p) += 6;
                }
            }
            if (!cp) { free(s); JERR("bad \\u escape: a NUL would cut the value short"); }
            /* Half a surrogate pair is not a character; a whole one left the range above. */
            if (cp >= 0xD800 && cp <= 0xDFFF)
                { free(s); JERR("bad \\u escape: half a surrogate pair"); }
            if      (cp < 0x80)   JPUSH(cp);
            else if (cp < 0x800) { JPUSH(0xC0 | (cp >> 6)); JPUSH(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { JPUSH(0xE0 | (cp >> 12)); JPUSH(0x80 | ((cp >> 6) & 0x3F));
                                     JPUSH(0x80 | (cp & 0x3F)); }
            else { JPUSH(0xF0 | (cp >> 18)); JPUSH(0x80 | ((cp >> 12) & 0x3F));
                   JPUSH(0x80 | ((cp >> 6) & 0x3F)); JPUSH(0x80 | (cp & 0x3F)); }
            continue;
        }
        switch (e) {
        case '"': case '\\': case '/': JPUSH(e);  break;
        case 'n':                      JPUSH('\n'); break;
        case 't':                      JPUSH('\t'); break;
        case 'r':                      JPUSH('\r'); break;
        case 'b':                      JPUSH('\b'); break;
        case 'f':                      JPUSH('\f'); break;
        default: free(s); JERR("bad escape \\%c", e);
        }
        (*p)++;
    }
    if (**p != '"') { free(s); JERR(J_UNTERM); }
    (*p)++;
    s[len] = 0;
    return s;
#undef JPUSH
}

/* Room for one more item; doubling spares a long list a realloc per element. */
static int j_room(JVal *v)
{
    if (v->n < v->cap) return 1;
    int ncap = v->cap ? v->cap * 2 : 8;
    JVal **ni = (JVal **)realloc(v->items, (size_t)ncap * sizeof *v->items);
    if (!ni) return 0;
    v->items = ni;
    if (v->type == J_OBJ) {
        char **nk = (char **)realloc(v->keys, (size_t)ncap * sizeof *v->keys);
        if (!nk) return 0;
        v->keys = nk;
    }
    v->cap = ncap;
    return 1;
}

static JVal *j_value(const char **p, int depth)
{
    if (depth > 32) JERR("JSON nested too deep");
    j_ws(p);
    char c = **p;

    /* An object and an array are one walk: an element is preceded by a "key": only in an object. */
    if (c == '{' || c == '[') {
        int  obj   = c == '{';
        char close = obj ? '}' : ']';
        (*p)++;
        JVal *v = j_new(obj ? J_OBJ : J_ARR);
        if (!v) JERR(J_OOM);
        j_ws(p);
        if (**p == close) { (*p)++; return v; }
        for (;;) {
            char *k = NULL;
            if (obj) {
                j_ws(p);
                k = j_string(p);
                if (!k) { j_free(v); return NULL; }
                j_ws(p);
                if (**p != ':') { free(k); j_free(v); JERR("expected ':'"); }
                (*p)++;
            }
            JVal *item = j_value(p, depth + 1);
            if (!item) { free(k); j_free(v); return NULL; }
            if (!j_room(v)) { free(k); j_free(item); j_free(v); JERR(J_OOM); }
            if (obj) v->keys[v->n] = k;
            v->items[v->n++] = item;
            j_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == close) { (*p)++; return v; }
            j_free(v); JERR("expected ',' or '%c'", close);
        }
    }
    if (c == '"') {
        JVal *v = j_new(J_STR);
        if (!v) JERR(J_OOM);
        v->str = j_string(p);
        if (!v->str) { free(v); return NULL; }
        return v;
    }
    if (c == 't' && !strncmp(*p, "true", 4)) {
        *p += 4; JVal *v = j_new(J_BOOL); if (v) v->num = 1; return v;
    }
    if (c == 'f' && !strncmp(*p, "false", 5)) { *p += 5; return j_new(J_BOOL); }
    if (c == 'n' && !strncmp(*p, "null", 4))  { *p += 4; return j_new(J_NULL); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        /* JSON has no hex or inf: strtod reads 0x10 as 16, and only lowercase is refused */
        const char *d = (**p == '-') ? *p + 1 : *p;
        if ((*d == '0' && (d[1] == 'x' || d[1] == 'X')) ||
            !strncmp(d, "inf", 3) || !strncmp(d, "nan", 3))
            JERR(J_BADNUM);
        char *end;
        double dd = strtod(*p, &end);
        if (end == *p) JERR(J_BADNUM);
        *p = end;
        JVal *v = j_new(J_NUM);
        if (v) v->num = dd;
        return v;
    }
    /* a '\0' printed with %c would end j_err at that byte */
    if (c) JERR("unexpected character '%c' (code %d)", c, (int)(unsigned char)c);
    JERR("unexpected end of input");
}

/* the text must hold one JSON value and nothing else */
static JVal *j_parse(const char *text)
{
    j_err[0] = 0;
    const char *p = text;
    JVal *v = j_value(&p, 0);
    if (!v) return NULL;
    j_ws(&p);
    if (*p) {
        j_free(v);
        JERR("unexpected trailing characters");
    }
    return v;
}

/* ---- small accessors: a key that is absent or mistyped reads as if it were not there ---- */

static const JVal *j_get(const JVal *obj, const char *key)
{
    if (!obj || obj->type != J_OBJ) return NULL;
    for (int i = 0; i < obj->n; i++)
        if (!strcmp(obj->keys[i], key)) return obj->items[i];
    return NULL;
}

static const char *j_kind(JType t)
{
    switch (t) {
    case J_NULL: return "null";
    case J_BOOL: return "a boolean";
    case J_NUM:  return "a number";
    case J_STR:  return "a string";
    case J_ARR:  return "an array";
    case J_OBJ:  return "an object";
    }
    return "a value";
}

/* NULL unless the key is there under another type; a JSON null reads as absent anyway. */
static const char *j_want(const JVal *o, const char *key, JType type)
{
    const JVal *v = j_get(o, key);
    if (!v || v->type == type || v->type == J_NULL) return NULL;
    static __thread char why[128];
    snprintf(why, sizeof why, "%s is %s, expected %s", key, j_kind(v->type), j_kind(type));
    return why;
}

static const char *j_str(const JVal *o, const char *key)
{
    const JVal *v = j_get(o, key);
    return v && v->type == J_STR ? v->str : NULL;
}

static double j_num(const JVal *o, const char *key, double dflt)
{
    const JVal *v = j_get(o, key);
    return v && v->type == J_NUM ? v->num : dflt;
}

static int j_bool(const JVal *o, const char *key, int dflt)
{
    const JVal *v = j_get(o, key);
    return v && v->type == J_BOOL ? (int)v->num : dflt;
}

#endif /* JSONLITE_H */
