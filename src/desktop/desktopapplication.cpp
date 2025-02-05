/* Copyright (C) 2004-2021 Robert Griebl. All rights reserved.
**
** This file is part of BrickStore.
**
** This file may be distributed and/or modified under the terms of the GNU
** General Public License version 2 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.
*/
#include <QtCore/QThread>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QCommandLineParser>
#include <QtCore/QProcess>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QApplication>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyle>
#if defined(Q_OS_WINDOWS)
#  if defined(Q_CC_MINGW)
#    undef _WIN32_WINNT
#    define _WIN32_WINNT 0x0600
#  endif
#  include <windows.h>
#  ifdef MessageBox
#    undef MessageBox
#  endif
#  if defined(Q_CC_MSVC) // needed for themed common controls (e.g. file open dialogs)
#    pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#  endif
#elif defined(Q_OS_MACOS)
#  include <QtCore/QVersionNumber>
#  include <QtGui/private/qguiapplication_p.h>
#  include <QtGui/private/qshortcutmap_p.h>
#endif

#include "bricklink/core.h"
#include "common/config.h"
#include "desktop/brickstoreproxystyle.h"
#include "desktop/desktopuihelpers.h"
#include "desktop/mainwindow.h"
#include "desktop/scriptmanager.h"
#include "desktop/smartvalidator.h"
#include "utility/utility.h"

#include "desktopapplication.h"



DesktopApplication::DesktopApplication(int &argc, char **argv)
    : Application(argc, argv)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
#endif

#if defined(Q_OS_WINDOWS)
    // the Vista style scales very badly when scaled to non-integer factors
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round);
#else
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

#if defined(Q_OS_LINUX)
    const QByteArray xdgDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    for (const auto *gtkBased : { "GNOME", "MATE", "Cinnamon" }) {
        if (xdgDesktop.contains(gtkBased))
            qputenv("QT_QPA_PLATFORMTHEME", "gtk2");
    }
#endif

    (void) new QApplication(argc, argv);

    m_clp.addHelpOption();
    m_clp.addVersionOption();
    m_clp.addOption({ "load-translation"_l1, "Load the specified translation (testing only)."_l1, "qm-file"_l1 });
    m_clp.addOption({ "new-instance"_l1, "Start a new instance."_l1 });
    m_clp.addPositionalArgument("files"_l1, "The BSX documents to open, optionally."_l1, "[files...]"_l1);
    m_clp.process(QCoreApplication::arguments());

    m_translationOverride = m_clp.value("load-translation"_l1);
    m_queuedDocuments << m_clp.positionalArguments();

    // check for an already running instance
    if (!m_clp.isSet("new-instance"_l1) && notifyOtherInstance())
        exit(0);

#if defined(Q_OS_LINUX)
    QPixmap pix(":/assets/generated-app-icons/brickstore.png"_l1);
    if (!pix.isNull())
        QGuiApplication::setWindowIcon(pix);
#endif

#if defined(Q_OS_MACOS)
#  if QT_VERSION <= QT_VERSION_CHECK(5, 15, 2)
    // the new default font San Francisco has rendering problems: QTBUG-88495
    if (QVersionNumber::fromString(QSysInfo::productVersion()).majorVersion() >= 11) {
        QFont f = QApplication::font();
        f.setFamily("Helvetica Neue"_l1);
        QApplication::setFont(f);
    }
#  endif
#endif
}

void DesktopApplication::init()
{
    DesktopUIHelpers::create();

    Application::init();

    connect(Config::inst(), &Config::uiThemeChanged, this, &DesktopApplication::setUiTheme);
    setUiTheme();
    setDesktopIconTheme();

    if (!m_startupErrors.isEmpty()) {
        QCoro::waitFor(UIHelpers::critical(m_startupErrors.join("\n\n"_l1)));

        // we cannot call quit directly, since there is no event loop to quit from...
        QMetaObject::invokeMethod(this, &QCoreApplication::quit, Qt::QueuedConnection);
        return;
    }

    // tranform . or , into the local decimal separator in all QDoubleSpinBoxes and all
    // QLineEdits with a SmartDoubleValidator set
    DotCommaFilter::install();

    ScriptManager::inst()->initialize();

    MainWindow::inst()->show();

#if defined(Q_OS_MACOS)
    MainWindow::inst()->raise();

#elif defined(Q_OS_WINDOWS)
    RegisterApplicationRestart(nullptr, 0); // make us restart-able by installers

#endif

#if defined(SENTRY_ENABLED)
    if (Config::inst()->sentryConsent() == Config::SentryConsent::Unknown) {
        QString text = tr("Enable anonymous crash reporting?<br><br>Please consider enabling this feature when available.<br>If you have any doubts about what information is being submitted and how it is used, please <a href='https://github.com/rgriebl/brickstore/wiki/Crash-Reporting'>see here</a>.<br><br>Crash reporting can be enabled or disabled at any time in the Settings dialog.");

        switch (QMessageBox::question(MainWindow::inst(), QCoreApplication::applicationName(), text)) {
        case QMessageBox::Yes:
            Config::inst()->setSentryConsent(Config::SentryConsent::Given);
            break;
        case QMessageBox::No:
            Config::inst()->setSentryConsent(Config::SentryConsent::Revoked);
            break;
        default:
            break;
        }
    }
#endif
}

