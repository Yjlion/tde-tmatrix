/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "matrixclient.h"

#include "tmatrix_core.h"

#include <tqsocketnotifier.h>
#include <tqdir.h>

#include <tdeglobal.h>
#include <kstandarddirs.h>
#include <kdebug.h>

#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

static MatrixClient *s_self = 0;

MatrixClient *MatrixClient::self()
{
    if ( !s_self )
        s_self = new MatrixClient;
    return s_self;
}

TQString MatrixClient::dataDir()
{
    return locateLocal( "appdata", "accounts/" );
}

MatrixClient::MatrixClient()
    : TQObject( 0, "MatrixClient" ), m_core( 0 ), m_notifier( 0 ), m_nextId( 1 )
{
    TQString dir = dataDir();
    TQDir().mkdir( dir );
    ::chmod( TQFile::encodeName( dir ), 0700 );
    m_core = tm_core_new( TQFile::encodeName( dir ) );
    if ( !m_core ) {
        kdWarning() << "tmatrix-core could not be started" << endl;
        return;
    }
    m_notifier = new TQSocketNotifier( tm_core_event_fd( m_core ), TQSocketNotifier::Read, this );
    connect( m_notifier, TQ_SIGNAL( activated( int ) ), this, TQ_SLOT( slotActivated() ) );
}

MatrixClient::~MatrixClient()
{
    if ( m_core )
        tm_core_free( m_core );
}

JsonValue MatrixClient::obj( const char *k1, const JsonValue &v1, const char *k2, const JsonValue &v2,
                             const char *k3, const JsonValue &v3, const char *k4, const JsonValue &v4 )
{
    JsonValue o = JsonValue::object();
    o.set( k1, v1 );
    if ( k2 ) o.set( k2, v2 );
    if ( k3 ) o.set( k3, v3 );
    if ( k4 ) o.set( k4, v4 );
    return o;
}

MatrixRequest *MatrixClient::call( const TQString &cmd, const JsonValue &args )
{
    int id = m_nextId++;
    MatrixRequest *r = new MatrixRequest( id, this );
    m_pending.insert( id, r );
    JsonValue a = args.isObject() ? args : JsonValue::object();
    a.set( "id", id );
    a.set( "cmd", cmd );
    if ( m_core )
        tm_core_command( m_core, a.serialize().data() );
    return r;
}

void MatrixClient::send( const TQString &cmd, const JsonValue &args )
{
    MatrixRequest *r = call( cmd, args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSendFailed( const JsonValue & ) ) );
}

void MatrixClient::slotSendFailed( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() )
        emit error( reply.get( "cmd" ).toString(), reply.get( "error" ).toString() );
}

void MatrixClient::refreshEncryption()
{
    MatrixRequest *r = call( "encryption_status" );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotEncryptionStatus( const JsonValue & ) ) );
}

void MatrixClient::slotEncryptionStatus( const JsonValue &reply )
{
    if ( reply.get( "ok" ).toBool() )
        emit encryptionChanged( reply );
}

void MatrixClient::slotActivated()
{
    uint64_t n;
    while ( ::read( tm_core_event_fd( m_core ), &n, sizeof( n ) ) == sizeof( n ) )
        ;
    // handle a bounded number per round so the user interface stays responsive
    for ( int i = 0; i < 200; ++i ) {
        char *s = tm_core_next_event( m_core );
        if ( !s )
            return;
        bool ok;
        JsonValue ev = JsonValue::parse( TQCString( s ), &ok );
        tm_string_free( s );
        if ( ok )
            dispatch( ev );
    }
    // more are waiting: come back after other events were processed
    uint64_t one = 1;
    ::write( tm_core_event_fd( m_core ), &one, sizeof( one ) );
}

void MatrixClient::dispatch( const JsonValue &ev )
{
    const TQString type = ev.get( "type" ).toString();
    if ( type == "reply" ) {
        int id = ev.get( "id" ).toInt();
        TQMap<int, TQGuardedPtr<MatrixRequest> >::Iterator it = m_pending.find( id );
        if ( it == m_pending.end() )
            return;
        TQGuardedPtr<MatrixRequest> r = it.data();
        m_pending.remove( it );
        if ( r ) {
            r->finish( ev );
            if ( r )
                r->deleteLater();
        }
        if ( ev.get( "ok" ).toBool() && ev.get( "cmd" ).toString() == "logout" ) {
            m_userId = TQString::null;
            emit loggedInChanged();
        }
    } else if ( type == "room" ) {
        emit roomChanged( ev );
    } else if ( type == "room_removed" ) {
        emit roomRemoved( ev.get( "room_id" ).toString() );
    } else if ( type == "timeline_reset" ) {
        emit timelineReset( ev.get( "room_id" ).toString(), ev.get( "items" ) );
    } else if ( type == "timeline_diff" ) {
        emit timelineDiff( ev.get( "room_id" ).toString(), ev.get( "ops" ) );
    } else if ( type == "timeline_paginated" ) {
        emit timelinePaginated( ev.get( "room_id" ).toString(), ev.get( "at_start" ).toBool() );
    } else if ( type == "notification" ) {
        emit notification( ev );
    } else if ( type == "logged_in" ) {
        m_userId = ev.get( "user_id" ).toString();
        m_deviceId = ev.get( "device_id" ).toString();
        m_homeserver = ev.get( "homeserver" ).toString();
        emit loggedInChanged();
    } else if ( type == "session" ) {
        emit sessionCreated( ev.get( "user_id" ).toString(), ev.get( "session" ).toString() );
    } else if ( type == "tokens" ) {
        emit tokensRefreshed( ev.get( "access_token" ).toString(), ev.get( "refresh_token" ).toString() );
    } else if ( type == "sync_state" ) {
        emit syncState( ev.get( "state" ).toString(), ev.get( "error" ).toString() );
        if ( ev.get( "state" ).toString() == "logged_out" ) {
            m_userId = TQString::null;
            emit loggedInChanged();
        }
    } else if ( type == "encryption" ) {
        emit encryptionChanged( ev );
    } else if ( type == "verification_request" ) {
        emit verificationRequest( ev );
    } else if ( type == "verification_state" ) {
        emit verificationState( ev );
    } else if ( type == "open_url" ) {
        emit openUrl( ev.get( "url" ).toString() );
    } else if ( type == "sso_finished" ) {
        emit ssoFinished( ev );
    }
}

#include "matrixclient.moc"
