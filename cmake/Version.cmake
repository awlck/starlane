# Derives STARLANE_VERSION from the exact git tag pointing at the current
# commit, so a release build's version comes from the tag that triggered it
# instead of a number someone has to remember to bump by hand before
# tagging. Falls back to STARLANE_VERSION_FALLBACK (not a real version --
# just a marker that this isn't a tagged release build) when HEAD isn't
# exactly on a version tag: normal development builds, PRs, a shallow
# checkout without tags, or a source tree with no .git directory at all.
#
# Must run before the top-level project() call, since that's what actually
# consumes STARLANE_VERSION.
#
# Release tags are expected to look like "v1.2.3"; project(VERSION ...)
# requires a plain dotted-numeric string, so the leading "v" is stripped.

set(STARLANE_VERSION_FALLBACK "0.0.0")
set(STARLANE_VERSION "${STARLANE_VERSION_FALLBACK}")

find_package(Git QUIET)
if(GIT_EXECUTABLE)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*.[0-9]*.[0-9]*" --exact-match
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		OUTPUT_VARIABLE STARLANE_GIT_TAG
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_VARIABLE STARLANE_GIT_DESCRIBE_ERROR
		ERROR_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE STARLANE_GIT_DESCRIBE_RESULT
	)
	# --match is a glob, not a strict pattern (e.g. "v1.2.3-rc1" would still
	# match "v[0-9]*.[0-9]*.[0-9]*"), and project(VERSION ...) hard-errors on
	# anything but a plain dotted-numeric string. Re-check strictly so a
	# not-quite-right tag falls back rather than breaking configure entirely.
	if(STARLANE_GIT_DESCRIBE_RESULT EQUAL 0 AND STARLANE_GIT_TAG MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+$")
		string(REGEX REPLACE "^v" "" STARLANE_VERSION "${STARLANE_GIT_TAG}")
	elseif(NOT STARLANE_GIT_DESCRIBE_RESULT EQUAL 0)
		# git describe --exact-match exits non-zero both for the ordinary
		# case (HEAD just isn't on a tag -- most dev builds) and for actual
		# failures (e.g. a CI container job's git refusing to touch a
		# checkout it doesn't consider "safe", which silently produced this
		# exact fallback once already). There's no reliable way to tell those
		# apart from the exit code alone, so report git's own message either
		# way and let a human judge whether it's expected.
		message(STATUS "cmake/Version.cmake: git describe exited ${STARLANE_GIT_DESCRIBE_RESULT}, "
			"falling back to ${STARLANE_VERSION_FALLBACK}: ${STARLANE_GIT_DESCRIBE_ERROR}")
	endif()
endif()

message(STATUS "STARLANE_VERSION: ${STARLANE_VERSION}")
