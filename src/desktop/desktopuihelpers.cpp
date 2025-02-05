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
#include <climits>
#include <cfloat>

#include <QApplication>
#include <QMessageBox>
#include <QTextDocument>
#include <QByteArray>
#include <QColorDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QDoubleSpinBox>
#include <QProgressDialog>
#include <QDialogButtonBox>
#include <QDebug>

#include "common/config.h"
#include "qcoro/core/qcorosignal.h"
#include "utility/utility.h"
#include "desktopuihelpers.h"


QPointer<QWidget> DesktopUIHelpers::s_defaultParent;

void DesktopUIHelpers::create()
{
    s_inst = new DesktopUIHelpers();
}

void DesktopUIHelpers::setDefaultParent(QWidget *defaultParent)
{
    s_defaultParent = defaultParent;
}

DesktopUIHelpers::DesktopUIHelpers()
{ }

QCoro::Task<UIHelpers::StandardButton> DesktopUIHelpers::showMessageBox(const QString &msg,
                                                                        UIHelpers::Icon icon,
                                                                        StandardButtons buttons,
                                                                        StandardButton defaultButton,
                                                                        const QString &title)
{
    if (qobject_cast<QApplication *>(qApp)) {
        QMessageBox mb(QMessageBox::Icon(icon), !title.isEmpty() ? title : defaultTitle(), msg,
                       QMessageBox::NoButton, s_defaultParent);
        mb.setObjectName("messagebox"_l1);
        mb.setStandardButtons(QMessageBox::StandardButton(int(buttons)));
        mb.setDefaultButton(QMessageBox::StandardButton(defaultButton));
        mb.setTextFormat(Qt::RichText);

        mb.open();

        int result = co_await qCoro(&mb, &QDialog::finished);
        co_return static_cast<StandardButton>(result);

    } else {
        QByteArray out;
        if (msg.contains(QLatin1Char('<'))) {
            QTextDocument doc;
            doc.setHtml(msg);
            out = doc.toPlainText().toLocal8Bit();
        } else {
            out = msg.toLocal8Bit();
        }

        if (icon == Critical)
            qCritical("%s", out.constData());
        else
            qWarning("%s", out.constData());

        co_return defaultButton;
    }
}

QCoro::Task<std::optional<QString>> DesktopUIHelpers::getInputString(const QString &text,
                                                                     const QString &initialValue,
                                                                     const QString &title)
{
    QInputDialog dlg(s_defaultParent, Qt::Sheet);
    dlg.setWindowTitle(!title.isEmpty() ? title : defaultTitle());
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setInputMode(QInputDialog::TextInput);
    dlg.setLabelText(text);
    dlg.setTextValue(initialValue);

    dlg.open();

    if (co_await qCoro(&dlg, &QDialog::finished) == QDialog::Accepted)
        co_return dlg.textValue();
    else
        co_return { };
}

QCoro::Task<std::optional<double>> DesktopUIHelpers::getInputDouble(const QString &text,
                                                                    const QString &unit,
                                                                    double initialValue,
                                                                    double minValue, double maxValue,
                                                                    int decimals, const QString &title)
{
    QInputDialog dlg(s_defaultParent, Qt::Sheet);
    dlg.setWindowTitle(!title.isEmpty() ? title : defaultTitle());
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setInputMode(QInputDialog::DoubleInput);
    dlg.setLabelText(text);
    dlg.setDoubleValue(initialValue);
    dlg.setDoubleMinimum(minValue);
    dlg.setDoubleMaximum(maxValue);
    dlg.setDoubleDecimals(decimals);

    if (auto *sp = dlg.findChild<QDoubleSpinBox *>()) {
        if (!unit.isEmpty())
            sp->setSuffix(QLatin1Char(' ') + unit);
        sp->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sp->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        sp->setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
        sp->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    }

    dlg.open();

    if (co_await qCoro(&dlg, &QDialog::finished) == QDialog::Accepted)
        co_return dlg.doubleValue();
    else
        co_return { };
}

