# Convenience wrappers around the CMake presets in CMakePresets.json, so the
# specific --preset/--target names don't need to be remembered. `configure`
# is a separate manual step (run it after editing CMakeLists.txt) — the other
# targets don't depend on it, so they won't silently reconfigure for you.

ifeq ($(OS),Windows_NT)
  CONFIGURE_CMD    := cmake --preset windows
  RELEASE_PRESET   := windows-release
  DEBUG_PRESET     := windows-debug
  TEST_UNIT_TARGET        := tests/smbmgr_tests_unit
  TEST_INTEGRATION_TARGET := tests/smbmgr_tests_integration
  TEST_DOCKER_TARGET      := tests/smbmgr_tests_docker
  TEST_ALL_TARGET         := tests/smbmgr_tests
  TEST_UNIT_PRESET        := windows-unit
  TEST_INTEGRATION_PRESET := windows-integration
  TEST_DOCKER_PRESET      := windows-docker
  TEST_ALL_PRESET         := windows-all
else
  # linux-debug and linux-release are separate single-config build dirs, so
  # both need configuring (unlike Windows' one multi-config preset).
  CONFIGURE_CMD    := cmake --preset linux-debug && cmake --preset linux-release
  RELEASE_PRESET   := linux-release
  DEBUG_PRESET     := linux-debug
  TEST_UNIT_TARGET        := smbmgr_tests_unit
  TEST_INTEGRATION_TARGET := smbmgr_tests_integration
  TEST_DOCKER_TARGET      := smbmgr_tests_docker
  TEST_ALL_TARGET         := smbmgr_tests
  TEST_UNIT_PRESET        := linux-unit
  TEST_INTEGRATION_PRESET := linux-integration
  TEST_DOCKER_PRESET      := linux-docker
  TEST_ALL_PRESET         := linux-all
endif

.PHONY: help configure release debug test-unit test-integration test-docker test-all fmt-docs check-docs clean

help:
	@echo "Targets:"
	@echo "  configure        - regenerate CMake's build files (run after editing CMakeLists.txt)"
	@echo "  release          - build smbmgr (Release, app only)"
	@echo "  debug            - build smbmgr (Debug, app only)"
	@echo "  test-unit        - build + run the unit tests (pure logic, fast)"
	@echo "  test-integration - build + run unit + serverless integration tests (no Docker)"
	@echo "  test-docker      - build + run the Samba-backed suites (needs Docker)"
	@echo "  test-all         - build + run every suite (needs Docker)"
	@echo "  fmt-docs         - re-wrap the markdown docs to the width in deno.jsonc (needs deno)"
	@echo "  check-docs       - fail if any markdown doc is not wrapped to that width"
	@echo "  clean            - remove the build/ directory"

configure:
	$(CONFIGURE_CMD)

release:
	cmake --build --preset $(RELEASE_PRESET) --target smbmgr --parallel

debug:
	cmake --build --preset $(DEBUG_PRESET) --target smbmgr --parallel

test-unit:
	cmake --build --preset $(DEBUG_PRESET) --target $(TEST_UNIT_TARGET) --parallel
	ctest --preset $(TEST_UNIT_PRESET)

test-integration:
	cmake --build --preset $(DEBUG_PRESET) --target $(TEST_INTEGRATION_TARGET) --parallel
	ctest --preset $(TEST_INTEGRATION_PRESET)

test-docker:
	cmake --build --preset $(DEBUG_PRESET) --target $(TEST_DOCKER_TARGET) --parallel
	ctest --preset $(TEST_DOCKER_PRESET)

test-all:
	cmake --build --preset $(DEBUG_PRESET) --target $(TEST_ALL_TARGET) --parallel
	ctest --preset $(TEST_ALL_PRESET)

# Which files, and the line width, live in deno.jsonc.
fmt-docs:
	deno fmt

check-docs:
	deno fmt --check

clean:
	rm -rf build
