/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_COMPOSER_H
#define TMATRIX_COMPOSER_H

#include <ktextedit.h>

class TQTimer;

/**
 * The message input.  Enter sends (configurable), Shift+Enter starts a new
 * line, Up edits the last message, Escape cancels a reply or edit.  Files
 * dropped or pasted are offered for upload.
 */
class Composer : public KTextEdit
{
    TQ_OBJECT
public:
    Composer( TQWidget *parent, const char *name = 0 );

signals:
    void sendRequested();
    void editLastRequested();
    void cancelRequested();
    void typing( bool typing );
    void filesDropped( const TQStringList &paths );

protected:
    void keyPressEvent( TQKeyEvent *e );
    void contentsDragEnterEvent( TQDragEnterEvent *e );
    void contentsDropEvent( TQDropEvent *e );

private slots:
    void slotChanged();
    void slotStopTyping();

private:
    TQTimer *m_typingTimer;
    bool m_typing;
};

#endif
