#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <QObject>
#include <QAtomicInt>
#include <QDateTime>
#include <QStringList>

class ArchiveHandler
{
public:
	static ArchiveHandler * openArchive( const QString & );
	
public:
	ArchiveHandler( class Archive * a );
	~ArchiveHandler();
	
	template <typename T> T getArchive() const;
	Archive * getArchive() { return archive; }

protected:
	class Archive * archive;
};

template <typename T> inline T ArchiveHandler::getArchive() const
{
	return static_cast<T>(archive);
}


class Archive : public QObject
{
	//Q_OBJECT

public:
	//! Constructor
	Archive() : ref( 0 ) {}
	virtual ~Archive() {}
	
	virtual bool open() = 0;
	virtual void close() = 0;
	
	virtual QString base() const = 0;
	virtual QString name() const = 0;
	virtual QString path() const = 0;
	
	virtual bool hasFolder( const QString & ) const = 0;
	virtual bool hasFile( const QString & ) const = 0;
	virtual qint64 fileSize( const QString & ) const = 0;
	virtual bool fileContents( const QString &, QByteArray & ) = 0;
	virtual QString absoluteFilePath( const QString & ) const = 0;

	virtual uint ownerId( const QString & ) const = 0;
	virtual QString owner( const QString & ) const = 0;
	virtual QDateTime fileTime( const QString & ) const = 0;

protected:
	//! A reference counter for an implicitly shared class
	QAtomicInt ref;
	
	friend class ArchiveHandler;
};

#endif // ARCHIVE_H
