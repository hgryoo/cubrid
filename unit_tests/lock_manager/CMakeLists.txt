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

set (TEST_LOCK_MANAGER_SOURCES
  test_main.cpp
  test_lock_manager_scenarios.cpp
  test_lock_manager_mock_runtime.cpp
  test_lock_manager_functional.cpp
  test_lock_manager_benchmark.cpp
  )

set (TEST_LOCK_MANAGER_HEADERS
  test_lock_manager_scenarios.hpp
  test_lock_manager_mock_runtime.hpp
  )

SET_SOURCE_FILES_PROPERTIES (
  ${TEST_LOCK_MANAGER_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable (test_lock_manager
  ${TEST_LOCK_MANAGER_SOURCES}
  ${TEST_LOCK_MANAGER_HEADERS}
  )

target_compile_definitions (test_lock_manager PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories (test_lock_manager PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries (test_lock_manager LINK_PRIVATE
  test_common
  )
