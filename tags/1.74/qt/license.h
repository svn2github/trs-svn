/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef LICENSE_DIALOG_H
#define LICENSE_DIALOG_H

#include <QDialog>

class LicenseDialog: public QDialog
{
        Q_OBJECT

    public:
        LicenseDialog( QWidget * parent = 0 );
        ~LicenseDialog( ) { }

};

#endif

