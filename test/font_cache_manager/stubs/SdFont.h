#pragma once

// Host-test stub of lib/EpdFont/SdFont.h. FontCacheManager only clears the shared SD glyph cache,
// so the stub counts those calls.

class SdFontData {
 public:
  static void clearCache() { ++clearCacheCalls; }
  static inline int clearCacheCalls = 0;
};
