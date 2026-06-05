# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.2.1] - 2026-06-05

### Fixed

- LuaJIT build regression introduced in 0.2.0: `find_package(Lua REQUIRED)`
  aborted at configure time when `LUA_LIBRARIES` was not found, which is
  the case for LuaJIT (its library is `libluajit-5.1`, not `liblua-5.1`).

## [0.2.0] - 2026-06-05

### Added

- Support for Lua 5.4 and 5.5 in the C compatibility layer.
- GitHub Actions CI workflow that builds and tests against Lua 5.1, 5.3,
  5.4 and 5.5 on both `amd64` (`ubuntu-24.04`) and `arm64`
  (`ubuntu-24.04-arm`) runners. The workflow builds each Lua from source,
  builds LuaRocks 3.13.0 against it, installs busted and runs the spec
  suite under `dbus-run-session`.

### Changed

- Bumped `cmake_minimum_required` from `2.6` to `3.10...4.3` so the
  project configures on CMake 4.x (which removed compatibility with
  pre-3.5 minimum requirements).
- Replaced the vendored `cmake/FindGLIB.cmake` with
  `pkg_check_modules(... IMPORTED_TARGET ...)`. The resulting
  `PkgConfig::GLIB` target carries includes, link flags and libraries
  for `glib-2.0`, `gobject-2.0`, `gio-2.0` and `gio-unix-2.0` directly
  from pkg-config, and works correctly on multiarch systems.
- Moved compiler warning flags from `add_definitions()` to
  `target_compile_options()` on the `easydbus_core` target.
- `find_package(Lua)` is now marked `REQUIRED` so a missing Lua aborts
  at configure time with a clear message.
- `lua_equal` replaced with `lua_compare(..., LUA_OPEQ)` so the code
  builds against unpatched Lua 5.3+; a shim is provided for Lua 5.1.
- `lua_resume` now goes through a per-version compatibility wrapper to
  handle the signature changes in Lua 5.2 (added `from`) and 5.4
  (added `nresults`).

### Removed

- `cmake/FindGLIB.cmake` (replaced by `pkg_check_modules`).
- The unused `CMAKE_MODULE_PATH` assignment.

### Fixed

- `spec/signal_spec.lua` now expects the trailing object-path argument
  added in commit `0f1d055` ("bus: Pass object name to the signal
  handler"); the spec was previously out of sync with the library.
- `spec/service_spec.lua` is skipped under Lua 5.1 with a `pending`
  marker. The async `bus:call` pattern yields across a C-call boundary,
  which requires `lua_yieldk` (Lua 5.2+); on 5.1 the yield errored out
  and `dbus.mainloop_quit()` was never reached, hanging the test run
  until the CI job timed out.
