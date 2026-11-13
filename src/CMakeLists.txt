include_directories(
  ${CMAKE_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${TMATRIX_CORE_INCLUDE_DIR}
  ${TDE_INCLUDE_DIR}
  ${TQT_INCLUDE_DIRS}
)

link_directories(
  ${TQT_LIBRARY_DIRS}
  ${TDE_LIB_DIR}
)


##### tmatrixcommon (static): the engine connection, shared with the tests

tde_add_library( tmatrixcommon STATIC_PIC AUTOMOC
  SOURCES
    json.cpp
    matrixclient.cpp
  LINK
    tdecore-shared
    ${TMATRIX_CORE_LIBRARIES}
)


##### tde-tmatrix (executable)

tde_add_executable( tde-tmatrix AUTOMOC
  SOURCES
    main.cpp
    mainwindow.cpp
    accounts.cpp
    settings.cpp
    mediacache.cpp
    timelineview.cpp
    roomlist.cpp
    composer.cpp
    logindialog.cpp
    roomdialogs.cpp
    accountdialog.cpp
    verificationdialog.cpp
    settingsdialog.cpp
    tray.cpp
  LINK
    tmatrixcommon-static
    ${TMATRIX_CORE_LIBRARIES}
    tdeio-shared
    tdeui-shared
    tdecore-shared
    tdewalletclient-shared
  DESTINATION ${BIN_INSTALL_DIR}
)


##### other data

install( FILES tde-tmatrixui.rc DESTINATION ${DATA_INSTALL_DIR}/tde-tmatrix )
install( FILES eventsrc DESTINATION ${DATA_INSTALL_DIR}/tde-tmatrix )
tde_create_translated_desktop( SOURCE tde-tmatrix.desktop DESTINATION ${XDG_APPS_INSTALL_DIR} )
tde_create_translated_desktop( SOURCE tde-tmatrix-autostart.desktop DESTINATION ${AUTOSTART_INSTALL_DIR} )
