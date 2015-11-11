#include "bsa.h"
#include "zlib.h"
#include "dds.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>


quint32 BSA::BSAFile::size() const
{
	if ( sizeFlags > 0 ) {
		// Skyrim and earlier
		return sizeFlags & OB_BSAFILE_SIZEMASK;
	}

	return (packedLength == 0) ? unpackedLength : packedLength;
}

bool BSA::BSAFile::compressed() const
{
	return sizeFlags & OB_BSAFILE_FLAG_COMPRESS;
}

//! Reads a foldername sized string (length + null-terminated string) from the BSA
static bool BSAReadSizedString( QFile & bsa, QString & s )
{
	//qDebug() << "BSA is at" << bsa.pos();
	quint8 len;
	if ( bsa.read( (char *)&len, 1 ) != 1 ) {
		//qDebug() << "bailout on" << __FILE__ << "line" << __LINE__;
		return false;
	}
	//qDebug() << "folder string length is" << len;

	QByteArray b( len, char( 0 ) );
	if ( bsa.read( b.data(), len ) == len ) {
		s = b;
		//qDebug() << "bailout on" << __FILE__ << "line" << __LINE__;
		return true;
	} else {
		//qDebug() << "bailout on" << __FILE__ << "line" << __LINE__;
		return false;
	}
}

QByteArray gUncompress( const QByteArray &data )
{
	if ( data.size() <= 4 ) {
		qWarning( "gUncompress: Input data is truncated" );
		return QByteArray();
	}

	QByteArray result;

	int ret;
	z_stream strm;
	static const int CHUNK_SIZE = 1024;
	char out[CHUNK_SIZE];

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = data.size();
	strm.next_in = (Bytef*)(data.data());

	ret = inflateInit2( &strm, 15 + 32 ); // gzip decoding
	if ( ret != Z_OK )
		return QByteArray();

	// run inflate()
	do {
		strm.avail_out = CHUNK_SIZE;
		strm.next_out = (Bytef*)(out);

		ret = inflate( &strm, Z_NO_FLUSH );
		Q_ASSERT( ret != Z_STREAM_ERROR );  // state not clobbered

		switch ( ret ) {
		case Z_NEED_DICT:
			ret = Z_DATA_ERROR;     // and fall through
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			(void)inflateEnd( &strm );
			return QByteArray();
		}

		result.append( out, CHUNK_SIZE - strm.avail_out );
	} while ( strm.avail_out == 0 );

	// clean up and return
	inflateEnd( &strm );
	return result;
}

BSA::BSA( const QString & filename )
	: Archive(), bsa( filename ), bsaInfo( QFileInfo( filename ) ), status( "initialized" )
{
	bsaPath = bsaInfo.absolutePath() + "/" + bsaInfo.fileName();
	bsaBase = bsaInfo.absolutePath();
	bsaName = bsaInfo.fileName();
}

BSA::~BSA()
{
	close();
}

bool BSA::canOpen( const QString & fn )
{
	QFile f( fn );
	if ( f.open( QIODevice::ReadOnly ) ) {
		quint32 magic, version;

		if ( f.read( (char *)&magic, sizeof( magic ) ) != 4 )
			return false;

		//qDebug() << "Magic:" << QString::number( magic, 16 );
		if ( magic == F4_BSAHEADER_FILEID ) {
			if ( f.read( (char *)&version, sizeof( version ) ) != 4 )
				return false;
			return version == F4_BSAHEADER_VERSION;
		} else if ( magic == OB_BSAHEADER_FILEID ) {
			if ( f.read( (char *)&version, sizeof( version ) ) != 4 )
				return false;

			return (version == OB_BSAHEADER_VERSION || version == F3_BSAHEADER_VERSION);
		} else {
			return magic == MW_BSAHEADER_FILEID;
		}

	}

	return false;
}

