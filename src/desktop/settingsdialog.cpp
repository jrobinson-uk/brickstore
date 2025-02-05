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
#include <QTabWidget>
#include <QFileDialog>
#include <QComboBox>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QImage>
#include <QSortFilterProxyModel>
#include <QMenu>
#include <QStringBuilder>
#include <QMimeData>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMenuBar>

#if defined(MODELTEST)
#  include <QAbstractItemModelTester>
#  define MODELTEST_ATTACH(x)   { (void) new QAbstractItemModelTester(x, QAbstractItemModelTester::FailureReportingMode::Warning, x); }
#else
#  define MODELTEST_ATTACH(x)   ;
#endif

#include "bricklink/core.h"
#include "bricklink/model.h"
#include "common/actionmanager.h"
#include "common/config.h"
#include "ldraw/ldraw.h"
#include "utility/currency.h"
#include "utility/utility.h"
#include "betteritemdelegate.h"
#include "mainwindow.h"
#include "settingsdialog.h"


class ActionModel : public QAbstractItemModel
{
    Q_OBJECT
public:    
    enum Option {
        NoOptions = 0x00,
        AddSubMenus = 0x01,
        AddSeparators = 0x02,
        RemoveGoHome = 0x04,
    };
    Q_DECLARE_FLAGS(Options, Option)

