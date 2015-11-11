#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;

	a.setApplicationName( "Bethesda Archive Extractor" );
	a.setApplicationDisplayName( "B.A.E." );

    w.show();
	
    return a.exec();
}