bool BSA::open()
{
	QMutexLocker lock( &bsaMutex );

	try {
		if ( !bsa.open( QIODevice::ReadOnly ) )
			throw QString( "file open" );

		quint32 magic, version;

		bsa.read( (char*)&magic, sizeof( magic ) );

		if ( magic == F4_BSAHEADER_FILEID ) {
			bsa.read( (char*)&version, sizeof( version ) );

			if ( version != F4_BSAHEADER_VERSION )
				throw QString( "file version" );

			F4BSAHeader header;
			if ( bsa.read( (char *)&header, sizeof( header ) ) != sizeof( header ) )
				throw QString( "header size" );

			numFiles = header.numFiles;

			QVector<QString> filepaths;
			if ( bsa.seek( header.nameTableOffset ) ) {
				for ( quint32 i = 0; i < header.numFiles; i++ ) {
					quint8 length;
					bsa.read( (char*)&length, 2 );

					QByteArray strdata( length, char( 0 ) );
					bsa.read( strdata.data(), length );

					filepaths.append( QString( strdata ) );
				}
			}

			QString h = QString::fromLatin1( header.type, 4 );
			if ( h == "GNRL" ) {
				// General BA2 Format
				if ( bsa.seek( sizeof( header ) + 8 ) ) {
					for ( quint32 i = 0; i < header.numFiles; i++ ) {
						F4GeneralInfo finfo;
						bsa.read( (char*)&finfo, 36 );

						QString fullpath = filepaths[i];
						fullpath.replace( "\\", "/" );

						QString filename = fullpath.right( fullpath.length() - fullpath.lastIndexOf( "/" ) - 1 );
						QString folderName = fullpath.left( fullpath.lastIndexOf( "/" ) );

						BSAFolder * folder = insertFolder( folderName );

						insertFile( folder, filename, finfo.packedSize, finfo.unpackedSize, finfo.offset );
					}
				}
			} else if ( h == "DX10" ) {
				// Texture BA2 Format
				if ( bsa.seek( sizeof( header ) + 8 ) ) {
					for ( quint32 i = 0; i < header.numFiles; i++ ) {
						F4Tex tex;
						bsa.read( (char*)&tex.header, 24 );

						QVector<F4TexChunk> texChunks;
						for ( quint32 j = 0; j < tex.header.numChunks; j++ ) {
							F4TexChunk texChunk;
							bsa.read( (char*)&texChunk, 24 );
							texChunks.append( texChunk );
						}

						tex.chunks = texChunks;

						QString fullpath = filepaths[i];
						fullpath.replace( "\\", "/" );

						QString filename = fullpath.right( fullpath.length() - fullpath.lastIndexOf( "/" ) - 1 );
						QString folderName = fullpath.left( fullpath.lastIndexOf( "/" ) );

						BSAFolder * folder = insertFolder( folderName );

						F4TexChunk chunk = tex.chunks[0];

						insertFile( folder, filename, chunk.packedSize, chunk.unpackedSize, chunk.offset, tex );
					}
				}
			}
		}
		// From NifSkope
		else if ( magic == OB_BSAHEADER_FILEID ) {
			bsa.read( (char*)&version, sizeof( version ) );

			if ( version != OB_BSAHEADER_VERSION && version != F3_BSAHEADER_VERSION )
				throw QString( "file version" );

			OBBSAHeader header;

			if ( bsa.read( (char *)&header, sizeof( header ) ) != sizeof( header ) )
				throw QString( "header size" );

			numFiles = header.FileCount;

			//qDebug() << bsaName << header;

			if ( (header.ArchiveFlags & OB_BSAARCHIVE_PATHNAMES) == 0 || (header.ArchiveFlags & OB_BSAARCHIVE_FILENAMES) == 0 )
				throw QString( "header flags" );

			compressToggle = header.ArchiveFlags & OB_BSAARCHIVE_COMPRESSFILES;

			if ( version == F3_BSAHEADER_VERSION ) {
				namePrefix = header.ArchiveFlags & F3_BSAARCHIVE_PREFIXFULLFILENAMES;
			} else {
				namePrefix = false;
			}

			if ( !bsa.seek( header.FolderRecordOffset + header.FolderNameLength + header.FolderCount * (1 + sizeof( OBBSAFolderInfo )) + header.FileCount * sizeof( OBBSAFileInfo ) ) )
				throw QString( "file name seek" );

			QByteArray fileNames( header.FileNameLength, char( 0 ) );
			if ( bsa.read( fileNames.data(), header.FileNameLength ) != header.FileNameLength )
				throw QString( "file name read" );
			quint32 fileNameIndex = 0;

			//qDebug() << bsa.pos() - header.FileNameLength << fileNames;

			if ( !bsa.seek( header.FolderRecordOffset ) )
				throw QString( "folder info seek" );

			QVector<OBBSAFolderInfo> folderInfos( header.FolderCount );
			if ( bsa.read( (char *)folderInfos.data(), header.FolderCount * sizeof( OBBSAFolderInfo ) ) != header.FolderCount * sizeof( OBBSAFolderInfo ) )
				throw QString( "folder info read" );

			quint32 totalFileCount = 0;

			for ( const OBBSAFolderInfo folderInfo : folderInfos ) {
				// useless?
				/*
				qDebug() << __LINE__ << "position" << bsa.pos() << "offset" << folderInfo.offset;
				if ( folderInfo.offset < header.FileNameLength || ! bsa.seek( folderInfo.offset - header.FileNameLength ) )
				throw QString( "folder content seek" );
				*/


				QString folderName;
				if ( !BSAReadSizedString( bsa, folderName ) || folderName.isEmpty() ) {
					//qDebug() << "folderName" << folderName;
					throw QString( "folder name read" );
				}


				BSAFolder * folder = insertFolder( folderName );

				qDebug() << folderName;

				quint32 fcnt = folderInfo.fileCount;
				totalFileCount += fcnt;
				QVector<OBBSAFileInfo> fileInfos( fcnt );
				if ( bsa.read( (char *)fileInfos.data(), fcnt * sizeof( OBBSAFileInfo ) ) != fcnt * sizeof( OBBSAFileInfo ) )
					throw QString( "file info read" );

				for ( const OBBSAFileInfo fileInfo : fileInfos ) {
					if ( fileNameIndex >= header.FileNameLength )
						throw QString( "file name size" );

					QString fileName = (fileNames.data() + fileNameIndex);
					fileNameIndex += fileName.length() + 1;

					insertFile( folder, fileName, fileInfo.sizeFlags, fileInfo.offset );
				}
			}

			if ( totalFileCount != header.FileCount )
				throw QString( "file count" );
		} else if ( magic == MW_BSAHEADER_FILEID ) {
			MWBSAHeader header;

			if ( bsa.read( (char *)&header, sizeof( header ) ) != sizeof( header ) )
				throw QString( "header" );

			numFiles = header.FileCount;
			compressToggle = false;
			namePrefix = false;

			// header is 12 bytes, hash table is 8 bytes per entry
			quint32 dataOffset = 12 + header.HashOffset + header.FileCount * 8;

			// file size/offset table
			QVector<MWBSAFileSizeOffset> sizeOffset( header.FileCount );
			if ( bsa.read( (char *)sizeOffset.data(), header.FileCount * sizeof( MWBSAFileSizeOffset ) ) != header.FileCount * sizeof( MWBSAFileSizeOffset ) )
				throw QString( "file size/offset" );

			// filename offset table
			QVector<quint32> nameOffset( header.FileCount );
			if ( bsa.read( (char *)nameOffset.data(), header.FileCount * sizeof( quint32 ) ) != header.FileCount * sizeof( quint32 ) )
				throw QString( "file name offset" );

			// filenames. size is given by ( HashOffset - ( 8 * number of file/size offsets) - ( 4 * number of filenames) )
			// i.e. ( HashOffset - ( 12 * number of files ) )
			QByteArray fileNames;
			fileNames.resize( header.HashOffset - 12 * header.FileCount );
			if ( bsa.read( (char *)fileNames.data(), header.HashOffset - 12 * header.FileCount ) != header.HashOffset - 12 * header.FileCount )
				throw QString( "file names" );

			// table of 8 bytes of hash values follow, but we don't need to know what they are
			// file data follows that, which is fetched by fileContents

			for ( quint32 c = 0; c < header.FileCount; c++ ) {
				QString fname = fileNames.data() + nameOffset[c];
				QString dname;
				int x = fname.lastIndexOf( "\\" );
				if ( x > 0 ) {
					dname = fname.left( x );
					fname = fname.remove( 0, x + 1 );
				}

				// qDebug() << "inserting" << dname << fname;

				insertFile( insertFolder( dname ), fname, sizeOffset[c].size, dataOffset + sizeOffset[c].offset );
			}
		} else
			throw QString( "file magic" );
	}
	catch ( QString e ) {
		status = e;
		return false;
	}

	status = "loaded successful";

	return true;
}

