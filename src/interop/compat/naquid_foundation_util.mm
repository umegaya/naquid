// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/mac/foundation_util.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/mac_logging.h"
#include "base/macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "build/build_config.h"

#if !defined(OS_IOS)
#import <AppKit/AppKit.h>
#endif

#if !defined(OS_IOS)
extern "C" {
CFTypeID SecACLGetTypeID();
CFTypeID SecTrustedApplicationGetTypeID();
Boolean _CFIsObjC(CFTypeID typeID, CFTypeRef obj);
}  // extern "C"
#endif

namespace base {
namespace mac {

void* CFTypeRefToNSObjectAutorelease(CFTypeRef cf_object) {
  // When GC is on, NSMakeCollectable marks cf_object for GC and autorelease
  // is a no-op.
  //
  // In the traditional GC-less environment, NSMakeCollectable is a no-op,
  // and cf_object is autoreleased, balancing out the caller's ownership claim.
  //
  // NSMakeCollectable returns nil when used on a NULL object.
  return [NSMakeCollectable(cf_object) autorelease];
}

}
}