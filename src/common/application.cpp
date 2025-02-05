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
#include <QtCore/QFileInfo>
#include <QtCore/QEvent>
#include <QtCore/QTranslator>
#include <QtCore/QSysInfo>
#include <QtCore/QStringBuilder>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkProxyFactory>
#include <QtGui/QGuiApplication>
#include <QtGui/QFont>
#include <QtGui/QPalette>
#include <QtGui/QFileOpenEvent>
#include <QtGui/QPixmapCache>
#include <QtGui/QImageReader>
#include <QtGui/QDesktopServices>

#include "bricklink/core.h"
#include "bricklink/store.h"
#include "bricklink/updatedatabase.h"
#include "common/actionmanager.h"
#include "common/announcements.h"
#include "common/application.h"
#include "common/config.h"
#include "common/document.h"
#include "common/documentio.h"
#include "common/documentlist.h"
#include "common/onlinestate.h"
#include "common/recentfiles.h"
#include "common/uihelpers.h"
#include "ldraw/ldraw.h"
#include "utility/currency.h"
#include "utility/exception.h"
#include "utility/systeminfo.h"
#include "utility/transfer.h"
#include "utility/undo.h"
#include "utility/utility.h"
#include "version.h"

#if defined(SENTRY_ENABLED)
#  include "sentry.h"
#  include "version.h"
Q_LOGGING_CATEGORY(LogSentry, "sentry")
#endif

#if defined(Q_OS_UNIX) || defined(Q_CC_MINGW)
#  define HAS_CXXABI 1
#  include <cxxabi.h>
#endif

#if defined(STATIC_QT_BUILD) // this should be needed for linking against a static Qt, but it works without
#  include <QtCore/QtPlugin>
Q_IMPORT_PLUGIN(qjpeg)
Q_IMPORT_PLUGIN(qgif)
#endif

using namespace std::chrono_literals;


Application *Application::s_inst = nullptr;

Application::Application(int &argc, char **argv)
    : QObject()
{
    Q_UNUSED(argc)
    Q_UNUSED(argv)

    s_inst = this;

    QCoreApplication::setApplicationName(QLatin1String(BRICKSTORE_NAME));
    QCoreApplication::setApplicationVersion(QLatin1String(BRICKSTORE_VERSION));
    QGuiApplication::setApplicationDisplayName(QCoreApplication::applicationName());

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
}

void Application::init()
{
    QCoreApplication::instance()->installEventFilter(this);

    setupTerminateHandler();
    setupLogging();
    setupSentry();

    m_announcements = new Announcements(Application::inst()->gitHubUrl(), this);

    QNetworkProxyFactory::setUseSystemConfiguration(true);

    //TODO5: find out why we are blacklisted ... for now, fake the UA
    Transfer::setDefaultUserAgent("Br1ckstore"_l1 % u'/' % QCoreApplication::applicationVersion()
                                  % u" (" + QSysInfo::prettyProductName() % u')');

    // initialize config & resource
    (void) Config::inst()->upgrade(BRICKSTORE_MAJOR, BRICKSTORE_MINOR, BRICKSTORE_PATCH);
    checkSentryConsent();

    QIcon::setThemeSearchPaths({ ":/assets/icons"_l1 });

    (void) OnlineState::inst();
    (void) Currency::inst();
    (void) SystemInfo::inst();
    (void) RecentFiles::inst();

    connect(RecentFiles::inst(), &RecentFiles::openDocument,
            this, &Application::openDocument);
    connect(this, &Application::openDocument,
            this, &Document::load);

    m_defaultFontSize = QGuiApplication::font().pointSizeF();
    QCoreApplication::instance()->setProperty("_bs_defaultFontSize", m_defaultFontSize); // the settings dialog needs this

    auto setFontSizePercentLambda = [this](int p) {
        QFont f = QGuiApplication::font();
        f.setPointSizeF(m_defaultFontSize * qreal(qBound(50, p, 200)) / 100.);
        QGuiApplication::setFont(f);
    };
    connect(Config::inst(), &Config::fontSizePercentChanged, this, setFontSizePercentLambda);
    int fsp = Config::inst()->fontSizePercent();
    if (fsp != 100)
        setFontSizePercentLambda(fsp);

    updateTranslations();
    connect(Config::inst(), &Config::languageChanged,
            Application::inst(), &Application::updateTranslations);

    // sanity check - we might run into endless update loops later on if one of these is missing
    const auto imgFormats = QImageReader::supportedImageFormats();
    if (!imgFormats.contains("png") || !imgFormats.contains("jpg") || !imgFormats.contains("gif"))
        m_startupErrors << tr("Your installation is broken: image format plugins are missing!");

    QString errString;
    if (!initBrickLink(&errString))
        m_startupErrors << tr("Could not initialize the BrickLink kernel:") % u' ' % errString;

    connect(BrickLink::core(), &BrickLink::Core::authenticationFailed,
            this, [](const QString &userName, const QString &error) {
        UIHelpers::warning(tr("Failed to authenticate with BrickLink as user %1")
                            .arg(userName) % u"<br><b>" % error % u"</b>");
    });

    if (LDraw::create(Config::inst()->ldrawDir(), &errString))
        BrickLink::core()->setLDrawDataPath(LDraw::core()->dataPath());

    m_undoGroup = new UndoGroup(this);

    auto *am = ActionManager::inst();
    connect(this, &Application::languageChanged,
            am, &ActionManager::retranslate);
    am->retranslate();
}

