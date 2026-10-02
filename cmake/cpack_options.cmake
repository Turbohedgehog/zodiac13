# Per generator, at packaging time: the .deb installs under /opt, the archive unpacks as is.
if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/zodiac13")
endif()
