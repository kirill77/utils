# The portable utils modules, one static library each (utils::log, utils::fileUtils, ...).
# The Windows-only modules (visLib, nvapi, openXR, frameView*, systemInfo, ...) build
# from their .vcxproj files only.
#
# Headers are included as "utils/<module>/<file>.h", so the include root is the
# directory that contains this checkout, which must be named "utils".
cmake_minimum_required(VERSION 3.20)
project(utils CXX)

set(UTILS_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(utils_add_library name)
    add_library(utils_${name} STATIC ${ARGN})
    add_library(utils::${name} ALIAS utils_${name})
    target_include_directories(utils_${name} PUBLIC ${UTILS_INCLUDE_ROOT})
    target_compile_features(utils_${name} PUBLIC cxx_std_20)
endfunction()

utils_add_library(timeUtils timeUtils/timeUtils.cpp)

utils_add_library(fileUtils fileUtils/fileUtils.cpp)
target_link_libraries(utils_fileUtils PUBLIC utils::timeUtils)

utils_add_library(log log/Log.cpp)
target_link_libraries(utils_log PUBLIC utils::fileUtils utils::timeUtils)

utils_add_library(rng rng/RNG.cpp rng/RNGSobol.cpp)

utils_add_library(stringMap stringMap/StringMap.cpp)

utils_add_library(csvFile csvFile/CSVFileReader.cpp csvFile/CSVFileWriter.cpp csvFile/DataCollector.cpp)

utils_add_library(httpServer httpServer/HttpRequest.cpp httpServer/HttpResponse.cpp httpServer/HttpServer.cpp)
if(WIN32)
    target_link_libraries(utils_httpServer PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(utils_httpServer PUBLIC Threads::Threads)
endif()

if(WIN32)
    utils_add_library(HttpClient HttpClient/HttpClient.cpp)
    target_link_libraries(utils_HttpClient PUBLIC winhttp)
else()
    find_package(CURL REQUIRED)
    utils_add_library(HttpClient HttpClient/HttpClientCurl.cpp)
    target_link_libraries(utils_HttpClient PRIVATE CURL::libcurl)
endif()
