/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "settings.h"

#include <tdeconfig.h>
#include <tdeglobal.h>

static TDEConfig *cfg( const char *group = "General" )
{
    TDEConfig *c = TDEGlobal::config();
    c->setGroup( group );
    return c;
}

#define BOOL_SETTING( getter, setter, key, def ) \
    bool Settings::getter() { return cfg()->readBoolEntry( key, def ); } \
    void Settings::setter( bool on ) { cfg()->writeEntry( key, on ); }

BOOL_SETTING( useWallet, setUseWallet, "UseWallet", true )
BOOL_SETTING( enterSends, setEnterSends, "EnterSends", true )
BOOL_SETTING( showMembershipEvents, setShowMembershipEvents, "ShowMembershipEvents", true )
BOOL_SETTING( showImages, setShowImages, "ShowImages", true )
BOOL_SETTING( sendTyping, setSendTyping, "SendTyping", true )
BOOL_SETTING( trayIcon, setTrayIcon, "TrayIcon", true )
BOOL_SETTING( notifications, setNotifications, "Notifications", true )
BOOL_SETTING( notificationPreview, setNotificationPreview, "NotificationPreview", true )

// read by the autostart desktop file (X-TDE-autostart-condition)
bool Settings::autostart() { return cfg()->readBoolEntry( "Autostart", false ); }
void Settings::setAutostart( bool on ) { cfg()->writeEntry( "Autostart", on ); }

void Settings::sync()
{
    TDEGlobal::config()->sync();
}