void BSA::close()
{
	QMutexLocker lock( &bsaMutex );

	bsa.close();
	qDeleteAll( root.children );
	qDeleteAll( root.files );
	root.children.clear();
	root.files.clear();
	folders.clear();
}

qint64 BSA::fileCount() const
{
	return numFiles;
}

qint64 BSA::fileSize( const QString & fn ) const
{
	// note: lazy size count (not accurate for compressed files)
	if ( const BSAFile * file = getFile( fn ) ) {
		if ( file->sizeFlags > 0 )
			return file->size();

		qint64 size = file->unpackedLength;

		if ( file->tex.chunks.count() ) {
			for ( int i = 1; i < file->tex.chunks.count(); i++ ) {
				size += file->tex.chunks[i].unpackedSize;
			}
		}

		return size;
	}
	return 0;
}

bool BSA::fileContents( const QString & fn, QByteArray & content )
{
	//qDebug() << "entering fileContents for" << fn;
	if ( const BSAFile * file = getFile( fn ) ) {
		QMutexLocker lock( &bsaMutex );
		if ( bsa.seek( file->offset ) ) {
			qint64 filesz = file->size();
			bool ok = true;
			if ( namePrefix ) {
				char len;
				ok = bsa.read( &len, 1 );
				filesz -= len;
				if ( ok ) ok = bsa.seek( file->offset + 1 + len );
			}

			content.resize( filesz );
			if ( ok && bsa.read( content.data(), filesz ) == filesz ) {
				if ( file->sizeFlags > 0 ) {
					// BSA
					if ( file->compressed() ^ compressToggle )
						content = gUncompress( content );
				} else if ( file->packedLength > 0 ) {
					// BA2
					content = gUncompress( content );
				}

				if ( file->tex.chunks.count() ) {
					// Fill DDS Header
					DDS_HEADER ddsHeader = { 0 };

					ddsHeader.dwSize = sizeof( ddsHeader );
					ddsHeader.dwHeaderFlags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_LINEARSIZE | DDS_HEADER_FLAGS_MIPMAP;
					ddsHeader.dwHeight = file->tex.header.height;
					ddsHeader.dwWidth = file->tex.header.width;
					ddsHeader.dwMipMapCount = file->tex.header.numMips;
					ddsHeader.ddspf.dwSize = sizeof( DDS_PIXELFORMAT );
					ddsHeader.dwSurfaceFlags = DDS_SURFACE_FLAGS_TEXTURE | DDS_SURFACE_FLAGS_MIPMAP;

					bool ok = true;

					switch ( file->tex.header.format ) {
					case DXGI_FORMAT_BC1_UNORM:
						ddsHeader.ddspf.dwFlags = DDS_FOURCC;
						ddsHeader.ddspf.dwFourCC = MAKEFOURCC( 'D', 'X', 'T', '1' );
						ddsHeader.dwPitchOrLinearSize = file->tex.header.width * file->tex.header.height / 2;	// 4bpp
						break;

					case DXGI_FORMAT_BC2_UNORM:
						ddsHeader.ddspf.dwFlags = DDS_FOURCC;
						ddsHeader.ddspf.dwFourCC = MAKEFOURCC( 'D', 'X', 'T', '3' );
						ddsHeader.dwPitchOrLinearSize = file->tex.header.width * file->tex.header.height;	// 8bpp
						break;

					case DXGI_FORMAT_BC3_UNORM:
						ddsHeader.ddspf.dwFlags = DDS_FOURCC;
						ddsHeader.ddspf.dwFourCC = MAKEFOURCC( 'D', 'X', 'T', '5' );
						ddsHeader.dwPitchOrLinearSize = file->tex.header.width * file->tex.header.height;	// 8bpp
						break;

					case DXGI_FORMAT_BC5_UNORM:
						// Incorrect
						ddsHeader.ddspf.dwFlags = DDS_FOURCC;
						ddsHeader.ddspf.dwFourCC = MAKEFOURCC( 'A', 'T', 'I', '2' );
						//ddsHeader.ddspf.dwFourCC =		MAKEFOURCC('D', 'X', 'T', '5');
						ddsHeader.dwPitchOrLinearSize = file->tex.header.width * file->tex.header.height;	// 8bpp
						break;

					case DXGI_FORMAT_BC7_UNORM:
						// Incorrect
						ddsHeader.ddspf.dwFlags = DDS_FOURCC;
						ddsHeader.ddspf.dwFourCC = MAKEFOURCC( 'B', 'C', '7', '\0' );
						ddsHeader.dwPitchOrLinearSize = file->tex.header.width * file->tex.header.height;	// 8bpp
						break;

					case DXGI_FORMAT_B8G8R8A8_UNORM:
						ddsHeader.ddspf.dwFlags = DDS_RGBA;
						ddsHeader.ddspf.dwRGBBitCount = 32;
						ddsHeader.ddspf.dwRBitMask = 0x00FF0000;
						ddsHeader.ddspf.dwGBitMask = 0x0000FF00;
						ddsHeader.ddspf.dwBBitMask = 0x000000FF;
						ddsHeader.ddspf.dwABitMask = 0xFF000000;
						ddsHeader.dwPitchOrLinearSize = file->tex.header.width * file->tex.header.height * 4;	// 32bpp
						break;

					case DXGI_FORMAT_R8_UNORM:
						ddsHeader.ddspf.dwFlags = DDS_RGB;
						ddsHeader.ddspf.dwRGBBitCount = 8;
						ddsHeader.ddspf.dwRBitMask = 0xFF;
						ddsHeader.dwPitchOrLinearSize = file->tex.header.width * file->tex.header.height;	// 8bpp
						break;

					default:
						ok = false;
						break;
					}

					char buf[sizeof( ddsHeader )];
					memcpy( buf, &ddsHeader, sizeof( ddsHeader ) );

					// Prepend DDS Header
					content.prepend( QByteArray::fromRawData( buf, sizeof( ddsHeader ) ) );
					content.prepend( QByteArray::fromStdString( "DDS " ) );

					// Start at 2nd chunk
					for ( int i = 1; i < file->tex.chunks.count(); i++ ) {
						F4TexChunk chunk = file->tex.chunks[i];
						if ( bsa.seek( chunk.offset ) ) {
							QByteArray chunkData;

							if ( chunk.packedSize > 0 ) {
								chunkData.resize( chunk.packedSize );
								if ( bsa.read( chunkData.data(), chunk.packedSize ) == chunk.packedSize )
									chunkData = gUncompress( chunkData );

								if ( chunkData.size() != chunk.unpackedSize )
									qCritical() << "Size does not match at " << chunk.offset;

							} else {
								chunkData.resize( chunk.unpackedSize );
								if ( !(bsa.read( chunkData.data(), chunk.unpackedSize ) == chunk.unpackedSize) )
									qCritical() << "Size does not match at " << chunk.offset;
							}
							content.append( chunkData );
						} else {
							qCritical() << "Seek error";
						}
					}
				}

				return true;
			}
		}
	}
	return false;
}

