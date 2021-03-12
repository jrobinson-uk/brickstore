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
#include <QApplication>
#include <QFileDialog>
#include <QClipboard>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QStringView>
#include <QBuffer>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>

#include "framework.h"
#include "progressdialog.h"
#include "messagebox.h"
#include "exception.h"
#include "xmlhelpers.h"
#include "config.h"
#include "utility.h"
#include "document.h"
#include "window.h"
#include "stopwatch.h"
#include "minizip.h"
#include "documentio.h"


QString DocumentIO::s_lastDirectory { };

QString DocumentIO::lastDirectory()
{
    return s_lastDirectory.isEmpty() ? Config::inst()->documentDir() : s_lastDirectory;
}

void DocumentIO::setLastDirectory(const QString &dir)
{
    if (!dir.isEmpty())
        s_lastDirectory = dir;
}


Document *DocumentIO::create()
{
    auto *doc = new Document();
    doc->setTitle(tr("Untitled"));
    return doc;
}

Window *DocumentIO::open()
{
    QStringList filters;
    filters << tr("BrickStore XML Data") + " (*.bsx)";
    filters << tr("All Files") + "(*)";

    auto fn = QFileDialog::getOpenFileName(FrameWork::inst(), tr("Open File"), lastDirectory(),
                                           filters.join(";;"));
    if (fn.isEmpty())
        return nullptr;
    setLastDirectory(QFileInfo(fn).absolutePath());
    return open(fn);
}

Window *DocumentIO::open(const QString &s)
{
    if (s.isEmpty())
        return nullptr;

    QString fn = QFileInfo(s).absoluteFilePath();

    const auto all = Window::allWindows();
    for (Window *win : all) {
        if (QFileInfo(win->document()->fileName()).absoluteFilePath() == fn)
            return win;
    }

    return loadFrom(s);
}

Document *DocumentIO::importBrickLinkInventory(const BrickLink::Item *item, int multiply,
                                               BrickLink::Condition condition,
                                               BrickLink::Status extraParts,
                                               bool includeInstructions)
{
    if (item && !item->hasInventory())
        return nullptr;

    if (item && (multiply != 0)) {
        const auto &parts = item->consistsOf();

        if (!parts.empty()) {
            LotList lots;

            for (const BrickLink::Item::ConsistsOf &part : parts) {
                const BrickLink::Item *partItem = part.item();
                if (!partItem)
                    continue;
                Lot *lot = new Lot(part.color(), partItem);
                lot->setQuantity(part.quantity() * multiply);
                lot->setCondition(condition);
                if (part.isExtra())
                    lot->setStatus(extraParts);
                lot->setAlternate(part.isAlternate());
                lot->setAlternateId(part.alternateId());
                lot->setCounterPart(part.isCounterPart());
                lots << lot;
            }
            if (includeInstructions) {
                if (const auto *instructions = BrickLink::core()->item('I', item->id())) {
                    auto *lot =  new Lot(BrickLink::core()->color(0), instructions);
                    lot->setQuantity(multiply);
                    lot->setCondition(condition);
                    lots << lot;
                }
            }

            DocumentIO::BsxContents bsx;
            bsx.lots = lots;

            auto *doc = new Document(bsx); // Document own the items now
            doc->setTitle(tr("Inventory for %1").arg(item->id()));
            return doc;
        } else {
            MessageBox::warning(nullptr, { }, tr("Internal error: Could not create an Inventory object for item %1").arg(CMB_BOLD(item->id())));
        }
    }
    return nullptr;
}

Document *DocumentIO::importBrickLinkOrder(BrickLink::Order *order, const LotList &lots)
{
    BsxContents bsx;
    bsx.lots = lots;
    bsx.currencyCode = order->currencyCode();

    auto *doc = new Document(bsx); // Document owns the items now
    doc->setTitle(tr("Order %1 (%2)").arg(order->id(), order->otherParty()));
    doc->setOrder(order);
    return doc;
}

Document *DocumentIO::importBrickLinkStore()
{
    Transfer trans;
    ProgressDialog pd(tr("Import BrickLink Store Inventory"), &trans, FrameWork::inst());

    pd.setHeaderText(tr("Importing BrickLink Store"));

    pd.setMessageText(tr("Download: %p"));

    QUrl url("https://www.bricklink.com/invExcelFinal.asp");
    QUrlQuery query;
    query.addQueryItem("itemType",      "");
    query.addQueryItem("catID",         "");
    query.addQueryItem("colorID",       "");
    query.addQueryItem("invNew",        "");
    query.addQueryItem("itemYear",      "");
    query.addQueryItem("viewType",      "x");    // XML
    query.addQueryItem("invStock",      "Y");
    query.addQueryItem("invStockOnly",  "");
    query.addQueryItem("invQty",        "");
    query.addQueryItem("invQtyMin",     "0");
    query.addQueryItem("invQtyMax",     "0");
    query.addQueryItem("invBrikTrak",   "");
    query.addQueryItem("invDesc",       "");
    query.addQueryItem("frmUsername",   Config::inst()->loginForBrickLink().first);
    query.addQueryItem("frmPassword",   Config::inst()->loginForBrickLink().second);
    url.setQuery(query);

    QByteArray xml;

    QObject::connect(&pd, &ProgressDialog::transferFinished,
                     &pd, [&pd, &xml]() {
        TransferJob *j = pd.job();
        QByteArray *data = j->data();
        bool ok = false;

        if (j->isFailed() || j->responseCode() != 200 || !j->data()) {
            pd.setErrorText(tr("Failed to download the store inventory."));
        } else if ((data->left(30).contains("<html") || data->left(30).contains("<HTML"))
                   && data->contains("there was a problem during login")) {
            pd.setErrorText(tr("Either your username or password are incorrect."));
        } else {
            xml = *data;
            ok = true;
        }
        pd.setFinished(ok);
    });

    pd.post(url);
    pd.layout();

    if (pd.exec() == QDialog::Accepted) {
        try {
            auto result = fromBrickLinkXML(xml);
            auto *doc = new Document(result); // Document owns the items now
            doc->setTitle(tr("Store %1").arg(QLocale().toString(QDate::currentDate(), QLocale::ShortFormat)));
            return doc;

        } catch (const Exception &e) {
            MessageBox::warning(nullptr, { }, tr("Failed to import store inventory") % u"<br><br>" % e.error());
        }
    }
    return nullptr;
}

Document *DocumentIO::importBrickLinkCart(BrickLink::Cart *cart, const LotList &lots)
{
    BsxContents bsx;
    bsx.lots = lots;
    bsx.currencyCode = cart->currencyCode();

    auto *doc = new Document(bsx); // Document owns the items now
    doc->setTitle(tr("Cart in store %1").arg(cart->storeName()));
    return doc;
}