DesktopApplication::~DesktopApplication()
{
    delete ScriptManager::inst();
}

void DesktopApplication::checkRestart()
{
    if (m_restart)
        QProcess::startDetached(qApp->applicationFilePath(), { });
}

bool DesktopApplication::eventFilter(QObject *o, QEvent *e)
{
    switch (e->type()) {
    case QEvent::ChildPolished:
        if (auto *tb = qobject_cast<QToolButton *>(static_cast<QChildEvent *>(e)->child())) {
            if (!qobject_cast<QToolBar *>(o)) {
                QPointer<QToolButton> tbptr(tb);
                QMetaObject::invokeMethod(this, [tbptr]() {
                    if (tbptr && tbptr->autoRaise()) {
#if defined(Q_OS_MACOS)
                        // QToolButtons look really ugly on macOS, so we re-style them
                        static QStyle *fusion = QStyleFactory::create("fusion"_l1);
                        tbptr->setStyle(fusion);
#endif
                        if (qstrcmp(tbptr->style()->metaObject()->className(), "QFusionStyle") == 0) {
                            QPalette pal = tbptr->palette();
                            pal.setColor(QPalette::Button, Utility::premultiplyAlpha(
                                             qApp->palette("QAbstractItemView")
                                             .color(QPalette::Highlight)));
                            tbptr->setPalette(pal);
                        }
                    }
                });
            }
        }
        break;

#if defined(Q_OS_MACOS)
    // the handling of emacs-style multi-key shortcuts is broken on macOS, because menu
    // shortcuts are handled directly in the QPA plugin, instad of going through the global
    // QShortcutMap. The workaround is override any shortcut while the map is in PartialMatch state.
    case QEvent::ShortcutOverride: {
        auto &scm = QGuiApplicationPrivate::instance()->shortcutMap;
        if (scm.state() == QKeySequence::PartialMatch) {
            e->accept();
            return true;
        }
        break;
    }
#endif
    case QEvent::ApplicationPaletteChange:
        // we need to delay this: otherwise macOS crashes on theme changes
        QMetaObject::invokeMethod(this, &DesktopApplication::setDesktopIconTheme,
                                  Qt::QueuedConnection);
        break;

    default:
        break;
    }

    return Application::eventFilter(o, e);
}

QCoro::Task<bool> DesktopApplication::closeAllViews()
{
    co_return MainWindow::inst()->closeAllViews();
}

bool DesktopApplication::notifyOtherInstance()
{
    const int timeout = 1000;
    enum { Undecided, Server, Client } state = Undecided;
    QString socketName = "BrickStore"_l1;
    QLocalServer *server = nullptr;

#if defined(Q_OS_WINDOWS)
    // QLocalServer::listen() would succeed for any number of callers
    HANDLE semaphore = ::CreateSemaphore(nullptr, 0, 1, L"Local\\BrickStore");
    state = (semaphore && (::GetLastError() == ERROR_ALREADY_EXISTS)) ? Client : Server;
#endif
    if (state != Client) {
        server = new QLocalServer(this);

        bool res = server->listen(socketName);
#if defined(Q_OS_UNIX)
        if (!res && server->serverError() == QAbstractSocket::AddressInUseError) {
            QFile::remove(QDir::cleanPath(QDir::tempPath()) + QLatin1Char('/') + socketName);
            res = server->listen(socketName);
        }
#endif
        if (res) {
            connect(server, &QLocalServer::newConnection,
                    this, [this]() {
                auto *server = qobject_cast<QLocalServer *>(sender());
                if (!server)
                    return;
                QLocalSocket *client = server->nextPendingConnection();
                if (!client)
                    return;

                QDataStream ds(client);
                QStringList files;
                bool header = true;
                int need = sizeof(qint32);
                while (need) {
                    auto got = client->bytesAvailable();
                    if (got < need) {
                        client->waitForReadyRead();
                    } else if (header) {
                        need = 0;
                        ds >> need;
                        header = false;
                    } else {
                        ds >> files;
                        need = 0;
                    }
                }
                client->write("X", 1);

                m_queuedDocuments << files;
                openQueuedDocuments();

                if (auto fw = MainWindow::inst()) {
                    fw->setWindowState(fw->windowState() & ~Qt::WindowMinimized);
                    fw->raise();
                    fw->activateWindow();
                }
            });
            state = Server;
        }
    }
    if (state != Server) {
        QLocalSocket client(this);

        for (int i = 0; i < 2; ++i) { // try twice
            client.connectToServer(socketName);
            if (client.waitForConnected(timeout / 2) || i)
                break;
            else
                QThread::msleep(static_cast<unsigned long>(timeout) / 4);
        }

        if (client.state() == QLocalSocket::ConnectedState) {
            QStringList files;
            foreach (const QString &arg, QCoreApplication::arguments().mid(1)) {
                QFileInfo fi(arg);
                if (fi.exists() && fi.isFile())
                    files << fi.absoluteFilePath();
            }
            QByteArray data;
            QDataStream ds(&data, QIODevice::WriteOnly);
            ds << qint32(0) << files;
            ds.device()->seek(0);
            ds << qint32(data.size() - int(sizeof(qint32)));

            bool res = (client.write(data) == data.size());
            res = res && client.waitForBytesWritten(timeout / 2);
            res = res && client.waitForReadyRead(timeout / 2);
            res = res && (client.read(1) == QByteArray(1, 'X'));

            if (res) {
                delete server;
                return true;
            }
        }
    }
    return false;
}

