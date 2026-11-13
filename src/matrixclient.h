/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_MATRIXCLIENT_H
#define TMATRIX_MATRIXCLIENT_H

#include "json.h"

#include <tqmap.h>
#include <tqobject.h>
#include <tqguardedptr.h>

class TQSocketNotifier;
struct Core;

/**
 * One command sent to the engine.  done() is emitted once with the reply;
 * the object deletes itself afterwards.
 */
class MatrixRequest : public TQObject
{
    TQ_OBJECT
public:
    MatrixRequest( int id, TQObject *parent ) : TQObject( parent ), m_id( id ) {}
    int id() const { return m_id; }
    void finish( const JsonValue &reply ) { emit done( reply ); }
signals:
    /** @p reply has "ok" (bool) and "error" (string) if it failed. */
    void done( const JsonValue &reply );
private:
    int m_id;
};

/**
 * The connection to the Rust engine (tmatrix-core): sends commands and turns
 * the engine's events into TQt signals.
 */
class MatrixClient : public TQObject
{
    TQ_OBJECT

public:
    static MatrixClient *self();

    /** Sends @p cmd with the members of @p args.  Connect to done() of the result. */
    MatrixRequest *call( const TQString &cmd, const JsonValue &args = JsonValue::object() );
    /** Sends a command whose reply is only reported as an error message. */
    void send( const TQString &cmd, const JsonValue &args = JsonValue::object() );

    bool loggedIn() const { return !m_userId.isEmpty(); }
    TQString userId() const { return m_userId; }
    TQString deviceId() const { return m_deviceId; }
    TQString homeserver() const { return m_homeserver; }

    /** Builds an object from key/value pairs: obj( "room_id", id, "body", text ). */
    static JsonValue obj( const char *k1, const JsonValue &v1,
                          const char *k2 = 0, const JsonValue &v2 = JsonValue(),
                          const char *k3 = 0, const JsonValue &v3 = JsonValue(),
                          const char *k4 = 0, const JsonValue &v4 = JsonValue() );

    /** Asks for the encryption status; encryptionChanged() follows. */
    void refreshEncryption();

    /** Directory of the stores and the media cache. */
    static TQString dataDir();

signals:
    void loggedInChanged();
    /** A new or refreshed login; @p session must be stored (TDEWallet). */
    void sessionCreated( const TQString &userId, const TQString &session );
    void tokensRefreshed( const TQString &accessToken, const TQString &refreshToken );
    void roomChanged( const JsonValue &room );
    void roomRemoved( const TQString &roomId );
    void timelineReset( const TQString &roomId, const JsonValue &items );
    void timelineDiff( const TQString &roomId, const JsonValue &ops );
    void timelinePaginated( const TQString &roomId, bool atStart );
    void notification( const JsonValue &n );
    void syncState( const TQString &state, const TQString &error );
    void encryptionChanged( const JsonValue &status );
    void verificationRequest( const JsonValue &request );
    void verificationState( const JsonValue &state );
    void openUrl( const TQString &url );
    void ssoFinished( const JsonValue &result );
    /** A command sent with send() failed. */
    void error( const TQString &cmd, const TQString &message );

private slots:
    void slotActivated();
    void slotSendFailed( const JsonValue &reply );
    void slotEncryptionStatus( const JsonValue &reply );

private:
    MatrixClient();
    ~MatrixClient();
    void dispatch( const JsonValue &ev );

    Core *m_core;
    TQSocketNotifier *m_notifier;
    int m_nextId;
    TQMap<int, TQGuardedPtr<MatrixRequest> > m_pending;
    TQString m_userId, m_deviceId, m_homeserver;
};

#endif
