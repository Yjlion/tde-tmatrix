/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "settingsdialog.h"
#include "settings.h"

#include <tqcheckbox.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqvgroupbox.h>

#include <kiconloader.h>
#include <tdelocale.h>
#include <knotifydialog.h>
#include <kpushbutton.h>
#include <tdeaboutdata.h>
#include <tdeapplication.h>
#include <tdeglobal.h>

SettingsDialog::SettingsDialog( TQWidget *parent )
    : KDialogBase( IconList, i18n( "Configure TMatrix" ), Default | Ok | Apply | Cancel, Ok, parent, "settings", true, true )
{
    TQFrame *chat = addPage( i18n( "Messages" ), i18n( "Reading and Writing Messages" ), DesktopIcon( "message", 32 ) );
    TQVBoxLayout *cv = new TQVBoxLayout( chat, 0, spacingHint() );
    m_enterSends = new TQCheckBox( i18n( "&Enter sends the message (Shift+Enter starts a new line; otherwise Ctrl+Enter sends)" ), chat );
    m_typing = new TQCheckBox( i18n( "Let others see when I am &typing" ), chat );
    m_membership = new TQCheckBox( i18n( "Show &joins, leaves and name changes" ), chat );
    m_images = new TQCheckBox( i18n( "Show &images in the conversation" ), chat );
    cv->addWidget( m_enterSends );
    cv->addWidget( m_typing );
    cv->addWidget( m_membership );
    cv->addWidget( m_images );
    TQLabel *md = new TQLabel( i18n( "Messages are written in Markdown: *italic*, **bold**, `code`, > quote, - lists and [links](https://…)." ), chat );
    md->setAlignment( TQt::WordBreak );
    cv->addWidget( md );
    cv->addStretch( 1 );

    TQFrame *notify = addPage( i18n( "Notifications" ), i18n( "Tray Icon and Notifications" ), DesktopIcon( "knotify", 32 ) );
    TQVBoxLayout *nv = new TQVBoxLayout( notify, 0, spacingHint() );
    m_tray = new TQCheckBox( i18n( "Show an icon in the system &tray (closing the window keeps TMatrix running)" ), notify );
    m_autostart = new TQCheckBox( i18n( "&Start TMatrix in the tray when I log in" ), notify );
    m_notifications = new TQCheckBox( i18n( "&Notify me about new messages (direct chats, mentions and invitations)" ), notify );
    m_preview = new TQCheckBox( i18n( "Show the &text of the message in notifications" ), notify );
    nv->addWidget( m_tray );
    nv->addWidget( m_autostart );
    nv->addWidget( m_notifications );
    nv->addWidget( m_preview );
    KPushButton *events = new KPushButton( KGuiItem( i18n( "Configure &Sounds and Popups…" ), "knotify" ), notify );
    nv->addWidget( events, 0, TQt::AlignLeft );
    TQLabel *rooms = new TQLabel( i18n( "Which rooms notify you is set per room in Room Settings → Notifications." ), notify );
    rooms->setAlignment( TQt::WordBreak );
    nv->addWidget( rooms );
    nv->addStretch( 1 );
    connect( events, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotNotifyConfig() ) );
    connect( m_tray, TQ_SIGNAL( toggled( bool ) ), this, TQ_SLOT( slotTrayToggled( bool ) ) );
    connect( m_notifications, TQ_SIGNAL( toggled( bool ) ), m_preview, TQ_SLOT( setEnabled( bool ) ) );

    TQFrame *privacy = addPage( i18n( "Privacy" ), i18n( "Privacy and Security" ), DesktopIcon( "encrypted", 32 ) );
    TQVBoxLayout *pv = new TQVBoxLayout( privacy, 0, spacingHint() );
    m_wallet = new TQCheckBox( i18n( "Keep my login in the TDE &Wallet" ), privacy );
    pv->addWidget( m_wallet );
    TQLabel *w = new TQLabel( i18n( "Otherwise the login is stored in a file only you can read. Your messages and encryption keys are "
                                    "always stored encrypted on disk." ), privacy );
    w->setAlignment( TQt::WordBreak );
    pv->addWidget( w );
    pv->addStretch( 1 );

    load( false );
    resize( 600, 380 );
}

void SettingsDialog::load( bool defaults )
{
    m_enterSends->setChecked( defaults ? true : Settings::enterSends() );
    m_typing->setChecked( defaults ? true : Settings::sendTyping() );
    m_membership->setChecked( defaults ? true : Settings::showMembershipEvents() );
    m_images->setChecked( defaults ? true : Settings::showImages() );
    m_tray->setChecked( defaults ? true : Settings::trayIcon() );
    m_autostart->setChecked( defaults ? false : Settings::autostart() );
    m_notifications->setChecked( defaults ? true : Settings::notifications() );
    m_preview->setChecked( defaults ? true : Settings::notificationPreview() );
    m_wallet->setChecked( defaults ? true : Settings::useWallet() );
    slotTrayToggled( m_tray->isChecked() );
    m_preview->setEnabled( m_notifications->isChecked() );
}

void SettingsDialog::save()
{
    Settings::setEnterSends( m_enterSends->isChecked() );
    Settings::setSendTyping( m_typing->isChecked() );
    Settings::setShowMembershipEvents( m_membership->isChecked() );
    Settings::setShowImages( m_images->isChecked() );
    Settings::setTrayIcon( m_tray->isChecked() );
    Settings::setAutostart( m_tray->isChecked() && m_autostart->isChecked() );
    Settings::setNotifications( m_notifications->isChecked() );
    Settings::setNotificationPreview( m_preview->isChecked() );
    Settings::setUseWallet( m_wallet->isChecked() );
    Settings::sync();
    emit settingsChanged();
}

void SettingsDialog::slotTrayToggled( bool on )
{
    m_autostart->setEnabled( on );
}

void SettingsDialog::slotNotifyConfig()
{
    KNotifyDialog::configure( this );
}

void SettingsDialog::slotOk()
{
    save();
    accept();
}

void SettingsDialog::slotApply()
{
    save();
}

void SettingsDialog::slotDefault()
{
    load( true );
}

#include "settingsdialog.moc"
