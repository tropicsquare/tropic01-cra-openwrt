#include "safety.h"

#include "errors.h"
#include "output.h"

int safety_refuse(const char *flag, const char *what)
{
	return out_error(TC_REFUSED, "%s; this cannot be undone or is hazardous: give %s to confirm (or use --dry-run to see what would happen)", what, flag);
}
