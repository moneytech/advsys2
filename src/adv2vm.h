/* adv2vm.h - definitions for a simple virtual machine
 *
 * Copyright (c) 2018 by David Michael Betz.  All rights reserved.
 *
 */

#ifndef __ADV2VM_H__
#define __ADV2VM_H__

#include "adv2image.h"

#define MAXSTACK    128

/* prototypes from adv2exe.c */
int Execute(ImageHdr *image, int debug);

#endif
