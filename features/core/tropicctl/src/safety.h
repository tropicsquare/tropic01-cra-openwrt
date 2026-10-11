/* safety.h -- the gate in front of irreversible and hazardous commands (SPEC.md section 4.4).
 *
 * A command consumes its named confirmation flag with args_flag() before it validates the rest of its arguments. Without
 * the flag it refuses (exit code 9) before the chip is touched, unless --dry-run was given: then it checks everything
 * read-only and says what it would do. */
#ifndef TROPICCTL_SAFETY_H
#define TROPICCTL_SAFETY_H

/* print the refusal for a missing confirmation flag and return TC_REFUSED */
int safety_refuse(const char *flag, const char *what);

#endif
