#ifndef MCBL_STDLIB_H
#define MCBL_STDLIB_H

/*
 * McBL# Standard Library v2.0
 * ============================
 * System library lengkap seperti bahasa modern (Rust/Go/C++).
 * Semua diakses via keyword namespace di McBL#:
 *
 *   math.*    — matematika + kalkulus
 *   str.*     — string manipulation
 *   file.*    — file system
 *   net.*     — networking
 *   sys.*     — system / OS
 *   io.*      — input/output
 *   mem.*     — memory management (no GC, pure manual/refcount)
 *   arr.*     — array operations
 *   map.*     — hash map
 *   set.*     — hash set
 *   chan.*    — channels (concurrent)
 *   time.*    — time + date
 *   crypto.*  — hashing + encoding
 *   json.*    — JSON parse/emit
 *   path.*    — path manipulation
 *   proc.*    — process management
 *   rand.*    — random number generation
 *   sort.*    — sorting algorithms
 *   regex.*   — regular expressions
 *   buf.*     — byte buffer / builder
 *   fmt.*     — formatting
 *   env.*     — environment variables
 *   log.*     — logging
 *   test.*    — unit testing
 */

/* Include all sub-modules */
#include "mcbl_math_full.h"
#include "mcbl_str_full.h"
#include "mcbl_io.h"
#include "mcbl_mem.h"
#include "mcbl_collections.h"
#include "mcbl_time.h"
#include "mcbl_crypto.h"
#include "mcbl_json.h"
#include "mcbl_path.h"
#include "mcbl_proc.h"
#include "mcbl_sort.h"
#include "mcbl_fmt.h"
#include "mcbl_log.h"
#include "mcbl_test.h"

#endif /* MCBL_STDLIB_H */
