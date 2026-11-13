###########################################
#                                         #
#  This file is released under GPL >= 2   #
#                                         #
###########################################


# required stuff
find_package( TQt )
find_package( TDE )

tde_setup_compiler_flags( )
tde_setup_largefiles( )


##### check for gcc visibility support

if( WITH_GCC_VISIBILITY )
  tde_setup_gcc_visibility( )
endif( WITH_GCC_VISIBILITY )


##### the Rust toolchain for the Matrix engine (matrix-rust-sdk needs Rust >= 1.96)

find_program( CARGO_EXECUTABLE cargo HINTS $ENV{CARGO_HOME}/bin $ENV{HOME}/.cargo/bin )
if( NOT CARGO_EXECUTABLE )
  tde_message_fatal( "cargo (Rust >= 1.96) is required to build the Matrix engine; install it with rustup" )
endif( )
execute_process( COMMAND ${CARGO_EXECUTABLE} --version OUTPUT_VARIABLE CARGO_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE )
message( STATUS "Found ${CARGO_VERSION}" )

find_package( Threads REQUIRED )
