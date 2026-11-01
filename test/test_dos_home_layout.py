"""Host-test DOS home geometry for X3/X4 logical orientations and UI font sizes."""

from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include "DosHomeLayoutGeometry.h"

using dos_layout::FontMetrics;
using dos_layout::Geometry;

static void check(int width, int height, int lineHeight, bool print) {
  const FontMetrics font{lineHeight, lineHeight, lineHeight, lineHeight, lineHeight, 20, 12};
  const int recentLimit = dos_layout::recentBookLimit(width, height, font);
  assert(recentLimit >= 0);
  assert(recentLimit <= dos_layout::MAX_RECENT_BOOKS);
  assert(dos_layout::calculate(width, height, font, recentLimit).contentFits);
  const Geometry reference = dos_layout::calculate(width, height, font, 0);
  const int safeRuleWidth = width - 2 * 18;
  for (int equalsWidth = 1; equalsWidth <= lineHeight * 2; ++equalsWidth) {
    const int count = dos_layout::asciiRuleCharacterCount(width, 18, equalsWidth, 255);
    assert(count * equalsWidth <= safeRuleWidth);
    assert(count == 255 || (count + 1) * equalsWidth > safeRuleWidth);
  }

  for (int recentCount = 0; recentCount <= dos_layout::MAX_RECENT_BOOKS; ++recentCount) {
    const Geometry g = dos_layout::calculate(width, height, font, recentCount);
    if (recentCount <= recentLimit) assert(g.contentFits);
    if (recentCount > recentLimit) assert(!g.contentFits);
    assert(g.recentBookCount == recentCount);
    assert(g.header.y >= 0 && g.header.y + g.header.height <= height);
    assert(g.titleY >= g.header.y + font.statusStripHeight);
    assert(g.titleY + font.titleLineHeight <= g.memoryY);
    assert(g.memoryY + font.sectionLineHeight <= g.separatorY);
    assert(g.separatorY + font.sectionLineHeight <= g.header.y + g.header.height);
    assert(g.recent.y == g.header.y + g.header.height);
    assert(g.recent.y + g.recent.height == g.menu.y);
    assert(g.menu.y + g.menu.height <= height);
    assert(g.footer.y >= 0 && g.footer.y + g.footer.height <= height);
    assert(g.header.y == reference.header.y && g.header.height == reference.header.height);
    assert(g.titleY == reference.titleY && g.memoryY == reference.memoryY && g.separatorY == reference.separatorY);
    assert(g.footer.y == reference.footer.y && g.promptY == reference.promptY);
    assert(g.contentX >= 0 && g.contentX < width / 2);
    assert(g.selectionX >= 0 && g.selectionX + g.selectionWidth <= width);

    const int recentTextBottom = recentCount == 0
                                     ? g.recentRowsTop + lineHeight
                                     : g.recentRowsTop + (recentCount - 1) * g.recentRowStep +
                                           g.recentProgressOffset + lineHeight;
    assert(g.recentLabelY >= g.recent.y);
    assert(g.recentLabelY + lineHeight <= g.recentRowsTop);
    assert(g.recentRowsTop >= g.recent.y);
    assert(recentTextBottom <= g.syntaxErrorY);
    assert(g.syntaxErrorY + lineHeight <= g.recent.y + g.recent.height);
    assert(g.menuLabelY >= g.syntaxErrorY + lineHeight);
    assert(g.menuLabelY + lineHeight <= g.menuRowsTop);
    if (recentCount > recentLimit) continue;
    if (recentCount > 0 && recentCount <= recentLimit) {
      assert(g.recentProgressOffset + lineHeight <= g.recentRowStep);
      const int recentBlockGap = g.recentRowStep - g.recentProgressOffset - lineHeight;
      assert(recentBlockGap >= 1);
      assert(recentBlockGap <= std::max(2, std::min(lineHeight / 4, lineHeight / 3)));
      const int lastTitleY = g.recentRowsTop + (recentCount - 1) * g.recentRowStep;
      const int progressY = lastTitleY + g.recentProgressOffset;
      const auto selected = dos_layout::recentTitleSelection(g.selectionX, g.selectionWidth, g.recent.y,
                                                             g.recent.y + g.recent.height, lastTitleY, lineHeight,
                                                             progressY, g.sectionGap);
      assert(selected.height > 0);
      assert(selected.y <= lastTitleY);
      assert(selected.y + selected.height <= progressY);
      assert(selected.y + selected.height <= g.syntaxErrorY);
    }

    const int menuStepFour = dos_layout::menuRowStep(g, 4);
    const int menuStepFive = dos_layout::menuRowStep(g, 5);
    assert(menuStepFour == menuStepFive);
    for (int count = 4; count <= 5; ++count) {
      const int step = dos_layout::menuRowStep(g, count);
      const int rowHeight = dos_layout::menuRowHeight(g, count);
      const int rowsBottom = g.menuRowsTop + (count - 1) * step + rowHeight;
      assert(step >= rowHeight);
      assert(step == g.menuRowStep);
      assert(step - rowHeight >= 0);
      assert(step - rowHeight <= std::max(2, lineHeight / 4));
      assert(g.menuRowsTop >= g.menuLabelY + lineHeight);
      assert(rowsBottom <= g.menuRowsBottom);
      assert(rowsBottom <= g.promptY - g.sectionGap);
    }
    assert(g.promptY + lineHeight <= g.footer.y);

    if (print && recentCount == recentLimit) {
      std::printf("%dx%d: header=[%d,%d) recent%d=[%d,%d) syntax=%d menu=[%d,%d) rows=%d/%d prompt=%d footer=[%d,%d)\n",
                  width, height, g.header.y, g.header.y + g.header.height, recentCount, g.recent.y,
                  g.recent.y + g.recent.height, g.syntaxErrorY, g.menu.y, g.menu.y + g.menu.height,
                  g.menuRowsTop, dos_layout::menuRowStep(g, 4), g.promptY, g.footer.y, g.footer.y + g.footer.height);
    }
  }
}

