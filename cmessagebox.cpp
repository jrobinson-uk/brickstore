/* Copyright (C) 2013-2014 Patrick Brans.  All rights reserved.
**
** This file is part of BrickStock.
** BrickStock is based heavily on BrickStore (http://www.brickforge.de/software/brickstore/)
** by Robert Griebl, Copyright (C) 2004-2008.
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
#include <limits.h>
#include <float.h>

#include <qapplication.h>
#include <qvalidator.h>
#include <qpushbutton.h>
#include <qlineedit.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qdialog.h>
//Added by qt3to4:
#include <Q3BoxLayout>
#include <Q3HBoxLayout>
#include <Q3VBoxLayout>
#include <Q3Frame>

#include "cmessagebox.h"


CMessageBox::CMessageBox ( )
	: QMessageBox ( )
{ }

QString CMessageBox::s_deftitle;

void CMessageBox::setDefaultTitle ( const QString &s )
{
	s_deftitle = s;
}

QString CMessageBox::defaultTitle ( )
{
	return s_deftitle;
}

int CMessageBox::msgbox ( QWidget *parent, const QString &msg, QMessageBox::Icon icon, int but0, int but1, int but2 )
{
    QMessageBox *mb = new QMessageBox ( s_deftitle, msg, icon, but0, but1, but2, parent ? parent : qApp-> mainWidget ( ), "brickstock_msgbox", true, Qt::WDestructiveClose );

    mb-> setTextFormat ( Qt::RichText );

	return mb-> exec ( );
}

int CMessageBox::information ( QWidget *parent, const QString &text, int button0, int button1, int button2 )
{
	return msgbox ( parent, text, QMessageBox::Information, button0, button1, button2 );
}

int CMessageBox::question ( QWidget *parent, const QString &text, int button0, int button1, int button2 )
{
	return msgbox ( parent, text, QMessageBox::Question, button0, button1, button2 );
}

int CMessageBox::warning ( QWidget *parent, const QString &text, int button0, int button1, int button2 )
{
	return msgbox ( parent, text, QMessageBox::Warning, button0, button1, button2 );
}

int CMessageBox::critical ( QWidget *parent, const QString &text, int button0, int button1, int button2 )
{
	return msgbox ( parent, text, QMessageBox::Critical, button0, button1, button2 );
}

bool CMessageBox::getString ( QWidget *parent, const QString &text, QString &value )
{
	return getString ( parent, text, QString::null, value, 0 );
}

bool CMessageBox::getDouble ( QWidget *parent, const QString &text, const QString &unit, double &value, QValidator *validate )
{
	QString str_value = QString::number ( value );
	bool b = getString ( parent, text, unit, str_value, validate ? validate : new QDoubleValidator ( -DBL_MAX, DBL_MAX, 3, 0 ));

	if ( b )
		value = str_value. toDouble ( );
	return b;
}

bool CMessageBox::getInteger ( QWidget *parent, const QString &text, const QString &unit, int &value, QValidator *validate )
{
	QString str_value = QString::number ( value );
	bool b = getString ( parent, text, unit, str_value, validate ? validate : new QIntValidator ( INT_MIN, INT_MAX, 0 ));

	if ( b )
		value = str_value. toInt ( );
	return b;
}

bool CMessageBox::getString ( QWidget *parent, const QString &text, const QString &unit, QString &value, QValidator *validate )
{
	bool b = false;

	QDialog *d = new QDialog ( parent, "getbox", true );
	d-> setCaption ( s_deftitle );

	QLabel *wlabel = new QLabel ( text, d );
	wlabel-> setTextFormat( Qt::RichText );
	QLabel *wunit = unit. isEmpty ( ) ? 0 : new QLabel ( unit, d );
	Q3Frame *wline = new Q3Frame ( d );
	wline-> setFrameStyle ( Q3Frame::HLine | Q3Frame::Sunken );

	QLineEdit *wedit = new QLineEdit ( value, d );
	wedit-> setValidator ( validate );

	QFontMetrics wefm ( wedit-> font ( ));
	wedit-> setMinimumWidth ( 20 + QMAX( wefm. width ( "Aa0" ) * 3, wefm. width ( value )));
	wedit-> setSizePolicy ( QSizePolicy ( QSizePolicy::Preferred, QSizePolicy::Fixed ));

	QPushButton *wok = new QPushButton ( tr( "&OK" ), d );
	wok-> setAutoDefault ( true );
	wok-> setDefault ( true );

	QPushButton *wcancel = new QPushButton ( tr( "&Cancel" ), d );
	wcancel-> setAutoDefault ( true );

	Q3BoxLayout *toplay = new Q3VBoxLayout ( d, 11, 6 );
	toplay-> addWidget ( wlabel );

	Q3BoxLayout *midlay = new Q3HBoxLayout ( );
	toplay-> addLayout ( midlay );
	toplay-> addWidget ( wline );

	Q3BoxLayout *botlay = new Q3HBoxLayout ( );
	toplay-> addLayout ( botlay );

	midlay-> addWidget ( wedit );
	if ( wunit ) {
		midlay-> addWidget ( wunit );
		midlay-> addStretch ( );
	}

	botlay-> addSpacing ( QFontMetrics ( d-> font ( )). width ( "Aa0" ) * 6 );
	botlay-> addStretch ( );
	botlay-> addWidget ( wok );
	botlay-> addWidget ( wcancel );

	connect ( wedit, SIGNAL( returnPressed ( )), wok, SLOT( animateClick ( )));
	connect ( wok, SIGNAL( clicked ( )), d, SLOT( accept ( )));
	connect ( wcancel, SIGNAL( clicked ( )), d, SLOT( reject ( )));

	d-> setSizeGripEnabled ( true );
	d-> setMinimumSize ( d-> minimumSizeHint ( ));
	d-> resize ( d-> sizeHint ( ));

	if ( d-> exec ( ) == QDialog::Accepted ) {
		b = true;
		value = wedit-> text ( );
	}

	delete d;
	delete validate;
	return b;
}