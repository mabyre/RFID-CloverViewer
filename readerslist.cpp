/*--------------------------------------------------------------------------*\
 *
\*--------------------------------------------------------------------------*/

#include <QFile>
#include "readerslist.h"

ReadersList::ReadersList(){}
ReadersList::~ReadersList(){}

/*--------------------------------------------------------------------------*\
 *
\*--------------------------------------------------------------------------*/
void ReadersList::ConvertASCIToHex( unsigned char *pStringBuf, unsigned inLen, unsigned char *pOutData, unsigned long *pu32OutLen )
{
    clu8 ucNibble = 0;
    if (!pStringBuf )
        return;

    if (!pOutData)
        return;

    if ( !inLen )
        return;

    if ( !pu32OutLen )
        return;

    if ( (*pu32OutLen) < (inLen / 2) )
        return;


    /* convert ASCII to byte */
    for ( int i = 0; i < inLen; i++ )
    {
        if (!(i%2))
        {
            if (( pStringBuf[i]>= 0x30 ) & ( pStringBuf[i] <= 0x39))
                ucNibble = (pStringBuf[i]- 0x30)<<4;
            else
            {
                if (( pStringBuf[i] >= 0x41 ) & ( pStringBuf[i] <0x47))
                    ucNibble = (pStringBuf[i]- 0x41 + 0x0A)<<4;
            }
        }
        else
        {
            if (( pStringBuf[i]>= 0x30 ) & ( pStringBuf[i] <= 0x39))
                ucNibble |= (pStringBuf[i]- 0x30)&0x0F;
            else
            {
                if (( pStringBuf[i] >= 0x41 ) & ( pStringBuf[i] <0x47))
                    ucNibble |= (pStringBuf[i]- 0x41 + 0x0A)&0x0F;
            }
            pOutData[i/2]= ucNibble;
        }
    }
}

QString ReadersList::BaudRateInString( t_Reader *pReader )
{
    QString result;
    switch ( pReader->tCOMParams.eBaudRate )
    {
        case CL_COM_BAUDRATE_4800 :
        {
            result = QString("4800");
            break;
        }
        case CL_COM_BAUDRATE_9600 :
        {
            result = QString("9600");
            break;
        }
        case CL_COM_BAUDRATE_19200 :
        {
            result = QString("19200");
            break;
        }
        case CL_COM_BAUDRATE_38400 :
        {
            result = QString("38400");
            break;
        }
        case CL_COM_BAUDRATE_57600 :
        {
            result = QString("57600");
            break;
        }
        case CL_COM_BAUDRATE_115200 :
        default:
        {
            result = QString("115200");
            break;
        }
    }

    return result;
}

