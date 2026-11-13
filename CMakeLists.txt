############################################
#                                          #
#  tde-tmatrix - a Matrix chat client for  #
#  the Trinity Desktop Environment         #
#                                          #
#  This file is released under GPL >= 2    #
#                                          #
############################################


##### set project version ########################

include( TDEVersion )
cmake_minimum_required( VERSION ${TDE_CMAKE_MINIMUM_VERSION} )
tde_set_project_version( )
set( TMATRIX_VERSION "0.1.0" )


#### general package setup

project( tde-tmatrix C CXX )


#### include essential cmake modules

include( FindPkgConfig          )
include( CheckFunctionExists    )
include( CheckSymbolExists      )
include( CheckIncludeFile       )
include( CheckLibraryExists     )
include( CheckCSourceCompiles   )
include( CheckCXXSourceCompiles )


#### include our cmake modules

include( TDEMacros )


##### setup install paths

include( TDESetupPaths )
tde_setup_paths( )


##### optional stuff

option( WITH_ALL_OPTIONS "Enable all optional support" OFF                                          )
option( WITH_GCC_VISIBILITY "Enable fvisibility and fvisibility-inlines-hidden" ${WITH_ALL_OPTIONS} )
option( CARGO_OFFLINE "Build the Rust engine without network access (crates in core/vendor or the cargo cache)" OFF )


##### user requested modules

option( BUILD_ALL "Build all" ON                     )
option( BUILD_DOC "Build documentation" ${BUILD_ALL} )
option( BUILD_TESTS "Build the test programs (not installed)" OFF )


##### configure checks

include( ConfigureChecks.cmake )


###### global compiler settings

add_definitions( -DHAVE_CONFIG_H )

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TQT_CXX_FLAGS}" )
set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined" )
set( CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--no-undefined" )


##### directories

add_subdirectory( core )
add_subdirectory( src )
add_subdirectory( icons )
if( BUILD_TESTS )
  add_subdirectory( tests )
endif( )


##### other data ################################

tde_conditional_add_project_docs( BUILD_DOC )


##### write configure files

configure_file( config.h.cmake config.h @ONLY )