void Application::afterInit()
{
    ActionManager::ActionTable applicationActionTable = {
        { "document_new", [](auto) { new Document(); } },
        { "document_open", [](auto) { Document::load(); } },
        { "document_import_bl_xml", [](auto) { DocumentIO::importBrickLinkXML(); } },
        { "document_import_ldraw_model", [](auto) { DocumentIO::importLDrawModel(); } },
        { "document_import_bl_store_inv", [this](auto) -> QCoro::Task<> {
              if (!co_await checkBrickLinkLogin())
                  co_return;

              auto store = BrickLink::core()->store();
              if (store->updateStatus() == BrickLink::UpdateStatus::Updating)
                  co_return;  // no caching right now

              bool success = co_await UIHelpers::progressDialog(tr("Import BrickLink Store"),
                                                                tr("Importing BrickLink Store"),
                                                                store,
                                                                &BrickLink::Store::updateProgress,
                                                                &BrickLink::Store::updateFinished,
                                                                &BrickLink::Store::startUpdate,
                                                                &BrickLink::Store::cancelUpdate);

              if (success)
                  Document::fromStore(store);
          } },
        { "view_show_input_errors", [](bool b) {
              Config::inst()->setShowInputErrors(b);
              ActionManager::inst()->qAction("view_goto_next_input_error")->setEnabled(b);
          } },
        { "view_show_diff_indicators", [](bool b) {
              Config::inst()->setShowDifferenceIndicators(b);
              ActionManager::inst()->qAction("view_goto_next_diff")->setEnabled(b);
          } },
        { "configure", [this](auto) { emit showSettings(); } },
        { "help_reportbug", [](auto) {
              QString url = "https://"_l1 % Application::inst()->gitHubUrl() % "/issues/new"_l1;
              QDesktopServices::openUrl(url);
        } },
        { "help_releasenotes", [](auto) {
              QString url = "https://"_l1 % Application::inst()->gitHubUrl() % "/releases"_l1;
              QDesktopServices::openUrl(url);
        } },
        { "help_announcements", [](auto) {
              QString url = Application::inst()->announcements()->announcementsWikiUrl();
              QDesktopServices::openUrl(url);
          } },
    };

    ActionManager::inst()->connectActionTable(applicationActionTable);

    connect(DocumentList::inst(), &DocumentList::documentAdded,
            this, [this](Document *document) {
        m_undoGroup->addStack(document->model()->undoStack());
    });
    connect(DocumentList::inst(), &DocumentList::documentRemoved,
            this, [this](Document *document) {
        m_undoGroup->removeStack(document->model()->undoStack());
    });

    if (!BrickLink::core()->isDatabaseValid() || BrickLink::core()->isDatabaseUpdateNeeded()) {
        if (!QCoro::waitFor(updateDatabase())) {
            QCoro::waitFor(UIHelpers::warning(tr("Could not load the BrickLink database files.<br /><br />The program is not functional without these files.")));
        }
    }
    if (BrickLink::core()->isDatabaseValid()) {
        openQueuedDocuments();

        // restore autosaves and/or last session
        QMetaObject::invokeMethod(this, [this]() { restoreLastSession(); }, Qt::QueuedConnection);
    }
}

