// PolyKybd port: engine-local config.h, hand-instantiated from
// ../cmake/config.h.cin (upstream generates this via CMake; we use QMK's build).
// MUST live in this directory: engine sources say `#include "config.h"`, and the
// same-directory quote-include is what keeps them from picking up the keyboard's
// own keyboards/polykybd/config.h further down the include path.
#pragma once

#define PACKAGE_NAME "rp2040-doom (PolyKybd)"
#define PACKAGE_TARNAME "rp2040-doom"
#define PACKAGE_VERSION "0.2"
#define PACKAGE_STRING "rp2040-doom (PolyKybd) 0.2"
#define PROGRAM_PREFIX ""

/* no libsamplerate, libpng, dirent, mmap on the keyboard */
#define HAVE_DECL_STRCASECMP 1
#define HAVE_DECL_STRNCASECMP 1

/* The full rp2040-doom "doom_tiny" compile-definition set — upstream passes it
 * on the compiler command line per CMake target; we scope it to engine files by
 * riding this config.h (pulled in early by every engine unit via doomtype.h). */
#include "doom_tiny_defs.h"
