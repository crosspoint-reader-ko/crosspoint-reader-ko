"""Host-test stable persisted theme IDs and device-specific option mapping."""

from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <cassert>
#include "UIThemeOptionMapping.h"

int main() {
  using namespace ui_theme_options;
  assert(CLASSIC == 0 && LYRA == 1 && LYRA_3_COVERS == 2 && ROUNDEDRAFF == 3);
  assert(COVER_GRID == 4 && DOS == 5);

  const uint8_t psramOptions[] = {CLASSIC, LYRA, LYRA_3_COVERS, ROUNDEDRAFF, COVER_GRID, DOS};
  assert(optionCount(true) == 6);
  for (uint8_t option = 0; option < optionCount(true); ++option) {
    assert(themeForOption(option, true) == psramOptions[option]);
    assert(optionForTheme(psramOptions[option], true) == option);
  }

  const uint8_t noPsramOptions[] = {CLASSIC, LYRA, LYRA_3_COVERS, ROUNDEDRAFF, DOS};
  assert(optionCount(false) == 5);
  for (uint8_t option = 0; option < optionCount(false); ++option) {
    assert(themeForOption(option, false) == noPsramOptions[option]);
    assert(optionForTheme(noPsramOptions[option], false) == option);
  }

  assert(optionForTheme(COVER_GRID, false) == LYRA);
  assert(!isAvailableTheme(COVER_GRID, false));
  assert(isAvailableTheme(COVER_GRID, true));
  assert(isAvailableTheme(DOS, false) && isAvailableTheme(DOS, true));
  assert(optionForTheme(255, true) == LYRA);
}
"""

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "home_theme_settings.cpp"
    executable = Path(directory) / "home_theme_settings"
    cpp.write_text(HARNESS)
    compiler = [os.environ["ZIG"], "c++"] if os.environ.get("ZIG") else [os.environ.get("CXX", "c++")]
    subprocess.run(
        compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src/components"), str(cpp), "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)

print("Home theme settings compatibility checks passed")