QCoro::Task<> Application::restoreLastSession()
{
    bool autosavesRestored = false;
    int restorable = Document::restorableAutosaves();
    if (restorable > 0) {
        bool b = (co_await UIHelpers::question(tr("It seems like BrickStore crashed while %n document(s) had unsaved modifications.", nullptr, restorable)
                                               % u"<br><br>" % tr("Should these documents be restored from their last available auto-save state?"),
                                               UIHelpers::Yes | UIHelpers::No, UIHelpers::Yes, tr("Restore Documents"))
                  == UIHelpers::Yes);

        int restoredCount = Document::processAutosaves(b ? Document::AutosaveAction::Restore
                                                         : Document::AutosaveAction::Delete);
        autosavesRestored = (restoredCount > 0);
    }

    if (!autosavesRestored) {
        if (Config::inst()->restoreLastSession()) {
            const auto files = Config::inst()->value("/MainWindow/LastSessionDocuments"_l1).toStringList();
            for (const auto &file : files)
                Document::load(file);
        }
    }
}

Application::~Application()
{
    exitBrickLink();

    delete SystemInfo::inst();
    delete Currency::inst();
//    delete DocumentList::inst();
    delete Config::inst();

    s_inst = nullptr;
    delete qApp;

    shutdownSentry();
}

QString Application::applicationUrl() const
{
    return QLatin1String(BRICKSTORE_URL);
}

QString Application::gitHubUrl() const
{
    return QLatin1String(BRICKSTORE_GITHUB_URL);
}

void Application::checkRestart()
{ }

QCoro::Task<bool> Application::checkBrickLinkLogin()
{
    if (!Config::inst()->brickLinkUsername().isEmpty()) {
        if (!Config::inst()->brickLinkPassword().isEmpty()) {
            co_return true;
        } else {
            if (auto pw = co_await UIHelpers::getString(tr("Please enter the password for the BrickLink account %1:")
                                                        .arg(u"<b>" % Config::inst()->brickLinkUsername() % u"</b>"))) {
                Config::inst()->setBrickLinkPassword(*pw, true /*do not save*/);
                co_return true;
            }
        }
    } else {
        if (co_await UIHelpers::question(tr("No valid BrickLink login settings found.<br /><br />Do you want to change the settings now?")
                                         ) == UIHelpers::Yes) {
            emit showSettings("bricklink"_l1);
        }
    }
    co_return false;
}

QCoro::Task<bool> Application::updateDatabase()
{
    bool noWindows = (DocumentList::inst()->count() == 0);

    //TODO: block UI here

    QStringList files = DocumentList::inst()->allFiles();

    if (noWindows || co_await closeAllViews()) {
        if (DocumentList::inst()->count())
            co_await qCoro(DocumentList::inst(), &DocumentList::lastDocumentClosed);

        if (DocumentList::inst()->count())
            co_return false;

        UpdateDatabase update;

        bool success = co_await UIHelpers::progressDialog(tr("Update Database"),
                                                          tr("Updating the BrickLink database"),
                                                          &update,
                                                          &UpdateDatabase::progress,
                                                          &UpdateDatabase::finished,
                                                          qOverload<>(&UpdateDatabase::start),
                                                          &UpdateDatabase::cancel);
        for (const auto &file : files)
            Document::load(file);

        co_return success;
    }
    co_return false;
}

Announcements *Application::announcements()
{
    if (!m_announcements) {
        m_announcements = new Announcements(gitHubUrl(), this);
        m_announcements->check();
        auto checkTimer = new QTimer(this);
        checkTimer->callOnTimeout(this, [this]() { m_announcements->check(); });
        checkTimer->start(12h);

    }
    return m_announcements;
}

UndoGroup *Application::undoGroup()
{
    return m_undoGroup;
}

