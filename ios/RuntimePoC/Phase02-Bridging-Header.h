/*
 * Phase02-Bridging-Header.h — exposes the C harness and the ObjC bridge to Swift.
 *
 * PHASE_02_RECONSTRUCTED_POC.
 */
#ifndef PHASE02_BRIDGING_HEADER_H
#define PHASE02_BRIDGING_HEADER_H

#import "Phase02Bridge.h"

/* Only the Diagnostics API is exposed to Swift. RuntimeCore headers are deliberately
 * absent: Swift needs nothing from them that phase02_harness.h does not already
 * expose (see phase02_platform_summary), and every extra header here is another chance
 * to call something without a visible declaration. */
#include "phase02_harness.h"
#include "phase02_log.h"

#endif /* PHASE02_BRIDGING_HEADER_H */