/*--------------------------------------------------------------------------*\
 * Open the file "readers_list.txt"
 * Create Reader
 * Add Reader to the list
\*--------------------------------------------------------------------------*/
int ReadersList::InitializeReadersListFromFile()
{
    // open reader file
    QFile file("readers_list.txt");

    char buf[1024];
    qint64 i64LineLen;
    e_ReaderType eType;
    t_Reader newReader;
    t_Reader *pReaderInList;
    int iState = 0;
    int iFieldLen = 0;
    qint64 iFirstIndex ;
    qint64 iLastIndex ;
    qint64 iFoundIndex = 0;
    bool bFound = false;
    int nbErrorsInReaderlistFile = 0;

    if ( !file.open(QFile::ReadOnly) )
        return CL_ERROR;

    // reader line
    i64LineLen= file.readLine( buf, sizeof(buf) ) ;

    while ( ( i64LineLen != -1 ) & ( i64LineLen !=0 ) )
    {
        iFirstIndex = iLastIndex = iFoundIndex = 0;
        char aucArg[64];
        bFound = false;

        iFirstIndex = iLastIndex = iState = 0;
        eType = UNKNOWN_READER_TYPE;
        pReaderInList = CL_NULL;

        // parse line to capture each field
        for ( qint64 i = 0;  i < i64LineLen; i++ )
        {
            // look for ";"
            if ( buf[i] == ';' )
            {
                if ( i > 0 )
                {
                    iLastIndex = i-1;
                    bFound = true;
                }
            }

            if ( bFound == true ) // a new field is found => get it
            {
                // prepare array to get new arguments
                memset( aucArg, 0, sizeof( aucArg ) );
                memcpy( aucArg, &buf[iFirstIndex], iLastIndex - iFirstIndex + 1);
                iFieldLen = iLastIndex - iFirstIndex + 1;

                // depending on state, fill either field
                switch ( iState )
                {
                    case (0):    //  get Reader Type
                    {
                        // if first element;
                        if ( !memcmp( aucArg, "Serial", strlen("Serial")))
                        {

                            if ( CL_FAILED( cl_ReaderFillWithDefaultFields( &newReader, COM_READER_TYPE ) ) )
                                break;
                            eType = COM_READER_TYPE;
                            bFound = false;
                            iFirstIndex = i + 1;
                            iState = 1;
                            continue;

                        }
                        // check if IP reader
                        if ( !memcmp( aucArg, "IP", strlen("IP")))
                        {

                            if ( CL_FAILED( cl_ReaderFillWithDefaultFields( &newReader, IP_READER_TYPE ) ) )
                                break;
                            eType = IP_READER_TYPE;
                            iFirstIndex = i + 1;
                            bFound = false;
                            iState = 1;
                            continue;
                        }
                        if ( !memcmp( aucArg, "BlueTooth", strlen("BlueTooth")))
                        {

                            if ( CL_FAILED( cl_ReaderFillWithDefaultFields( &newReader, BT_READER_TYPE ) ) )
                                break;
                            eType = BT_READER_TYPE;
                            iFirstIndex = i + 1;
                            iState = 1;
                            bFound = false;
                            continue;
                        }
                    }
                    case (1):      // get reader name
                    {
                        memset( newReader.aucLabel, 0, sizeof( newReader.aucLabel ) );
                        memcpy( newReader.aucLabel, &buf[iFirstIndex], iFieldLen );
                        bFound = false;
                        iFirstIndex = i + 1;
                        iState = 2;
                        continue;
                        break;
                    }

                    case (2): // get serial com port/ip address
                    {
                        switch ( eType)
                        {
                            case ( COM_READER_TYPE ):
                            {
                                // set to 0 all bytes
                                memset( newReader.tCOMParams.aucPortName, 0, sizeof( newReader.tCOMParams.aucPortName ) );

                                // copy reader name
                                memcpy( newReader.tCOMParams.aucPortName, &buf[iFirstIndex], iFieldLen );
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 3;
                                break;
                            }
                            case ( IP_READER_TYPE):
                            {
                                memset( newReader.tIPParams.aucIpAddr, 0, sizeof( newReader.tIPParams.aucIpAddr ) );
                                memcpy( newReader.tIPParams.aucIpAddr, &buf[iFirstIndex], iFieldLen);
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 3;
                                continue;
                                break;
                            }
                            case ( BT_READER_TYPE ):
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 3;
                                break;
                            }
                        }
                        break;
                    }
                    case ( 3 ): // get serial port/ip tcp port
                    {
                        switch (eType)
                        {
                            case ( COM_READER_TYPE ):
                            {
                                if (!memcmp( &buf[iFirstIndex], "9600", strlen("9600")))
                                    newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_9600;

                                if (!memcmp( &buf[iFirstIndex], "19200", strlen("19200")))
                                    newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_19200;

                                if (!memcmp( &buf[iFirstIndex], "38400", strlen("38400")))
                                    newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_38400;

                                if (!memcmp( &buf[iFirstIndex], "57600", strlen("57600")))
                                    newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_57600;

                                if (!memcmp( &buf[iFirstIndex], "115200", strlen("115200")))
                                    newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_115200;

                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 4;
                                break;
                            }
                            case ( IP_READER_TYPE ):
                            {
                                if ( iFieldLen == 4 )   // IPV4
                                {
                                    QByteArray ByteArray2UTF8( (const char *)&buf[iFirstIndex], iFieldLen );
                                    char *pData = ByteArray2UTF8.data();
                                    newReader.tIPParams.u32Port = atoi( pData );
                                }

                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 4;
                                break;

                            }
                            case ( BT_READER_TYPE ):
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 4;
                                break;
                            }
                            default:
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 4;
                               break;
                            }
                        }
                        break;
                    }
                    case ( 4 ): // get additionnal data
                    {
                        switch (eType)
                        {
                            case ( COM_READER_TYPE ):
                            {
                                // convert ASCII to Hex
                                clu32 ulOutLen = sizeof( newReader.tCloverSense.au8RadioAddress );
                                ConvertASCIToHex( (clu8*)&buf[iFirstIndex], iFieldLen, (clu8*)newReader.tCloverSense.au8RadioAddress, &ulOutLen );
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 5;
                                break;
                            }
                            case ( IP_READER_TYPE ):
                            {
                                // convert ASCII to Hex
                                clu32 ulOutLen = sizeof( newReader.tCloverSense.au8RadioAddress );
                                ConvertASCIToHex( (clu8*)&buf[iFirstIndex], iFieldLen, (clu8*)newReader.tCloverSense.au8RadioAddress, &ulOutLen );
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 5;
                                break;

                            }
                            case ( BT_READER_TYPE ):
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 5;
                                break;
                            }
                            default:
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 4;
                                break;
                            }
                        }
                    }
                    case ( 5 ): // get additionnal data
                    {
                        switch (eType)
                        {
                            case ( COM_READER_TYPE ):
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 6;
                                break;
                            }
                            case ( IP_READER_TYPE ):
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 6;
                                break;

                            }
                            case ( BT_READER_TYPE ):
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 6;
                                break;
                            }
                            default:
                            {
                                bFound = false;
                                iFirstIndex = i + 1;
                                iState = 4;
                                break;
                            }
                        }
                    }
                    default:
                    {
                        bFound = false;
                        iFirstIndex = i + 1;
                        break;
                    }
                }
            }
        }

        // Skype a line that begin with ';'
        if ( buf[ 0 ] != ';' )
        {
            if ( CL_FAILED( cl_readerAddToList( &newReader ) ) )
            {
                nbErrorsInReaderlistFile += 1;
            }

            // Get reader from the list
            cl_readerFindInList( &pReaderInList, &newReader );

            if ( pReaderInList )
            {
                // if already in DISCOVER.... do not reask to reopen port and so on
                if ( pReaderInList->eState != STATE_DISCOVER )
                {
                    // set state for reader. If an error happens while trying to connect, we check the status afterwards
                    // so not trying return code at this point is ok
                    cl_ReaderSetState( pReaderInList, STATE_DISCOVER );
                }
            }
        }

        i64LineLen = file.readLine( buf, sizeof(buf) );
    };

    // close file
    file.close();

    return nbErrorsInReaderlistFile;
}