QString BSA::absoluteFilePath( const QString & fn ) const
{
	if ( hasFile( fn ) ) {
		return QFileInfo( this->bsaPath, fn ).absoluteFilePath();
	}
	return QString();
}

BSA::BSAFolder * BSA::insertFolder( QString name )
{
	if ( name.isEmpty() )
		return &root;

	name = name.replace( "\\", "/" ).toLower();

	BSAFolder * folder = folders.value( name );
	if ( !folder ) {
		//qDebug() << "inserting" << name;

		folder = new BSAFolder;
		folders.insert( name, folder );

		int p = name.lastIndexOf( "/" );
		if ( p >= 0 ) {
			folder->parent = insertFolder( name.left( p ) );
			folder->parent->children.insert( name.right( name.length() - p - 1 ), folder );
		} else {
			folder->parent = &root;
			root.children.insert( name, folder );
		}
	}

	return folder;
}

BSA::BSAFile * BSA::insertFile( BSAFolder * folder, QString name, quint32 sizeFlags, quint64 offset )
{
	name = name.toLower();

	BSAFile * file = new BSAFile;
	file->sizeFlags = sizeFlags;
	file->offset = offset;

	folder->files.insert( name, file );
	return file;
}

BSA::BSAFile * BSA::insertFile( BSAFolder * folder, QString name, quint32 packed, quint32 unpacked, quint64 offset, F4Tex dds )
{
	name = name.toLower();

	BSAFile * file = new BSAFile;
	file->tex = dds;
	file->packedLength = packed;
	file->unpackedLength = unpacked;
	file->offset = offset;
	folder->files.insert( name, file );

	return file;
}

