#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>

#include <QStandardItemModel>
#include <QSortFilterProxyModel>


class BSA;
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
	
	void cancelExtract();

protected slots:
    void openDlg();
	void extract();
	void itemChanged( QStandardItem * item );
    
private:
    void openFile( QString filepath );
	
	void recurseModel( QStandardItem * item, QList<QStandardItem *> & itemList );
    
    Ui::MainWindow * ui;
    
    QTreeView * archiveView;
    
    QStandardItemModel * archiveModel;
    QSortFilterProxyModel * archiveProxyModel;

	QStandardItemModel * emptyModel;
	
	BSA * bsa;
	
	ProgressDialog * progDlg;
	
	bool process = false;
};

#endif // MAINWINDOW_H
