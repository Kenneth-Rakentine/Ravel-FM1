/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Ravel: projects in the platform's slots. A project is the whole rv_project, about 40 KB.
 * plat_load copies up to max bytes and returns the slot's stored length, so the header can be
 * checked before anything is copied over the playing project. */
#include "rv.h"

typedef struct { uint32_t magic, format; char name[12]; } rv_head;

int rv_save(int slot)
{
    rv.magic = RV_MAGIC;
    rv.format = RV_FORMAT;
    return plat_save(slot, &rv, sizeof(rv));
}

static int head_ok(int slot, rv_head *h)
{
    int n = plat_load(slot, h, sizeof(*h));
    return n == (int)sizeof(rv) && h->magic == RV_MAGIC && h->format == RV_FORMAT;
}

int rv_load(int slot)
{
    rv_head h;
    if (!head_ok(slot, &h))
        return -1;
    return plat_load(slot, &rv, sizeof(rv)) == (int)sizeof(rv) ? 0 : -1;
}

int rv_slot_used(int slot, char name[13])
{
    rv_head h;
    int i;
    name[0] = 0;
    if (!head_ok(slot, &h))
        return 0;
    for (i = 0; i < 11 && h.name[i]; i++)
        name[i] = h.name[i];
    name[i] = 0;
    return 1;
}