void Application::setupTerminateHandler()
{
    std::set_terminate([]() {
        char buffer [1024];

        std::exception_ptr e = std::current_exception();

        if (e) {
            const char *typeName = "<unknown type>";
#if defined(HAS_CXXABI)
            static size_t demangleBufferSize = 768;
            static char *demangleBuffer = static_cast<char *>(malloc(demangleBufferSize));

            if (auto type = abi::__cxa_current_exception_type()) {
                typeName = type->name();
                if (typeName) {
                    int status;
                    demangleBuffer = abi::__cxa_demangle(typeName, demangleBuffer, &demangleBufferSize, &status);
                    if (status == 0 && *demangleBuffer)
                        typeName = demangleBuffer;
                }
            }
#endif
            try {
                std::rethrow_exception(e);
            } catch (const std::exception &exc) {
                snprintf(buffer, sizeof(buffer), "uncaught exception of type %s (%s)", typeName, exc.what());
            } catch (const std::exception *exc) {
                snprintf(buffer, sizeof(buffer), "uncaught exception of type %s (%s)", typeName, exc->what());
            } catch (const char *exc) {
                snprintf(buffer, sizeof(buffer), "uncaught exception of type 'const char *' (%s)", exc);
            } catch (...) {
                snprintf(buffer, sizeof(buffer), "uncaught exception of type %s", typeName);
            }
        } else {
            snprintf(buffer, sizeof(buffer), "terminate was called although no exception was thrown");
        }
        qWarning().noquote() << buffer;
        abort();
    });
}

void Application::openQueuedDocuments()
{
    m_canEmitOpenDocuments = true;

    QMetaObject::invokeMethod(this, [this]() {
        while (!m_queuedDocuments.isEmpty())
            emit openDocument(m_queuedDocuments.takeFirst());
    }, Qt::QueuedConnection);
}

void Application::updateTranslations()
{
    QString language = Config::inst()->language();
    if (language.isEmpty())
        return;

    QString i18n = ":/translations"_l1;

    static bool once = false; // always load english
    if (!once) {
        auto transQt = new QTranslator(this);
        if (transQt->load("qtbase_en"_l1, i18n))
            QCoreApplication::installTranslator(transQt);

        auto transBrickStore = new QTranslator(this);
        if (transBrickStore->load("brickstore_en"_l1, i18n))
            QCoreApplication::installTranslator(transBrickStore);
        once = true;
    }

    m_trans_qt.reset(new QTranslator);
    m_trans_brickstore.reset(new QTranslator);

    if (language != "en"_l1) {
        if (m_trans_qt->load("qtbase_"_l1 + language, i18n))
            QCoreApplication::installTranslator(m_trans_qt.get());
    }
    if ((language != "en"_l1) || (!m_translationOverride.isEmpty())) {
        QString translationFile = "brickstore_"_l1 % language;
        QString translationDir = i18n;

        QFileInfo qm(m_translationOverride);
        if (qm.isReadable()) {
            translationFile = qm.fileName();
            translationDir = qm.absolutePath();
        }

        if (m_trans_brickstore->load(translationFile, translationDir))
            QCoreApplication::installTranslator(m_trans_brickstore.get());
    }
}