/*--------------------------------------------------------------------------*\
 * Add the Reader to the file "readers_list.txt"
\*--------------------------------------------------------------------------*/
e_Result ReadersList::AddReaderToFile( t_Reader *pReader )
{
    QString readerType;
    QString friendlyName;
    QString portName;
    QString baudRate;
    QString otherData = QString("000000000000");

    // Open readers list
    QFile file( "readers_list.txt" );

    // If file don't exit it's created otherwise appended
    file.open( QIODevice::WriteOnly | QIODevice::Append );

    //
    // Reader's type
    //
    if ( pReader->tType == COM_READER_TYPE )
    {
       readerType = QString("Serial");
       friendlyName = QString((const char *)pReader->aucLabel);
       portName = QString((const char *)pReader->tCOMParams.aucPortName);
    }

    if ( pReader->tType == IP_READER_TYPE )
    {
       readerType = QString("IPReader");
    }

    if ( pReader->tType == BT_READER_TYPE )
    {
       readerType = QString("BlueThooth");
    }

    baudRate = ReadersList::BaudRateInString( pReader );

    QString line_file = QString("%1;%2;%3;%4;%5;\n").arg( readerType, friendlyName, portName, baudRate, otherData );
    file.write(line_file.toLatin1());

    file.close();
}

/*--------------------------------------------------------------------------*\
 * Remove the Reader to the file "readers_list.txt"
\*--------------------------------------------------------------------------*/
e_Result ReadersList::RemoveReaderFromFile( QString aFriendlyName )
{
    // Open readers list
    QFile fileOld( "readers_list.txt" );
    QFile file( "readers_list_new.txt" );

    QFile::remove( "readers_list_saved.txt" );
    QFile::copy( "readers_list.txt", "readers_list_saved.txt" );

    if ( fileOld.open( QFile::ReadOnly ) == true )
    {
        // If file don't exit it's created otherwise appended
        file.open(QIODevice::WriteOnly | QIODevice::Append);

        while ( fileOld.atEnd() == false )
        {
            QString line = fileOld.readLine();
            if ( line.startsWith( aFriendlyName ) == false )
            {
                file.write( line.toLatin1() );
            }
        }

        fileOld.remove();
        fileOld.close();

        file.copy("readers_list.txt");
        file.close();

        QFile::remove( "readers_list_new.txt" );
    }
}

/*--------------------------------------------------------------------------*/

bool ReadersList::IsReaderInFile( QString aFriendlyName )
{
    bool result = false;
    QFile file( "readers_list.txt" );

    if ( file.open( QFile::ReadOnly ) == true )
    {
        while ( file.atEnd() == false )
        {
            QString line = file.readLine();
            if ( line.startsWith( aFriendlyName ) == true )
            {
                result = true;
                break;
            }
        }

        file.close();
    }

    return result;
}

/*--------------------------------------------------------------------------*/

bool ReadersList::IsReaderFormatedOk( QString aFriendlyName )
{
    bool result = true; // good format
    bool isNum = true;
    bool isHex = true;

    // Good format is : XX XX XX XX
    if ( aFriendlyName.length() != 11 )
        return false;

    for ( int i = 0; i < aFriendlyName.size(); i++ )
    {
        // spaces
        if ( i == 2 || i == 5 || i == 8 )
        {
            if ( aFriendlyName.at(i) != QChar(' ') )
                return false;
        }
        else
        {
            isNum = aFriendlyName.at(i) >= QChar('0') && aFriendlyName.at(i) <= QChar('9');
            if ( isNum == false )
            {
                isHex = aFriendlyName.at(i) >= QChar('A') && aFriendlyName.at(i) <= QChar('F');
            }

            if ( isNum == false && isHex == false )
            {
                return false;
            }
        }
    }

    return result;
}

