# Native macOS builds can use a separately installed static Crypto++.
find_path(cryptopp_INCLUDE_DIR NAMES cryptopp/cryptlib.h)
find_library(cryptopp_LIBRARY NAMES cryptopp crypto++)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cryptopp REQUIRED_VARS cryptopp_INCLUDE_DIR cryptopp_LIBRARY)
set(cryptopp_INCLUDE_DIRS "${cryptopp_INCLUDE_DIR}")
set(cryptopp_LIBRARIES "${cryptopp_LIBRARY}")
mark_as_advanced(cryptopp_INCLUDE_DIR cryptopp_LIBRARY)
