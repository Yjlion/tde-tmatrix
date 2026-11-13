# The Matrix engine (Rust, matrix-rust-sdk) is built with cargo into a static
# library that the application links.

set( CARGO_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/target )
set( TMATRIX_CORE_LIB ${CARGO_TARGET_DIR}/release/libtmatrix_core.a )

set( _cargo_flags --release --locked )
if( CARGO_OFFLINE )
  list( APPEND _cargo_flags --offline )
endif( )

file( GLOB _core_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.rs )

add_custom_command(
  OUTPUT ${TMATRIX_CORE_LIB}
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CARGO_TARGET_DIR}
          ${CARGO_EXECUTABLE} build ${_cargo_flags} --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
  DEPENDS ${_core_sources} ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Building the Matrix engine (Rust, this takes a while the first time)"
  VERBATIM
)
add_custom_target( tmatrix-core-build DEPENDS ${TMATRIX_CORE_LIB} )

add_library( tmatrix-core STATIC IMPORTED GLOBAL )
set_target_properties( tmatrix-core PROPERTIES IMPORTED_LOCATION ${TMATRIX_CORE_LIB} )
add_dependencies( tmatrix-core tmatrix-core-build )

# system libraries the Rust standard library needs
set( TMATRIX_CORE_LIBRARIES tmatrix-core ${CMAKE_THREAD_LIBS_INIT} dl m PARENT_SCOPE )
set( TMATRIX_CORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE )
