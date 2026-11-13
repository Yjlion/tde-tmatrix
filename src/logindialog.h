/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_LOGINDIALOG_H
#define TMATRIX_LOGINDIALOG_H

#include "json.h"

#include <kdialogbase.h>

class KComboBox;
class KLineEdit;
class KPushButton;
class TQLabel;
class TQTimer;

/** Logs in to a homeserver with a password or single sign-on (SSO). */
class LoginDialog : public KDialogBase
{
    TQ_OBJECT
public:
    LoginDialog( TQWidget *parent );

    TQString userId() const { return m_userId; }

protected slots:
    void slotOk();
    void slotCancel();

private slots:
    void slotServerChanged();
    void slotDiscover();
    void slotDiscovered( const JsonValue &reply );
    void slotLoggedIn( const JsonValue &reply );
    void slotSso();
    void slotSsoStarted( const JsonValue &reply );
    void slotSsoFinished( const JsonValue &result );
    void slotOpenUrl( const TQString &url );
    void slotUserChanged();

private:
    void setBusy( bool busy, const TQString &message = TQString::null );
    void showError( const TQString &message );

    KComboBox *m_server;
    KLineEdit *m_user, *m_password;
    KPushButton *m_ssoButton;
    TQLabel *m_status, *m_serverInfo;
    TQTimer *m_discoverTimer;
    bool m_busy, m_ssoRunning;
    TQString m_userId, m_resolved;
};

#endif
