# Cinema compatibility development maps

These no-note maps are retained as manual development fixtures for comparing
Big Screen on Quest with Cinema on PC. They are source/test assets only.

They are deliberately outside `src`, `assets`, the QMOD manifest, and every
source-deployment plan. `Build-And-Deploy.bat`, QMOD installation, MBF, and the
mod at runtime must never copy these folders to a headset or Beat Saber
installation. A developer who wants to use one must copy it manually into the
appropriate PC `CustomWIPLevels` folder or Quest WIP/custom-level location.

- `individual-tests/` contains one map per Cinema compatibility behavior for
  side-by-side PC and Quest checks.
- `quest-effects-cycle/` contains the combined Quest-only cycle that changes
  presentation phases every ten seconds through Big Screen's internal test
  harness.

These maps are not the downloadable **Big Screen Showcase**. The showcase uses
an explicitly user-requested, pinned BeatSaver map and remains governed by its
separate readiness/download workflow.
