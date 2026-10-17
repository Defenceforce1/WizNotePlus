# Native destination transform candidate — NOT engine-tested

This directory is a **draft**, not part of the App build or package workflow.
No installed SDK/framework is patched. `git apply --check` proves applicability,
not C++ compilation, passing Blink tests, or accurate PDFs.

## Pinned source

The existing Qt 6.11.3 SDK's SBOM identifies the QtWebEngine module as
`8c73295524f3ce6a2e10ad2f71141c695376c876`. Its `src/3rdparty` gitlink identifies
Chromium source `7927ce61b5a302a3da8172eefbe260ec17484bcd`.
The official source archive's `.tag` matches the module; both original files
match the SHA-256 values in `source-manifest.json`.

Official source component metadata:
`https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/6113/src_doc_examples/Updates.xml`

Archive component: `extensions.qtwebengine.6113.src/`

Archive: `6.11.3-0-202609240437qtwebengine-everywhere-src-6.11.3.tar.xz`

Official archive SHA-1: `e545e2a47265b873f685a513f2768c259799c623`

The Unix archive is **rootless**: extract into a new empty directory WITHOUT
`--strip-components`. Validate relative, non-traversing archive entries first.
Do not extract into an existing SDK, repository or Applications.

```sh
node tests/standalone-qt-pdf/engine-patches/check-source.cjs \
  /absolute/unmodified/qtwebengine-source
```

The checker is read-only: it verifies exact originals, patch hash and
applicability. It does not apply, compile, install, or download anything.

## Candidate and regression design

`OutputLinkedDestinations` currently records destinations under the passed root
properties and subtracts the page rectangle. The candidate records paginated
destinations under `PaginationState::ContentAreaPropertyTreeStateForCurrentPage`
instead, keeping the stitched point because that state's transform already
subtracts the stitched-page offset. Non-paginated capture retains its existing
page-relative behavior. No CSS, IDs, source text, viewport or PDF postprocessing
is changed by this draft.

The additional Blink regression compares each self-linked block's annotation
rectangle with its named destination point, across two pages, asymmetric
margins, wide-content shrinking and explicit print scaling. It is **not run**:
Qt's normal build does not automatically compile Chromium's `blink_unittests`.
Old behavior must fail this regression; candidate behavior must pass. Existing
print-context tests and non-paginated preview must also remain valid.

## Isolated build requirements and acceptance

Configure an unmodified source baseline first; keep all output/staging outside
the existing SDK. Never install into the SDK used by the running App. Use the
same build configuration when applying the draft incrementally afterward.

- In addition to `CMAKE_MAKE_PROGRAM`, set `Ninja_EXECUTABLE` explicitly: the
  module's nested Chromium build uses its own imported Ninja target.
- Put the task-local CMake/Ninja bin directory on the build process's `PATH`
  too. The separate GN bootstrap configures another CMake project and does not
  inherit the outer `CMAKE_MAKE_PROGRAM`; without `PATH` it stops before Chromium.
- Set `NINJAFLAGS='-j3 -l6'` **at CMake configure time**, not merely at build time;
  this module embeds the flags in generated custom commands.
- Set `CMAKE_OSX_DEPLOYMENT_TARGET` explicitly. The Qt 6.11.3 SDK's WebEngine
  arm64 binary reports minimum 13.0, so the isolated baseline uses 13.0 too.
  Leaving it empty propagated `-mmacos-version-min=` into Chromium and failed;
  do not remove the flag from generated files or infer a new App minimum.
- Use a task-local Python environment with html5lib/Jinja2/packaging and SPDX
  tools; do not alter system Python or suppress missing license metadata.
- Xcode must have an actual Metal Toolchain. `xcrun --find metal` alone can
  locate a stub even when `xcrun metal --version` fails. Do not disable Metal,
  override the failed check or silently install a shared toolchain. With the
  user's explicit authorization, the official `xcodebuild -downloadComponent
  MetalToolchain` completed for 27A266a, and actual compiler invocation passed.
- Check `QT_FEATURE_qtwebengine_core_build:INTERNAL=ON` in the generated cache.
  CMake returning success while explicitly skipping QtWebEngine is **not** a
  successful engine build.
- Preserve the warning that the current Apple SDK is newer than this Qt
  release's tested SDK. Do not silence it or imply future-system validation.

No build has yet produced a patched engine. After a valid baseline build,
first verify its complete runtime reproduces the original SDK's nine-case
geometry, pagination and raster output. If it does not, investigate build
configuration differences before attributing any improvement to this patch.
Then apply the candidate only in the isolated source tree. Run the standalone
geometry generator with each corresponding complete runtime, then require:

```sh
python3 tests/standalone-qt-pdf/audit-anchor-geometry.py \
  /absolute/new/geometry-output --require-accurate \
  --output /absolute/new/geometry-output/acceptance.json
```

All nine scenarios/27 destinations must be within one point on the correct
page, with source rectangles contained and aligned. `--cases` is for explicit
positive/negative controls only; a subset can never accept an engine. Even
subset controls require an intact nine-case generator manifest: empty,
duplicate, missing, unsaved or HTML-mismatched cases fail before PDF analysis.
Source links must cover all three distinct targets, not merely count to three. The
unmodified baseline's full acceptance deliberately fails seven cases. The two
known accurate controls pass, confirming the checker can distinguish outcomes.

Then repeat the original editor/MathJax/scientific-CSS corpus (including SVG2
equation references, pagination edges and ordinary labels), check exact source
and modified-state restoration, compare pagination and rendered output, rerun
save/export lifetime and full regressions, and verify package/signature and
native UI separately. Passing a source patch check cannot replace these steps.