    ActionModel(Options options, QObject *parent = nullptr)
        : QAbstractItemModel(parent)
    {
        MODELTEST_ATTACH(this);

        auto all = ActionManager::inst()->allActions();
        for (const auto &aa : all) {
            if (auto *qa = aa->qAction()) {
                if (options.testFlag(RemoveGoHome) && (aa->name() == QByteArray("go_home")))
                    continue;

                QString mtitle = rootMenu(qa);
                if (mtitle.isEmpty())
                    mtitle = tr("Other");
                m_actions.insert(mtitle, qa->objectName());
            }
        }

        static const std::vector<std::pair<QString, const char *>> separators = {
            { "-"_l1,  QT_TR_NOOP("Bar Separator") },
            { "|"_l1,  QT_TR_NOOP("Space Separator") },
            { "<>"_l1, QT_TR_NOOP("Flexible Space Separator") },
        };
        for (const auto &sep : separators) {
            QAction *a = new QAction(this);
            a->setObjectName(sep.first);
            a->setText(tr(sep.second));
            m_separatorActions.insert(sep.first, a);
        }

        if (options.testFlag(AddSeparators)) {
            for (const QAction *a : qAsConst(m_separatorActions))
                m_actions.insert(tr("Separators"), a->objectName());
        }
        if (!options.testFlag(AddSubMenus)) {
            for (auto it = m_actions.begin(); it != m_actions.end(); ) {
                if (QAction *a = ActionManager::inst()->qAction(it->toLatin1().constData())) {
                    if (a->menu()) {
                        it = m_actions.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }
    }

    static QString rootMenu(const QAction *action)
    {
        QList<QWidget *> widgets = action->associatedWidgets();
        for (QWidget *w : qAsConst(widgets)) {
            if (auto m = qobject_cast<QMenu *>(w))
                return rootMenu(m->menuAction());
            else if (qobject_cast<QMenuBar *>(w))
                return action->text();
        }
        return { };
    }

    QModelIndex index(int row, int column, const QModelIndex &parent) const override
    {
        if (hasIndex(row, column, parent)) {
            quint32 id;
            if (!parent.isValid())
                id = row << 16 | 0x0000ffffu;
            else
                id = (parent.row() << 16) | (row & 0x0000ffff);
            return createIndex(row, column, id);
        }
        return {};
    }

    QModelIndex parent(const QModelIndex &idx) const override
    {
        if (!idx.isValid() || isCategory(idx))
            return { };
        else
            return createIndex(categoryIndex(idx), 0, index(categoryIndex(idx), 0, { }).internalId());
    }

    int rowCount(const QModelIndex &parent = { }) const override
    {
        if (parent.column() > 0)
            return 0;
        if (!parent.isValid()) {
            return m_actions.uniqueKeys().size();
        } else {
            if (isCategory(parent))
                return m_actions.values(m_actions.uniqueKeys().at(parent.row())).size();
            else
                return 0;
        }
    }

    int columnCount(const QModelIndex &parent = { }) const override
    {
        Q_UNUSED(parent);
        return 1;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags flags = QAbstractItemModel::flags(index);
        if (index.isValid() && !isCategory(index))
            flags |= Qt::ItemIsDragEnabled;
        else
            flags &= ~Qt::ItemIsSelectable;
        return flags;
    }

    Qt::DropActions supportedDragActions() const override
    {
        return Qt::CopyAction;
    }

    QStringList mimeTypes() const override
    {
        return { "application/x-brickstore-toolbar-dnd"_l1 };
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        if (indexes.size() != 1)
            return nullptr;

        QMimeData *mimeData = new QMimeData();
        QByteArray encodedData;
        QDataStream ds(&encodedData, QIODevice::WriteOnly);

        ds << quintptr(this);

        foreach (const QModelIndex &index, indexes) {
            auto s = actionAt(index);
            if (!s.isEmpty())
                ds << index.row() << index.column() << s;
        }

        mimeData->setData(mimeTypes().constFirst(), encodedData);
        return mimeData;
    }

    QVariant data(const QModelIndex &idx, int role) const override
    {
        if (!idx.isValid() || (idx.column() >= columnCount())) {
            return { };
        }

        static auto removeMnemonic = [](const QString &s) {
            QString s2(s);
            s2.remove(QRegularExpression(R"(\&(?!\&))"_l1));
            return s2;
        };

        if (isCategory(idx)) {
            if ((idx.column() == 0) && (role == Qt::DisplayRole)) {
                QString s = m_actions.uniqueKeys().at(categoryIndex(idx));
                return removeMnemonic(s);
            } else if (role == Qt::FontRole) {
                auto fnt = qApp->font();
                fnt.setBold(true);
                return fnt;
            }
            return { };
        }

        const QAction *action = nullptr;
        auto name = actionAt(idx);
        if (m_separatorActions.contains(name))
            action = m_separatorActions.value(name);
        else
            action = ActionManager::inst()->qAction(name.toLatin1().constData());
        if (!action)
            return { };

        switch (role) {
        case Qt::ToolTipRole:
        case Qt::DisplayRole: {
            std::function<QStringList(const QAction *)> buildPath = [&buildPath](const QAction *a) {
                QStringList path;
                if (a && !a->text().isEmpty()) {
                    path.prepend(removeMnemonic(a->text()));

                    const QList<QWidget *> widgets = a->associatedWidgets();
                    for (auto w : widgets) {
                        if (auto *m = qobject_cast<QMenu *>(w))
                            path = buildPath(m->menuAction()) + path;
                    }
                }
                return path;
            };
            QString s = m_textCache.value(action);
            if (s.isEmpty()) {
                auto sl = buildPath(action);
                s = (sl.size() == 1) ? sl.constFirst() : sl.mid(1).join(" / "_l1);
                m_textCache.insert(action, s);
            }
            return s;
        }
        case Qt::DecorationRole: {
            QIcon ico = action->icon();
            // returning the icon directly will create scaling artefacts
            if (!ico.isNull()) {
                return QIcon(ico.pixmap(64, QIcon::Normal, QIcon::On));
            } else {
                QPixmap pix(64, 64);
                pix.fill(Qt::transparent);
                return QIcon(pix);
            }
        }
        }
        return { };
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if ((section < 0) || (section >= columnCount()) || (orientation != Qt::Horizontal))
            return { };
        if (role == Qt::DisplayRole)
            return tr("Action name");
        return { };
    }

protected:
    bool isCategory(const QModelIndex &idx) const
    {
        return (idx.internalId() & 0x0000ffffu) == 0x0000ffffu;
    }
    int categoryIndex(const QModelIndex &idx) const
    {
        return idx.internalId() >> 16;
    }
    QString actionAt(const QModelIndex &idx) const
    {
        if (!idx.isValid() || isCategory(idx))
            return { };
        return m_actions.values(m_actions.uniqueKeys().at(categoryIndex(idx)))
                .at(int(idx.internalId() & 0x0000ffffu));
    }

    QMultiMap<QString, QString> m_actions;

protected:
    QMap<QString, QAction *> m_separatorActions;
private:
    mutable QHash<const QAction *, QString> m_textCache;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ActionModel::Options)


class ToolBarModel : public ActionModel
{
    Q_OBJECT
public:
    ToolBarModel(const QStringList &actions, QObject *parent)
        : ActionModel(NoOptions, parent)
    {
        m_actions.clear();
        m_category = tr("Toolbar");
        for (auto it = actions.crbegin(); it != actions.crend(); ++it)
            m_actions.insert(m_category, *it);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags defaultFlags = ActionModel::flags(index);

        if (index.isValid())
            return Qt::ItemIsDragEnabled | defaultFlags;
        else
            return Qt::ItemIsDropEnabled | defaultFlags;
    }

    int rowCount(const QModelIndex &parent = { }) const override
    {
        return parent.isValid() ? 0 : m_actions.size();
    }

    QModelIndex index(int row, int column, const QModelIndex &parent) const override
    {
        if (hasIndex(row, column, parent))
            return createIndex(row, column, row);
        return {};
    }

    QModelIndex parent(const QModelIndex &) const override
    {
        return { };
    }

    Qt::DropActions supportedDragActions() const override
    {
        return Qt::MoveAction;
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::CopyAction | Qt::MoveAction;
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override
    {
        if (!canDropMimeData(data, action, row, column, parent))
            return false;
        if (action == Qt::IgnoreAction)
            return true;

        if (row < 0)
            return false;

        QByteArray encodedData = data->data(mimeTypes().constFirst());
        QDataStream ds(&encodedData, QIODevice::ReadOnly);
        QString newAction;

        quintptr source;
        int sourceRow;
        int sourceColumn;
        ds >> source >> sourceRow >> sourceColumn >> newAction;

        if (!newAction.isEmpty()) {
            if (source == quintptr(this)) { // internal move
                if ((row == sourceRow) || (row == (sourceRow + 1))) // no-op move
                    return false;

                beginMoveRows({ }, sourceRow, sourceRow, { }, row);
                QStringList actions = m_actions.values(m_category);
                actions.move(sourceRow, row < sourceRow ? row : row - 1);
                m_actions.remove(m_category);
                for (auto it = actions.crbegin(); it != actions.crend(); ++it)
                    m_actions.insert(m_category, *it);
                endMoveRows();
                return false; // actually true, but QAIV would remove the sourceRow
            } else {
                QStringList actions = m_actions.values(m_category);

                // no duplicated allowed
                if (!m_separatorActions.contains(newAction) && actions.contains(newAction)) {
                    int removeRow = actions.indexOf(newAction);

                    if ((row == removeRow) || (row == (removeRow + 1))) // no-op
                        return false;

                    beginRemoveRows({ }, removeRow, removeRow);
                    actions.removeAt(removeRow);
                    endRemoveRows();

                    if (row > removeRow)
                        --row;
                }

                beginInsertRows({ }, row, row);
                actions.insert(row, newAction);
                m_actions.remove(m_category);
                for (auto it = actions.crbegin(); it != actions.crend(); ++it)
                    m_actions.insert(m_category, *it);
                endInsertRows();
                return true;
            }
        }
        return false;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if ((section < 0) || (section >= columnCount()) || (orientation != Qt::Horizontal))
            return { };
        if (role == Qt::DisplayRole)
            return tr("Toolbar");
        return { };
    }

    void removeActionAt(int row)
    {
        if (row < 0 || row >= rowCount())
            return;

        QStringList actions = m_actions.values(m_category);

        beginRemoveRows({ }, row, row);

        actions.removeAt(row);
        m_actions.remove(m_category);
        for (auto it = actions.crbegin(); it != actions.crend(); ++it)
            m_actions.insert(m_category, *it);

        endRemoveRows();
    }

    void resetToDefaults()
    {
        auto actions = MainWindow::inst()->defaultToolBarActionNames();
        beginResetModel();
        m_actions.remove(m_category);
        for (auto it = actions.crbegin(); it != actions.crend(); ++it)
            m_actions.insert(m_category, *it);
        endResetModel();
    }

    QStringList actionList() const
    {
        return m_actions.values(m_category);
    }

private:
    QString m_category;
};

class ShortcutModel : public ActionModel
{
    Q_OBJECT
public:
    ShortcutModel(QObject *parent)
        : ActionModel(NoOptions, parent)
    {
        const auto list = m_actions.values();
        for (const QString &actionName : list) {
            if (const auto *aa = ActionManager::inst()->action(actionName.toLatin1().constData())) {
                Entry entry = { aa->defaultShortcuts(), aa->shortcuts() };
                m_entries.insert(actionName, entry);
            }
        }
    }

    void apply()
    {
        QMap<QByteArray, QKeySequence> customShortcuts;
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if ((it->keys != it->defaultKeys) && !it->keys.isEmpty())
                customShortcuts.insert(it.key().toLatin1(), it->keys.at(0));
        }
        ActionManager::inst()->setCustomShortcuts(customShortcuts);
        ActionManager::inst()->saveCustomShortcuts();
    }

    int columnCount(const QModelIndex &parent = { }) const override
    {
        Q_UNUSED(parent)
        return ActionModel::columnCount() * 3;
    }

    QVariant data(const QModelIndex &idx, int role) const override
    {
        if (idx.column() == 0)
            return ActionModel::data(idx, role);

        if (!idx.isValid() || (idx.column() >= columnCount())) {
            return { };
        }
        const Entry &entry = entryAt(idx);

        switch (role) {
        case Qt::DisplayRole:
        case Qt::ToolTipRole:
            switch (idx.column()) {
            case 1: return QKeySequence::listToString(entry.keys, QKeySequence::NativeText);
            case 2: return QKeySequence::listToString(entry.defaultKeys, QKeySequence::NativeText);
            }
            break;
        }
        return { };
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if ((section < 0) || (section >= columnCount()) || (orientation != Qt::Horizontal))
            return { };
        if (role == Qt::DisplayRole) {
            switch (section) {
            case 0: return tr("Action name");
            case 1: return tr("Current");
            case 2: return tr("Default");
            }
        }
        return { };
    }

    const QList<QKeySequence> shortcuts(const QModelIndex &idx) const
    {
        return entryAt(idx).keys;
    }

    const QList<QKeySequence> defaultShortcuts(const QModelIndex &idx) const
    {
        return entryAt(idx).defaultKeys;
    }

    bool setShortcut(const QModelIndex &idx, const QList<QKeySequence> &newShortcuts)
    {
        if (idx.isValid()) {
            Entry &entry = entryAt(idx);

            if (entry.keys == newShortcuts)
                return true; // no change

            if (!newShortcuts.isEmpty()) {
                for (const Entry &entry : qAsConst(m_entries)) {
                    if (entry.keys == newShortcuts)
                        return false; // duplicate
                }
            }

            entry.keys = newShortcuts;
            auto cidx = idx.siblingAtColumn(1);
            emit dataChanged(cidx, cidx);
            return true;
        }
        return false;
    }

    bool resetShortcut(const QModelIndex &idx)
    {
        return setShortcut(idx, entryAt(idx).defaultKeys);
    }

    void resetAllShortcuts()
    {
        beginResetModel();
        for (auto &entry : m_entries)
            entry.keys = entry.defaultKeys;
        endResetModel();
    }

private:
    struct Entry {
        QList<QKeySequence> defaultKeys;
        QList<QKeySequence> keys;
    };

    Entry &entryAt(const QModelIndex &idx)
    {
        static Entry dummy;
        if (idx.isValid() && !isCategory(idx)) {
            QString s = actionAt(idx);
            if (m_entries.contains(s))
                return m_entries[s];
        }
        return dummy;
    }

    const Entry &entryAt(const QModelIndex &idx) const
    {
        return const_cast<ShortcutModel *>(this)->entryAt(idx);
    }


    QHash<QString, Entry> m_entries;
};








class ToolBarDelegate : public BetterItemDelegate
{
public:
    ToolBarDelegate(Options options, QObject *parent);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

private:
    QIcon m_deleteIcon;
};

ToolBarDelegate::ToolBarDelegate(Options options, QObject *parent)
    : BetterItemDelegate(options, parent)
    , m_deleteIcon(QIcon::fromTheme("window-close"_l1))
{ }

void ToolBarDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    BetterItemDelegate::paint(painter, option, index);

    if (option.state & QStyle::State_MouseOver) {
        QRect r = option.rect;
        r.setLeft(r.left() + r.width() - r.height());
        const int margin = 2;
        r.adjust(margin, margin, -margin, -margin);
        m_deleteIcon.paint(painter, r);
    }
}

bool ToolBarDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                  const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event && (event->type() == QEvent::MouseButtonPress) && index.isValid()) {
        auto me = static_cast<QMouseEvent *>(event);
        int d = option.rect.height();

        if (me->pos().x() > (option.rect.right() - d)) {
            if (auto tbmodel = qobject_cast<ToolBarModel *>(model)) {
                tbmodel->removeActionAt(index.row());
                return true;
            }
        }
    }
    return BetterItemDelegate::editorEvent(event, model, option, index);
}













