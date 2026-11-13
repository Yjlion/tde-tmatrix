/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mainwindow.h"
#include "tmatrix_core.h"

#include <config.h>

#include <tdeaboutdata.h>
#include <tdecmdlineargs.h>
#include <tdelocale.h>
#include <kuniqueapplication.h>
#include <twin.h>

static const char description[] = I18N_NOOP( "A Matrix chat client for the Trinity Desktop Environment" );

static TDECmdLineOptions options[] = {
    { "minimized", I18N_NOOP( "Start in the system tray" ), 0 },
    { "room <id>", I18N_NOOP( "Open the room with this ID" ), 0 },
    TDECmdLineLastOption
};

class TMatrixApp : public KUniqueApplication
{
public:
    TMatrixApp() : m_window( 0 ) {}
    int newInstance()
    {
        TDECmdLineArgs *args = TDECmdLineArgs::parsedArgs();
        if ( !m_window ) {
            m_window = new MainWindow;
            setMainWidget( m_window );
            m_window->start( args->isSet( "minimized" ) );
        } else if ( !args->isSet( "minimized" ) ) {
            m_window->show();
            KWin::activateWindow( m_window->winId() );
        }
        if ( args->isSet( "room" ) )
            m_window->openRoom( TQString::fromLocal8Bit( args->getOption( "room" ) ) );
        args->clear();
        return 0;
    }
private:
    MainWindow *m_window;
};

int main( int argc, char **argv )
{
    TDEAboutData about( "tde-tmatrix", I18N_NOOP( "TMatrix" ), VERSION, description, TDEAboutData::License_GPL_V2,
                        "(C) 2026 tde-tmatrix contributors", I18N_NOOP( "Uses matrix-rust-sdk for the Matrix protocol and encryption." ),
                        "https://github.com/Yjlion/tde-tmatrix", "https://github.com/Yjlion/tde-tmatrix/issues" );
    about.addAuthor( "tde-tmatrix contributors", I18N_NOOP( "Written with the help of an AI coding assistant" ) );
    about.addCredit( "The Matrix.org Foundation", I18N_NOOP( "matrix-rust-sdk and vodozemac (Apache 2.0)" ), 0, "https://github.com/matrix-org/matrix-rust-sdk" );
    TDECmdLineArgs::init( argc, argv, &about );
    TDECmdLineArgs::addCmdLineOptions( options );
    KUniqueApplication::addCmdLineOptions();
    if ( !KUniqueApplication::start() )
        return 0;
    TMatrixApp app;
    return app.exec();
}
