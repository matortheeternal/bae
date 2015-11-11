#include "archive.h"
#include "bsa.h"

#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QMutex>
#include <QStringList>

//! \file fsengine.cpp File system engine implementations

// see fsengine.h
ArchiveHandler * ArchiveHandler::openArchive( const QString & fn )
{
	if ( BSA::canOpen( fn ) )
	{
		BSA * bsa = new BSA( fn );
		if ( bsa->open() ) {
			//qDebug() << "BSA Open: " << fn;
			return new ArchiveHandler( bsa );
		} else {
			qDebug() << "didn't open";
		}
		qDebug() << "I/O error:" << fn << ":" << bsa->statusText();
		delete bsa;
	}
	return nullptr;
}

ArchiveHandler::ArchiveHandler( Archive * a )
{
	archive = a;
	archive->ref.ref();
}

ArchiveHandler::~ArchiveHandler()
{
	if ( ! archive->ref.deref() )
		delete archive;
}

