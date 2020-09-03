#
# Copyright (C) 2016 Search Solution Corporation. All rights reserved.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

add_custom_target(jsp_build ALL
  COMMAND ${CMAKE_COMMAND} -E echo "  copying ivy.xml to ${CMAKE_BINARY_DIR}/java/ivy.xml"
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/java/ivy.xml ${CMAKE_BINARY_DIR}/java/ivy.xml
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/java/ivysettings.xml ${CMAKE_BINARY_DIR}/java/ivysettings.xml
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/java/jcas.sh ${CMAKE_BINARY_DIR}/java/jcas.sh
  COMMAND ${ANT} dist -buildfile ${CMAKE_SOURCE_DIR}/java/build.xml -Dbasedir=. -Dversion=${BUILD_NUMBER} -Dsrc=${JDBC_DIR}
  COMMENT "Build JSP driver with Ant ..."
  )

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/jcas.jar
  ${CMAKE_CURRENT_BINARY_DIR}/jcas.sh
  DESTINATION ${CUBRID_JAVADIR})

install(DIRECTORY
  ${CMAKE_CURRENT_BINARY_DIR}/lib
  DESTINATION ${CUBRID_JAVADIR})

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/jspserver.jar
  ${JDBC_DIR}/logging.properties
  DESTINATION ${CUBRID_JAVADIR})

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E create_symlink \
    ${CMAKE_BINARY_DIR}/java/jcas.sh \
    ${CMAKE_BINARY_DIR}/cub_jcas   \
    )"
)

install(FILES
  ${CMAKE_BINARY_DIR}/cub_jcas
  DESTINATION ${CUBRID_BINDIR}
)