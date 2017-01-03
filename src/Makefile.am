bin_PROGRAMS = drivefusesync
AM_CFLAGS = -I$(top_srcdir)/include ${FUSE_CFLAGS} ${CURL_CFLAGS} ${JSONC_CFLAGS} ${SQLITE3_CFLAGS}
drivefusesync_SOURCES = main.c driveapi.c dbcache.c fscache.c inotifyapi.c fuseapi.c
drivefusesync_LDADD = ${FUSE_LIBS} ${CURL_LIBS} ${JSONC_LIBS} ${SQLITE3_LIBS}

