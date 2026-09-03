#
#  Copyright 2008 Search Solution Corporation
#  Copyright 2016 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#
#

# fuzz targets logic, mirroring unit_tests/:
# - configure with -DENABLE_FUZZING=ON (and clang), then build the fuzz_* targets
# - the engine itself is instrumented by the flags the top-level CMakeLists sets

project(Fuzz)

string(REPLACE ";" "," FUZZ_SANITIZERS_CSV "${FUZZ_SANITIZERS}")

# src/storage/bestspace.cpp leaves a call to the generic __atomic_compare_exchange
# in libcubrid.  GCC links libatomic implicitly, clang does not, so the fuzz
# executables have to ask for it.  Scoped here rather than on the cubrid target
# so the ordinary GCC build is untouched.
set(FUZZ_EXTRA_LIBS "")
if(UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND FUZZ_EXTRA_LIBS atomic)
endif()

set(FUZZ_INCLUDES
  ${EP_INCLUDES}
  ${CMAKE_SOURCE_DIR}/src/base
  ${CMAKE_SOURCE_DIR}/src/object
  ${CMAKE_SOURCE_DIR}/src/storage
  ${CMAKE_SOURCE_DIR}/src/connection
  ${CMAKE_SOURCE_DIR}/src/thread
  ${CMAKE_BINARY_DIR}
  )

# fuzz_record - or_get_value (), the packed-value decode path
add_executable(fuzz_record
  record_fuzzer.cpp
  )

target_compile_definitions(fuzz_record PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(fuzz_record PRIVATE
  ${FUZZ_INCLUDES}
  )

target_link_options(fuzz_record PRIVATE
  -fsanitize=fuzzer,${FUZZ_SANITIZERS_CSV}
  )

target_link_libraries(fuzz_record LINK_PRIVATE
  cubrid
  ${FUZZ_EXTRA_LIBS}
  )