static int sec2day(int s)
{
    return (s + 60*60*24 - 1) / (60*60*24);
}

static int day2sec(int d)
{
    return d * (60*60*24);
}

static QString systemDirName(const QString &path)
{
    static const QStandardPaths::StandardLocation locations[] = {
        QStandardPaths::DesktopLocation,
        QStandardPaths::DocumentsLocation,
        QStandardPaths::MusicLocation,
        QStandardPaths::MoviesLocation,
        QStandardPaths::PicturesLocation,
        QStandardPaths::TempLocation,
        QStandardPaths::HomeLocation,
        QStandardPaths::AppLocalDataLocation,
        QStandardPaths::CacheLocation
    };

    for (auto &location : locations) {
        if (QDir(QStandardPaths::writableLocation(location)) == QDir(path)) {
            QString name = QStandardPaths::displayName(location);
            if (!name.isEmpty())
                return name;
            else
                break;
        }
    }
    return path;
}


SettingsDialog::SettingsDialog(const QString &start_on_page, QWidget *parent)
    : QDialog(parent)
{
    setupUi(this);

    w_font_size_percent->setFixedWidth(w_font_size_percent->width());
    w_item_image_size_percent->setFixedWidth(w_item_image_size_percent->width());

    auto setFontSize = [this](int v) {
        w_font_size_percent->setText(QString::number(v * 10) % u" %");
        QFont f = font();
        qreal defaultFontSize = qApp->property("_bs_defaultFontSize").toReal();
        if (defaultFontSize <= 0)
            defaultFontSize = 10;
        f.setPointSizeF(defaultFontSize * qreal(v) / 10.);
        w_font_example->setFont(f);
    };

    connect(w_font_size, &QAbstractSlider::valueChanged,
            this, setFontSize);
    setFontSize(10);

    connect(w_font_size_reset, &QToolButton::clicked,
            this, [this]() { w_font_size->setValue(10); });

    auto setImageSize = [this](int v) {
        w_item_image_size_percent->setText(QString::number(v * 10) % u" %");
        int s = int(BrickLink::core()->standardPictureSize().height() * v
                    / 10 / BrickLink::core()->itemImageScaleFactor());
        QImage img(":/assets/generated-app-icons/brickstore.png"_l1);
        img = img.scaled(s, s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        w_item_image_example->setPixmap(QPixmap::fromImage(img));
    };

    connect(w_item_image_size, &QAbstractSlider::valueChanged,
            this, setImageSize);
    setImageSize(10);

    connect(w_item_image_size_reset, &QToolButton::clicked,
            this, [this]() { w_item_image_size->setValue(10); });

    w_upd_reset->setAttribute(Qt::WA_MacSmallSize);
    w_modifications_label->setAttribute(Qt::WA_MacSmallSize);

    w_docdir->insertItem(0, style()->standardIcon(QStyle::SP_DirIcon), QString());
    w_docdir->insertSeparator(1);
    w_docdir->insertItem(2, QIcon(), tr("Other..."));

    int is = fontMetrics().height();
    w_currency_update->setIconSize(QSize(is, is));

    w_ldraw_dir->insertItem(0, style()->standardIcon(QStyle::SP_DirIcon), QString());
    w_ldraw_dir->insertSeparator(1);
    w_ldraw_dir->insertItem(2, QIcon(), tr("Auto Detect"));
    w_ldraw_dir->insertItem(3, QIcon(), tr("Other..."));

    connect(w_ldraw_dir, QOverload<int>::of(&QComboBox::activated),
            this, &SettingsDialog::selectLDrawDir);
    connect(w_docdir, QOverload<int>::of(&QComboBox::activated),
            this, &SettingsDialog::selectDocDir);
    connect(w_upd_reset, &QAbstractButton::clicked,
            this, &SettingsDialog::resetUpdateIntervals);
    connect(w_currency, &QComboBox::currentTextChanged,
            this, &SettingsDialog::currentCurrencyChanged);
    connect(w_currency_update, &QAbstractButton::clicked,
            Currency::inst(), &Currency::updateRates);
    connect(Currency::inst(), &Currency::ratesChanged,
            this, &SettingsDialog::currenciesUpdated);

    w_bl_username_note->hide();
    connect(w_bl_username, &QLineEdit::textChanged,
            this, [this](const QString &s) {
        bool b = s.contains(QLatin1Char('@'));
        if (w_bl_username_note->isVisible() != b) {
            w_bl_username_note->setVisible(b);
            w_bl_username->setProperty("showInputError", b);
        }
    });
    w_bl_password_note->hide();
    connect(w_bl_password, &QLineEdit::textChanged,
            this, [this](const QString &s) {
        bool b = (s.length() > 15);
        if (w_bl_password_note->isVisible() != b) {
            w_bl_password_note->setVisible(b);
            w_bl_password->setProperty("showInputError", b);
        }
    });

    m_tb_model = new ActionModel(ActionModel::AddSeparators | ActionModel::AddSubMenus
                                 | ActionModel::RemoveGoHome, this);
    m_tb_proxymodel = new QSortFilterProxyModel(this);
    m_tb_proxymodel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_tb_proxymodel->setRecursiveFilteringEnabled(true);
    m_tb_proxymodel->setFilterKeyColumn(-1);
    m_tb_proxymodel->setSourceModel(m_tb_model);
    w_tb_actions->setModel(m_tb_proxymodel);
    w_tb_actions->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    w_tb_actions->setItemDelegate(new BetterItemDelegate(BetterItemDelegate::AlwaysShowSelection, this));
    w_tb_actions->expandAll();
    w_tb_actions->sortByColumn(0, Qt::AscendingOrder);
    w_tb_filter->addAction(QIcon::fromTheme("view-filter"_l1), QLineEdit::LeadingPosition);

    auto tbActionNames = MainWindow::inst()->toolBarActionNames();

    m_tb_actions = new ToolBarModel(tbActionNames, this);
    w_tb_toolbar->setModel(m_tb_actions);
    w_tb_toolbar->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    w_tb_toolbar->setItemDelegate(new ToolBarDelegate(BetterItemDelegate::AlwaysShowSelection, this));

    connect(w_tb_reset, &QPushButton::clicked, this, [this]() {
        m_tb_actions->resetToDefaults();
        w_tb_filter->clear();
        w_tb_actions->sortByColumn(0, Qt::AscendingOrder);
    });
    connect(w_tb_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_tb_proxymodel->setFilterFixedString(text);
        w_tb_actions->expandAll();
    });

    m_sc_model = new ShortcutModel(this);
    m_sc_proxymodel = new QSortFilterProxyModel(this);
    m_sc_proxymodel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_sc_proxymodel->setFilterKeyColumn(-1);
    m_sc_proxymodel->setRecursiveFilteringEnabled(true);
    m_sc_proxymodel->setSourceModel(m_sc_model);
    w_sc_list->setModel(m_sc_proxymodel);
    w_sc_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    w_sc_list->setItemDelegate(new BetterItemDelegate(BetterItemDelegate::AlwaysShowSelection, this));
    w_sc_list->expandAll();
    w_sc_list->sortByColumn(0, Qt::AscendingOrder);
    w_sc_filter->addAction(QIcon::fromTheme("view-filter"_l1), QLineEdit::LeadingPosition);

    connect(w_sc_list->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
        QModelIndexList idxs = w_sc_list->selectionModel()->selectedRows();
        QModelIndex idx;
        if (!idxs.isEmpty())
            idx = m_sc_proxymodel->mapToSource(idxs.constFirst());

        w_sc_edit->setEnabled(idx.isValid());
        w_sc_reset->setEnabled(idx.isValid());
        if (idx.isValid()) {
            auto shortcuts = m_sc_model->shortcuts(idx);
            auto defaultShortcuts = m_sc_model->defaultShortcuts(idx);
            w_sc_edit->setKeySequence(shortcuts.isEmpty() ? QKeySequence { } : shortcuts.at(0));
            w_sc_reset->setEnabled(shortcuts != defaultShortcuts);
        }
    });
    connect(m_sc_model, &QAbstractItemModel::dataChanged,
            this, [this](const QModelIndex &tl, const QModelIndex &br) {
        QModelIndex idx = m_sc_proxymodel->mapToSource(w_sc_list->currentIndex());
        if (idx.isValid() && QItemSelection(tl.siblingAtColumn(0), br.siblingAtColumn(0))
                .contains(idx.siblingAtColumn(0))) {
            auto shortcuts = m_sc_model->shortcuts(idx);
            auto defaultShortcuts = m_sc_model->defaultShortcuts(idx);
            w_sc_edit->setKeySequence(shortcuts.isEmpty() ? QKeySequence { } : shortcuts.at(0));
            w_sc_reset->setEnabled(shortcuts != defaultShortcuts);
        }
    });
    connect(w_sc_edit, &QKeySequenceEdit::editingFinished,
            this, [this]() {
        auto newShortcut = w_sc_edit->keySequence();
        // disallow Alt only shortcuts, because this interferes with standard menu handling
        for (int i = 0; i < newShortcut.count(); ++i) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            auto mod = (newShortcut[i] & Qt::KeyboardModifierMask);
#else
            auto mod = newShortcut[i].keyboardModifiers();
#endif
            if ((mod & Qt::AltModifier) && !(mod & Qt::ControlModifier)) {
                QMessageBox::warning(this, QCoreApplication::applicationName(),
                                     tr("Shortcuts with 'Alt' need to also include 'Control' in order to not interfere with the menu system."));
                return;
            }
        }

        if (!m_sc_model->setShortcut(m_sc_proxymodel->mapToSource(w_sc_list->currentIndex()),
                                     { w_sc_edit->keySequence() })) {
            QMessageBox::warning(this, QCoreApplication::applicationName(),
                                 tr("This shortcut is already used by another action."));
        }
    });
    connect(w_sc_reset, &QPushButton::clicked, this, [this]() {
        if (!m_sc_model->resetShortcut(m_sc_proxymodel->mapToSource(w_sc_list->currentIndex()))) {
            QMessageBox::warning(this, QCoreApplication::applicationName(),
                                 tr("This shortcut is already used by another action."));
        }
    });
    connect(w_sc_reset_all, &QPushButton::clicked, this, [this]() {
        m_sc_model->resetAllShortcuts();
        w_sc_filter->clear();
        w_sc_list->sortByColumn(0, Qt::AscendingOrder);
        w_sc_list->expandAll();
    });
    connect(w_sc_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_sc_proxymodel->setFilterFixedString(text);
        w_sc_list->expandAll();
    });

    load();

    QWidget *w = w_tabs->widget(0);
    if (!start_on_page.isEmpty())
        w = findChild<QWidget *>(start_on_page);
    w_tabs->setCurrentWidget(w);
}

