# Rewrites every "Vendor" value in a VST3 moduleinfo.json with the correct
# UTF-8 vendor name. JUCE builds its moduleinfo helper as a separate CMake
# project without /utf-8, so on a non-UTF-8 Windows code page a non-ASCII
# COMPANY_NAME (the c with caron in Kasperavicius) comes out as a code-page
# byte or U+FFFD. The plugin binary itself reports the name correctly.
#
# Usage: cmake -DMODULEINFO=<path> -DVENDOR_FILE=<file holding the name> -P FixModuleInfoVendor.cmake

if(NOT EXISTS "${MODULEINFO}")
    return()
endif()
file(READ "${VENDOR_FILE}" vendor)
string(STRIP "${vendor}" vendor)
file(READ "${MODULEINFO}" json)
string(REGEX REPLACE "\"Vendor\": \"[^\"]*\"" "\"Vendor\": \"${vendor}\"" fixed "${json}")
if(NOT fixed STREQUAL json)
    file(WRITE "${MODULEINFO}" "${fixed}")
endif()
