# Westeros GL DRM L1 tests

These Linux-only GoogleTest suites exercise the display-console client and the main renderer without a DRM device or running display server.

## Build and run

On Ubuntu, install dependencies:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libgtest-dev lcov libdrm-dev libgbm-dev libegl1-mesa-dev
```

From the repository root:

```sh
cmake -S L1 -B build/L1 -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build/L1 --parallel
ctest --test-dir build/L1 -L WesterosGlDrm --output-on-failure
cmake --build build/L1 --target coverage-westeros-gl-drm
```

CTest registers two executables: `WesterosGlDrmL1Tests` (console helper) and `WesterosGlRendererL1Tests` (renderer). Both are included by the existing `WesterosGlDrm` label and coverage target. Set `ENABLE_COVERAGE=OFF` to build tests without coverage tools.

## Renderer test boundary

- Compiles the unchanged `westeros-gl/westeros-gl.c` as C through `renderer_test_support.c`. Test-only wrappers expose opaque context state and selected static helpers; production code is not copied or edited.
- Retains the original display capabilities/safe areas, mode syntax, listeners, window bookkeeping, EGL lookup, reference counting, integer encoding, and rectangle tests.
- Adds four groups of tests to the same executable and coverage target:

	| Test source | Behaviors exercised |
	| --- | --- |
	| `renderer_frame_test.cpp` | Overlay list ordering/allocation/reuse; frame queue growth, scheduling, dropping, pause/advance/underflow; rectangle updates; mode/rate selection |
	| `renderer_protocol_test.cpp` | Display commands and responses; byte-exact video messages; interrupted/failed writes; offload dispatch, queue wraparound/overflow and selective flush |
	| `renderer_drm_test.cpp` | DRM property discovery/unwinding; atomic and legacy swaps; crop/scale/visibility properties; synchronous page-flip events; framebuffer, GEM handle and buffer cleanup |
	| `renderer_init_test.cpp` | Mocked initialization success/failure; connector/encoder/CRTC selection; capabilities and environment options; display updates; service/connection startup failures; cross-fixture reset regression |

- Uses real DRM/GBM/EGL development headers and libraries to compile/link the full generic implementation. Test-only replacements intercept exercised graphics, device, transport and thread-creation calls. Initialization mocks can return synthetic device handles; no host DRM device is opened or closed. Page-flip completion is delivered synchronously, protocol writes are captured, and thread creation fails deterministically instead of starting background workers.
- The `.inc` support files are included at the end of the C harness, not compiled as separate source files. Redirect headers wrap only production-source inclusion. The shared `sendmsg` dispatcher preserves frame-test capture when protocol capture is inactive. Fixtures reset mock faults/counters between tests and reclaim owned resources before resetting them.
- The generic test configuration still disables hotplug, refresh locking, native/output fences, and generic AV sync. Amlogic-specific paths, real rendering/page flips, successful hardware initialization, live server threads, and incoming video frame/descriptor handling are **not validated**. Mocked rendering state transitions are not hardware integration tests.
- Device-open failure tests call the internal initializer directly. The public initializer holds `gMutex` before calling it, while its error cleanup acquires that mutex again; that potential production deadlock is not changed by this suite.
- Existing production cleanup defects are not silently asserted away: tests observe failure state before fixture reclamation. DRM fixtures return used overlay planes before final context cleanup to avoid the existing mixed used/available-list dangling-head path. Initialization tests reclaim unpublished service allocations after observing failed startup.

For additional fixture-isolation validation on Linux, run the renderer tests in shuffled order before regenerating coverage:

```sh
./build/L1/test_westeros_gl_renderer_l1 --gtest_shuffle --gtest_repeat=5
```

## Coverage

The report includes both `westeros-gl/westeros-gl-console-helper.c` and `westeros-gl/westeros-gl.c`. Unexercised compiled renderer functions remain in the measured scope; optional code excluded by preprocessor flags is not measured. The renderer is attributed to its original filename even though it is included by the C harness.

lcov and genhtml report actual coverage with **no minimum percentage and no separate checker script**. Test failures and coverage-tool errors still fail the target. The HTML report is written to `build/L1/coverage_html_westeros_gl_drm/`. Confirm measured percentages by running the target on Linux; no estimated coverage percentage is claimed.

The supplied pre-expansion report measured the renderer at **474/3664 lines (12.9%)**. With that denominator, exceeding 60% requires at least **2199 covered lines**, or **1725 additional lines**. The expanded suites target the large previously uncovered subsystems; they do not exclude functions or change feature flags to inflate the percentage. Their resulting percentage must be measured by a fresh Linux run. The previously generated HTML is a baseline, not evidence that the new tests pass or meet the target.

Local validation of this expansion was limited to source review and mock integration checks: the Windows workspace has no usable Linux C/C++ toolchain, and IntelliSense cannot resolve standard C headers. Compilation, shuffled/repeated execution, and regenerated coverage remain required.