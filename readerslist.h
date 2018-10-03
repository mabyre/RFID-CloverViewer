#ifndef READERSLIST_H
#define READERSLIST_H

extern "C"
{
    #include "cltypes.h"
    #include "clstructs.h"

    e_Result cl_readerAddToList( t_Reader *pReader);
    e_Result cl_readerFindInList( t_Reader **ppReader, t_Reader *pReaderFilter );
    e_Result cl_ReaderFillWithDefaultFields( t_Reader *pReader, e_ReaderType eType );
    e_Result cl_ReaderSetState( t_Reader *pReader, e_State eState );
}

class ReadersList
{
public:
    ReadersList();
    ~ReadersList();

    static void ConvertASCIToHex( unsigned char *pStringBuf, unsigned inLen, unsigned char *pOutData, unsigned long *pu32OutLen );
    static int InitializeReadersListFromFile();
    static e_Result AddReaderToFile(t_Reader *pReader);
    static QString BaudRateInString(t_Reader *pReader);
    static e_Result RemoveReaderFromFile(QString aFriendlyName);
    static bool IsReaderInFile(QString aFriendlyName);
    static bool IsReaderFormatedOk(QString aFriendlyName);
};

#endif // READERSLIST_H
