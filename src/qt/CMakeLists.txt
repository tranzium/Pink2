# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Pinkcoin-Qt GUI wallet

#-----------------------------------------
# Qt GUI source files
#-----------------------------------------
set(PINKCOIN_QT_SOURCES
    ../init.cpp
    aboutdialog.cpp
    addressbookpage.cpp
    addresstablemodel.cpp
    askpassphrasedialog.cpp
    bitcoin.cpp
    bitcoinaddressvalidator.cpp
    bitcoinamountfield.cpp
    bitcoingui.cpp
    bitcoinstrings.cpp
    bitcoinunits.cpp
    clientmodel.cpp
    coincontroldialog.cpp
    coincontroltreewidget.cpp
    csvmodelwriter.cpp
    editaddressdialog.cpp
    guiutil.cpp
    messagepage.cpp
    messagemodel.cpp
    monitoreddatamapper.cpp
    notificator.cpp
    optionsdialog.cpp
    optionsmodel.cpp
    overviewpage.cpp
    qtipcserver.cpp
    qvalidatedlineedit.cpp
    qvalidatedtextedit.cpp
    qvaluecombobox.cpp
    rpcconsole.cpp
    sendcoinsdialog.cpp
    sendcoinsentry.cpp
    sendmessagesdialog.cpp
    sendmessagesentry.cpp
    signverifymessagedialog.cpp
    trafficgraphwidget.cpp
    transactiondesc.cpp
    transactiondescdialog.cpp
    transactionfilterproxy.cpp
    transactionrecord.cpp
    transactiontablemodel.cpp
    transactionview.cpp
    walletmodel.cpp
    plugins/mrichtexteditor/mrichtextedit.cpp
)

set(PINKCOIN_QT_HEADERS
    aboutdialog.h
    addressbookpage.h
    addresstablemodel.h
    askpassphrasedialog.h
    bitcoinaddressvalidator.h
    bitcoinamountfield.h
    bitcoingui.h
    bitcoinunits.h
    clabel.h
    clientmodel.h
    coincontroldialog.h
    coincontroltreewidget.h
    csvmodelwriter.h
    editaddressdialog.h
    guiconstants.h
    guiutil.h
    messagepage.h
    messagemodel.h
    monitoreddatamapper.h
    notificator.h
    optionsdialog.h
    optionsmodel.h
    overviewpage.h
    qtipcserver.h
    qvalidatedlineedit.h
    qvalidatedtextedit.h
    qvaluecombobox.h
    rpcconsole.h
    sendcoinsdialog.h
    sendcoinsentry.h
    sendmessagesdialog.h
    sendmessagesentry.h
    signverifymessagedialog.h
    trafficgraphwidget.h
    transactiondesc.h
    transactiondescdialog.h
    transactionfilterproxy.h
    transactionrecord.h
    transactiontablemodel.h
    transactionview.h
    walletmodel.h
    plugins/mrichtexteditor/mrichtextedit.h
)

#-----------------------------------------
# Qt UI forms
#-----------------------------------------
set(PINKCOIN_QT_FORMS
    forms/aboutdialog.ui
    forms/addressbookpage.ui
    forms/askpassphrasedialog.ui
    forms/coincontroldialog.ui
    forms/editaddressdialog.ui
    forms/messagepage.ui
    forms/optionsdialog.ui
    forms/overviewpage.ui
    forms/rpcconsole.ui
    forms/sendcoinsdialog.ui
    forms/sendcoinsentry.ui
    forms/sendmessagesdialog.ui
    forms/sendmessagesentry.ui
    forms/signverifymessagedialog.ui
    forms/transactiondescdialog.ui
    plugins/mrichtexteditor/mrichtextedit.ui
)

