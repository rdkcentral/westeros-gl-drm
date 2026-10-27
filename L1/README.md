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
- Exercises display capabilities/safe areas, supported mode syntax and refresh-rate selection, missing devices/resources/connectors, display-size listener registration/notification/removal, GBM window bookkeeping, required EGL symbol lookup failures, context reference counting/cleanup, protocol integer encoding, and video-rectangle normalization.
- Uses real DRM/GBM/EGL development headers and libraries to compile/link the full generic implementation. Test-only replacements intercept the graphics calls exercised by these tests. Device opens always fail in the mock; synthetic descriptors never reach host close operations. Synthetic contexts do not start rendering/server threads.
- The generic test configuration disables hotplug, refresh locking, native/output fences, and generic AV sync. Amlogic-specific paths, actual rendering/page flips, and a successful hardware initialization are **not covered**. This is not a hardware integration test or a claim of full renderer coverage.
- Device-open failure tests call the internal initializer directly. The public initializer holds `gMutex` before calling it, while its error cleanup acquires that mutex again; that potential production deadlock is not changed by this suite.

## Coverage

The report includes both `westeros-gl/westeros-gl-console-helper.c` and `westeros-gl/westeros-gl.c`. Unexercised compiled renderer functions remain in the measured scope; optional code excluded by preprocessor flags is not measured. The renderer is attributed to its original filename even though it is included by the C harness.

lcov and genhtml report actual coverage with **no minimum percentage and no separate checker script**. Test failures and coverage-tool errors still fail the target. The HTML report is written to `build/L1/coverage_html_westeros_gl_drm/`. Confirm measured percentages by running the target on Linux; no estimated coverage percentage is claimed.