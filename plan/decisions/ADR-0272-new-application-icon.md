# ADR-0272 — New application icon

- **Status:** Accepted
- **Date:** 2026-11-05
- **Decided by:** user (Matej Strnad)

## Context

Plan 0015 A3 reused the v1 icon (`icons.icns` / `icons.ico` from branch `main`) as the application icon;
that decision was recorded only in the plan 0015 decision table (row "App icon source", no ADR) and in
`plan/open-questions.md` #27 ("a later brand icon only replaces the files"). On 2026-11-05 the user supplied a
new icon: a 512×512 RGBA PNG, a red/grey circuit-trace "H" on a white square background.

## Options considered

- Keep the v1 icon.
- Use the supplied PNG as the single master and generate every platform format from it.

## Decision

The supplied PNG becomes the master `packaging/common/icons/hwio-monitor-source.png` (Git LFS). This
supersedes the icon part of the plan 0015 A3 decision. `packaging/common/icons/extract.sh` (python3 -I + Pillow)
generates from it the hicolor PNGs `hwio-monitor-{16,24,32,48,64,128,256,512}.png` (Lanczos),
`hwio-monitor.ico` (16, 24, 32, 48, 64, 128, 256) and `hwio-monitor.icns` (16…512 including @2x up to
256@2x; written as PNG entries). The master is never upscaled, so the .icns has no 1024 px (512@2x) entry.
File names and all build/packaging consumers stay unchanged.

## Consequences

- One source file for all platforms; replacing the icon again = replace the master and rerun extract.sh.
- macOS Retina at 512 pt has no native 1024 px representation; a 1024 px master would fix it.
- The icon is a full white square without rounded corners or padding, so it looks different from native
  macOS (squircle) icons; a macOS-specific variant can be added later if wanted.