const BSA::BSAFolder * BSA::getFolder( QString fn ) const
{
	if ( fn.isEmpty() )
		return &root;
	else
		return folders.value( fn );
}

const BSA::BSAFile * BSA::getFile( QString fn ) const
{
	QString folderName;
	QString fileName = fn;
	int p = fn.lastIndexOf( "/" );
	if ( p >= 0 ) {
		folderName = fn.left( p );
		fileName = fn.right( fn.length() - p - 1 );
	}

	if ( const BSAFolder * folder = getFolder( folderName ) ) {
		return folder->files.value( fileName );
	} else {
		return nullptr;
	}
}

bool BSA::hasFile( const QString & fn ) const
{
	return getFile( fn );
}

bool BSA::hasFolder( const QString & fn ) const
{
	return getFolder( fn );
}

uint BSA::ownerId( const QString & ) const
{
	return bsaInfo.ownerId();
}

QString BSA::owner( const QString & ) const
{
	return bsaInfo.owner();
}

QDateTime BSA::fileTime( const QString & ) const
{
	return bsaInfo.created();
}

bool BSA::scan( const BSA::BSAFolder * folder, QStandardItem * item, QString path )
{
	if ( !folder || folder->children.count() == 0 )
		return false;

	QHash<QString, BSA::BSAFolder *>::const_iterator i;
	for ( i = folder->children.begin(); i != folder->children.end(); i++ ) {

		auto files = i.value()->files;
		auto children = i.value()->children;

		if ( !files.count() && !children.count() )
			continue;

		auto folderItem = new QStandardItem( i.key() );
		auto pathDummy = new QStandardItem( "" );
		//auto sizeDummy = new QStandardItem( "" );

		item->appendRow( { folderItem, pathDummy } );
		item->setCheckable( true );

		// Recurse through folders
		if ( children.count() ) {
			QString fullpath = ((path.isEmpty()) ? path : path + "/") + i.key();
			scan( i.value(), folderItem, fullpath );
		}

		// List files
		if ( files.count() ) {
			QHash<QString, BSA::BSAFile *>::const_iterator f;
			for ( f = files.begin(); f != files.end(); f++ ) {
				QString fullpath = path + "/" + i.key() + "/" + f.key();

				//int bytes = f.value()->size();
				//QString filesize = (bytes > 1024) ? QString::number( bytes / 1024 ) + "KB" : QString::number( bytes ) + "B";

				auto fileItem = new QStandardItem( f.key() );
				auto pathItem = new QStandardItem( fullpath );
				//auto sizeItem = new QStandardItem( "0" );

				folderItem->appendRow( { fileItem, pathItem } );
				folderItem->setCheckable( true );
				folderItem->setCheckState( Qt::Checked );

				fileItem->setCheckable( true );
				fileItem->setCheckState( Qt::Checked );

				//emit progress( ++filesScanned );
			}
		}

		item->setCheckState( Qt::Checked );
	}

	return true;
}