void SettingsDialog::currentCurrencyChanged(const QString &ccode)
{
    qreal rate = Currency::inst()->rate(ccode);

    QString s;
    if (qFuzzyIsNull(rate))
        s = tr("could not find a cross rate for %1").arg(ccode);
    else if (!qFuzzyCompare(rate, 1))
        s = tr("1 %1 equals %2 USD").arg(ccode).arg(qreal(1) / rate, 0, 'f', 3);

    w_currency_status->setText(s);
    m_preferedCurrency = ccode;
}

void SettingsDialog::selectDocDir(int index)
{
    if (index > 0) {
        QString newdir = QFileDialog::getExistingDirectory(this, tr("Document directory location"),
                                                           w_docdir->itemData(0).toString());
        if (!newdir.isNull()) {
            w_docdir->setItemData(0, QDir::toNativeSeparators(newdir));
            w_docdir->setItemText(0, systemDirName(newdir));
        }
    }
    w_docdir->setCurrentIndex(0);
}

void SettingsDialog::selectLDrawDir(int index)
{
    if (index == 2) {
        // auto detect
        w_ldraw_dir->setItemData(0, QString());
        w_ldraw_dir->setItemText(0, w_ldraw_dir->itemText(2));
    } if (index == 3) {
        QString newdir = QFileDialog::getExistingDirectory(this, tr("LDraw directory location"),
                                                           w_ldraw_dir->itemData(0).toString());
        if (!newdir.isNull()) {
            w_ldraw_dir->setItemData(0, QDir::toNativeSeparators(newdir));
            w_ldraw_dir->setItemText(0, systemDirName(newdir));
        }
    }
    w_ldraw_dir->setCurrentIndex(0);
    checkLDrawDir();
}

