// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

// Revision 2 boards (left "Rev 2 04/2023", right "Rev.2 03/2023") pass VBUS over the
// split cable, and the GP24 divider taps VSYS, so GP24 reads high on BOTH halves and
// both come up as master. Without USB_VBUS_PIN, chibios_config.h defines
// SPLIT_USB_DETECT and the master is the half that enumerates on USB. Rev3 (09/2023)
// removed VBUS from the split port, which is why the default keymap can use GP24.
//
// Everything else comes from the default keymap, so this file cannot fall behind it.
#include "../default/config.h"
#undef USB_VBUS_PIN
