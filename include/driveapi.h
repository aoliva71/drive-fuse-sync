
#ifndef _DRIVE_API_H_
#define _DRIVE_API_H_

#include <stdio.h>

int drive_setup(void);

int drive_start(void);
int drive_stop(void);

int drive_download(const char *, FILE *);

#endif /* _DRIVE_API_H_ */