void SettingsDialog::resetUpdateIntervals()
{
    QMap<QByteArray, int> intervals = Config::inst()->updateIntervalsDefault();

    w_upd_picture->setValue(sec2day(intervals["Picture"]));
    w_upd_priceguide->setValue(sec2day(intervals["PriceGuide"]));
    w_upd_database->setValue(sec2day(intervals["Database"]));
}

void SettingsDialog::accept()
{
    save();
    QDialog::accept();
}

void SettingsDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);

    w_sc_list->setColumnWidth(1, e->size().width() / 6);
    w_sc_list->setColumnWidth(2, e->size().width() / 6);
}

void SettingsDialog::currenciesUpdated()
{
    QString oldprefered = m_preferedCurrency;
    QStringList currencies = Currency::inst()->currencyCodes();
    currencies.sort();
    currencies.removeOne("USD"_l1);
    currencies.prepend("USD"_l1);
    w_currency->clear();
    w_currency->insertItems(0, currencies);
    if (currencies.count() > 1)
        w_currency->insertSeparator(1);
    w_currency->setCurrentIndex(qMax(0, w_currency->findText(oldprefered)));

//    currentCurrencyChanged(w_currency->currentText());
}

void SettingsDialog::load()
{
    // --[ GENERAL ]---------------------------------------------------

    QVector<Config::Translation> translations = Config::inst()->translations();

    if (translations.isEmpty()) {
        w_language->setEnabled(false);
    }
    else {
        const QString currentLanguage = Config::inst()->language();

        for (const auto &trans : translations) {
            QString s = trans.name;
            if (!trans.localName.isEmpty())
                s = trans.localName % u" (" % s % u')';
            w_language->addItem(s, trans.language);


            if (currentLanguage == trans.language)
                w_language->setCurrentIndex(w_language->count()-1);
        }
    }

    w_metric->setChecked(Config::inst()->measurementSystem() == QLocale::MetricSystem);
    w_imperial->setChecked(Config::inst()->measurementSystem() == QLocale::ImperialSystem);

    w_partout->setCurrentIndex(int(Config::inst()->partOutMode()));
    w_openbrowser->setChecked(Config::inst()->openBrowserOnExport());
    w_restore_session->setChecked(Config::inst()->restoreLastSession());
    w_modifications->setChecked(Config::inst()->visualChangesMarkModified());

    m_preferedCurrency = Config::inst()->defaultCurrencyCode();
    currenciesUpdated();

    QString docdir = QDir::toNativeSeparators(Config::inst()->documentDir());
    w_docdir->setItemData(0, docdir);
    w_docdir->setItemText(0, systemDirName(docdir));

#if !defined(SENTRY_ENABLED)
    w_crash_reports->setEnabled(false);
#else
    w_crash_reports->setChecked(Config::inst()->sentryConsent() == Config::SentryConsent::Given);
#endif

    // --[ INTERFACE ]-------------------------------------------------

    w_icon_size->setCurrentIndex(int(Config::inst()->iconSize()));
    w_font_size->setValue(Config::inst()->fontSizePercent() / 10);

    w_item_image_size->setValue(Config::inst()->itemImageSizePercent() / 10);

    w_theme->setCurrentIndex(int(Config::inst()->uiTheme()));

    // --[ UPDATES ]---------------------------------------------------

    QMap<QByteArray, int> intervals = Config::inst()->updateIntervals();

    w_upd_picture->setValue(sec2day(intervals["Picture"]));
    w_upd_priceguide->setValue(sec2day(intervals["PriceGuide"]));
    w_upd_database->setValue(sec2day(intervals["Database"]));

    // --[ BRICKLINK ]-------------------------------------------------

    w_bl_username->setText(Config::inst()->brickLinkUsername());
    w_bl_password->setText(Config::inst()->brickLinkPassword());

    // --[ LDRAW ]-----------------------------------------------------

    QString ldrawdir = QDir::toNativeSeparators(Config::inst()->ldrawDir());
    w_ldraw_dir->setItemData(0, ldrawdir.isEmpty() ? QString() : ldrawdir);
    w_ldraw_dir->setItemText(0, ldrawdir.isEmpty() ? w_ldraw_dir->itemText(2) : systemDirName(ldrawdir));
    checkLDrawDir();

    // --[ SHORTCUTS ]-------------------------------------------------

}