QCoro::Task<std::optional<int>> DesktopUIHelpers::getInputInteger(const QString &text,
                                                                  const QString &unit,
                                                                  int initialValue, int minValue,
                                                                  int maxValue, const QString &title)
{
    QInputDialog dlg(s_defaultParent, Qt::Sheet);
    dlg.setWindowTitle(!title.isEmpty() ? title : defaultTitle());
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setInputMode(QInputDialog::IntInput);
    dlg.setLabelText(text);
    dlg.setIntValue(initialValue);
    dlg.setIntMinimum(minValue);
    dlg.setIntMaximum(maxValue);

    if (auto *sp = dlg.findChild<QSpinBox *>()) {
        if (!unit.isEmpty())
            sp->setSuffix(QLatin1Char(' ') + unit);
        sp->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sp->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        sp->setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
        sp->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    }

    dlg.open();

    if (co_await qCoro(&dlg, &QDialog::finished) == QDialog::Accepted)
        co_return dlg.intValue();
    else
        co_return { };
}

QCoro::Task<std::optional<QColor>> DesktopUIHelpers::getInputColor(const QColor &initialColor,
                                                                   const QString &title)
{
    QColorDialog dlg(s_defaultParent);
    dlg.setWindowFlag(Qt::Sheet);
    dlg.setWindowTitle(!title.isEmpty() ? title : defaultTitle());
    dlg.setWindowModality(Qt::WindowModal);
    if (initialColor.isValid())
        dlg.setCurrentColor(initialColor);

    dlg.open();

    if (co_await qCoro(&dlg, &QDialog::finished) == QDialog::Accepted)
        co_return dlg.selectedColor();
    else
        co_return { };
}

QCoro::Task<std::optional<QString>> DesktopUIHelpers::getFileName(bool doSave, const QString &fileName,
                                                                  const QStringList &filters,
                                                                  const QString &title)
{
    QFileDialog fd(s_defaultParent, !title.isEmpty() ? title : defaultTitle(), fileName,
                   filters.join(";;"_l1));
    fd.setFileMode(doSave ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    fd.setAcceptMode(doSave ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    fd.open();
    if (co_await qCoro(&fd, &QFileDialog::finished) == QDialog::Accepted) {
        auto files = fd.selectedFiles();
        if (!files.isEmpty())
            co_return files.constFirst();
    }
    co_return { };
}


class ForceableProgressDialog : public QProgressDialog
{
public:
    ForceableProgressDialog(QWidget *parent)
        : QProgressDialog(parent)
    { }

    // why is forceShow() protected?
    using QProgressDialog::forceShow;
};


class DesktopPDI : public UIHelpers_ProgressDialogInterface
{
    Q_OBJECT

public:
    DesktopPDI(const QString &title, const QString &message)
        : m_title(title)
        , m_message(message)
        , m_pd(new ForceableProgressDialog(DesktopUIHelpers::s_defaultParent))
    {
        m_pd->setModal(true);
        m_pd->setWindowTitle(title);
        m_pd->setLabelText(message);
        m_pd->setAutoReset(false);
        m_pd->setAutoClose(false);
        m_pd->setMinimumWidth(m_pd->fontMetrics().averageCharWidth() * 60);

        connect(m_pd, &QProgressDialog::canceled,
                this, &DesktopPDI::cancel);
    }

    ~DesktopPDI() override
    {
        m_pd->deleteLater();
    }

    QCoro::Task<bool> exec() override
    {
        emit start();
        m_pd->show();
        int result = co_await qCoro(m_pd, &ForceableProgressDialog::finished);
        co_return (result == 0);
    }

    void progress(int done, int total) override
    {
        if (total <= 0) {
            m_pd->setMaximum(0);
            m_pd->setValue(0);
        } else {
            m_pd->setMaximum(total);
            m_pd->setValue(done);
        }
    }

    void finished(bool success, const QString &message) override
    {
        bool showMessage = !message.isEmpty();
        if (showMessage) {
            m_pd->setLabelText(message);
            m_pd->setCancelButtonText(QDialogButtonBox::tr("Ok"));
            m_pd->forceShow();
        }

        if (!showMessage) {
            m_pd->reset();
            m_pd->done(success ? 0 : 1);
        }
    }

private:
    QString m_title;
    QString m_message;
    ForceableProgressDialog *m_pd;
};

UIHelpers_ProgressDialogInterface *DesktopUIHelpers::createProgressDialog(const QString &title, const QString &message)
{
    return new DesktopPDI(title, message);
}

#include "moc_desktopuihelpers.cpp"
#include "desktopuihelpers.moc"

