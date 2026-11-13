/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_SETTINGSDIALOG_H
#define TMATRIX_SETTINGSDIALOG_H

#include <kdialogbase.h>

class TQCheckBox;

/** The preferences of TMatrix. */
class SettingsDialog : public KDialogBase
{
    TQ_OBJECT
public:
    SettingsDialog( TQWidget *parent );
signals:
    void settingsChanged();
protected slots:
    void slotOk();
    void slotApply();
    void slotDefault();
private slots:
    void slotNotifyConfig();
    void slotTrayToggled( bool on );
private:
    void load( bool defaults );
    void save();
    TQCheckBox *m_enterSends, *m_typing, *m_membership, *m_images;
    TQCheckBox *m_tray, *m_autostart, *m_notifications, *m_preview, *m_wallet;
};

#endif
