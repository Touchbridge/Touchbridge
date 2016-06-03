/*
 *
 * debug.h
 *
 * This file is part of Touchbridge
 *
 * Copyright 2015 James L Macfarlane
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef AEL_DEBUG
#define AEL_DEBUG

/*
 * AEL standard debugging and error-reporting macros.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#ifndef WARNING_FP
    #define WARNING_FP (stderr)
#endif

/*
 * This should be declared in main file and should normally be initialised to 0
 * and made settable on command line.
 */
extern int debug_level;

/*
 * This should be declared in main file and set to argv[0] at top of main
 * block.
 */
extern char *progname;

/*
 * PRINTD is for printing application specific messages which ease debugging
 * and discoverability.
 */
#define PRINTD(lvl, fmt, ...) do { \
        if (debug_level >= (lvl)) { \
            printf(fmt, ##__VA_ARGS__); \
        } \
    } while (0);

/*
 * SYSERROR is for fatal I/O related errors and invokes perror() before exiting.
 */
#define SYSERROR(fmt, ...) do { \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, ": "); \
        perror(""); \
        exit(1); \
    } while (0);

#define SYSERROR_IF(cond, fmt, ...) if (cond) { \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, ": "); \
        perror(""); \
        exit(1); \
    }

/*
 * ERROR is for fatal internal errors.
 */
#define ERROR(fmt, ...) do { \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, "\n"); \
        exit(1); \
    } while (0);

/*
 * WARNING is for reporting non-fatal internal warnings.
 */
#define WARNING(fmt, ...) do { \
        fprintf(WARNING_FP, "%s: ", progname); \
        fprintf(WARNING_FP, fmt, ##__VA_ARGS__); \
        fprintf(WARNING_FP, "\n"); \
    } while (0);

/*
 * SYSWARNING is for reporting non-fatal I/O errors via perror().
 */
#define SYSWARNING(fmt, ...) do { \
        fprintf(stderr, "%s: ", progname); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, ": "); \
        perror(""); \
    } while (0);


#endif /* AEL_DEBUG */
