#include "progressdialog.h"
#include "ui_progressdialog.h"

#include <QPushButton>

ProgressDialog::ProgressDialog( QWidget *parent )
	: QDialog( parent ), ui( new Ui::ProgressDialog )
{
	ui->setupUi( this );

	ui->progressBar->setMinimum( 0 );

	connect( ui->btnCancel, &QPushButton::pressed, this, &ProgressDialog::cancel );
}

ProgressDialog::~ProgressDialog()
{
	delete ui;
}

void ProgressDialog::setNumFiles( int num )
{
	ui->lblNumFiles->setText( QString::number( num ) );

	ui->progressBar->setValue( num );
}

void ProgressDialog::setTotalFiles( int num )
{
	ui->lblTotalFiles->setText( QString::number( num ) );

	ui->progressBar->setMaximum( num );
}

bool ProgressDialog::finished()
{
	return ui->progressBar->value() == ui->progressBar->maximum();
}
