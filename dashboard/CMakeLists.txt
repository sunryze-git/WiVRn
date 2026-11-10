include(KDEInstallDirs)
include(KDECMakeSettings)
include(ECMFeatureSummary)
include(ECMFindQmlModule)
include(ECMQmlModule)
include(I18n)

ecm_find_qmlmodule(org.kde.kirigami REQUIRED)
ecm_find_qmlmodule(org.kde.kirigamiaddons.formcard REQUIRED)
ecm_feature_summary(WHAT REQUIRED_PACKAGES_NOT_FOUND FATAL_ON_MISSING_REQUIRED_PACKAGES)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(wyrm-dashboard
    avahi.cpp
    main.cpp
    wyrm_server.cpp
    wyrm_qdbus_types.cpp
    apps.cpp
    dashboard_utils.cpp
    escape_string.cpp
    settings.cpp
    steam.cpp
    adb.cpp
    apk_installer.cpp
    firewall.cpp
    vulkan_info.cpp
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.wyrmvr.Server.xml PROPERTIES INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/wyrm_qdbus_types.h")
qt_add_dbus_interface(DBUS_INTERFACE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.wyrmvr.Server.xml wyrm_server_dbus)
target_sources(wyrm-dashboard PRIVATE ${DBUS_INTERFACE_SRC})

set_source_files_properties(../images/wyrm.svg PROPERTIES QT_RESOURCE_ALIAS qml/wyrm.svg)
qt_add_resources(wyrm-dashboard PREFIX "/" BASE .. FILES ../images/wyrm.svg)

set_source_files_properties(qml/DashboardSettings.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(wyrm-dashboard
    URI org.wyrmvr.wyrm
    QML_FILES
        qml/Main.qml
        qml/About.qml
        qml/ApkInstallPage.qml
        qml/DashboardSettings.qml
        qml/SettingsPage.qml
        qml/HeadsetsPage.qml
        qml/HeadsetStatsPage.qml
        qml/WizardPage.qml
        qml/WizardStep.qml
        qml/DeviceCard.qml
        qml/BetterLabel.qml
        qml/ConnectUsbDialog.qml
        qml/SelectGame.qml
        qml/SteamLaunchOptions.qml
        qml/TroubleshootPage.qml
        qml/TroubleshootCard.qml
    RESOURCES
        ../images/wyrm.svg
        qml/Discord.svg
        qml/Github.svg
)

configure_file(gui_config.h.in gui_config.h)
configure_file(org.wyrmvr.wyrm.desktop.in org.wyrmvr.wyrm.desktop)

target_link_libraries(wyrm-dashboard
    PRIVATE
    Qt6::DBus
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    KF6::IconThemes
    KF6::Kirigami
    QCoro::Core
    QCoro::Network
    QCoro::Qml
    wyrm-common-base
    wyrm-common-server
    wyrm-external
)

install(TARGETS wyrm-dashboard ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES org.wyrmvr.wyrm.metainfo.xml
	DESTINATION share/metainfo
)

install(FILES ../images/wyrm.svg
	DESTINATION share/icons/hicolor/scalable/apps
	RENAME org.wyrmvr.wyrm.svg
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.wyrmvr.wyrm.desktop
	DESTINATION share/applications
)

create_mo_files(wyrm-dashboard wyrm-dashboard "${CMAKE_CURRENT_SOURCE_DIR}/../locale")
