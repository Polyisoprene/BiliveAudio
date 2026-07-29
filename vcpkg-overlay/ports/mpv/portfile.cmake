vcpkg_download_distfile(MPV_NUPKG
    URLS "https://www.nuget.org/api/v2/package/Endpne.LibMPV.Windows/0.39.0"
    FILENAME "Endpne.LibMPV.Windows.0.39.0.nupkg"
    SHA512 7ca06f1e3e8e0d1f37d47f37603a1affc3880d79378867fc5b5bb3bdea4f111ccf44761701bfc2182244f484f6fa47926b379a6bbca9d8639c73a398ce588620
)

vcpkg_extract_source_archive(
    ${CURRENT_PACKAGES_DIR}/temp_extract
    ARCHIVE ${MPV_NUPKG}
    NO_REMOVE_ONE_LEVEL
)

set(ARCH "x64")
if(DEFINED VCPKG_TARGET_ARCHITECTURE)
    set(ARCH ${VCPKG_TARGET_ARCHITECTURE})
endif()

file(GLOB HEADERS "${CURRENT_PACKAGES_DIR}/temp_extract/build/${ARCH}/include/mpv/*.h")
file(INSTALL ${HEADERS} DESTINATION ${CURRENT_PACKAGES_DIR}/include)

file(GLOB LIBS "${CURRENT_PACKAGES_DIR}/temp_extract/build/${ARCH}/libmpv-2.lib")
file(INSTALL ${LIBS} DESTINATION ${CURRENT_PACKAGES_DIR}/lib)

file(GLOB DLLS "${CURRENT_PACKAGES_DIR}/temp_extract/build/${ARCH}/*.dll")
file(INSTALL ${DLLS} DESTINATION ${CURRENT_PACKAGES_DIR}/bin)

file(GLOB PDB "${CURRENT_PACKAGES_DIR}/temp_extract/build/${ARCH}/*.pdb")
if(PDB)
    file(INSTALL ${PDB} DESTINATION ${CURRENT_PACKAGES_DIR}/bin)
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/temp_extract")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION ${CURRENT_PACKAGES_DIR}/share/mpv)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/vcpkg.json" DESTINATION ${CURRENT_PACKAGES_DIR}/share/mpv)
