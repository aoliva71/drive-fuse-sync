bin_PROGRAMS = drivefusesync
AM_CFLAGS = -I$(top_srcdir)/include ${FUSE_CFLAGS} ${LIBCURL_CFLAGS} ${SQLITE3_CFLAGS}
drivefusesync_SOURCES = main.c driveapi.c dbcache.c fscache.c inotifyapi.c fuseapi.c
drivefusesync_LDADD = ${FUSE_LIBS} ${LIBCURL_LIBS} ${SQLITE3_LIBS}

