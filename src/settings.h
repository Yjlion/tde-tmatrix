/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_SETTINGS_H
#define TMATRIX_SETTINGS_H

#include <tqstring.h>

/** The preferences of TMatrix (tde-tmatrixrc). */
namespace Settings
{
    /** Store logins in TDEWallet (else in a file only the user can read). */
    bool useWallet();
    void setUseWallet( bool on );

    /** Enter sends the message, Shift+Enter starts a new line. */
    bool enterSends();
    void setEnterSends( bool on );

    /** Show joins, leaves and name changes in the timeline. */
    bool showMembershipEvents();
    void setShowMembershipEvents( bool on );

    /** Download images and show them in the timeline. */
    bool showImages();
    void setShowImages( bool on );

    /** Send typing notifications and read receipts. */
    bool sendTyping();
    void setSendTyping( bool on );

    /** Show a system tray icon; closing the window keeps TMatrix running. */
    bool trayIcon();
    void setTrayIcon( bool on );

    /** Start TMatrix (in the tray) when TDE starts. */
    bool autostart();
    void setAutostart( bool on );

    /** Notify about new messages: popups (KNotify). */
    bool notifications();
    void setNotifications( bool on );

    /** Show the text of the message in notifications. */
    bool notificationPreview();
    void setNotificationPreview( bool on );

    void sync();
}

#endif
