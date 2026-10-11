#include "lock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "output.h"

int lock_acquire(int timeout_sec)
{
	const char *path = getenv("TROPICCTL_LOCK");
	struct timespec nap = {0, 100 * 1000 * 1000};
	int fd, tries = timeout_sec * 10;
	if (!path || !*path) path = "/var/lock/tropicctl.lock";
	fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0)
		return out_error(TC_UNREACHABLE, "cannot open the lock file %s: %s (use --no-lock only for tests)", path,
		                 strerror(errno));
	for (;;) {
		if (flock(fd, LOCK_EX | LOCK_NB) == 0) return TC_OK; /* held until the process exits */
		if (errno != EWOULDBLOCK) return out_error(TC_UNREACHABLE, "lock %s: %s", path, strerror(errno));
		if (tries-- <= 0)
			return out_error(TC_UNREACHABLE,
			                 "the chip is busy: another process holds %s (it allows one secure session at a time)", path);
		nanosleep(&nap, NULL);
	}
}
