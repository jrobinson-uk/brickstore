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
#include <QComboBox>
#include <QToolButton>
#include <QLineEdit>
#include <QPainter>
#include <QApplication>
#include <QPalette>
#include <QPixmapCache>
#include <QDynamicPropertyChangeEvent>

#include "utility/utility.h"
#include "desktop/headerview.h"
#include "brickstoreproxystyle.h"


BrickStoreProxyStyle::BrickStoreProxyStyle(QStyle *baseStyle)
    : QProxyStyle(baseStyle)
{
#if defined(Q_OS_MACOS)
    // macOS style guide doesn't want shortcut keys in dialogs (Alt + underlined character)
    void qt_set_sequence_auto_mnemonic(bool on);
    qt_set_sequence_auto_mnemonic(true);

    extern void removeUnneededMacMenuItems();
    removeUnneededMacMenuItems();
#endif
}

void BrickStoreProxyStyle::polish(QWidget *w)
{
    if (auto *le = qobject_cast<QLineEdit *>(w)) {
        le->installEventFilter(this);
    }
}

void BrickStoreProxyStyle::unpolish(QWidget *w)
{
    if (auto *le = qobject_cast<QLineEdit *>(w)) {
        le->removeEventFilter(this);
    }
}

int BrickStoreProxyStyle::styleHint(QStyle::StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *returnData) const
{
    if (hint == SH_UnderlineShortcut) {
        // show _ shortcuts even on macOS
        return true;
    } else if (hint == SH_ItemView_MovementWithoutUpdatingSelection) {
        // move the current item within a selection in a QTableView
        return true;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

void BrickStoreProxyStyle::drawItemText(QPainter *painter, const QRect &rect, int flags, const QPalette &pal, bool enabled, const QString &text, QPalette::ColorRole textRole) const
{
#if defined(Q_OS_MACOS)
    // show _ shortcuts even on macOS
    return QCommonStyle::drawItemText(painter, rect, flags, pal, enabled, text, textRole);
#else
    return QProxyStyle::drawItemText(painter, rect, flags, pal, enabled, text, textRole);
#endif
}

void BrickStoreProxyStyle::drawComplexControl(ComplexControl control,
                                                    const QStyleOptionComplex *option,
                                                    QPainter *painter, const QWidget *widget) const
{
    if (control == CC_ComboBox) {
        if (qobject_cast<const QComboBox *>(widget)
                && widget->property("transparentCombo").toBool()) {
            if (const auto *comboOpt = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
                if (!comboOpt->editable) {
                    QStyleOptionComboBox opt(*comboOpt);
#if defined(Q_OS_MACOS)
                    opt.rect.setLeft(opt.rect.right() - 32);
#else
                    if (!opt.frame)
                        opt.subControls &= ~SC_ComboBoxFrame;
#endif
                    QProxyStyle::drawComplexControl(control, &opt, painter, widget);
                    return;
                }
            }
        }
    } else if (control == CC_ToolButton) {
        if (qobject_cast<const QToolButton *>(widget)
                && widget->property("noMenuArrow").toBool()) {
            if (const auto *tbOpt = qstyleoption_cast<const QStyleOptionToolButton *>(option)) {
                if (tbOpt->features & QStyleOptionToolButton::HasMenu) {
                    QStyleOptionToolButton opt(*tbOpt);
                    opt.features &= ~QStyleOptionToolButton::HasMenu;
                    QProxyStyle::drawComplexControl(control, &opt, painter, widget);
                    return;
                }
            }
        }
    }
    return QProxyStyle::drawComplexControl(control, option, painter, widget);
}


void BrickStoreProxyStyle::drawPrimitive(PrimitiveElement elem, const QStyleOption *option,
                                         QPainter *painter, const QWidget *widget) const
{
#if !defined(Q_OS_MACOS)
    if (elem == PE_PanelButtonCommand) {
        if (qobject_cast<const QComboBox *>(widget)
                && widget->property("transparentCombo").toBool()) {
            if (option->state & State_HasFocus && option->state & State_KeyboardFocusChange) {
                painter->save();

                QRect r = option->rect.adjusted(0, 1, -1, 0);
                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->translate(0.5, -0.5);

                painter->setPen(option->palette.highlight().color());
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(r, 2.0, 2.0);

                painter->restore();
            }
            return;
        }
    }
#endif
    QProxyStyle::drawPrimitive(elem, option, painter, widget);
}

void BrickStoreProxyStyle::drawControl(ControlElement element, const QStyleOption *opt, QPainter *p, const QWidget *widget) const
{
    if (element == CE_HeaderLabel) {
        if (qobject_cast<const HeaderView *>(widget)
                && widget->property("multipleSortColumns").toBool()) {
            if (const auto *headerOpt = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
                QStyleOptionHeader myheaderOpt(*headerOpt);
                // .iconAlignment only works in the vertical direction.
                // switching to RTL is a very bad hack, but it gets the job done, even in the
                // native platforms styles.
                myheaderOpt.direction = Qt::RightToLeft;
                int iconSize = pixelMetric(PM_SmallIconSize, opt);

                const auto *headerView = static_cast<const HeaderView *>(widget);
                const auto sortColumns = headerView->sortColumns();

                for (int i = 0; i < sortColumns.size(); ++i) {
                    if (sortColumns.at(i).first == headerOpt->section) {
                        QString key = "hv_"_l1 % QString::number(iconSize) % "-"_l1 %
                                QString::number(i) % "-"_l1 % QString::number(
                                    headerView->isSorted() ? int(sortColumns.at(i).second) : 2);

                        QPixmap pix;
                        if (!QPixmapCache::find(key, &pix)) {
                            QString name = "view-sort"_l1;
                            if (headerView->isSorted()) {
                                name = name % ((sortColumns.at(i).second == Qt::AscendingOrder)
                                               ? "-ascending"_l1 : "-descending"_l1);
                            }
                            pix = QIcon::fromTheme(name).pixmap(widget->window()->windowHandle(),
                                                                QSize(iconSize, iconSize),
                                                                (opt->state & State_Enabled) ?
                                                                    QIcon::Normal : QIcon::Disabled);

                            if (i > 0) {
                                QPixmap pix2(pix.size());
                                pix2.setDevicePixelRatio(pix.devicePixelRatio());
                                pix2.fill(Qt::transparent);
                                QPainter p(&pix2);
                                p.setOpacity(qMax(0.15, 0.75 - (i / 4.)));
                                p.drawPixmap(pix2.rect(), pix);
                                p.end();
                                pix = pix2;
                            }
                            QPixmapCache::insert(key, pix);
                        }
                        myheaderOpt.icon = QIcon(pix);
                    }
                }
                QProxyStyle::drawControl(element, &myheaderOpt, p, widget);
                return;
            }
        }
    }
    QProxyStyle::drawControl(element, opt, p, widget);

}

bool BrickStoreProxyStyle::eventFilter(QObject *o, QEvent *e)
{
    if (auto *le = qobject_cast<QLineEdit *>(o)) {
        if (e->type() == QEvent::DynamicPropertyChange) {
            auto *dpce = static_cast<QDynamicPropertyChangeEvent *>(e);
            if (dpce->propertyName() == "showInputError") {
                bool error = le->property("showInputError").toBool();

                QPalette pal = QApplication::palette("QLineEdit");
                if (error) {
                    pal.setColor(QPalette::Base,
                                 Utility::gradientColor(pal.color(QPalette::Base), Qt::red, 0.25));
                }
                le->setPalette(pal);
            }
        }
    }

    return QProxyStyle::eventFilter(o, e);
}
