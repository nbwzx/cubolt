/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* version.h -- the version, the name beside it, and the copyright the resource block carries. */
#ifndef VERSION_H
#define VERSION_H

#define CUBOLT_VERSION_MAJOR 0
#define CUBOLT_VERSION_MINOR 3
#define CUBOLT_VERSION_PATCH 1

#define CUBOLT_NAME    "Cubolt"
#define CUBOLT_AUTHOR  "Zixing Wang"
#define CUBOLT_EMAIL   "zixingwang.cn@gmail.com"
#define CUBOLT_YEAR    "2026"

/* Two levels: the stringize operator does not expand its argument. */
#define CUBOLT_STR2_(x) #x
#define CUBOLT_STR_(x)  CUBOLT_STR2_(x)
#define CUBOLT_VERSION  CUBOLT_STR_(CUBOLT_VERSION_MAJOR) "." \
                        CUBOLT_STR_(CUBOLT_VERSION_MINOR) "." \
                        CUBOLT_STR_(CUBOLT_VERSION_PATCH)

#define CUBOLT_TITLE    CUBOLT_NAME " " CUBOLT_VERSION

#endif /* VERSION_H */
