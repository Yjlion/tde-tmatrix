/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Checks the JSON model and the C interface of the engine without a
 * homeserver: an unknown command and a command without login must be
 * answered with an error.
 */

#include "json.h"
#include "tmatrix_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <stdint.h>

static int failures = 0;
#define CHECK( cond ) do { if ( !( cond ) ) { fprintf( stderr, "FAILED: %s (line %d)\n", #cond, __LINE__ ); ++failures; } } while ( 0 )

static JsonValue waitReply( TmCore *core, int id )
{
    for ( int round = 0; round < 100; ++round ) {
        struct pollfd p = { tm_core_event_fd( core ), POLLIN, 0 };
        poll( &p, 1, 100 );
        uint64_t n;
        if ( read( p.fd, &n, sizeof( n ) ) < 0 ) {
        }
        while ( char *s = tm_core_next_event( core ) ) {
            JsonValue ev = JsonValue::parse( TQCString( s ) );
            tm_string_free( s );
            if ( ev.get( "type" ).toString() == "reply" && ev.get( "id" ).toInt() == id )
                return ev;
        }
    }
    return JsonValue();
}

int main()
{
    // JSON
    bool ok;
    JsonValue v = JsonValue::parse( TQCString( "{\"a\":[1,2.5,\"x\\u00e9\\n\",true,null],\"b\":{\"c\":\"d\"}}" ), &ok );
    CHECK( ok );
    CHECK( v.get( "a" ).count() == 5 );
    CHECK( v.get( "a" ).at( 1 ).toDouble() == 2.5 );
    CHECK( v.get( "a" ).at( 2 ).toString() == TQString::fromUtf8( "x\xc3\xa9\n" ) );
    CHECK( v.get( "a" ).at( 3 ).toBool() );
    CHECK( v.get( "a" ).at( 4 ).isNull() );
    CHECK( v.get( "b" ).get( "c" ).toString() == "d" );
    JsonValue back = JsonValue::parse( v.serialize(), &ok );
    CHECK( ok && back.serialize() == v.serialize() );
    JsonValue big = JsonValue::parse( TQCString( "{\"ts\":1762948800123}" ), &ok );
    CHECK( (long long) big.get( "ts" ).toDouble() == 1762948800123LL );
    JsonValue::parse( TQCString( "{\"a\":" ), &ok );
    CHECK( !ok );

    // engine
    char dir[] = "/tmp/tmatrix-coretest-XXXXXX";
    CHECK( mkdtemp( dir ) != 0 );
    TmCore *core = tm_core_new( dir );
    CHECK( core != 0 );
    CHECK( strlen( tm_core_version() ) > 0 );
    tm_core_command( core, "{\"id\":1,\"cmd\":\"no_such_command\"}" );
    JsonValue r1 = waitReply( core, 1 );
    CHECK( r1.get( "ok" ).type() == JsonValue::Bool && !r1.get( "ok" ).toBool() );
    tm_core_command( core, "{\"id\":2,\"cmd\":\"rooms\"}" );
    JsonValue r2 = waitReply( core, 2 );
    CHECK( !r2.get( "ok" ).toBool() && r2.get( "error" ).toString() == "not logged in" );
    tm_core_free( core );
    char cmd[128];
    snprintf( cmd, sizeof( cmd ), "rm -rf %s", dir );
    if ( system( cmd ) != 0 ) {
    }

    if ( failures )
        fprintf( stderr, "%d checks failed\n", failures );
    else
        printf( "all checks passed\n" );
    return failures ? 1 : 0;
}
