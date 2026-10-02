# Per generator, at packaging time: the .deb installs under /opt with its launcher files,
# the archive unpacks as is.
if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "${CPACK_Z13_DEB_INSTALL_DIR}")
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CPACK_Z13_DEB_PROJECTS}")
endif()
