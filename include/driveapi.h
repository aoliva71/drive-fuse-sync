
#ifndef _DRIVE_API_H_
#define _DRIVE_API_H_

#define DRIVE_USER_MAX     63
#define DRIVE_PASSWD_MAX   63

int drive_start(void);
int drive_stop(void);

int drive_login(const char *, const char *);
int drive_logout(void);

#endif /* _DRIVE_API_H_ */