#-----------------------------------------
# Qt translations (compile .ts to .qm)
#-----------------------------------------
find_package(Qt5 COMPONENTS LinguistTools QUIET)
if(Qt5LinguistTools_FOUND)
    file(GLOB PINKCOIN_TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/locale/*.ts")
    # Compile translations to the locale source directory so qrc can find them
    set_source_files_properties(${PINKCOIN_TS_FILES} PROPERTIES
        OUTPUT_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/locale"
    )
    qt5_add_translation(PINKCOIN_QM_FILES ${PINKCOIN_TS_FILES})
else()
    message(WARNING "Qt5 LinguistTools not found, translations will not be compiled")
    set(PINKCOIN_QM_FILES "")
endif()

#-----------------------------------------
# Qt resources
#-----------------------------------------
set(PINKCOIN_QT_RESOURCES
    bitcoin.qrc
)

#-----------------------------------------
# Optional QR code support
#-----------------------------------------
if(USE_QRCODE AND QREncode_FOUND)
    list(APPEND PINKCOIN_QT_SOURCES qrcodedialog.cpp)
    list(APPEND PINKCOIN_QT_HEADERS qrcodedialog.h)
    list(APPEND PINKCOIN_QT_FORMS forms/qrcodedialog.ui)
endif()

#-----------------------------------------
# Platform-specific files
#-----------------------------------------

# macOS specific
if(APPLE)
    list(APPEND PINKCOIN_QT_SOURCES
        macdockiconhandler.mm
        macnotificationhandler.mm
    )
    list(APPEND PINKCOIN_QT_HEADERS
        macdockiconhandler.h
        macnotificationhandler.h
    )
endif()

# Windows resource file
if(WIN32)
    set(PINKCOIN_QT_RC res/bitcoin-qt.rc)
endif()

#-----------------------------------------
# Create executable
#-----------------------------------------
if(WIN32)
    add_executable(Pinkcoin-Qt WIN32
        ${PINKCOIN_QT_SOURCES}
        ${PINKCOIN_QT_HEADERS}
        ${PINKCOIN_QT_FORMS}
        ${PINKCOIN_QT_RESOURCES}
        ${PINKCOIN_QT_RC}
        ${PINKCOIN_QM_FILES}
    )
elseif(APPLE)
    add_executable(Pinkcoin-Qt MACOSX_BUNDLE
        ${PINKCOIN_QT_SOURCES}
        ${PINKCOIN_QT_HEADERS}
        ${PINKCOIN_QT_FORMS}
        ${PINKCOIN_QT_RESOURCES}
        ${PINKCOIN_QM_FILES}
    )
else()
    add_executable(Pinkcoin-Qt
        ${PINKCOIN_QT_SOURCES}
        ${PINKCOIN_QT_HEADERS}
        ${PINKCOIN_QT_FORMS}
        ${PINKCOIN_QT_RESOURCES}
        ${PINKCOIN_QM_FILES}
    )
endif()

#-----------------------------------------
# Include directories
#-----------------------------------------
target_include_directories(Pinkcoin-Qt
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/mrichtexteditor
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated UI headers
)

# Set AUTOUIC search paths for .ui files in forms/ subdirectory
set_target_properties(Pinkcoin-Qt PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/forms;${CMAKE_CURRENT_SOURCE_DIR}/plugins/mrichtexteditor"
)

# Skip AUTOUIC for source files that include ui_interface.h (not a Qt UI file)
set_source_files_properties(
    ../init.cpp
    bitcoin.cpp
    clientmodel.cpp
    messagemodel.cpp
    qtipcserver.cpp
    transactiondesc.cpp
    transactiontablemodel.cpp
    walletmodel.cpp
    PROPERTIES SKIP_AUTOUIC ON
)

#-----------------------------------------
# Qt definitions
#-----------------------------------------
target_compile_definitions(Pinkcoin-Qt
    PRIVATE
        QT_GUI
        QT_DISABLE_DEPRECATED_BEFORE=0
)

#-----------------------------------------
# Link libraries
#-----------------------------------------
target_link_libraries(Pinkcoin-Qt
    PRIVATE
        pinkcoin_core
        Qt5::Core
        Qt5::Widgets
        Qt5::Gui
        Qt5::Network
        Qt5::Svg
)

# Optional D-Bus (Linux)
if(USE_DBUS AND Qt5DBus_FOUND)
    target_link_libraries(Pinkcoin-Qt PRIVATE Qt5::DBus)
endif()

# Optional QR code
if(USE_QRCODE AND QREncode_FOUND)
    target_link_libraries(Pinkcoin-Qt PRIVATE QREncode::QREncode)
endif()

#-----------------------------------------
# macOS specific settings
#-----------------------------------------
if(APPLE)
    set_target_properties(Pinkcoin-Qt PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Pinkcoin-Qt"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.pinkcoin.Pinkcoin-Qt"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE_ICON_FILE "pinkcoin.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/share/qt/Info.plist"
    )

    # Copy icon to bundle
    set(MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/pinkcoin.icns")
    if(EXISTS "${MACOS_ICON}")
        set_source_files_properties(${MACOS_ICON} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        target_sources(Pinkcoin-Qt PRIVATE ${MACOS_ICON})
    endif()

    # Link macOS frameworks
    target_link_libraries(Pinkcoin-Qt
        PRIVATE
            "-framework Foundation"
            "-framework ApplicationServices"
            "-framework AppKit"
    )
endif()

#-----------------------------------------
# Windows specific settings
#-----------------------------------------
if(WIN32)
    # Static linking for Windows
    if(STATIC_BUILD)
        set_target_properties(Pinkcoin-Qt PROPERTIES
            LINK_FLAGS "-static-libgcc -static-libstdc++ -static"
        )
    endif()

    # Windows subsystem
    set_target_properties(Pinkcoin-Qt PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

#-----------------------------------------
# Install themes
#-----------------------------------------
if(WIN32)
    # Copy themes to build directory
    add_custom_command(TARGET Pinkcoin-Qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/res/themes"
            "$<TARGET_FILE_DIR:Pinkcoin-Qt>/themes"
        COMMENT "Copying themes to output directory"
    )
elseif(APPLE)
    # Copy themes to bundle
    add_custom_command(TARGET Pinkcoin-Qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/res/themes"
            "$<TARGET_BUNDLE_DIR:Pinkcoin-Qt>/Contents/MacOS/themes"
        COMMENT "Copying themes to application bundle"
    )
else()
    # Linux: copy themes to build directory (will be installed separately)
    add_custom_command(TARGET Pinkcoin-Qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/res/themes"
            "$<TARGET_FILE_DIR:Pinkcoin-Qt>/themes"
        COMMENT "Copying themes to output directory"
    )
endif()