void SettingsDialog::save()
{
    // --[ GENERAL ]-------------------------------------------------------------------

    if (w_language->currentIndex() >= 0)
        Config::inst()->setLanguage(w_language->itemData(w_language->currentIndex()).toString());
    Config::inst()->setMeasurementSystem(w_imperial->isChecked() ? QLocale::ImperialSystem : QLocale::MetricSystem);
    Config::inst()->setDefaultCurrencyCode(m_preferedCurrency);

    Config::inst()->setPartOutMode(Config::PartOutMode(w_partout->currentIndex()));
    Config::inst()->setOpenBrowserOnExport(w_openbrowser->isChecked());
    Config::inst()->setRestoreLastSession(w_restore_session->isChecked());
    Config::inst()->setVisualChangesMarkModified(w_modifications->isChecked());

    QDir dd(w_docdir->itemData(0).toString());

    if (dd.exists() && dd.isReadable()) {
        Config::inst()->setDocumentDir(dd.absolutePath());
    } else {
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             tr("The specified document directory does not exist or is not read- and writeable.<br />The document directory setting will not be changed."));
    }

    Config::inst()->setSentryConsent(w_crash_reports->isChecked() ? Config::SentryConsent::Given
                                                                  : Config::SentryConsent::Revoked);

    // --[ INTERFACE ]-----------------------------------------------------------------

    Config::inst()->setIconSize(static_cast<Config::IconSize>(w_icon_size->currentIndex()));
    Config::inst()->setFontSizePercent(w_font_size->value() * 10);

    Config::inst()->setItemImageSizePercent(w_item_image_size->value() * 10);

    Config::inst()->setUiTheme(Config::UiTheme(w_theme->currentIndex()));

    // --[ UPDATES ]-------------------------------------------------------------------

    QMap<QByteArray, int> intervals;

    intervals.insert("Picture", day2sec(w_upd_picture->value()));
    intervals.insert("PriceGuide", day2sec(w_upd_priceguide->value()));
    intervals.insert("Database", day2sec(w_upd_database->value()));

    Config::inst()->setUpdateIntervals(intervals);

    // --[ BRICKLINK ]-----------------------------------------------------------------

    Config::inst()->setBrickLinkUsername(w_bl_username->text());
    Config::inst()->setBrickLinkPassword(w_bl_password->text());

    // --[ LDRAW ]---------------------------------------------------------------------

    const QString ldrawDir = w_ldraw_dir->itemData(0).toString();
    if (ldrawDir != Config::inst()->ldrawDir()) {
        QMessageBox::information(this, QCoreApplication::applicationName(),
                                 tr("You have changed the LDraw directory. Please restart BrickStore to apply this setting."));
        Config::inst()->setLDrawDir(ldrawDir);
    }

    // --[ TOOLBAR ]-------------------------------------------------------------------
    Config::inst()->setToolBarActions(m_tb_actions->actionList());

    // --[ SHORTCUTS ]-----------------------------------------------------------------

    m_sc_model->apply();
}

