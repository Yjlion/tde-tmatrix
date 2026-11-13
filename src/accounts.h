/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_ACCOUNTS_H
#define TMATRIX_ACCOUNTS_H

#include <tqstringlist.h>
#include <tqwindowdefs.h>

namespace TDEWallet { class Wallet; }

/**
 * The accounts the user logged in with.  The login data (access token and the
 * passphrase of the encrypted store) is kept in TDEWallet, or in a file only
 * the user can read if TDEWallet is turned off.
 */
class Accounts
{
public:
    static TQStringList list();
    static TQString current();
    static void setCurrent( const TQString &userId );

    /** Stores the login of @p userId and adds it to the list. */
    static bool storeSession( const TQString &userId, const TQString &session, WId window = 0 );
    static TQString loadSession( const TQString &userId, WId window = 0 );
    /** Puts refreshed tokens into the stored login. */
    static void updateTokens( const TQString &userId, const TQString &accessToken, const TQString &refreshToken );
    /** Forgets the login and removes the account from the list. */
    static void remove( const TQString &userId );

    /** True if logins are stored in TDEWallet. */
    static bool usesWallet();

private:
    static TDEWallet::Wallet *wallet( WId window );
    static TQString fileFor( const TQString &userId );
};

#endif
