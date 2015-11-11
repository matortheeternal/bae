#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>

namespace Ui {
class ProgressDialog;
}

class ProgressDialog : public QDialog
{
	Q_OBJECT
	
public:
	explicit ProgressDialog( QWidget * parent = nullptr );
	~ProgressDialog();
	
	void setNumFiles( int );
	void setTotalFiles( int );
	bool finished();
	
signals:
	void cancel();
	
private:
	Ui::ProgressDialog * ui;
};

#endif // PROGRESSDIALOG_H
