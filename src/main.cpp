#include "mainwindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStack>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
	QStack<QString> files;

	// Command Line setup
	QCommandLineParser parser;
	// Process options
	parser.process( a );

	int incorrectFiles = 0;
	// Files were passed
	for ( const QString & arg : parser.positionalArguments() ) {
		QFileInfo finfo( arg );

		QRegularExpression ext( "^(bsa|ba2)$", QRegularExpression::CaseInsensitiveOption );
		if ( finfo.exists() && finfo.suffix().contains( ext ) ) {
			files.push( arg );
		} else {
			incorrectFiles++;
		}
	}

    MainWindow w;

	a.setApplicationName( "Bethesda Archive Extractor" );
	a.setApplicationDisplayName( "B.A.E." );

    w.show();

	if ( files.count() && !incorrectFiles ) {
		w.openFile( files.pop() );

		while ( !files.isEmpty()  ) {
			w.appendFile( files.pop() );
		}
	} else if ( incorrectFiles ) {
		// Incorrect files passed
		return 0;
	}

    return a.exec();
}