Document *DocumentIO::importBrickLinkXML()
{
    QStringList filters;
    filters << tr("BrickLink XML File") + " (*.xml)";
    filters << tr("All Files") + "(*)";

    QString fn = QFileDialog::getOpenFileName(FrameWork::inst(), tr("Import File"), lastDirectory(),
                                             filters.join(";;"));
    if (fn.isEmpty())
        return nullptr;
    setLastDirectory(QFileInfo(fn).absolutePath());

    QFile f(fn);
    if (f.open(QIODevice::ReadOnly)) {
        try {
            auto result = fromBrickLinkXML(f.readAll());
            auto *doc = new Document(result); // Document owns the items now
            doc->setTitle(tr("Import of %1").arg(QFileInfo(fn).fileName()));
            return doc;

        } catch (const Exception &e) {
            MessageBox::warning(nullptr, { }, tr("Could not parse the XML data.") % u"<br><br>" % e.error());
        }
    } else {
        MessageBox::warning(nullptr, { }, tr("Could not open file %1 for reading.").arg(CMB_BOLD(fn)));
    }
    return nullptr;
}

Window *DocumentIO::loadFrom(const QString &name)
{
    QFile f(name);

    if (!f.open(QIODevice::ReadOnly)) {
        MessageBox::warning(nullptr, { }, tr("Could not open file %1 for reading.").arg(CMB_BOLD(name)));
        return nullptr;
    }

    try {
        BsxContents result = DocumentIO::parseBsxInventory(&f); // we own the items now

        if (result.invalidLotsCount) {
            if (MessageBox::question(nullptr, { },
                                     tr("This file contains %n unknown item(s).<br /><br />Do you still want to open this file?",
                                        nullptr, result.invalidLotsCount)) == QMessageBox::Yes) {
                result.invalidLotsCount = 0;
            }
        }

        if (result.invalidLotsCount) {
            qDeleteAll(result.lots);
            return nullptr;
        }

        if (result.currencyCode.isEmpty()) // flag as legacy currency
            result.currencyCode = qL1S("$$$");

        // Document owns the items now
        auto doc = new Document(result);
        doc->setFileName(name);
        Config::inst()->addToRecentFiles(name);

        return new Window(doc, result.guiColumnLayout, result.guiSortFilterState);

    } catch (const Exception &e) {
        MessageBox::warning(nullptr, { }, tr("This XML document is not a valid BrickStoreXML file.")
                            % u"<br><br>" % e.error());
        return nullptr;
    }
}

Document *DocumentIO::importLDrawModel()
{
    QStringList filters;
    filters << tr("All Models") + " (*.dat *.ldr *.mpd *.io)";
    filters << tr("LDraw Models") + " (*.dat *.ldr *.mpd)";
    filters << tr("BrickLink Studio Models") + " (*.io)";
    filters << tr("All Files") + " (*)";

    QString fn = QFileDialog::getOpenFileName(FrameWork::inst(), tr("Import File"), lastDirectory(),
                                             filters.join(";;"));
    if (fn.isEmpty())
        return nullptr;
    setLastDirectory(QFileInfo(fn).absolutePath());

    QScopedPointer<QFile> f;

    if (fn.endsWith(QLatin1String(".io"))) {
        stopwatch unpack("unpack/decrypt studio zip");

        // this is a zip file - unpack the encrypted model.ldr (pw: soho0909)

        f.reset(new QTemporaryFile());

        if (f->open(QIODevice::ReadWrite)) {
            try {
                MiniZip::unzip(fn, f.get(), "model.ldr", "soho0909");
                f->close();
            } catch (const Exception &e) {
                MessageBox::warning(nullptr, { }, tr("Could not parse the Studio model:")
                                    % u"<br><br>" % e.error());
                return nullptr;
            }
        }
    } else {
        f.reset(new QFile(fn));
    }

    if (!f->open(QIODevice::ReadOnly)) {
        MessageBox::warning(nullptr, { }, tr("Could not open file %1 for reading.").arg(CMB_BOLD(fn)));
        return nullptr;
    }

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    int invalidLots = 0;
    LotList lots; // we own the items

    bool b = DocumentIO::parseLDrawModel(f.get(), lots, &invalidLots);
    Document *doc = nullptr;

    QApplication::restoreOverrideCursor();

    if (b && !lots.isEmpty()) {
        if (invalidLots) {
            if (MessageBox::question(nullptr, { },
                                     tr("This file contains %n unknown item(s).<br /><br />Do you still want to open this file?",
                                        nullptr, invalidLots)) == QMessageBox::Yes) {
                invalidLots = 0;
            }
        }

        if (!invalidLots) {
            DocumentIO::BsxContents bsx;
            bsx.lots = lots;
            doc = new Document(bsx); // Document owns the items now
            lots.clear();
            doc->setTitle(tr("Import of %1").arg(QFileInfo(fn).fileName()));
        }
    } else {
        MessageBox::warning(nullptr, { }, tr("Could not parse the LDraw model in file %1.").arg(CMB_BOLD(fn)));
    }

    qDeleteAll(lots);
    return doc;
}

bool DocumentIO::parseLDrawModel(QFile *f, LotList &lots, int *invalidLots)
{
    QVector<QString> recursion_detection;
    QHash<QString, QVector<Lot *>> subCache;
    QVector<Lot *> ldrawLots;

    {
        stopwatch parse("parse ldraw model");

        if (!parseLDrawModelInternal(f, QString(), ldrawLots, subCache, recursion_detection))
            return false;
    }
    {
        stopwatch consolidate("removing duplicates");
        // consolidate everything
        for (int i = 0; i < ldrawLots.count(); ++i) {
            if (auto *lot = ldrawLots[i]) {
                for (int j = i + 1; j < ldrawLots.count(); ++j) {
                    if (auto *&otherLot = ldrawLots[j]) {
                        if (lot == otherLot) {
                            lot->setQuantity(lot->quantity() + 1);
                            otherLot = nullptr;
                        }
                    }
                }
            }
        }
    }
    {
        stopwatch consolidate("consolidate ldraw model");
        // consolidate everything
        for (int i = 0; i < ldrawLots.count(); ++i) {
            if (auto *lot = ldrawLots[i]) {
                if (invalidLots && lot->isIncomplete())
                    ++*invalidLots;

                for (int j = i + 1; j < ldrawLots.count(); ++j) {
                    if (auto *&otherLot = ldrawLots[j]) {
                        bool mergeable = false;
                        if (!lot->isIncomplete() && !otherLot->isIncomplete()) {
                            mergeable = (lot->item() == otherLot->item())
                                    && (lot->color() == otherLot->color());
                        } else if (lot->isIncomplete() && otherLot->isIncomplete()) {
                            mergeable = (*lot->isIncomplete() == *otherLot->isIncomplete());
                        }

                        if (mergeable) {
                            lot->setQuantity(lot->quantity() + otherLot->quantity());
                            delete otherLot;
                            otherLot = nullptr;
                        }
                    }
                }
                lots.append(lot);
            }
        }
    }
    return true;
}