int main() {
  // Board panel sizes are supplied in logical orientation by GfxRenderer.
  const int dimensions[][2] = {{480, 800}, {800, 480}, {528, 792}, {792, 528}};
  for (const auto& size : dimensions) {
    for (int lineHeight = 8; lineHeight <= 24; lineHeight += 4) {
      check(size[0], size[1], lineHeight, lineHeight == 20);
    }
  }

  char memoryLabel[32];
  dos_layout::formatFreeHeapLabel(memoryLabel, sizeof(memoryLabel), 243U * 1024U + 1023U);
  assert(std::strcmp(memoryLabel, "243K bytes free") == 0);
  const int availableRuleWidth = 480 - 2 * 18;
  const int ruleCount = dos_layout::asciiRuleCharacterCount(480, 18, 6, 255);
  assert(ruleCount > 0);
  assert(ruleCount * 6 <= availableRuleWidth);
  assert((ruleCount + 1) * 6 > availableRuleWidth);
  char asciiRule[256];
  std::fill_n(asciiRule, ruleCount, '=');
  asciiRule[ruleCount] = '\0';
  assert(static_cast<int>(std::strlen(asciiRule)) == ruleCount);
  for (int i = 0; i < ruleCount; ++i) assert(asciiRule[i] == '=');

  for (int textWidth = 18; textWidth <= 30; textWidth += 6) {
    const auto line = dos_layout::progressLine(480, 451, 28, 8, 7, 6, 6, 32, 6, 20);
    assert(line.segmentCount == 20);
    assert(line.barX >= 28);
    assert(line.barX + line.barWidth + line.gap == line.percentageColumnX);
    assert(line.percentageColumnX + line.percentageColumnWidth == 451);
    assert(line.percentageTextX(textWidth) + textWidth == 451);
  }
  const auto largeFontBar = dos_layout::progressLine(480, 451, 28, 18, 17, 12, 12, 52, 8, 20);
  assert(largeFontBar.segmentCount > 0 && largeFontBar.segmentCount < 20);
  assert(largeFontBar.barX >= 28);
  assert(largeFontBar.barX + largeFontBar.barWidth + largeFontBar.gap == largeFontBar.percentageColumnX);
}
"""

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "dos_home_layout.cpp"
    executable = Path(directory) / "dos_home_layout"
    cpp.write_text(HARNESS)
    compiler = [os.environ["ZIG"], "c++"] if os.environ.get("ZIG") else [os.environ.get("CXX", "c++")]
    subprocess.run(
        compiler
        + [
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "src/components/themes/dos"),
            str(cpp),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)

print("DOS home layout coordinate checks passed")
