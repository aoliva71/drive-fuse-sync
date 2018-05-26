This file is part of drive-fuse-sync.

drive-fuse-sync is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

drive-fuse-sync is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with drive-fuse-sync.  If not, see <http://www.gnu.org/licenses/>.

#ifndef _LOG_H_
#define _LOG_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>

int log_init(const char *);
int log_term(void);

int log_context(void **, const char *);

/*#ifdef HAVE_LIBLOG4C*/
#ifdef HAVE_LIBLOG4C_JUST_AS_EXAMPLE

#define log_debug(...)      log4c_category_debug(appender, __VA_ARGS__)
#define log_info(...)       log4c_category_info(appender, __VA_ARGS__)
#define log_warning(...)    log4c_category_warning(appender, __VA_ARGS__)
#define log_error(...)      log4c_category_error(appender, __VA_ARGS__)

#else /* HAVE_LIBLOG4C */

#define log_debug(...)      printf( __VA_ARGS__); printf("\n")
#define log_info(...)       printf( __VA_ARGS__); printf("\n")
#define log_warning(...)    printf( __VA_ARGS__); printf("\n")
#define log_error(...)      fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

#endif /* HAVE_LIBLOG4C */

#endif /* _LOG_H_ */

