/* cfgfile.h -- configuration files: "R NAME 0xVALUE" lines, the layout `config dump` prints. */
#ifndef TROPICCTL_CFGFILE_H
#define TROPICCTL_CFGFILE_H

#include <stdint.h>

#include "chip.h"

/* position of a configuration object by name; the TR01_CFG_ prefix may be left out. -1 if unknown. */
int cfg_find(const chip_config_obj_t *objs, const char *name);
/* Parse R lines (and bare "NAME 0xVALUE" lines) into words. I lines and comments are ignored. Every object must be
 * present exactly once. Returns 0, or -1 with a message in err. */
int cfg_parse(const char *path, const chip_config_obj_t *objs, uint32_t words[CHIP_CONFIG_OBJ_CNT], char *err, size_t errlen);
int cfg_write(const char *path, const chip_config_obj_t *objs, const uint32_t words[CHIP_CONFIG_OBJ_CNT], const char *comment);

#endif
