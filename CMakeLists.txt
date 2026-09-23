cmake_minimum_required(VERSION 3.20)
project(KnowledgeOS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(MSVC)
  add_compile_options(/utf-8 /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS)
endif()

add_library(sqlite3 STATIC third_party/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
if(MSVC)
  target_compile_options(sqlite3 PRIVATE /wd4996 /wd4244 /wd4267)
endif()

add_library(miniz STATIC
  third_party/miniz.c
  third_party/miniz_tdef.c
  third_party/miniz_tinfl.c
  third_party/miniz_zip.c
)
target_include_directories(miniz PUBLIC third_party)
if(MSVC)
  target_compile_options(miniz PRIVATE /wd4996)
endif()

add_executable(knowledgeos
  src_cpp/config.cpp
  src_cpp/db.cpp
  src_cpp/stylometry.cpp
  src_cpp/detectors.cpp
  src_cpp/embedding.cpp
  src_cpp/calibration.cpp
  src_cpp/analysis.cpp
  src_cpp/html.cpp
  src_cpp/http_client.cpp
  src_cpp/pdf.cpp
  src_cpp/url_crawl.cpp
  src_cpp/wiki_crawl.cpp
  src_cpp/ingest.cpp
  src_cpp/insights.cpp
  src_cpp/server.cpp
)

target_include_directories(knowledgeos PRIVATE src_cpp third_party)
target_link_libraries(knowledgeos PRIVATE sqlite3 miniz)

if(WIN32)
  target_link_libraries(knowledgeos PRIVATE ws2_32 winhttp)
else()
  find_package(OpenSSL REQUIRED)
  target_compile_definitions(knowledgeos PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
  target_link_libraries(knowledgeos PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)
endif()

add_custom_command(TARGET knowledgeos POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/web
          $<TARGET_FILE_DIR:knowledgeos>/web
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/tools
          $<TARGET_FILE_DIR:knowledgeos>/tools
  COMMENT "Copy dashboard and tools next to knowledgeos binary")
