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

#include <QtCore/QObject>
#include <QtCore/QDateTime>

#include "global.h"
#include "lot.h"

class TransferJob;


namespace BrickLink {

class Store : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool valid READ isValid NOTIFY updateFinished)
    Q_PROPERTY(BrickLink::UpdateStatus updateStatus READ updateStatus NOTIFY updateFinished)
    Q_PROPERTY(QDateTime lastUpdated READ lastUpdated NOTIFY updateFinished)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY updateFinished)
    Q_PROPERTY(int lotCount READ lotCount NOTIFY updateFinished)

public:
    bool isValid() const          { return m_valid; }
    QDateTime lastUpdated() const { return m_lastUpdated; }
    BrickLink::UpdateStatus updateStatus() const  { return m_updateStatus; }
    int lotCount() const          { return m_lots.count(); }
    const LotList &lots() const   { return m_lots; }
    QString currencyCode() const  { return m_currencyCode; }

    Q_INVOKABLE bool startUpdate();
    Q_INVOKABLE void cancelUpdate();

signals:
    void updateStarted();
    void updateProgress(int received, int total);
    void updateFinished(bool success, const QString &message);

private:
    Store(QObject *parent = nullptr);
    ~Store();

    bool m_valid = false;
    BrickLink::UpdateStatus m_updateStatus = BrickLink::UpdateStatus::UpdateFailed;
    TransferJob *m_job = nullptr;
    LotList m_lots;
    QDateTime m_lastUpdated;
    QString m_currencyCode;

    friend class Core;
};

} // namespace BrickLink
