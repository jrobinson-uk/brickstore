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
#pragma once

#include <QSettings>
#include <QDateTime>
#include <QLocale>
#include <QVector>


class Config : public QSettings
{
    Q_OBJECT
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QLocale::MeasurementSystem measurementSystem READ measurementSystem WRITE setMeasurementSystem NOTIFY measurementSystemChanged)
    Q_PROPERTY(QString defaultCurrencyCode READ defaultCurrencyCode WRITE setDefaultCurrencyCode NOTIFY defaultCurrencyCodeChanged)
    Q_PROPERTY(QString documentDir READ documentDir WRITE setDocumentDir NOTIFY documentDirChanged)
    Q_PROPERTY(Config::PartOutMode partOutMode READ partOutMode WRITE setPartOutMode NOTIFY partOutModeChanged)
    Q_PROPERTY(bool openBrowserOnExport READ openBrowserOnExport WRITE setOpenBrowserOnExport NOTIFY openBrowserOnExportChanged)
    Q_PROPERTY(bool restoreLastSession READ restoreLastSession WRITE setRestoreLastSession NOTIFY restoreLastSessionChanged)
    Q_PROPERTY(bool showInputErrors READ showInputErrors WRITE setShowInputErrors NOTIFY showInputErrorsChanged)
    Q_PROPERTY(bool showDifferenceIndicators READ showDifferenceIndicators WRITE setShowDifferenceIndicators NOTIFY showDifferenceIndicatorsChanged)
    Q_PROPERTY(QStringList recentFiles READ recentFiles WRITE setRecentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QString brickLinkUsername READ brickLinkUsername WRITE setBrickLinkUsername NOTIFY brickLinkCredentialsChanged)
    Q_PROPERTY(QString brickLinkPassword READ brickLinkPassword WRITE setBrickLinkPassword NOTIFY brickLinkCredentialsChanged)
    Q_PROPERTY(Config::UiTheme uiTheme READ uiTheme WRITE setUiTheme NOTIFY uiThemeChanged)

private:
    Config();
    static Config *s_inst;

    static QString scramble(const QString &);

public:
    ~Config() override;
    static Config *inst();

    void upgrade(int vmajor, int vminor, int vpatch);

    QVariantList availableLanguages() const;
    QString language() const;
    QLocale::MeasurementSystem measurementSystem() const;

    bool areFiltersInFavoritesMode() const;

    QPair<QString, double> legacyCurrencyCodeAndRate() const;
    QString defaultCurrencyCode() const;

    QString documentDir() const;
    QString ldrawDir() const;
    QString brickLinkCacheDir() const;

    bool showInputErrors() const;
    bool showDifferenceIndicators() const;
    bool onlineStatus() const;

    enum class PartOutMode {
        Ask,
        InPlace,
        NewDocument
    };
    Q_ENUM(PartOutMode)

    PartOutMode partOutMode() const;
    void setPartOutMode(PartOutMode pom);

    bool visualChangesMarkModified() const;
    void setVisualChangesMarkModified(bool b);

    bool restoreLastSession() const;
    void setRestoreLastSession(bool b);

    bool openBrowserOnExport() const;
    void setOpenBrowserOnExport(bool b);

    QStringList recentFiles() const;

    static constexpr int MaxFilterHistory = 20;

    QString brickLinkUsername() const;
    QString brickLinkPassword() const;
    QMap<QByteArray, int> updateIntervals() const;
    QMap<QByteArray, int> updateIntervalsDefault() const;

    struct Translation {
        QString language;
        QString name;
        QString localName;
        QString author;
        QString authorEmail;
    };

    QVector<Translation> translations() const;

    enum class IconSize {
        System,
        Small,
        Large,
    };

    IconSize iconSize() const;
    void setIconSize(IconSize iconSize);

    int fontSizePercent() const;
    int itemImageSizePercent() const;

    QByteArray columnLayout(const QString &id) const;
    QString columnLayoutName(const QString &id) const;
    int columnLayoutOrder(const QString &id) const;
    QStringList columnLayoutIds() const;

    QVariantMap shortcuts() const;
    void setShortcuts(const QVariantMap &list);

    QStringList toolBarActions() const;
    void setToolBarActions(const QStringList &actions);

    QString lastDirectory() const;
    void setLastDirectory(const QString &dir);

    enum class SentryConsent {
        Unknown,
        Given,
        Revoked,
    };
    SentryConsent sentryConsent() const;
    void setSentryConsent(SentryConsent consent);

    enum class UiTheme {
        SystemDefault,
        Light,
        Dark
    };
    Q_ENUM(UiTheme)

    UiTheme uiTheme() const;
    void setUiTheme(UiTheme theme);

public slots:
    void setLanguage(const QString &lang);
    void setMeasurementSystem(QLocale::MeasurementSystem ms);
    void setFiltersInFavoritesMode(bool b);
    void setDefaultCurrencyCode(const QString &ccode);

    void setDocumentDir(const QString &dir);
    void setLDrawDir(const QString &dir);

    void setShowInputErrors(bool b);
    void setShowDifferenceIndicators(bool b);
    void setOnlineStatus(bool b);

    void setRecentFiles(const QStringList &recent);

    void setBrickLinkUsername(const QString &user);
    void setBrickLinkPassword(const QString &pass, bool doNotSave = false);
    void setUpdateIntervals(const QMap<QByteArray, int> &intervals);

    void setFontSizePercent(int p);
    void setItemImageSizePercent(int p);

    QString setColumnLayout(const QString &id, const QByteArray &layout);
    bool deleteColumnLayout(const QString &id);
    bool renameColumnLayout(const QString &id, const QString &name);
    bool reorderColumnLayouts(const QStringList &ids);

signals:
    void languageChanged();
    void measurementSystemChanged(QLocale::MeasurementSystem ms);
    void documentDirChanged(const QString &dir);
    void restoreLastSessionChanged(bool b);
    void partOutModeChanged(Config::PartOutMode partMode);
    void filtersInFavoritesModeChanged(bool favoritesMode);
    void defaultCurrencyCodeChanged(const QString &ccode);
    void openBrowserOnExportChanged(bool b);
    void showInputErrorsChanged(bool b);
    void showDifferenceIndicatorsChanged(bool b);
    void visualChangesMarkModifiedChanged(bool b);
    void updateIntervalsChanged(const QMap<QByteArray, int> &intervals);
    void onlineStatusChanged(bool b);
    void recentFilesChanged(const QStringList &recent);
    void iconSizeChanged(Config::IconSize iconSize);
    void fontSizePercentChanged(int p);
    void itemImageSizePercentChanged(int p);
    void columnLayoutChanged(const QString &id, const QByteArray &layout);
    void columnLayoutNameChanged(const QString &id, const QString &name);
    void columnLayoutIdsOrderChanged(const QStringList &ids);
    void columnLayoutIdsChanged(const QStringList &ids);
    void shortcutsChanged(const QVariantMap &list);
    void sentryConsentChanged(Config::SentryConsent consent);
    void toolBarActionsChanged(const QStringList &actions);
    void uiThemeChanged(Config::UiTheme theme);
    void brickLinkCredentialsChanged();

protected:
    bool parseTranslations() const;

private:
    bool                       m_show_input_errors = false;
    bool                       m_show_difference_indicators = false;
    QLocale::MeasurementSystem m_measurement = QLocale::MetricSystem;
    mutable bool               m_translations_parsed = false;
    mutable QVector<Translation> m_translations;
    QString                    m_lastDirectory;
    mutable QString            m_bricklinkUsername;
    mutable QString            m_bricklinkPassword;
};

Q_DECLARE_METATYPE(Config *)