bool BSA::fillModel( BSAModel * bsaModel, const QString & folder )
{
	filesScanned = 0;
	return scan( getFolder( folder ), bsaModel->invisibleRootItem(), folder );
}


BSAModel::BSAModel( QObject * parent )
	: QStandardItemModel( parent )
{

}

void BSAModel::init()
{
	setColumnCount( 2 );
	setHorizontalHeaderLabels( { "File", "Path" } );
}

Qt::ItemFlags BSAModel::flags( const QModelIndex & index ) const
{
	return QStandardItemModel::flags( index ) ^ Qt::ItemIsEditable;
}


BSAProxyModel::BSAProxyModel( QObject * parent )
	: QSortFilterProxyModel( parent )
{

}

void BSAProxyModel::setFiletypes( QStringList types )
{
	filetypes = types;
}

void BSAProxyModel::setFilterByNameOnly( bool nameOnly )
{
	filterByNameOnly = nameOnly;

	setFilterRegExp( filterRegExp() );
}

void BSAProxyModel::resetFilter()
{
	setFilterRegExp( QRegExp( "*", Qt::CaseInsensitive, QRegExp::Wildcard ) );
}

bool BSAProxyModel::filterAcceptsRow( int sourceRow, const QModelIndex & sourceParent ) const
{
	if ( !filterRegExp().isEmpty() ) {

		QModelIndex sourceIndex0 = sourceModel()->index( sourceRow, 0, sourceParent );
		QModelIndex sourceIndex1 = sourceModel()->index( sourceRow, 1, sourceParent );
		if ( sourceIndex0.isValid() ) {
			// If children match, parent matches
			int c = sourceModel()->rowCount( sourceIndex0 );
			for ( int i = 0; i < c; ++i ) {
				if ( filterAcceptsRow( i, sourceIndex0 ) )
					return true;
			}

			QString key0 = sourceModel()->data( sourceIndex0, filterRole() ).toString();
			QString key1 = sourceModel()->data( sourceIndex1, filterRole() ).toString();

			bool typeMatch = true;
			if ( filetypes.count() ) {
				typeMatch = false;
				for ( auto f : filetypes ) {
					typeMatch |= key1.endsWith( f );
				}
			}

			bool stringMatch = (filterByNameOnly) ? key0.contains( filterRegExp() ) : key1.contains( filterRegExp() );

			return typeMatch && stringMatch;
		}
	}

	return QSortFilterProxyModel::filterAcceptsRow( sourceRow, sourceParent );
}

bool BSAProxyModel::lessThan( const QModelIndex & left, const QModelIndex & right ) const
{
	QString leftString = sourceModel()->data( left ).toString();
	QString rightString = sourceModel()->data( right ).toString();

	QModelIndex leftChild = left.child( 0, 0 );
	QModelIndex rightChild = right.child( 0, 0 );

	if ( !leftChild.isValid() && rightChild.isValid() )
		return false;

	if ( leftChild.isValid() && !rightChild.isValid() )
		return true;

	return leftString < rightString;
}
