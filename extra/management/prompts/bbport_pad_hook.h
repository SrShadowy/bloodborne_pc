// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the pad provides to the guest, on top of SDL. It is one header with three parts
// (each of them static inline, so the C runtime files can call it without a link dependency):
//   bbport_pad_hook_identity.h  which pad is in use, and what its buttons are called;
//   bbport_pad_hook_touch.h     the guest's 1920x943 touch panel (SDL's touches and clicks);
//   bbport_pad_hook_input.h     gyro, the hold-to-walk modifier and the cutscene skip.
// Include this file: it keeps the order the parts expect.
#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbport_pad_hook_identity.h"
#include "bbport_pad_hook_touch.h"
#include "bbport_pad_hook_input.h"
