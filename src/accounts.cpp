/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "accounts.h"
#include "json.h"
#include "settings.h"

#include <tqfile.h>
#include <tqtextstream.h>

#include <tdeapplication.h>
#include <tdeconfig.h>
#include <tdeglobal.h>
#include <kstandarddirs.h>
#include <tdewallet.h>

#include <sys/stat.h>

static const char *WALLET_FOLDER = "tde-tmatrix";

static TDEConfig *cfg()
{
    TDEConfig *c = TDEGlobal::config();
    c->setGroup( "Accounts" );
    return c;
}

TQStringList Accounts::list()
{
    return cfg()->readListEntry( "Users" );
}

TQString Accounts::current()
{
    TQString c = cfg()->readEntry( "Current" );
    TQStringList l = list();
    if ( !l.contains( c ) )
        c = l.isEmpty() ? TQString::null : l.first();
    return c;
}

void Accounts::setCurrent( const TQString &userId )
{
    cfg()->writeEntry( "Current", userId );
    cfg()->sync();
}

bool Accounts::usesWallet()
{
    return Settings::useWallet() && TDEWallet::Wallet::isEnabled();
}

TDEWallet::Wallet *Accounts::wallet( WId window )
{
    static TDEWallet::Wallet *w = 0;
    if ( !usesWallet() )
        return 0;
    if ( w && w->isOpen() )
        return w;
    delete w;
    w = TDEWallet::Wallet::openWallet( TDEWallet::Wallet::NetworkWallet(), window, TDEWallet::Wallet::Synchronous );
    if ( !w )
        return 0;
    if ( !w->hasFolder( WALLET_FOLDER ) )
        w->createFolder( WALLET_FOLDER );
    w->setFolder( WALLET_FOLDER );
    return w;
}

TQString Accounts::fileFor( const TQString &userId )
{
    TQString name = userId;
    for ( uint i = 0; i < name.length(); ++i )
        if ( !name[i].isLetterOrNumber() && name[i] != '.' && name[i] != '-' )
            name[i] = '_';
    return locateLocal( "appdata", "sessions/" + name );
}

bool Accounts::storeSession( const TQString &userId, const TQString &session, WId window )
{
    bool ok = false;
    if ( TDEWallet::Wallet *w = wallet( window ) ) {
        ok = w->writePassword( userId, session ) == 0;
        if ( ok )
            TQFile::remove( fileFor( userId ) );
    }
    if ( !ok ) {
        TQString path = fileFor( userId );
        TQFile f( path );
        ::umask( 077 );
        if ( f.open( IO_WriteOnly | IO_Truncate ) ) {
            ::fchmod( f.handle(), 0600 );
            TQCString data = session.utf8();
            f.writeBlock( data.data(), data.length() );
            ok = true;
        }
    }
    TQStringList l = list();
    if ( !l.contains( userId ) ) {
        l.append( userId );
        cfg()->writeEntry( "Users", l );
    }
    setCurrent( userId );
    return ok;
}

TQString Accounts::loadSession( const TQString &userId, WId window )
{
    TQString path = fileFor( userId );
    if ( TQFile::exists( path ) ) {
        TQFile f( path );
        if ( f.open( IO_ReadOnly ) )
            return TQString::fromUtf8( f.readAll() );
    }
    if ( TDEWallet::Wallet *w = wallet( window ) ) {
        TQString s;
        if ( w->readPassword( userId, s ) == 0 )
            return s;
    }
    return TQString::null;
}

void Accounts::updateTokens( const TQString &userId, const TQString &accessToken, const TQString &refreshToken )
{
    TQString s = loadSession( userId );
    if ( s.isEmpty() )
        return;
    bool ok;
    JsonValue v = JsonValue::parse( s, &ok );
    if ( !ok )
        return;
    v.set( "access_token", accessToken );
    v.set( "refresh_token", refreshToken.isEmpty() ? JsonValue() : JsonValue( refreshToken ) );
    storeSession( userId, TQString::fromUtf8( v.serialize() ) );
}

void Accounts::remove( const TQString &userId )
{
    if ( TDEWallet::Wallet *w = wallet( 0 ) )
        w->removeEntry( userId );
    TQFile::remove( fileFor( userId ) );
    TQStringList l = list();
    l.remove( userId );
    cfg()->writeEntry( "Users", l );
    if ( current() == userId || cfg()->readEntry( "Current" ) == userId )
        cfg()->writeEntry( "Current", l.isEmpty() ? TQString::null : l.first() );
    cfg()->sync();
}
