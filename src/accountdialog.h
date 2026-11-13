/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_ACCOUNTDIALOG_H
#define TMATRIX_ACCOUNTDIALOG_H

#include "json.h"

#include <kdialogbase.h>

class KLineEdit;
class TDEListView;
class KPushButton;
class TQLabel;

/** Profile, sessions (devices), encryption and password of the account. */
class AccountDialog : public KDialogBase
{
    TQ_OBJECT
public:
    enum { Profile = 0, Sessions, Security, Password };
    AccountDialog( TQWidget *parent, int page = Profile );

signals:
    void verifyOwnDevice();
    void verifyDevice( const TQString &deviceId );

protected slots:
    void slotApply();
    void slotOk();

private slots:
    void slotProfile( const JsonValue &reply );
    void slotDevices( const JsonValue &reply );
    void slotEncryption( const JsonValue &status );
    void slotReloadDevices();
    void slotAvatar();
    void slotRename();
    void slotSignOut();
    void slotVerify();
    void slotSetupRecovery();
    void slotRecoveryReady( const JsonValue &reply );
    void slotEnterKey();
    void slotRecovered( const JsonValue &reply );
    void slotChangePassword();
    void slotPasswordChanged( const JsonValue &reply );
    void slotDeviceSelection();

private:
    void applyProfile();
    TQLabel *m_avatar, *m_userInfo, *m_securityInfo, *m_passwordStatus;
    KLineEdit *m_displayName;
    TQString m_originalName;
    TDEListView *m_devices;
    KPushButton *m_rename, *m_signOut, *m_verifyDevice;
    KPushButton *m_setupRecovery, *m_enterKey, *m_verifySession;
    KLineEdit *m_oldPassword, *m_newPassword, *m_newPassword2;
};

/** Shows a recovery key once, with the advice to keep it safe. */
class RecoveryKeyDialog : public KDialogBase
{
    TQ_OBJECT
public:
    RecoveryKeyDialog( TQWidget *parent, const TQString &key );
private slots:
    void slotCopy();
    void slotSave();
private:
    TQString m_key;
};

#endif
