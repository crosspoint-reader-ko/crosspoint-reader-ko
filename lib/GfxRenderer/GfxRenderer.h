#pragma once

#include <EpdFontFamily.h>
#include <HalDisplay.h>
#include <SdFontFamily.h>

namespace BidiUtils {
// Paragraph base direction for the Unicode BiDi algorithm (UAX#9).
// AUTO: scan text for first strong directional character (P2/P3 rules)
// LTR:  force left-to-right paragraph embedding level
// RTL:  force right-to-left paragraph embedding level
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}  // namespace BidiUtils

class FontCacheManager;

#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Bitmap.h"

namespace glyphBitmap {
struct Frame;
}

// Color representation: uint8_t mapped to 4x4 Bayer matrix dithering levels
// 0 = transparent, 1-16 = gray levels (white to black)
enum Color : uint8_t { Clear = 0x00, White = 0x01, LightGray = 0x05, DarkGray = 0x0A, Black = 0x10 };

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  // Logical screen orientation from the perspective of callers
  enum Orientation {
    Portrait,                  // 480x800 logical coordinates (current default)
    LandscapeClockwise,        // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    PortraitInverted,          // 480x800 logical coordinates, inverted
    LandscapeCounterClockwise  // 800x480 logical coordinates, native panel orientation
  };

 private:
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;  // 8KB chunks to allow for non-contiguous memory

  HalDisplay& display;
  RenderMode renderMode;
  mutable bool absoluteGrayPlanes = false;
  bool ditherImagesInBw = false;
  Orientation orientation;
  bool fadingFix;
  uint8_t* frameBuffer = nullptr;
  uint16_t panelWidth = HalDisplay::DISPLAY_WIDTH;
  uint16_t panelHeight = HalDisplay::DISPLAY_HEIGHT;
  uint16_t panelWidthBytes = HalDisplay::DISPLAY_WIDTH_BYTES;
  uint32_t frameBufferSize = HalDisplay::BUFFER_SIZE;
  std::vector<uint8_t*> bwBufferChunks;
  // Unified font registry: each slot wraps either a flash EpdFontFamily (non-owning) or an
  // owned SD-card SdFontFamily, so both render through one path. UnifiedFontFamily also carries
  // an optional glyph-level fallback (Hangul/Latin UI font backed by an SD "system font").
  std::map<int, UnifiedFontSlot> fontMap;
  int fallbackFontId = 0;  // Default fallback font ID (set after fonts are loaded)
  // UI system-font redirect: requests for fontRedirectFrom_ resolve to fontRedirectTo_ in
  // getEffectiveFontId(). Used to replace the whole UI font with a user-selected SD "system
  // font" while keeping the original UI font registered (as that font's glyph fallback).
  // 0 = inactive. Two scalars instead of a map to avoid any heap allocation.
  int fontRedirectFrom_ = 0;
  int fontRedirectTo_ = 0;

  // Mutable because drawText() is const but needs to delegate scan-mode
  // recording to the (non-const) FontCacheManager.
  mutable FontCacheManager* fontCacheManager_ = nullptr;

  // One-shot refresh promotion (see promoteNextRefresh). Mutable because
  // displayBuffer() is const but must consume the flag.
  mutable bool promotedRefreshPending_ = false;
  mutable HalDisplay::RefreshMode promotedRefresh_ = HalDisplay::FAST_REFRESH;
  // Swap in (and clear) the promoted mode, if one is pending.
  HalDisplay::RefreshMode applyPromotedRefresh(HalDisplay::RefreshMode refreshMode) const;

  // Tiled grayscale strip target. When active, drawPixel()/clearScreen()
  // operate on a caller-owned scratch holding one horizontal band of physical
  // rows [_stripY0, _stripY0 + _stripRows) (panelWidthBytes wide) instead of
  // the shared framebuffer, clipping pixels outside the band. Lets grayscale
  // planes render band-by-band straight to the controller without destroying
  // the BW framebuffer (no storeBwBuffer). Mutable because the render path is
  // const. See beginStripTarget()/endStripTarget().
  mutable uint8_t* _stripBuf = nullptr;
  mutable int _stripY0 = 0;
  mutable int _stripRows = 0;
  mutable bool _stripActive = false;
  mutable int clipLeft_ = 0;
  mutable int clipTop_ = 0;
  mutable int clipRight_ = 32767;
  mutable int clipBottom_ = 32767;

  // Shared implementation behind both drawText overloads (Korean letter-spacing + upstream tracking).
  void drawTextImpl(int fontId, int x, int y, const char* text, int8_t letterSpacing, bool black,
                    EpdFontFamily::Style style, BidiUtils::BidiBaseDir baseDir, int8_t tracking) const;
  void freeBwBufferChunks();
  template <Color color>
  void drawPixelDither(int x, int y) const;
  template <Color color>
  void fillArc(int maxRadius, int cx, int cy, int xDir, int yDir) const;
  // Byte-aligned, orientation-specialized rectangle fill. Rotates the rect's
  // two opposing corners into physical-framebuffer space once, then walks each
  // physical row with head-mask / middle memset / tail-mask byte writes — no
  // per-pixel rotation, no per-pixel RMW.
  template <Color color>
  void fillRectImpl(int x, int y, int width, int height) const;

 public:
  explicit GfxRenderer(HalDisplay& halDisplay)
      : display(halDisplay), renderMode(BW), orientation(Portrait), fadingFix(false) {}
  ~GfxRenderer() { freeBwBufferChunks(); }

  // Setup
  void begin();  // must be called right after display.begin()

  // Font registry (Korean API — supports both flash and SD-card fonts)
  // Flash fonts (EpdFontFamily) - stores pointer to global font
  void insertFont(int fontId, const EpdFontFamily* font);
  // SD card fonts (SdFontFamily) - takes ownership
  void insertSdFont(int fontId, SdFontFamily* font);
  // Set fallback font ID (used when requested font is not found)
  void setFallbackFont(int fontId) { fallbackFontId = fontId; }
  // Glyph-level fallback: when targetFontId lacks a real glyph for a codepoint, that glyph is
  // rendered from fallbackFontId instead. Used to back the Hangul/Latin UI font with a
  // user-selected SD "system font" for Hanja/Kana. Both fonts must already be registered.
  // Returns false if either font is not found.
  bool setGlyphFallback(int targetFontId, int fallbackFontId);
  // Remove a previously-set glyph-level fallback from targetFontId.
  void clearGlyphFallback(int targetFontId);
  // Whole-font redirect: text requests for fromId resolve to toId (see getEffectiveFontId).
  // Used to swap the entire UI font over to a user-selected SD "system font" while keeping the
  // original UI font registered as that font's glyph-level fallback. Both ids should be registered.
  void setFontRedirect(int fromId, int toId) {
    fontRedirectFrom_ = fromId;
    fontRedirectTo_ = toId;
  }
  // Remove the whole-font redirect (UI reverts to its native font).
  void clearFontRedirect() {
    fontRedirectFrom_ = 0;
    fontRedirectTo_ = 0;
  }
  // Check if a font is registered
  bool hasFont(int fontId) const { return fontMap.find(fontId) != fontMap.end(); }
  // Remove a font from the registry (frees memory for SD fonts)
  bool removeFont(int fontId);
  // Get effective font ID (returns redirect target / fallback if requested font not found)
  int getEffectiveFontId(int fontId) const;

  // FontCacheManager integration (upstream)
  void setFontCacheManager(FontCacheManager* m) { fontCacheManager_ = m; }
  FontCacheManager* getFontCacheManager() const { return fontCacheManager_; }
  // Upstream batch-prewarms CJK fallback glyphs from its .cpfont SD fonts here. In the Korean
  // build UnifiedFontFamily resolves fallback glyphs per glyph and SD .epdfont glyphs stay
  // resident, so there is nothing to prewarm: these are no-ops kept for upstream call sites.
  using TextGetter = const char* (*)(const void* ctx, uint32_t index);
  static void prewarmFallbackText(int /*fontId*/, TextGetter /*getter*/, const void* /*ctx*/, uint32_t /*textCount*/,
                                  EpdFontFamily::Style /*style*/ = EpdFontFamily::REGULAR) {}
  static void prewarmFallbackText(int /*fontId*/, const char* /*text*/,
                                  EpdFontFamily::Style /*style*/ = EpdFontFamily::REGULAR) {}
  bool isFontCacheScanning() const;
  const std::map<int, UnifiedFontSlot>& getFontMap() const { return fontMap; }

  // Orientation control (affects logical width/height and coordinate transforms)
  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  // Fading fix control
  void setFadingFix(const bool enabled) { fadingFix = enabled; }

  // Screen ops
  int getScreenWidth() const;
  int getScreenHeight() const;
  void tapToLogical(float nx, float ny, int& outX, int& outY) const;
  void displayBuffer(HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  // One-shot: the next displayBuffer()/displayBufferAsync() call uses `mode`
  // instead of what its caller asked for, then the override clears itself.
  // Lets a closing overlay (the control center's refresh tile) hand a
  // ghost-cleanup waveform to the repaint of whatever screen is underneath,
  // which it cannot reach directly.
  void promoteNextRefresh(const HalDisplay::RefreshMode mode) const {
    promotedRefreshPending_ = true;
    promotedRefresh_ = mode;
  }
  // Non-blocking refresh: starts the waveform and returns so CPU work (e.g.
  // grayscale strip rendering) can overlap the panel's refresh time. The
  // framebuffer must stay untouched until waitRefreshComplete(). Falls back to
  // a blocking refresh when fadingFix is enabled or the panel lacks deferral
  // support. See HalDisplay::displayBufferAsync for the baseline contract.
  void displayBufferAsync(HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  void waitRefreshComplete() const;
  // True when displayBufferAsync() genuinely overlaps: panel defers and
  // fadingFix isn't forcing the blocking path. Callers can skip overlap
  // scaffolding (e.g. whole-plane grayscale buffers) when false.
  bool supportsAsyncRefresh() const;
  // True when the display can overlap an ordinary B/W refresh with grayscale
  // composition without bypassing a required grayscale base waveform.
  HalDisplay::GrayscaleCapabilities grayscaleCapabilities(
      HalDisplay::GrayscaleMode mode = HalDisplay::GrayscaleMode::Overlay) const;
  // Compatibility queries for Overlay mode.
  bool supportsAsyncGrayscaleBase() const;
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  // void displayWindow(int x, int y, int width, int height) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;

  // Tiled grayscale strip target. While active, drawPixel() and clearScreen()
  // operate on `scratch` (panelWidthBytes * stripRows bytes, holding physical
  // rows [stripY0, stripY0 + stripRows)) instead of the framebuffer; pixels
  // whose physical row falls outside the band are clipped. The clip is applied
  // after the orientation rotate, so it is orientation-agnostic. Used to render
  // grayscale planes band-by-band without a full second buffer.
  void beginStripTarget(uint8_t* scratch, int stripY0, int stripRows) const;
  void endStripTarget() const;

  // Band culling for tiled grayscale. Takes a glyph bounding box in logical
  // screen coords and returns false only when a strip is active AND the box's
  // physical y-extent lies entirely outside the active band, letting callers
  // skip an expensive bitmap decode. Returns true when no strip is active.
  // Corners are rotated to physical, so it is orientation-aware.
  bool glyphIntersectsStrip(int x0, int y0, int x1, int y1) const;

  // Active pixel-write target for raw writers (DirectPixelWriter) that bypass
  // drawPixel for speed. When a strip target is active these return the band
  // scratch plus its physical-row origin and extent; otherwise the full
  // framebuffer ([0, panelHeight)). Writers subtract the origin and clip to the
  // extent, so they honor tiled-grayscale banding without per-pixel method calls.
  uint8_t* getWriteTarget() const { return _stripActive ? _stripBuf : frameBuffer; }
  int getWriteOriginY() const { return _stripActive ? _stripY0 : 0; }
  int getWriteRows() const { return _stripActive ? _stripRows : panelHeight; }

  // Drawing
  // UI drawing clip in logical coordinates; independent of panel orientation.
  std::array<int, 4> getClipRect() const {
    return {clipLeft_, clipTop_, clipRight_ - clipLeft_, clipBottom_ - clipTop_};
  }
  void setClipRect(int x, int y, int width, int height) const {
    clipLeft_ = x;
    clipTop_ = y;
    clipRight_ = x + width;
    clipBottom_ = y + height;
  }
  void drawPixel(int x, int y, bool state = true) const;
  // Draw glyph ink with clipping and orientation resolved once per glyph.
  void drawGlyphBitmap(const uint8_t* bitmap, int width, int height, const glyphBitmap::Frame& frame, bool twoBit,
                       RenderMode mode, bool state) const;
  void drawLine(int x1, int y1, int x2, int y2, bool state = true) const;
  void drawLine(int x1, int y1, int x2, int y2, int lineWidth, bool state) const;
  void drawArc(int maxRadius, int cx, int cy, int xDir, int yDir, int lineWidth, bool state) const;
  void drawRect(int x, int y, int width, int height, bool state = true) const;
  void drawRect(int x, int y, int width, int height, int lineWidth, bool state) const;
  void drawRoundedRect(int x, int y, int width, int height, int lineWidth, int cornerRadius, bool state) const;
  void drawRoundedRect(int x, int y, int width, int height, int lineWidth, int cornerRadius, bool roundTopLeft,
                       bool roundTopRight, bool roundBottomLeft, bool roundBottomRight, bool state) const;
  void maskRoundedRectOutsideCorners(int x, int y, int width, int height, int radius, Color color = Color::White) const;
  void fillRect(int x, int y, int width, int height, bool state = true) const;
  void fillRectDither(int x, int y, int width, int height, Color color) const;
  void fillRoundedRect(int x, int y, int width, int height, int cornerRadius, Color color) const;
  void fillRoundedRect(int x, int y, int width, int height, int cornerRadius, bool roundTopLeft, bool roundTopRight,
                       bool roundBottomLeft, bool roundBottomRight, Color color) const;
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawIcon(const uint8_t bitmap[], int x, int y, int size) const;
  bool drawBitmap(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight, float cropX = 0, float cropY = 0,
                  bool whiteAsTransparent = false) const;
  bool drawBitmap1Bit(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight) const;
  // Counter-invert content images in the logical framebuffer so output-level
  // dark mode leaves their original polarity unchanged.
  void preserveImagePolarity(int x, int y, int width, int height) const;
  void fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state = true) const;

  // Snapshot / restore a screen-coordinate framebuffer region (byte-aligned in
  // panel memory). readFramebufferRegion returns the bytes written to dst, or
  // 0 when the region is empty, offscreen, or exceeds dstCapacity. Pass the
  // same rectangle to writeFramebufferRegion to restore the saved pixels.
  // Enables partial-repaint patterns (e.g. moving a selection highlight)
  // without re-rendering the whole page.
  size_t readFramebufferRegion(int x, int y, int w, int h, uint8_t* dst, size_t dstCapacity) const;
  void writeFramebufferRegion(int x, int y, int w, int h, const uint8_t* src);

  // Text
  // Layout may use advance-only SD font tables; rendered measurement includes kerning and ligatures.
  enum class TextMeasureMode { Layout, Rendered };
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO) const;
  void drawCenteredText(int fontId, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                        BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO) const;
  void drawText(int fontId, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO, int8_t tracking = 0) const;
  // Korean letter-spacing overload (invariant).
  void drawText(int fontId, int x, int y, const char* text, int8_t letterSpacing, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getSpaceWidth(int fontId, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  /// Returns the total inter-word advance: fp4::toPixel(spaceAdvance + kern(leftCp,' ') + kern(' ',rightCp)).
  /// Using a single snap avoids the +/-1 px rounding error that arises when space advance and kern are
  /// snapped separately and then added as integers.
  int getSpaceAdvance(int fontId, uint32_t leftCp, uint32_t rightCp, EpdFontFamily::Style style) const;
  /// Returns kerning plus optional tracking between two adjacent codepoints.
  int getKerning(int fontId, uint32_t leftCp, uint32_t rightCp, EpdFontFamily::Style style, int8_t tracking = 0) const;
  int getTextAdvanceX(int fontId, const char* text, EpdFontFamily::Style style, int8_t tracking = 0,
                      BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO,
                      TextMeasureMode mode = TextMeasureMode::Layout) const;
  int getFontAscenderSize(int fontId) const;
  int getLineHeight(int fontId) const;
  int getLineHeight(int fontId, float compression) const;
  std::string truncatedText(int fontId, const char* text, int maxWidth,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  /// Word-wrap \p text into at most \p maxLines lines, each no wider than
  /// \p maxWidth pixels. Overflowing words and excess lines are UTF-8-safely
  /// truncated with an ellipsis (U+2026).
  std::vector<std::string> wrappedText(int fontId, const char* text, int maxWidth, int maxLines,
                                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // Helper for drawing rotated text (90 degrees clockwise, for side buttons)
  void drawTextRotated90CW(int fontId, int x, int y, const char* text, bool black = true,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getTextHeight(int fontId) const;

  // Grayscale functions
  void setRenderMode(RenderMode mode);
  RenderMode getRenderMode() const { return renderMode; }
  // Grayscale preconditioning settle pass (no-op on X4). The rect overload
  // takes the gray region in LOGICAL screen coordinates and rotates it to the
  // panel; the no-arg overload settles the full frame. Call after the BW base
  // frame is displayed and before the grayscale planes are written.
  void preconditionGrayscale() const;
  void preconditionGrayscale(int x, int y, int w, int h) const;
  // Display the framebuffer as the base frame for a grayscale overlay that
  // follows (X3: OEM differential base waveform; others: plain display with
  // `fallback`).
  void displayGrayscaleBase(HalDisplay::RefreshMode fallback = HalDisplay::HALF_REFRESH) const;
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode mode,
                            HalDisplay::RefreshMode fallback = HalDisplay::HALF_REFRESH) const;
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer() const;
  // Active input encoding, used when drawing monochrome overlays into planes.
  bool grayPlanesAreAbsolute() const { return absoluteGrayPlanes; }
  // B/W pass: draw 2-bit image pixels as a 2x2 ordered dither instead of
  // "any ink is black". Only for pages that get no grayscale pass, because
  // overlay gray planes expect every gray pixel black in the base.
  void setDitherImagesInBw(bool enabled) { ditherImagesInBw = enabled; }
  bool imagesDitheredInBw() const { return ditherImagesInBw; }

  // Tiled grayscale (X4): stream one band of a plane straight to controller RAM
  // from `scratch` (panelWidthBytes * numRows, physical rows [yStart, yStart+
  // numRows)), bypassing the framebuffer. supportsStripGrayscale() gates use.
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* scratch, int yStart, int numRows) const;
  bool supportsStripGrayscale() const;
  // Paper Mono: the base activation is deferred so base + gray planes go out
  // as one waveform. Route the base through displayGrayscaleBase() when true.
  bool combinesGrayscaleBase() const;
  bool storeBwBuffer();  // Returns true if buffer was stored successfully
  // Restore and free the stored buffer. resyncPanelBaseline rewrites the
  // controller's differential baseline to the restored frame — correct after
  // a grayscale render (the glass matches the stored BW plane), WRONG when
  // the glass shows content painted after the store (overlay chrome): the
  // next differential would treat that content as already erased and leave
  // it on the glass. Such callers pass false so the baseline keeps tracking
  // what was last pushed.
  void restoreBwBuffer(bool resyncPanelBaseline = true);
  // Free a stored buffer without restoring it (the page under it changed).
  void discardStoredBwBuffer() { freeBwBufferChunks(); }
  void cleanupGrayscaleWithFrameBuffer() const;

  // Font helpers
  const uint8_t* getGlyphBitmap(const EpdFontData* fontData, const EpdGlyph* glyph) const;

  // Lend the 48 KB framebuffer's bytes to a memory-hungry phase (chapter
  // builds) WITHOUT freeing the allocation, so it never moves and repeated
  // loans cannot fragment the heap. Between release and restore NOTHING may
  // draw or display — the panel keeps showing its last refreshed image. The
  // lent bytes are published via buildscratch::claim() for consumers like
  // InflateStream. restore returns the buffer white, so the caller must
  // redraw the full screen; it cannot fail (no allocation involved).
  void releaseFrameBufferForBuild();
  bool restoreFrameBufferAfterBuild();
  bool hasFrameBuffer() const { return frameBuffer != nullptr; }

  // RAII form of the loan above, for blocking build regions with early-return
  // error paths: restores on scope exit (or explicitly via end()). Display the
  // popup/screen the panel should hold BEFORE constructing one. Constructing
  // while the framebuffer is already lent yields an inert loan (nesting-safe).
  class FrameBufferLoan {
   public:
    explicit FrameBufferLoan(GfxRenderer& renderer);
    ~FrameBufferLoan() { end(); }
    void end();
    FrameBufferLoan(const FrameBufferLoan&) = delete;
    FrameBufferLoan& operator=(const FrameBufferLoan&) = delete;

   private:
    GfxRenderer& renderer_;
    bool active_ = false;
  };

  // Low level functions
  uint8_t* getFrameBuffer() const;
  size_t getBufferSize() const;
  uint16_t getDisplayWidth() const { return panelWidth; }
  uint16_t getDisplayHeight() const { return panelHeight; }
  uint16_t getDisplayWidthBytes() const { return panelWidthBytes; }

  // Region cache: take a logical (orientation-aware) rect, hit the framebuffer
  // bytes that the rect can have touched, and pump them in or out of a caller-
  // supplied buffer. Used by HomeActivity to snapshot just the cover tile
  // (~16 KB in Portrait) instead of cloning the entire 48 KB framebuffer.
  //
  // getRegionByteSize: required buffer length for the rect at current orientation.
  // copyRegionToBuffer / copyBufferToRegion: false if `bufSize` is smaller than that.
  size_t getRegionByteSize(int logicalX, int logicalY, int logicalW, int logicalH) const;
  bool copyRegionToBuffer(int logicalX, int logicalY, int logicalW, int logicalH, uint8_t* buf, size_t bufSize) const;
  bool copyBufferToRegion(int logicalX, int logicalY, int logicalW, int logicalH, const uint8_t* buf,
                          size_t bufSize) const;
};
