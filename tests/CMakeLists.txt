include_directories(
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${TMATRIX_CORE_INCLUDE_DIR}
  ${TDE_INCLUDE_DIR}
  ${TQT_INCLUDE_DIRS}
)

link_directories(
  ${TQT_LIBRARY_DIRS}
  ${TDE_LIB_DIR}
)

# checks the JSON code and that the engine starts and answers (not installed)
tde_add_executable( coretest
  SOURCES coretest.cpp
  LINK tmatrixcommon-static ${TMATRIX_CORE_LIBRARIES} tdecore-shared
)

add_test( NAME coretest COMMAND coretest )
