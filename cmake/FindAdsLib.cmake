find_path(ADSLIB_INCLUDE_DIR
        NAMES AdsLib/AdsLib.h
        PATHS /usr/local/include
)

find_library(ADSLIB_LIBRARY
        NAMES AdsLib libAdsLib
        PATHS /usr/local/lib
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(AdsLib
        REQUIRED_VARS ADSLIB_INCLUDE_DIR ADSLIB_LIBRARY
)

if(AdsLib_FOUND AND NOT TARGET AdsLib::AdsLib)
    add_library(AdsLib::AdsLib UNKNOWN IMPORTED)

    set_target_properties(AdsLib::AdsLib PROPERTIES
            IMPORTED_LOCATION "${ADSLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ADSLIB_INCLUDE_DIR}"
    )
endif()