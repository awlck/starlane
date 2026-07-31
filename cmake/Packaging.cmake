# CPack configuration for Linux system packages (.deb / .rpm) of the Qt
# frontend. Windows and macOS builds are packaged separately in CI via
# windeployqt/macdeployqt directly (a plain .zip / .dmg, not a CPack
# archive), so none of this applies there.
#
# These packages don't bundle Qt -- they declare a dependency on the
# distro's own Qt6 runtime packages instead, detected automatically from
# the actual shared libraries the built binary links against:
#   * CPACK_DEBIAN_PACKAGE_SHLIBDEPS runs dpkg-shlibdeps (needs dpkg-dev)
#   * CPack's RPM generator runs rpmbuild's dependency generator by default
#     (CPACK_RPM_PACKAGE_AUTOREQPROV, "yes" unless overridden)
# The CI workflow picks the generator per distro with `cpack -G DEB` or
# `cpack -G RPM`; CPACK_GENERATOR is intentionally left unset here.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	return()
endif()

set(CPACK_PACKAGE_NAME "starlane")
set(CPACK_PACKAGE_VENDOR "Adrian Welcker")
set(CPACK_PACKAGE_CONTACT "Adrian Welcker <ardi@diepixelecke.de>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A C++ reimplementation of the ADRIFT 5 interactive fiction engine")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/awlck/starlane")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Without this, whether the packaged binary carries debug info is down to
# each distro's own rpmbuild/dpkg-deb defaults rather than a deliberate
# choice -- e.g. Fedora's rpmbuild strips and splits it into a -debuginfo
# subpackage automatically, but plain CPack DEB does not. Strip explicitly
# so all three packages end up the same way.
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games/Other")
set(CPACK_RPM_PACKAGE_AUTOREQPROV YES)
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")

include(CPack)