bool DocumentIO::parseLDrawModelInternal(QFile *f, const QString &modelName,
                                         QVector<Lot *> &lots,
                                         QHash<QString, QVector<Lot *>> &subCache,
                                         QVector<QString> &recursionDetection)
{
    auto it = subCache.constFind(modelName);
    if (it != subCache.cend()) {
        lots = it.value();
        return true;
    }

    if (recursionDetection.contains(modelName))
        return false;
    recursionDetection.append(modelName);

    QStringList searchpath;
    int linecount = 0;

    bool is_mpd = false;
    int current_mpd_index = -1;
    QString current_mpd_model;
    bool is_mpd_model_found = false;


    searchpath.append(QFileInfo(*f).dir().absolutePath());
    if (!BrickLink::core()->ldrawDataPath().isEmpty()) {
        searchpath.append(BrickLink::core()->ldrawDataPath() + qL1S("/models"));
    }

    if (f->isOpen()) {
        QTextStream in(f);
        QString line;

        while (!(line = in.readLine()).isNull()) {
            linecount++;

            if (line.isEmpty())
                continue;

            if (line.at(0) == QLatin1Char('0')) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                const auto split = line.splitRef(QLatin1Char(' '), QString::SkipEmptyParts);
                auto strPosition = [](const QStringRef &sr) { return sr.position(); };
#else
                const auto split = QStringView{line}.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                auto strPosition = [line](QStringView sv) { return line.constData() - sv.constData(); };
#endif
                if ((split.count() >= 2) && (split.at(1) == qL1S("FILE"))) {
                    is_mpd = true;
                    current_mpd_model = line.mid(strPosition(split.at(2))).toLower();
                    current_mpd_index++;

                    if (is_mpd_model_found)
                        break;

                    if ((current_mpd_model == modelName.toLower()) || (modelName.isEmpty() && (current_mpd_index == 0)))
                        is_mpd_model_found = true;

                    if (f->isSequential())
                        return false; // we need to seek!
                }

            } else if (line.at(0) == QLatin1Char('1')) {
                if (is_mpd && !is_mpd_model_found)
                    continue;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                const auto split = line.splitRef(QLatin1Char(' '), QString::SkipEmptyParts);
                auto strPosition = [](const QStringRef &sr) { return sr.position(); };
#else
                const auto split = QStringView{line}.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                auto strPosition = [line](QStringView sv) { return sv.constData() - line.constData(); };
#endif

                if (split.count() >= 15) {
                    uint colid = split.at(1).toUInt();
                    QString partname = line.mid(strPosition(split.at(14))).toLower();

                    QString partid = partname;
                    partid.truncate(partid.lastIndexOf(QLatin1Char('.')));

                    const BrickLink::Item *itemp = BrickLink::core()->item('P', partid);

                    if (!itemp) {
                        bool got_subfile = false;
                        QVector<Lot *> subLots;

                        if (is_mpd) {
                            qint64 oldpos = f->pos();
                            f->seek(0);

                            got_subfile = parseLDrawModelInternal(f, partname, subLots, subCache, recursionDetection);

                            f->seek(oldpos);
                        }

                        if (!got_subfile) {
                            for (const auto &path : qAsConst(searchpath)) {
                                QFile subf(path % u'/' % partname);

                                if (subf.open(QIODevice::ReadOnly)) {

                                    (void) parseLDrawModelInternal(&subf, partname, subLots, subCache, recursionDetection);

                                    got_subfile = true;
                                    break;
                                }
                            }
                        }
                        if (got_subfile) {
                            subCache.insert(partname, subLots);
                            lots.append(subLots);
                            continue;
                        }
                    }

                    const BrickLink::Color *colp = BrickLink::core()->colorFromLDrawId(colid);

                    auto *lot = new Lot(colp, itemp);
                    lot->setQuantity(1);

                    if (!colp || !itemp) {
                        auto *inc = new BrickLink::Incomplete;

                        if (!itemp) {
                            inc->m_item_id = partid;
                            inc->m_itemtype_id = qL1S("P");
                            inc->m_itemtype_name = qL1S("Part");
                        }
                        if (!colp) {
                            inc->m_color_name = qL1S("LDraw #") + QByteArray::number(colid);
                        }
                        lot->setIncomplete(inc);
                    }
                    lots.append(lot);
                }
            }
        }
    }

    recursionDetection.removeLast();

    return is_mpd ? is_mpd_model_found : true;
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


bool DocumentIO::save(Window *win)
{
    if (win->document()->fileName().isEmpty())
        return saveAs(win);
    else if (win->document()->isModified())
        return saveTo(win, win->document()->fileName());
    return false;
}


bool DocumentIO::saveAs(Window *win)
{
    QStringList filters;
    filters << tr("BrickStore XML Data") + " (*.bsx)";

    QString fn = win->document()->fileName();
    if (fn.isEmpty()) {
        fn = lastDirectory();

        if (!win->document()->title().isEmpty()) {
            QString t = Utility::sanitizeFileName(win->document()->title());
            fn = fn % u'/' % t;
        }
    }
    if (fn.right(4) == ".xml")
        fn.truncate(fn.length() - 4);

    fn = QFileDialog::getSaveFileName(FrameWork::inst(), tr("Save File as"), fn, filters.join(";;"));

    if (fn.isEmpty())
        return false;
    setLastDirectory(QFileInfo(fn).absolutePath());

    if (fn.right(4) != ".bsx")
        fn += ".bsx";

    return saveTo(win, fn);
}


bool DocumentIO::saveTo(Window *win, const QString &s)
{
    Document *doc = win->document();

    QFile f(s);
    if (f.open(QIODevice::WriteOnly)) {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        BsxContents bsx;
        bsx.lots = doc->lots();
        bsx.currencyCode = doc->currencyCode();
        bsx.differenceModeBase = doc->differenceBase();
        bsx.guiSortFilterState = doc->saveSortFilterState();
        bsx.guiColumnLayout = win->currentColumnLayout();

        bool ok = createBsxInventory(&f, bsx);

        QApplication::restoreOverrideCursor();

        if (ok) {
            doc->unsetModified();
            doc->setFileName(s);

            Config::inst()->addToRecentFiles(s);
            return true;
        }
        else
            MessageBox::warning(nullptr, { }, tr("Failed to save data to file %1.").arg(CMB_BOLD(s)));
    }
    else
        MessageBox::warning(nullptr, { }, tr("Failed to open file %1 for writing.").arg(CMB_BOLD(s)));

    return false;
}

void DocumentIO::exportBrickLinkInvReqClipboard(const LotList &lots)
{
    XmlHelpers::CreateXML xml("INVENTORY", "ITEM");

    for (const Lot *lot : lots) {
        if (lot->isIncomplete() || (lot->status() == BrickLink::Status::Exclude))
            continue;

        xml.createElement();
        xml.createText("ITEMID", lot->item()->id());
        xml.createText("ITEMTYPE", QString(lot->itemType()->id()));
        xml.createText("COLOR", QString::number(lot->color()->id()));
        xml.createText("QTY", QString::number(lot->quantity()));
        if (lot->status() == BrickLink::Status::Extra)
            xml.createText("EXTRA", u"Y");
    }

    QGuiApplication::clipboard()->setText(xml.toString(), QClipboard::Clipboard);

    if (Config::inst()->openBrowserOnExport())
        BrickLink::core()->openUrl(BrickLink::URL_InventoryRequest);
}

