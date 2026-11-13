/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_TRAY_H
#define TMATRIX_TRAY_H

#include <ksystemtray.h>
#include <tqpixmap.h>

/** The icon in the system tray, with the number of unread messages. */
class Tray : public KSystemTray
{
    TQ_OBJECT
public:
    Tray( TQWidget *mainWindow );
    void setUnread( int unread, int highlights, int invites, bool offline );

signals:
    void nextUnreadRequested();

protected:
    void mousePressEvent( TQMouseEvent *e );

private:
    TQPixmap m_icon;
};

#endif
