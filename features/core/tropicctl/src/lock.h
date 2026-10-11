/* lock.h -- an exclusive lock around the chip: it allows one secure session at a time. */
#ifndef TROPICCTL_LOCK_H
#define TROPICCTL_LOCK_H

/* Waits up to timeout_sec for $TROPICCTL_LOCK (default /var/lock/tropicctl.lock). Returns 0 or a TC_* exit code. */
int lock_acquire(int timeout_sec);

#endif