void DesktopApplication::setUiTheme()
{
    static bool once = false;
    bool startup = !once;
    if (!once)
        once = true;
    auto theme = Config::inst()->uiTheme();

#if defined(Q_OS_MACOS)
    extern bool hasMacThemes();
    extern void setCurrentMacTheme(Config::UiTheme);

    // on macOS, we are using the native theme switch
    if (!hasMacThemes()) {
        if (!startup)
            QMessageBox::information(MainWindow::inst(), QCoreApplication::applicationName(),
                                     tr("Your macOS version is too old to support theme changes."));
    } else if (!startup || (theme != Config::UiTheme::SystemDefault)) {
        if (startup)
            setCurrentMacTheme(theme);
        else
            QMetaObject::invokeMethod(qApp, [=]() { setCurrentMacTheme(theme); }, Qt::QueuedConnection);
    }
    if (startup)
        QApplication::setStyle(new BrickStoreProxyStyle());

#else
    // on Windows and Linux, we have to instantiate a Fusion style with a matching palette for
    // the Light and Dark styles. Switching on the fly is hard because of possible QProxyStyle
    // instances, so we better restart the app.

    if (!startup) {
        QMessageBox mb(QMessageBox::Question, QCoreApplication::applicationName(),
                       tr("The theme change will take effect after a restart."),
                       QMessageBox::Yes | QMessageBox::No, MainWindow::inst());
        mb.setDefaultButton(QMessageBox::No);
        mb.setButtonText(QMessageBox::Yes, tr("Restart now"));
        mb.setButtonText(QMessageBox::No, tr("Later"));
        mb.setTextFormat(Qt::RichText);

        if (mb.exec() == QMessageBox::Yes) {
            m_restart = true;
            QCoreApplication::quit();
        }
        return;
    }

    if (theme != Config::UiTheme::SystemDefault) {
        QApplication::setStyle(new BrickStoreProxyStyle(QStyleFactory::create("fusion"_l1)));
        auto palette = QApplication::style()->standardPalette();

        if (theme == Config::UiTheme::Dark) {
            // from https://github.com/Jorgen-VikingGod/Qt-Frameless-Window-DarkStyle
            palette.setColor(QPalette::Window, QColor(53, 53, 53));
            palette.setColor(QPalette::WindowText, Qt::white);
            palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
            palette.setColor(QPalette::Base, QColor(42, 42, 42));
            palette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
            palette.setColor(QPalette::Inactive, QPalette::ToolTipText, Qt::white);
            palette.setColor(QPalette::Inactive, QPalette::ToolTipBase, QColor(53, 53, 53));
            palette.setColor(QPalette::Text, Qt::white);
            palette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
            palette.setColor(QPalette::Dark, QColor(35, 35, 35));
            palette.setColor(QPalette::Shadow, QColor(20, 20, 20));
            palette.setColor(QPalette::Button, QColor(53, 53, 53));
            palette.setColor(QPalette::ButtonText, Qt::white);
            palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
            palette.setColor(QPalette::BrightText, Qt::red);
            palette.setColor(QPalette::Link, QColor(42, 130, 218));
            palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
            palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
            palette.setColor(QPalette::HighlightedText, Qt::white);
            palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(127, 127, 127));
        }

        QApplication::setPalette(palette);
        QToolTip::setPalette(palette);
    } else {
        QApplication::setStyle(new BrickStoreProxyStyle());
    }
#endif // !Q_OS_MACOS
}

void DesktopApplication::setDesktopIconTheme()
{
    const auto pal = QGuiApplication::palette();
    auto winColor = pal.color(QPalette::Active, QPalette::Window);
    bool dark = ((winColor.lightnessF() * winColor.alphaF()) < 0.5);

    setIconTheme(dark ? DarkTheme : LightTheme);
}

#include "moc_desktopapplication.cpp"
