/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_VERIFICATIONDIALOG_H
#define TMATRIX_VERIFICATIONDIALOG_H

#include "json.h"

#include <kdialogbase.h>

class TQLabel;
class TQWidgetStack;
class TQGridLayout;
class TQFrame;

/**
 * Interactive verification of a session or user: the request, then the
 * comparison of seven emoji (SAS) shown on both devices.
 */
class VerificationDialog : public KDialogBase
{
    TQ_OBJECT
public:
    /** @p incoming: the other side asked; else we asked and wait. */
    VerificationDialog( TQWidget *parent, const TQString &userId, const TQString &flowId, bool incoming, bool self );
    TQString flowId() const { return m_flowId; }
    void setFlowId( const TQString &flowId ) { m_flowId = flowId; }

public slots:
    void stateChanged( const JsonValue &state );

protected slots:
    void slotUser1();  // accept / they match
    void slotUser2();  // they don't match
    void slotCancel();

private:
    void showMessage( const TQString &icon, const TQString &text );
    TQString m_userId, m_flowId;
    bool m_incoming, m_comparing, m_finished;
    TQLabel *m_icon, *m_text;
    TQFrame *m_emojiBox;
    TQGridLayout *m_emojiGrid;
};

#endif
