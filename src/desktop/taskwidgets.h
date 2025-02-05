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

#include <QStackedWidget>
#include <QPointer>
#include <QLabel>
#include <QTreeView>

#include "common/documentmodel.h"
#include "common/document.h"
#include "appearsinwidget.h"
#include "picturewidget.h"
#include "priceguidewidget.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QDockWidget)


class TaskPriceGuideWidget : public PriceGuideWidget
{
    Q_OBJECT

public:
    TaskPriceGuideWidget(QWidget *parent = nullptr);

protected slots:
    void documentUpdate(Document *document);
    void selectionUpdate(const LotList &list);
    void currencyUpdate(const QString &ccode);
    virtual void topLevelChanged(bool);
    virtual void dockLocationChanged(Qt::DockWidgetArea);

    void setPrice(double p);

protected:
    bool event(QEvent *e) override;
    void fixParentDockWindow();

private:
    QPointer<Document> m_document;
    QDockWidget *m_dock;
    QTimer m_delayTimer;
    LotList m_selection;
};


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------


class TaskInfoWidget : public QStackedWidget
{
    Q_OBJECT

public:
    TaskInfoWidget(QWidget *parent = nullptr);

protected slots:
    void documentUpdate(Document *document);
    void selectionUpdate(const LotList &list);
    void statisticsUpdate();
    void currencyUpdate();

    void languageChange();
    void refresh();

protected:
    void changeEvent(QEvent *e) override;

private:
    void delayedSelectionUpdate();

    QLabel *m_text;
    PictureWidget *m_pic;
    QPointer<Document> m_document;
    QTimer m_delayTimer;
    LotList m_selection;
};


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

class TaskAppearsInWidget : public AppearsInWidget
{
    Q_OBJECT

public:
    TaskAppearsInWidget(QWidget *parent = nullptr);

protected slots:
    void documentUpdate(Document *document);
    void selectionUpdate(const LotList &list);

private:
    QPointer<Document> m_document;
    QTimer m_delayTimer;
    LotList m_selection;
};


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------


class TaskOpenDocumentsWidget : public QTreeView
{
    Q_OBJECT

public:
    TaskOpenDocumentsWidget(QWidget *parent = nullptr);
};


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------


class TaskRecentDocumentsWidget : public QTreeView
{
    Q_OBJECT

public:
    TaskRecentDocumentsWidget(QWidget *parent = nullptr);
};