void SettingsDialog::checkLDrawDir()
{
    QString path = w_ldraw_dir->itemData(0).toString();

    auto setStatus = [this](bool ok, const QString &status, const QString &path = { }) {
        w_ldraw_status->setText(QString::fromLatin1("%1<br><i>%2</i>").arg(status, path));
        auto icon = QIcon::fromTheme(ok ? "vcs-normal"_l1 : "vcs-removed"_l1);
        w_ldraw_status_icon->setPixmap(icon.pixmap(fontMetrics().height() * 3 / 2));
    };

    if (path.isEmpty()) {
        const auto ldrawDirs = LDraw::Core::potentialDrawDirs();
        QString ldrawDir;
        for (auto &ld : ldrawDirs) {
            if (LDraw::Core::isValidLDrawDir(ld)) {
                ldrawDir = ld;
                break;
            }
        }
        if (!ldrawDir.isEmpty())
            setStatus(true, tr("Auto-detected an LDraw installation at:"), ldrawDir);
        else
            setStatus(false, tr("No LDraw installation could be auto-detected."));
    } else {
        if (LDraw::Core::isValidLDrawDir(path))
            setStatus(true, tr("Valid LDraw installation at:"), path);
        else
            setStatus(false, tr("Not a valid LDraw installation."));
    }
}

#include "moc_settingsdialog.cpp"
#include "settingsdialog.moc"