void DocumentIO::exportBrickLinkWantedListClipboard(const LotList &lots)
{
    QString wantedlist;

    if (MessageBox::getString(nullptr, { }, tr("Enter the ID number of Wanted List (leave blank for the default Wanted List)"), wantedlist)) {
        static QLocale c = QLocale::c();
        XmlHelpers::CreateXML xml("INVENTORY", "ITEM");

        for (const Lot *lot : lots) {
            if (lot->isIncomplete() || (lot->status() == BrickLink::Status::Exclude))
                continue;

            xml.createElement();
            xml.createText("ITEMID", lot->item()->id());
            xml.createText("ITEMTYPE", QString(lot->itemType()->id()));
            xml.createText("COLOR", QString::number(lot->color()->id()));

            if (lot->quantity())
                xml.createText("MINQTY", QString::number(lot->quantity()));
            if (!qFuzzyIsNull(lot->price()))
                xml.createText("MAXPRICE", c.toCurrencyString(lot->price(), { }, 3));
            if (!lot->remarks().isEmpty())
                xml.createText("REMARKS", lot->remarks());
            if (lot->condition() == BrickLink::Condition::New)
                xml.createText("CONDITION", u"N");
            if (!wantedlist.isEmpty())
                xml.createText("WANTEDLISTID", wantedlist);
        }

        QGuiApplication::clipboard()->setText(xml.toString(), QClipboard::Clipboard);

        if (Config::inst()->openBrowserOnExport())
            BrickLink::core()->openUrl(BrickLink::URL_WantedListUpload);
    }
}

void DocumentIO::exportBrickLinkUpdateClipboard(const Document *doc, const LotList &lots)
{
    bool withoutLotId = false;
    bool duplicateLotId = false;
    bool hasDifferences = false;
    QSet<uint> lotIds;
    for (const Lot *lot : lots) {
        const uint lotId = lot->lotId();
        if (!lotId) {
            withoutLotId = true;
        } else {
            if (lotIds.contains(lotId))
                duplicateLotId = true;
            else
                lotIds.insert(lotId);
        }
        if (auto *base = doc->differenceBaseLot(lot)) {
            if (*base != *lot)
                hasDifferences = true;
        }
    }

    QStringList warnings;

    if (!hasDifferences)
        warnings << tr("This document has no differences that could be exported.");
    if (withoutLotId)
        warnings << tr("This list contains items without a BrickLink Lot-ID.");
    if (duplicateLotId)
        warnings << tr("This list contains items with duplicate BrickLink Lot-IDs.");

    if (!warnings.isEmpty()) {
        QString s = u"<ul><li>" % warnings.join(qL1S("</li><li>")) % u"</li></ul>";
        s = tr("There are problems: %1Do you really want to export this list?").arg(s);

        if (MessageBox::question(nullptr, { }, s) != MessageBox::Yes)
            return;
    }

    static QLocale c = QLocale::c();
    XmlHelpers::CreateXML xml("INVENTORY", "ITEM");

    for (const Lot *lot : lots) {
        if (lot->isIncomplete() || (lot->status() == BrickLink::Status::Exclude))
            continue;

        auto *base = doc->differenceBaseLot(lot);
        if (!base)
            continue;

        // we don't care about reserved and status, so we have to mask it
        auto baseLot = *base;
        baseLot.setReserved(lot->reserved());
        baseLot.setStatus(lot->status());

        if (baseLot == *lot)
            continue;

        xml.createElement();
        xml.createText("LOTID", QString::number(lot->lotId()));
        int qdiff = lot->quantity() - base->quantity();
        if (qdiff && (lot->quantity() > 0))
            xml.createText("QTY", QString::number(qdiff).prepend(qdiff > 0 ? "+" : ""));
        else if (qdiff && (lot->quantity() <= 0))
            xml.createEmpty("DELETE");

        if (!qFuzzyCompare(base->price(), lot->price()))
            xml.createText("PRICE", QString::number(lot->price(), {}, 3));
        if (!qFuzzyCompare(base->cost(), lot->cost()))
            xml.createText("MYCOST", QString::number(lot->cost(), {}, 3));
        if (base->condition() != lot->condition())
            xml.createText("CONDITION", (lot->condition() == BrickLink::Condition::New) ? u"N" : u"U");
        if (base->bulkQuantity() != lot->bulkQuantity())
            xml.createText("BULK", QString::number(lot->bulkQuantity()));
        if (base->sale() != lot->sale())
            xml.createText("SALE", QString::number(lot->sale()));
        if (base->comments() != lot->comments())
            xml.createText("DESCRIPTION", lot->comments());
        if (base->remarks() != lot->remarks())
            xml.createText("REMARKS", lot->remarks());
        if (base->retain() != lot->retain())
            xml.createText("RETAIN", lot->retain() ? u"Y" : u"N");

        if (base->tierQuantity(0) != lot->tierQuantity(0))
            xml.createText("TQ1", QString::number(lot->tierQuantity(0)));
        if (!qFuzzyCompare(base->tierPrice(0), lot->tierPrice(0)))
            xml.createText("TP1", c.toCurrencyString(lot->tierPrice(0), {}, 3));
        if (base->tierQuantity(1) != lot->tierQuantity(1))
            xml.createText("TQ2", QString::number(lot->tierQuantity(1)));
        if (!qFuzzyCompare(base->tierPrice(1), lot->tierPrice(1)))
            xml.createText("TP2", c.toCurrencyString(lot->tierPrice(1), {}, 3));
        if (base->tierQuantity(2) != lot->tierQuantity(2))
            xml.createText("TQ3", QString::number(lot->tierQuantity(2)));
        if (!qFuzzyCompare(base->tierPrice(2), lot->tierPrice(2)))
            xml.createText("TP3", c.toCurrencyString(lot->tierPrice(2), {}, 3));

        if (base->subCondition() != lot->subCondition()) {
            const char16_t *st = nullptr;
            switch (lot->subCondition()) {
            case BrickLink::SubCondition::Incomplete: st = u"I"; break;
            case BrickLink::SubCondition::Complete  : st = u"C"; break;
            case BrickLink::SubCondition::Sealed    : st = u"S"; break;
            default                                 : break;
            }
            if (st)
                xml.createText("SUBCONDITION", st);
        }
        if (base->stockroom() != lot->stockroom()) {
            const char16_t *st = nullptr;
            switch (lot->stockroom()) {
            case BrickLink::Stockroom::A: st = u"A"; break;
            case BrickLink::Stockroom::B: st = u"B"; break;
            case BrickLink::Stockroom::C: st = u"C"; break;
            default                     : break;
            }
            xml.createText("STOCKROOM", st ? u"Y" : u"N");
            if (st)
                xml.createText("STOCKROOMID", st);
        }

        // Ignore the weight - it's just too confusing:
        // BrickStore displays the total weight, but that is dependent on the quantity.
        // On the other hand, the update would be done on the item weight.
    }

    QGuiApplication::clipboard()->setText(xml.toString(), QClipboard::Clipboard);

    if (Config::inst()->openBrowserOnExport())
        BrickLink::core()->openUrl(BrickLink::URL_InventoryUpdate);
}

