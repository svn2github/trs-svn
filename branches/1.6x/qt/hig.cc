/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>

#include "hig.h"


HIG :: HIG( QWidget * parent ):
    QWidget( parent ),
    myRow( 0 ),
    myGrid( new QGridLayout( this ) )
{
    myGrid->setContentsMargins( PAD_BIG, PAD_BIG, PAD_BIG, PAD_BIG );
    myGrid->setHorizontalSpacing( PAD_BIG );
    myGrid->setVerticalSpacing( PAD );
    myGrid->setColumnStretch ( 1, 1 );
}

HIG :: ~HIG( )
{
    delete myGrid;
}
 
/***
****
***/

void
HIG :: addSectionDivider( )
{
    QWidget * w = new QWidget( this );
    myGrid->addWidget( w, myRow, 0, 1, 2 );
    ++myRow;
}

void
HIG :: addSectionTitle( const QString& title )
{
    QLabel * label = new QLabel( this );
    label->setText( title );
    label->setStyleSheet( "font: bold" );
    label->setAlignment( Qt::AlignLeft|Qt::AlignVCenter );
    addSectionTitle( label );
}

void
HIG :: addSectionTitle( QWidget * w )
{
    myGrid->addWidget( w, myRow, 0, 1, 2, Qt::AlignLeft|Qt::AlignVCenter );
    ++myRow;
}

void
HIG :: addSectionTitle( QLayout * l )
{
    myGrid->addLayout( l, myRow, 0, 1, 2, Qt::AlignLeft|Qt::AlignVCenter );
    ++myRow;
}


QLayout *
HIG :: addRow( QWidget * w )
{
    QHBoxLayout * h = new QHBoxLayout( );
    h->addSpacing( 18 );
    h->addWidget( w );

    QLabel * l;
    if( ( l = qobject_cast<QLabel*>(w) ) )
      l->setAlignment( Qt::AlignLeft );

    return h;
}

void
HIG :: addWideControl( QLayout * l )
{
    QHBoxLayout * h = new QHBoxLayout( );
    h->addSpacing( 18 );
    h->addLayout( l );
    myGrid->addLayout( h, myRow, 0, 1, 2, Qt::AlignLeft|Qt::AlignVCenter );
    ++myRow;
}

void
HIG :: addWideControl( QWidget * w )
{
    QHBoxLayout * h = new QHBoxLayout( );
    h->addSpacing( 18 );
    h->addWidget( w );
    myGrid->addLayout( h, myRow, 0, 1, 2, Qt::AlignLeft|Qt::AlignVCenter );
    ++myRow;
}

QCheckBox*
HIG :: addWideCheckBox( const QString& text, bool isChecked )
{
    QCheckBox * check = new QCheckBox( text, this );
    check->setChecked( isChecked );
    addWideControl( check );
    return check;
}

void
HIG :: addLabel( QWidget * w )
{
    QHBoxLayout * h = new QHBoxLayout( );
    h->addSpacing( 18 );
    h->addWidget( w );
    myGrid->addLayout( h, myRow, 0, 1, 1, Qt::AlignLeft|Qt::AlignVCenter );
}

QLabel*
HIG :: addLabel( const QString& text )
{
    QLabel * label = new QLabel( this );
    label->setText( text );
    label->setAlignment( Qt::AlignLeft );
    addLabel( label );
    return label;
}

void
HIG :: addControl( QWidget * w )
{
    myGrid->addWidget( w, myRow, 1, 1, 1 );
}

void
HIG :: addControl( QLayout * l )
{
    myGrid->addLayout( l, myRow, 1, 1, 1 );
}

QLabel *
HIG :: addRow( const QString& text, QWidget * control, QWidget * buddy )
{
    QLabel * label = addLabel( text );
    addControl( control );
    label->setBuddy( buddy ? buddy : control );
    ++myRow;
    return label;
}

QLabel *
HIG :: addRow( const QString& text, QLayout * control, QWidget * buddy )
{
    QLabel * label = addLabel( text );
    addControl( control );
    if( buddy != 0 )
        label->setBuddy( buddy );
    ++myRow;
    return label;
}

void
HIG :: addRow( QWidget * label, QWidget * control, QWidget * buddy )
{
    addLabel( label );
    if( control ) {
        addControl( control );
        QLabel * l = qobject_cast<QLabel*>( label );
        if( l != 0 )
            l->setBuddy( buddy ? buddy : control );
    }
    ++myRow;
}

void
HIG :: addRow( QWidget * label, QLayout * control, QWidget * buddy )
{
    addLabel( label );
    if( control ) {
        addControl( control );
        QLabel * l = qobject_cast<QLabel*>( label );
        if( l != 0 && buddy != 0 )
            l->setBuddy( buddy );
    }
    ++myRow;
}

void
HIG :: finish( )
{
    QWidget * w = new QWidget( this );
    myGrid->addWidget( w, myRow, 0, 1, 2 );
    myGrid->setRowStretch( myRow, 100 );
    ++myRow;
}
