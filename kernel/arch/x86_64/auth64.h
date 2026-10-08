#ifndef POLLIK_X64_AUTH_H
#define POLLIK_X64_AUTH_H

/* Authenticate or create the shared PollikOS account on the mounted data disk.
 * Returns nonzero only after a valid account has been verified or created. */
int auth64_login(void);
int auth64_change_password(void);
int auth64_elevate(void);

#endif