QString DocumentIO::toBrickLinkXML(const LotList &lots)
{
    static QLocale c = QLocale::c();
    XmlHelpers::CreateXML xml("INVENTORY", "ITEM");

    for (const Lot *lot : lots) {
        if (lot->isIncomplete() || (lot->status() == BrickLink::Status::Exclude))
            continue;

        xml.createElement();
        xml.createText("ITEMID", lot->item()->id());
        xml.createText("ITEMTYPE", QString(lot->itemType()->id()));
        xml.createText("COLOR", QString::number(lot->color()->id()));
        xml.createText("CATEGORY", QString::number(lot->category()->id()));
        xml.createText("QTY", QString::number(lot->quantity()));
        xml.createText("PRICE", c.toCurrencyString(lot->price(), {}, 3));
        xml.createText("CONDITION", (lot->condition() == BrickLink::Condition::New) ? u"N" : u"U");

        if (lot->bulkQuantity() != 1)   xml.createText("BULK", QString::number(lot->bulkQuantity()));
        if (lot->sale())                xml.createText("SALE", QString::number(lot->sale()));
        if (!lot->comments().isEmpty()) xml.createText("DESCRIPTION", lot->comments());
        if (!lot->remarks().isEmpty())  xml.createText("REMARKS", lot->remarks());
        if (lot->retain())              xml.createText("RETAIN", u"Y");
        if (!lot->reserved().isEmpty()) xml.createText("BUYERUSERNAME", lot->reserved());
        if (!qFuzzyIsNull(lot->cost())) xml.createText("MYCOST", c.toCurrencyString(lot->cost(), {}, 3));
        if (lot->hasCustomWeight())     xml.createText("MYWEIGHT", QString::number(lot->weight(), 'f', 4));

        if (lot->tierQuantity(0)) {
            xml.createText("TQ1", QString::number(lot->tierQuantity(0)));
            xml.createText("TP1", c.toCurrencyString(lot->tierPrice(0), {}, 3));
            xml.createText("TQ2", QString::number(lot->tierQuantity(1)));
            xml.createText("TP2", c.toCurrencyString(lot->tierPrice(1), {}, 3));
            xml.createText("TQ3", QString::number(lot->tierQuantity(2)));
            xml.createText("TP3", c.toCurrencyString(lot->tierPrice(2), {}, 3));
        }

        if (lot->subCondition() != BrickLink::SubCondition::None) {
            const char16_t *st = nullptr;
            switch (lot->subCondition()) {
            case BrickLink::SubCondition::Incomplete: st = u"I"; break;
            case BrickLink::SubCondition::Complete  : st = u"C"; break;
            case BrickLink::SubCondition::Sealed    : st = u"S"; break;
            default                                 : break;
            }
            if (st)
                xml.createText("SUBCONDITION", st);
        }
        if (lot->stockroom() != BrickLink::Stockroom::None) {
            const char16_t *st = nullptr;
            switch (lot->stockroom()) {
            case BrickLink::Stockroom::A: st = u"A"; break;
            case BrickLink::Stockroom::B: st = u"B"; break;
            case BrickLink::Stockroom::C: st = u"C"; break;
            default                     : break;
            }
            if (st) {
                xml.createText("STOCKROOM", u"Y");
                xml.createText("STOCKROOMID", st);
            }
        }
    }
    return xml.toString();
}

DocumentIO::BsxContents DocumentIO::fromBrickLinkXML(const QByteArray &xml)
{
    BsxContents bsx;

    auto buf = new QBuffer(const_cast<QByteArray *>(&xml), nullptr);
    buf->open(QIODevice::ReadOnly);

    XmlHelpers::ParseXML p(buf, "INVENTORY", "ITEM");
    p.parse([&p, &bsx](QDomElement e) {
        const QString itemId = p.elementText(e, "ITEMID");
        char itemTypeId = XmlHelpers::firstCharInString(p.elementText(e, "ITEMTYPE"));
        uint colorId = p.elementText(e, "COLOR", "0").toUInt();
        uint categoryId = p.elementText(e, "CATEGORY", "0").toUInt();
        int qty = p.elementText(e, "MINQTY", "-1").toInt();
        if (qty < 0) {
            // The remove(',') stuff is a workaround for the broken Order XML generator: the QTY
            // field is generated with thousands-separators enabled (e.g. 1,752 instead of 1752)
            qty = p.elementText(e, "QTY", "0").remove(QChar(',')).toInt();
        }
        double price = p.elementText(e, "MAXPRICE", "-1").toDouble();
        if (price < 0)
            price = p.elementText(e, "PRICE", "0").toDouble();
        auto cond = p.elementText(e, "CONDITION", "N") == qL1S("N") ? BrickLink::Condition::New
                                                                             : BrickLink::Condition::Used;
        int bulk = p.elementText(e, "BULK", "1").toInt();
        int sale = p.elementText(e, "SALE", "0").toInt();
        QString comments = p.elementText(e, "DESCRIPTION", "");
        QString remarks = p.elementText(e, "REMARKS", "");
        bool retain = (p.elementText(e, "RETAIN", "") == qL1S("Y"));
        QString reserved = p.elementText(e, "BUYERUSERNAME", "");
        double cost = p.elementText(e, "MYCOST", "0").toDouble();
        double weight = p.elementText(e, "MYWEIGHT", "0").toDouble();
        if (qFuzzyIsNull(weight))
            weight = p.elementText(e, "ITEMWEIGHT", "0").toDouble();
        uint lotId = p.elementText(e, "LOTID", "").toUInt();
        QString subCondStr = p.elementText(e, "SUBCONDITION", "");
        auto subCond = (subCondStr == qL1S("I") ? BrickLink::SubCondition::Incomplete :
                        subCondStr == qL1S("C") ? BrickLink::SubCondition::Complete :
                        subCondStr == qL1S("S") ? BrickLink::SubCondition::Sealed
                                                         : BrickLink::SubCondition::None);
        auto stockroom = p.elementText(e, "STOCKROOM", "")
                == qL1S("Y") ? BrickLink::Stockroom::A : BrickLink::Stockroom::None;
        if (stockroom != BrickLink::Stockroom::None) {
            QString stockroomId = p.elementText(e, "STOCKROOMID", "");
            stockroom = (stockroomId == qL1S("A") ? BrickLink::Stockroom::A :
                         stockroomId == qL1S("B") ? BrickLink::Stockroom::B :
                         stockroomId == qL1S("C") ? BrickLink::Stockroom::C
                                                           : BrickLink::Stockroom::None);
        }
        QString ccode = p.elementText(e, "BASECURRENCYCODE", "");
        if (!ccode.isEmpty()) {
            if (bsx.currencyCode.isEmpty())
                bsx.currencyCode = ccode;
            else if (bsx.currencyCode != ccode)
                throw Exception("Multiple currencies in one XML file are not supported.");
        }

        const BrickLink::Item *item = BrickLink::core()->item(itemTypeId, itemId);
        const BrickLink::Color *color = BrickLink::core()->color(colorId);

        QScopedPointer<BrickLink::Incomplete> inc;

        if (!item || !color) {
            inc.reset(new BrickLink::Incomplete);
            if (!item) {
                inc->m_item_id = itemId;
                inc->m_itemtype_id = itemTypeId;
                if (auto cat = BrickLink::core()->category(categoryId))
                    inc->m_category_name = cat->name();
            }
            if (!color)
                inc->m_color_name = QString::number(colorId);

            if (!BrickLink::core()->applyChangeLog(item, color, inc.get()))
                ++bsx.invalidLotsCount;
        }
        auto *lot = new Lot(color, item);
        if (inc)
            lot->setIncomplete(inc.take());

        lot->setQuantity(qty);
        lot->setPrice(price);
        lot->setCondition(cond);
        lot->setBulkQuantity(bulk);
        lot->setSale(sale);
        lot->setComments(comments);
        lot->setRemarks(remarks);
        lot->setRetain(retain);
        lot->setReserved(reserved);
        lot->setCost(cost);
        lot->setWeight(weight);
        lot->setLotId(lotId);
        lot->setSubCondition(subCond);
        lot->setStockroom(stockroom);

        bsx.lots << lot;
    });

    if (bsx.currencyCode.isEmpty())
        bsx.currencyCode = qL1S("USD");

    return bsx;
}

