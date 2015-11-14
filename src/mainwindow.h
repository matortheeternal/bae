#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>

#include <QStandardItemModel>
#include <QSortFilterProxyModel>


class BSA;
class BSAModel;
class BSAProxyModel;
class ProgressDialog;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow( QWidget * parent = nullptr );
    ~MainWindow();

	void openFile( const QString & filepath );
	void appendFile( const QString & filepath );
	void cancelExtract();

protected slots:
    void openDlg();
	void extract();
	void itemChanged( QStandardItem * item );
    
private:
	void recurseModel( QStandardItem * item, QList<QStandardItem *> & itemList );
    
    Ui::MainWindow * ui;
    
    QTreeView * archiveView;
    
    BSAModel * archiveModel;
    BSAProxyModel * archiveProxyModel;

	QStandardItemModel * emptyModel;
	
	QHash<QString, BSA *> bsaHash;
	
	ProgressDialog * progDlg;
	
	bool process = false;
};

#endif // MAINWINDOW_H
