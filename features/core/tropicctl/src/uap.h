/*
 * uap.h -- the chip's configuration objects and the access ("UAP") words, as libtropic describes them.
 *
 * Layout (libtropic's headers and its hardware-wallet example): an access object is four bytes. Each byte covers one
 * group of targets; the four low bits of a byte stand for the pairing keys 0 to 3, and a SET bit grants access.
 *   single-target commands (ping, random, R-MEM, configuration)  byte 0 only
 *   pairing-key commands                                         byte N = the pairing slot acted on
 *   ECC commands                                                 byte N = ECC slots 8N to 8N+7
 *   monotonic counters                                           byte N = counters 4N to 4N+3
 *   MAC-and-Destroy                                              byte N = MAC-and-Destroy slots 32N to 32N+31
 * Clearing bit K in every byte therefore removes pairing key K from the command everywhere.
 */
#ifndef TROPICCTL_UAP_H
#define TROPICCTL_UAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "chip.h"

typedef enum { UAP_NONE = 0, UAP_SINGLE, UAP_PAIRING, UAP_ECC, UAP_COUNTER, UAP_MND } uap_kind_t;

uap_kind_t uap_kind(const char *name);                       /* kind of a configuration object, by its TR01_CFG_ name */
int uap_groups(uap_kind_t kind);                             /* how many bytes of the word matter (1 or 4) */
/* does pairing key `session` (0-3) have access to `target` (pairing slot, ECC slot, counter or MAC-and-Destroy slot;
 * ignored for single-target commands) under this word? */
int uap_allows(uint32_t word, uap_kind_t kind, int session, int target);
uint32_t uap_clear_key(uint32_t word, uap_kind_t kind, int key); /* remove pairing key `key` from every group */
void uap_group_label(uap_kind_t kind, int group, char *buf, size_t n); /* "ECC slots 8-15" ... */
void uap_describe_diff(FILE *f, const char *name, uint32_t oldw, uint32_t neww); /* which keys gain or lose access */
void uap_describe(FILE *f, const char *name, uint32_t word);     /* human-readable lines for one access object */
int uap_index(const chip_config_obj_t *o, const char *name);     /* position of a name in a dump, or -1 */

#endif