void DocumentIO::exportBrickLinkXML(const LotList &lots)
{
    QStringList filters;
    filters << tr("BrickLink XML File") + " (*.xml)";

    QString fn = QFileDialog::getSaveFileName(FrameWork::inst(), tr("Export File"), lastDirectory(),
                                              filters.join(";;"));
    if (fn.isEmpty())
        return;
    setLastDirectory(QFileInfo(fn).absolutePath());

    if (fn.right(4) != qL1S(".xml"))
        fn += qL1S(".xml");

    const QByteArray xml = toBrickLinkXML(lots).toUtf8();

    QFile f(fn);
    if (f.open(QIODevice::WriteOnly)) {
        if (f.write(xml.data(), xml.size()) != qint64(xml.size())) {
            MessageBox::warning(nullptr, { }, tr("Failed to save data to file %1.").arg(CMB_BOLD(fn)));
        }
    } else {
        MessageBox::warning(nullptr, { }, tr("Failed to open file %1 for writing.").arg(CMB_BOLD(fn)));
    }
}

void DocumentIO::exportBrickLinkXMLClipboard(const LotList &lots)
{
    const QString xml = toBrickLinkXML(lots);

    QGuiApplication::clipboard()->setText(xml, QClipboard::Clipboard);

    if (Config::inst()->openBrowserOnExport())
        BrickLink::core()->openUrl(BrickLink::URL_InventoryUpload);
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////



bool DocumentIO::resolveIncomplete(Lot *lot)
{
    auto ic = lot->isIncomplete();

    if (!ic->m_item_id.isEmpty() && !ic->m_itemtype_id.isEmpty()) {
        lot->setItem(BrickLink::core()->item(ic->m_itemtype_id[0].toLatin1(),
                      ic->m_item_id.toLatin1()));
        if (lot->item()) {
            ic->m_item_id.clear();
            ic->m_item_name.clear();
            ic->m_itemtype_id.clear();
            ic->m_itemtype_name.clear();
            ic->m_category_id.clear();
            ic->m_category_name.clear();
        }
    }

    if (!ic->m_color_id.isEmpty()) {
        lot->setColor(BrickLink::core()->color(ic->m_color_id.toUInt()));
        if (lot->color()) {
            ic->m_color_id.clear();
            ic->m_color_name.clear();
        }
    }

    if (lot->item() && lot->color()) {
        lot->setIncomplete(nullptr);
        return true;

    } else {
        qWarning() << "failed: insufficient data (item=" << ic->m_item_id << ", itemtype="
           << ic->m_itemtype_id << ", color=" << ic->m_color_id << ")";

        auto item = lot->item();
        auto color = lot->color();

        bool ok = BrickLink::core()->applyChangeLog(item, color, lot->isIncomplete());
        lot->setItem(item);
        lot->setColor(color);
        if (ok) {
            lot->setIncomplete(nullptr);
            return true;
        }
        return ok;
    }
}

DocumentIO::BsxContents DocumentIO::parseBsxInventory(QIODevice *in)
{
    Q_ASSERT(in);
    QXmlStreamReader xml(in);

    try {
        BsxContents bsx;
        bool foundRoot = false;
        bool foundInventory = false;

        auto parseGuiState = [&]() {
            while (xml.readNextStartElement()) {
                const auto tag = xml.name();
                bool isColumnLayout = (tag == qL1S("ColumnLayout"));
                bool isSortFilter = (tag == qL1S("SortFilterState"));

                if (isColumnLayout || isSortFilter) {
                    bool compressed = (xml.attributes().value("Compressed").toInt() == 1);
                    auto ba = QByteArray::fromBase64(xml.readElementText().toLatin1());
                    if (compressed)
                        ba = qUncompress(ba);

                    if (!ba.isEmpty()) {
                        if (isColumnLayout)
                            bsx.guiColumnLayout = ba;
                        if (isSortFilter)
                            bsx.guiSortFilterState = ba;
                    }
                }
            }
        };

        auto parseInventory = [&]() {
            Lot *lot = nullptr;
            QVariant legacyOrigPrice, legacyOrigQty;
            bool hasBaseValues = false;
            QXmlStreamAttributes baseValues;

            const QHash<QStringView, std::function<void(Lot *, const QString &value)>> tagHash {
            { u"ItemID",       [](auto lot, auto v) { lot->isIncomplete()->m_item_id = v; } },
            { u"ColorID",      [](auto lot, auto v) { lot->isIncomplete()->m_color_id = v; } },
            { u"CategoryID",   [](auto lot, auto v) { lot->isIncomplete()->m_category_id = v; } },
            { u"ItemTypeID",   [](auto lot, auto v) { lot->isIncomplete()->m_itemtype_id = v; } },
            { u"ItemName",     [](auto lot, auto v) { lot->isIncomplete()->m_item_name = v; } },
            { u"ColorName",    [](auto lot, auto v) { lot->isIncomplete()->m_color_name = v; } },
            { u"CategoryName", [](auto lot, auto v) { lot->isIncomplete()->m_category_name = v; } },
            { u"ItemTypeName", [](auto lot, auto v) { lot->isIncomplete()->m_itemtype_name = v; } },
            { u"Price",        [](auto lot, auto v) { lot->setPrice(v.toDouble()); } },
            { u"Bulk",         [](auto lot, auto v) { lot->setBulkQuantity(v.toInt()); } },
            { u"Qty",          [](auto lot, auto v) { lot->setQuantity(v.toInt()); } },
            { u"Sale",         [](auto lot, auto v) { lot->setSale(v.toInt()); } },
            { u"Comments",     [](auto lot, auto v) { lot->setComments(v); } },
            { u"Remarks",      [](auto lot, auto v) { lot->setRemarks(v); } },
            { u"TQ1",          [](auto lot, auto v) { lot->setTierQuantity(0, v.toInt()); } },
            { u"TQ2",          [](auto lot, auto v) { lot->setTierQuantity(1, v.toInt()); } },
            { u"TQ3",          [](auto lot, auto v) { lot->setTierQuantity(2, v.toInt()); } },
            { u"TP1",          [](auto lot, auto v) { lot->setTierPrice(0, v.toDouble()); } },
            { u"TP2",          [](auto lot, auto v) { lot->setTierPrice(1, v.toDouble()); } },
            { u"TP3",          [](auto lot, auto v) { lot->setTierPrice(2, v.toDouble()); } },
            { u"LotID",        [](auto lot, auto v) { lot->setLotId(v.toUInt()); } },
            { u"Retain",       [](auto lot, auto v) { lot->setRetain(v.isEmpty() || (v == qL1S("Y"))); } },
            { u"Reserved",     [](auto lot, auto v) { lot->setReserved(v); } },
            { u"TotalWeight",  [](auto lot, auto v) { lot->setTotalWeight(v.toDouble()); } },
            { u"Cost",         [](auto lot, auto v) { lot->setCost(v.toDouble()); } },
            { u"Condition",    [](auto lot, auto v) {
                lot->setCondition(v == qL1S("N") ? BrickLink::Condition::New
                                                 : BrickLink::Condition::Used); } },
            { u"SubCondition", [](auto lot, auto v) {
                // 'M' for sealed is an historic artefact. BL called this 'MISB' back in the day
                lot->setSubCondition(v == qL1S("C") ? BrickLink::SubCondition::Complete :
                                                      v == qL1S("I") ? BrickLink::SubCondition::Incomplete :
                                                      v == qL1S("M") ? BrickLink::SubCondition::Sealed
                                                                     : BrickLink::SubCondition::None); } },
            { u"Status",       [](auto lot, auto v) {
                lot->setStatus(v == qL1S("X") ? BrickLink::Status::Exclude :
                                                 v == qL1S("I") ? BrickLink::Status::Include :
                                                                  v == qL1S("E") ? BrickLink::Status::Extra
                                                                                 : BrickLink::Status::Include); } },
            { u"Stockroom",    [](auto lot, auto v) {
                lot->setStockroom(v == qL1S("A") || v.isEmpty() ? BrickLink::Stockroom::A :
                                                 v == qL1S("B") ? BrickLink::Stockroom::B :
                                                 v == qL1S("C") ? BrickLink::Stockroom::C
                                                                : BrickLink::Stockroom::None); } },
            { u"OrigPrice",    [&legacyOrigPrice](auto lot, auto v) {
                Q_UNUSED(lot)
                legacyOrigPrice.setValue(v.toDouble());
            } },
            { u"OrigQty",      [&legacyOrigQty](auto lot, auto v) {
                Q_UNUSED(lot)
                legacyOrigQty.setValue(v.toInt());
            } },
            { u"X-DifferenceModeBase", [&bsx](auto lot, auto v) {
                //TODO: Remove in 2021.4.1
                const auto ba = QByteArray::fromBase64(v.toLatin1());
                QDataStream ds(ba);
                if (auto base = Lot::restore(ds)) {
                    bsx.differenceModeBase.insert(lot, *base);
                    delete base;
                }
            } } };

            while (xml.readNextStartElement()) {
                if (xml.name() != qL1S("Item"))
                    throw Exception("Expected Item element, but got: %1").arg(xml.name());

                lot = new Lot();
                lot->setIncomplete(new BrickLink::Incomplete);
                baseValues.clear();

                while (xml.readNextStartElement()) {
                    auto tag = xml.name();

                    if (tag != qL1S("DifferenceBaseValues")) {
                        auto it = tagHash.find(tag);
                        if (it != tagHash.end())
                            (*it)(lot, xml.readElementText());
                        else
                            xml.skipCurrentElement();
                    } else {
                        hasBaseValues = true;
                        baseValues = xml.attributes();
                        xml.skipCurrentElement();
                    }
                }

                if (!resolveIncomplete(lot))
                    bsx.invalidLotsCount++;

                // convert the legacy OrigQty / OrigPrice fields
                if (!hasBaseValues && (legacyOrigPrice.isValid() || legacyOrigQty.isValid())) {
                    if (legacyOrigQty.isValid())
                        baseValues.append("Qty", QString::number(legacyOrigQty.toInt()));
                    if (!legacyOrigPrice.isNull())
                        baseValues.append("Price", QString::number(legacyOrigPrice.toDouble(), 'f', 3));
                }

                if (!bsx.differenceModeBase.contains(lot)) { // remove condition in 2021.4.1
                    Lot base = *lot;
                    if (!baseValues.isEmpty()) {
                        base.setIncomplete(new BrickLink::Incomplete);

                        for (int i = 0; i < baseValues.size(); ++i) {
                            auto attr = baseValues.at(i);
                            auto it = tagHash.find(attr.name());
                            if (it != tagHash.end()) {
                                (*it)(&base, attr.value().toString());
                            }
                        }
                        if (!resolveIncomplete(&base)) {
                            if (!base.item() && lot->item())
                                base.setItem(lot->item());
                            if (!base.color() && lot->color())
                                base.setColor(lot->color());
                            base.setIncomplete(nullptr);
                        }
                    }
                    bsx.differenceModeBase.insert(lot, base);
                }

                bsx.lots.append(lot);
            }
        };


        // #################### XML PARSING STARTS HERE #####################

        // In an ideal world, BrickStock wouldn't have changed the root tag.
        // DTDs are not required anymore, but if there is a DTD, it has to be correct

        static const QVector<QString> knownTypes { qL1S("BrickStoreXML"), qL1S("BrickStockXML") };

        while (true) {
            switch (xml.readNext()) {
            case QXmlStreamReader::DTD: {
                auto dtd = xml.text().toString().trimmed();

                if (!dtd.startsWith(qL1S("<!DOCTYPE "))
                        || !dtd.endsWith(qL1S(">"))
                        || !knownTypes.contains(dtd.mid(10).chopped(1))) {
                    throw Exception("Expected BrickStoreXML as DOCTYPE, but got: %1").arg(dtd);
                }
                break;
            }
            case QXmlStreamReader::StartElement:
                if (!foundRoot) { // check the root element
                    if (!knownTypes.contains(xml.name().toString()))
                        throw Exception("Expected BrickStoreXML as root element, but got: %1").arg(xml.name());
                    foundRoot = true;
                } else {
                    if (xml.name() == qL1S("Inventory")) {
                        foundInventory = true;
                        bsx.currencyCode = xml.attributes().value(qL1S("Currency")).toString();
                        parseInventory();
                    } else if ((xml.name() == qL1S("GuiState"))
                                && (xml.attributes().value(qL1S("Application")) == qL1S("BrickStore"))
                                && (xml.attributes().value(qL1S("Version")).toInt() == 2)) {
                        parseGuiState();
                    } else {
                        xml.skipCurrentElement();
                    }
                }
                break;

            case QXmlStreamReader::Invalid:
                throw Exception(xml.errorString());

            case QXmlStreamReader::EndDocument:
                if (!foundRoot || !foundInventory)
                    throw Exception("Not a valid BrickStoreXML file");
                return bsx;

            default:
                break;
            }
        }
    } catch (const Exception &e) {
        throw Exception("XML parse error at line %1, column %2: %3")
                .arg(xml.lineNumber()).arg(xml.columnNumber()).arg(e.error());
    }
}


bool DocumentIO::createBsxInventory(QIODevice *out, const BsxContents &bsx)
{
    if (!out)
        return false;

    QXmlStreamWriter xml(out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();

    // We don't write a DOCTYPE anymore, because RelaxNG wants the schema independent from
    // the actual XML file. As a side effect, BSX files can now be opened directly in Excel.

    xml.writeStartElement(qL1S("BrickStoreXML"));
    xml.writeStartElement(qL1S("Inventory"));
    xml.writeAttribute(qL1S("Currency"), bsx.currencyCode);

    const Lot *lot;
    const Lot *base;
    QXmlStreamAttributes baseValues;

    enum { Required = 0, Optional = 1, Constant = 2, WriteEmpty = 8 };

    auto create = [&](QStringView tagName, auto getter, auto stringify,
            int flags = Required, const QVariant &def = { }) {

        auto v = (lot->*getter)();
        const QString t = tagName.toString();
        const bool writeEmpty = (flags & WriteEmpty);
        const bool optional = ((flags & ~WriteEmpty) == Optional);
        const bool constant = ((flags & ~WriteEmpty) == Constant);

        if (!optional || (QVariant::fromValue(v) != def)) {
            if (writeEmpty)
                xml.writeEmptyElement(t);
            else
                xml.writeTextElement(t, stringify(v));
        }
        if (!constant && base) {
            auto bv = (base->*getter)();
            if (QVariant::fromValue(v) != QVariant::fromValue(bv))
                baseValues.append(t, stringify(bv));
        }
    };
    static auto asString   = [](const QString &s) { return s; };
    static auto asInt      = [](auto i)           { return QString::number(i); };
    static auto asCurrency = [](double d)         { return QString::number(d, 'f', 3); };


    for (const auto *loopLot : bsx.lots) {
        lot = loopLot;
        auto &baseRef = qAsConst(bsx.differenceModeBase)[lot];
        base = &baseRef;
        baseValues.clear();

        xml.writeStartElement(qL1S("Item"));

        // vvv Required Fields (part 1)
        create(u"ItemID",       &Lot::itemId,       asString);
        create(u"ItemTypeID",   &Lot::itemTypeId,   asString);
        create(u"ColorID",      &Lot::colorId,      asString);

        // vvv Redundancy Fields

        // this extra information is useful, if the e.g.the color- or item-id
        // are no longer available after a database update
        create(u"ItemName",     &Lot::itemName,     asString);
        create(u"ItemTypeName", &Lot::itemTypeName, asString);
        create(u"ColorName",    &Lot::colorName,    asString);
        create(u"CategoryID",   &Lot::categoryId,   asString);
        create(u"CategoryName", &Lot::categoryName, asString);

        // vvv Required Fields (part 2)

        create(u"Status", &Lot::status, [](auto st) {
            switch (st) {
            default                        :
            case BrickLink::Status::Exclude: return qL1S("X");
            case BrickLink::Status::Include: return qL1S("I");
            case BrickLink::Status::Extra  : return qL1S("E");
            }
        });

        create(u"Qty",       &Lot::quantity,     asInt);
        create(u"Price",     &Lot::price,        asCurrency);
        create(u"Condition", &Lot::condition, [](auto c) {
            return qL1S((c == BrickLink::Condition::New) ? "N" : "U"); });

        // vvv Optional Fields (part 2)

        create(u"SubCondition", &Lot::subCondition,
                [](auto sc) {
            // 'M' for sealed is an historic artefact. BL called this 'MISB' back in the day
            switch (sc) {
            case BrickLink::SubCondition::Incomplete: return qL1S("I");
            case BrickLink::SubCondition::Complete  : return qL1S("C");
            case BrickLink::SubCondition::Sealed    : return qL1S("M");
            default                                 : return qL1S("N");
            }
        }, Optional, QVariant::fromValue(BrickLink::SubCondition::None));

        create(u"Bulk",      &Lot::bulkQuantity,  asInt,      Optional, 1);
        create(u"Sale",      &Lot::sale,          asInt,      Optional, 0);
        create(u"Cost",      &Lot::cost,          asCurrency, Optional, 0);
        create(u"Comments",  &Lot::comments,      asString,   Optional, QString());
        create(u"Remarks",   &Lot::remarks,       asString,   Optional, QString());
        create(u"Reserved",  &Lot::reserved,      asString,   Optional, QString());
        create(u"LotID",     &Lot::lotId,         asInt,      Optional, 0);
        create(u"TQ1",       &Lot::tierQuantity0, asInt,      Optional, 0);
        create(u"TP1",       &Lot::tierPrice0,    asCurrency, Optional, 0);
        create(u"TQ2",       &Lot::tierQuantity1, asInt,      Optional, 0);
        create(u"TP2",       &Lot::tierPrice1,    asCurrency, Optional, 0);
        create(u"TQ3",       &Lot::tierQuantity2, asInt,      Optional, 0);
        create(u"TP3",       &Lot::tierPrice2,    asCurrency, Optional, 0);

        create(u"Retain", &Lot::retain, [](bool b) {
            return qL1S(b ? "Y" : "N"); }, Optional | WriteEmpty, false);

        create(u"Stockroom", &Lot::stockroom, [](auto st) {
            switch (st) {
            case BrickLink::Stockroom::A: return qL1S("A");
            case BrickLink::Stockroom::B: return qL1S("B");
            case BrickLink::Stockroom::C: return qL1S("C");
            default                     : return qL1S("N");
            }
        }, Optional, QVariant::fromValue(BrickLink::Stockroom::None));

        if (lot->hasCustomWeight()) {
            create(u"TotalWeight", &Lot::totalWeight, [](double d) {
                return QString::number(d, 'f', 4); }, Required);
        }

        if (base && !baseValues.isEmpty()) {
            xml.writeStartElement(qL1S("DifferenceBaseValues"));
            xml.writeAttributes(baseValues);
            xml.writeEndElement(); // DifferenceBaseValues
        }
        xml.writeEndElement(); // Item
    }

    xml.writeEndElement(); // Inventory

    xml.writeStartElement(qL1S("GuiState"));
    xml.writeAttribute(qL1S("Application"), qL1S("BrickStore"));
    xml.writeAttribute(qL1S("Version"), QString::number(2));
    if (!bsx.guiColumnLayout.isEmpty()) {
        xml.writeStartElement("ColumnLayout");
        xml.writeAttribute("Compressed", "1");
        xml.writeCDATA(QLatin1String(qCompress(bsx.guiColumnLayout).toBase64()));
        xml.writeEndElement(); // ColumnLayout
    }
    if (!bsx.guiSortFilterState.isEmpty()) {
        xml.writeStartElement("SortFilterState");
        xml.writeAttribute("Compressed", "1");
        xml.writeCDATA(QLatin1String(qCompress(bsx.guiSortFilterState).toBase64()));
        xml.writeEndElement(); // SortFilterState
    }
    xml.writeEndElement(); // GuiState

    xml.writeEndElement(); // BrickStoreXML
    xml.writeEndDocument();
    return !xml.hasError();
}