bool Application::eventFilter(QObject *o, QEvent *e)
{
    if ((o != qApp) || !e)
        return false;

    switch (e->type()) {
    case QEvent::FileOpen: {
        const QString file = static_cast<QFileOpenEvent *>(e)->file();
        if (m_canEmitOpenDocuments)
            emit openDocument(file);
        else
            m_queuedDocuments.append(file);
        return true;
    }
    case QEvent::LanguageChange: {
        emit languageChanged();
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(o, e);
}

QVariantMap Application::about() const
{
    QString header = R"(<p style="line-height: 150%;">)"_l1
            % R"(<span style="font-size: large"><b>)" BRICKSTORE_NAME "</b></span><br>"_l1
            % "<b>"_l1 % tr("Version %1 (build: %2)").arg(BRICKSTORE_VERSION ""_l1, BRICKSTORE_BUILD_NUMBER ""_l1)
            % "</b><br>"_l1
            % tr("Copyright &copy; %1").arg(BRICKSTORE_COPYRIGHT ""_l1) % "<br>"_l1
            % tr("Visit %1").arg(R"(<a href="https://)" BRICKSTORE_URL R"(">)" BRICKSTORE_URL R"(</a>)"_l1)
            % "</p>"_l1;

    QString license = tr(R"(<p>This program is free software; it may be distributed and/or modified under the terms of the GNU General Public License version 2 as published by the Free Software Foundation and appearing in the file LICENSE.GPL included in this software package.<br/>This program is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.<br/>See <a href="https://www.gnu.org/licenses/old-licenses/gpl-2.0.html">www.gnu.org/licenses/old-licenses/gpl-2.0.html</a> for GPL licensing information.</p><p>All data from <a href="https://www.bricklink.com">www.bricklink.com</a> is owned by BrickLink. Both BrickLink and LEGO are trademarks of the LEGO group, which does not sponsor, authorize or endorse this software. All other trademarks recognized.</p><p>Only made possible by <a href="https://www.danjezek.com/">Dan Jezek's</a> support.</p>)");
    license = "<br><b>"_l1 % tr("License") % R"(</b><div style="margin-left: 10px">)"_l1
            % license % "</div>"_l1;

    QString translators;
    const QString transRow = R"(<tr><td>%1</td><td width="2em">&nbsp;</td><td>%2 <a href="mailto:%3">%4</a></td></tr>)"_l1;
    const auto translations = Config::inst()->translations();
    for (const Config::Translation &trans : translations) {
        if (trans.language != "en"_l1 && !trans.author.isEmpty()) {
            QString langname = trans.localName % " ("_l1 % trans.name % ")"_l1;
            translators = translators % transRow.arg(langname, trans.author, trans.authorEmail, trans.authorEmail);
        }
    }
    translators = "<br><b>"_l1 % tr("Translators") % R"(</b><div style="margin-left: 10px">)"_l1
            % R"(<p><table border="0">)"_l1 % translators % R"(</p></table>)"_l1 % "</div>"_l1;

    return QVariantMap {
        { "header"_l1, header },
        { "license"_l1, license },
        { "translators"_l1, translators },
    };
}


void Application::setupSentry()
{
#if defined(SENTRY_ENABLED)
    auto *sentry = sentry_options_new();
    sentry_options_set_debug(sentry, 1);
    sentry_options_set_logger(sentry, [](sentry_level_t level, const char *message, va_list args, void *) {
        QDebug dbg(static_cast<QString *>(nullptr));
        switch (level) {
        default:
        case SENTRY_LEVEL_DEBUG:   dbg = QMessageLogger().debug(LogSentry); break;
        case SENTRY_LEVEL_INFO:    dbg = QMessageLogger().info(LogSentry); break;
        case SENTRY_LEVEL_WARNING: dbg = QMessageLogger().warning(LogSentry); break;
        case SENTRY_LEVEL_ERROR:
        case SENTRY_LEVEL_FATAL:   dbg = QMessageLogger().critical(LogSentry); break;
        }
        dbg.noquote() << QString::vasprintf(QByteArray(message).replace("%S", "%ls").constData(), args);
    }, nullptr);
    sentry_options_set_dsn(sentry, "https://335761d80c3042548349ce5e25e12a06@o553736.ingest.sentry.io/5681421");
    sentry_options_set_release(sentry, "brickstore@" BRICKSTORE_BUILD_NUMBER);
    sentry_options_set_require_user_consent(sentry, 1);

    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) % "/.sentry"_l1;
    QString crashHandler = QCoreApplication::applicationDirPath() % "/crashpad_handler"_l1;

#  if defined(Q_OS_WINDOWS)
    crashHandler.append(".exe"_l1);
    sentry_options_set_handler_pathw(sentry, reinterpret_cast<const wchar_t *>(crashHandler.utf16()));
    sentry_options_set_database_pathw(sentry, reinterpret_cast<const wchar_t *>(dbPath.utf16()));
#  else
    sentry_options_set_handler_path(sentry, crashHandler.toLocal8Bit().constData());
    sentry_options_set_database_path(sentry, dbPath.toLocal8Bit().constData());
#  endif
    if (sentry_init(sentry))
        qCWarning(LogSentry) << "Could not initialize sentry.io!";
    else
        qCInfo(LogSentry) << "Successfully initialized sentry.io";

    connect(SystemInfo::inst(), &SystemInfo::initialized, []() {
        auto sysInfo = SystemInfo::inst()->asMap();
        for (auto it = sysInfo.cbegin(); it != sysInfo.cend(); ++it) {
            if (!it.key().startsWith("os."_l1))
                sentry_set_tag(it.key().toUtf8().constData(), it.value().toString().toUtf8().constData());
        }
        sentry_set_tag("language", Config::inst()->language().toUtf8().constData());
    });
#endif
}

void Application::shutdownSentry()
{
#if defined(SENTRY_ENABLED)
    sentry_shutdown();
#endif
}

void Application::checkSentryConsent()
{
#if defined(SENTRY_ENABLED)
    auto check = []() {
        switch (Config::inst()->sentryConsent()) {
        case Config::SentryConsent::Unknown:
            sentry_user_consent_reset();
            break;
        case Config::SentryConsent::Given:
            sentry_user_consent_give();
            break;
        case Config::SentryConsent::Revoked:
            sentry_user_consent_revoke();
            break;
        }
    };
    connect(Config::inst(), &Config::sentryConsentChanged, check);
    check();
#endif
}

void Application::addSentryBreadcrumb(QtMsgType msgType, const QMessageLogContext &msgCtx, const QString &msg)
{
#if defined(SENTRY_ENABLED)
    sentry_value_t crumb = sentry_value_new_breadcrumb("default", msg.toUtf8());
    if (msgCtx.category)
        sentry_value_set_by_key(crumb, "category", sentry_value_new_string(msgCtx.category));
    msgType = qBound(QtDebugMsg, msgType, QtInfoMsg);
    static const char *msgTypeLevels[5] = { "debug", "warning", "error", "fatal", "info" };
    sentry_value_set_by_key(crumb, "level", sentry_value_new_string(msgTypeLevels[msgType]));
    const auto now = QDateTime::currentSecsSinceEpoch();
    sentry_value_set_by_key(crumb, "timestamp", sentry_value_new_int32(int32_t(now)));
    sentry_add_breadcrumb(crumb);
#else
    Q_UNUSED(msgType)
    Q_UNUSED(msgCtx)
    Q_UNUSED(msg)
#endif
}

void Application::setupLogging()
{
    qSetMessagePattern("%{if-category}%{category}: %{endif}%{message} (at %{file}, %{line})"_l1);
//    qSetMessagePattern("%{if-category}%{category}: %{endif}%{message} (at %{file}, %{line})\n%{backtrace}\n"_l1);

    auto messageHandler = [](QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
    {
        if (s_inst && s_inst->m_defaultMessageHandler)
            (*s_inst->m_defaultMessageHandler)(type, ctx, msg);

        addSentryBreadcrumb(type, ctx, msg);

        if (s_inst && s_inst->m_uiMessageHandler)
            s_inst->m_uiMessageHandler(type, ctx, msg);
    };
    m_defaultMessageHandler = qInstallMessageHandler(messageHandler);
}

void Application::setUILoggingHandler(QtMessageHandler callback)
{
    m_uiMessageHandler = callback;
}

void Application::setIconTheme(Theme theme)
{
    QPixmapCache::clear();
    QIcon::setThemeName(theme == DarkTheme ? "brickstore-breeze-dark"_l1 : "brickstore-breeze"_l1);
}


bool Application::initBrickLink(QString *errString)
{
    BrickLink::Core *bl = BrickLink::create(Config::inst()->brickLinkCacheDir(), errString);
    if (bl) {
        bl->setItemImageScaleFactor(Config::inst()->itemImageSizePercent() / 100.);
        connect(Config::inst(), &Config::itemImageSizePercentChanged,
                this, [](qreal p) { BrickLink::core()->setItemImageScaleFactor(p / 100.); });

        connect(Config::inst(), &Config::updateIntervalsChanged,
                BrickLink::core(), &BrickLink::Core::setUpdateIntervals);
        BrickLink::core()->setUpdateIntervals(Config::inst()->updateIntervals());
        BrickLink::core()->setCredentials({ Config::inst()->brickLinkUsername(),
                                            Config::inst()->brickLinkPassword() });
        connect(Config::inst(), &Config::brickLinkCredentialsChanged,
                this, []() {
            BrickLink::core()->setCredentials({ Config::inst()->brickLinkUsername(),
                                                Config::inst()->brickLinkPassword() });
        });
        BrickLink::core()->readDatabase();
    }
    return bl;
}

void Application::exitBrickLink()
{
    delete BrickLink::core();
    delete LDraw::core();
}

#include "moc_application.cpp"
