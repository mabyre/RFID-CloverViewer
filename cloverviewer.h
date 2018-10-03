#ifndef CLOVERVIEWER_H
#define CLOVERVIEWER_H

#include <QMainWindow>
#include <QFileDialog>
#include <QTimer>
#include <QTextEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QString>
#include <QProgressBar>
#include <QCompleter>
#include <QAbstractItemModel>

#include "UserControl/slineeditautocomplete.h"

namespace Ui {
class CloverViewer;
}

class CloverViewer : public QMainWindow
{
    Q_OBJECT

public:
    explicit CloverViewer(QWidget *parent = 0);
    ~CloverViewer();

    void UpdateScriptEditorDataReceived( void *pReader, void *pTuple, int eStatus );
    void UpdateScriptEditorWriteComplete( void *pReader, void *pTuple, int eStatus );
    void UpdateClViewReaderDeviceComplete( void *pReader, void *pTuple, int eStatus );
    void UpdateClViewReaderDeviceState( void *pCtxt, void *ptReader, void *ptDevice );
    void UpdateClViewOTAProgress( int eState, void *pReader, int u32Progress );
    void ConvertByteToAscii( unsigned char *pData, unsigned long u32Len, QString *pString );

    qint32  m_iPacketsReceived;
    qint32  m_iPacketsSent;

    bool LogMode();

protected:
    void showEvent( QShowEvent *event );

private slots:

    void onWindowLoaded_InitializeUIValues( void );
    void LogSlot(bool isAckReceived, QString aReaderName, QString aPortName, QString aData2Display, QString aTime, QString aDirection);

    void updateDataCounterDisplays( qint32 iPckts, bool bPacketsReceived );
    void updateScriptEditorWindow( int rows, int columns, QTableWidget *pTableWidget, QTableWidgetItem *pTableWidgetItem );
    void updateClViewReaderDeviceWindow( void *pCtxt, void *ptReader, void *ptDevice );
//    void updateClViewOTAReaderWindow( void *pCtxt, void *ptReader, void *ptDevice );
    void updateCSLDataActivitySlot( bool bDataReceived, QString SReaderInfo, QString SReaderName, QString SData2Display, QString STimeString, QString SDirection );
    void on_Reader_ConnectionType_List_activated(const QString &arg1);
//    void on_ReadersIPTableWidget_cellChanged(int row, int column);
    void on_ReadersCOMTableWidget_cellChanged(int row, int column);

    void on_pushButton_SetLocalRTC_clicked();

    void on_SetRTCDistant_Open();
    void on_pushButton_SetRTCDistant_clicked();

    void on_pushButton_AddReader_clicked();

    void on_pushButton_RemoveReader_clicked();

    // Very important: maintain gIndexTabReader
    void on_tabWidget_Readers_currentChanged(int index);
    void on_GlobalSetupWidget_currentChanged(int index);

    void on_pushButton_ClearCurrentTableWidget_clicked();

    void timerForRepeatCommandeSlot();

    void on_pushButton_SaveLogFilePath_clicked();

    void on_lineEdit_SaveLogFileName_customContextMenuRequested(const QPoint &pos);

    void on_ContextMenu_Open();
    void on_ContextMenu_Disable();
    void on_pushButton_SetRoute_clicked();

    void on_pushButton_Cmd2Send_2_clicked();

    void on_pushButton_Cmd2Send_3_clicked();

    void on_pushButton_Cmd2Send_4_clicked();

    void on_pushButton_Cmd2Send_5_clicked();

    void on_pushButton_Cmd2Send_6_clicked();

    void on_pushButton_Cmd2Send_7_clicked();

    void on_pushButton_Cmd2Send_8_clicked();

    void on_tableWidget_History_Scripting_2_doubleClicked(const QModelIndex &index);

    void on_tableWidget_History_Scripting_3_doubleClicked(const QModelIndex &index);

    void on_tableWidget_History_Scripting_4_doubleClicked(const QModelIndex &index);

    void on_tableWidget_History_Scripting_5_doubleClicked(const QModelIndex &index);

    void on_tableWidget_History_Scripting_6_doubleClicked(const QModelIndex &index);

    void on_tableWidget_History_Scripting_7_doubleClicked(const QModelIndex &index);

    void on_tableWidget_History_Scripting_8_doubleClicked(const QModelIndex &index);

    void on_pushButton_SetRTCDistant_customContextMenuRequested(const QPoint &pos);

    void on_pushButton_Script_1_clicked();
    void on_pushButton_Script_1_customContextMenuRequested(const QPoint &pos);
    void on_pushButton_Script_1_contextMenu_Open();
    void on_pushButton_Script_1_contextMenu_Refresh();

    void on_pushButton_Script_2_customContextMenuRequested(const QPoint &pos);
    void on_pushButton_Script_2_clicked();
    void on_pushButton_Script_2_contextMenu_Open();
    void on_pushButton_Script_2_contextMenu_Refresh();

    void on_pushButton_Script_3_customContextMenuRequested(const QPoint &pos);
    void on_pushButton_Script_3_clicked();
    void on_pushButton_Script_3_contextMenu_Open();
    void on_pushButton_Script_3_contextMenu_Refresh();

    void on_pushButton_Script_4_clicked();
    void on_pushButton_Script_4_customContextMenuRequested(const QPoint &pos);
    void on_pushButton_Script_4_contextMenu_Open();
    void on_pushButton_Script_4_contextMenu_Refresh();
signals:
    void    updateClViewReaderDeviceSgnl( void *, void *, void *  );
    void    updateClViewOTAReaderSgnl( void *, void *, void *  );
    void    window_loaded();
    void    updateRollingListModelSgl( void );
    void    updateScriptEditorSignal( int, int, QTableWidget *, QTableWidgetItem * );
    void    updateDataCounterSignal( qint32, bool);
    void    updateCSLDataActivitySignal( bool , QString, QString, QString, QString, QString );
    void    updateOTAProgressBarSignal(qint32, qint32);
    void    LogSignal( bool , QString, QString, QString, QString, QString );

private:
    Ui::CloverViewer *ui;

    bool m_bOtaTransmitInitialized[4];

    QCompleter *completerLineEdit_Cmd2Send_2;
    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_2;

    QCompleter *completerLineEdit_Cmd2Send_3;
    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_3;
    
    QCompleter *completerLineEdit_Cmd2Send_4;
    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_4;

    QCompleter *completerLineEdit_Cmd2Send_5;
    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_5;

    QCompleter *completerLineEdit_Cmd2Send_6;
    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_6;

    QCompleter *completerLineEdit_Cmd2Send_7;
    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_7;

    QCompleter *completerLineEdit_Cmd2Send_8;
    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_8;

//    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_3;
//    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_4;
//    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_5;
//    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_6;
//    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_7;
//    SLineEditAutoComplete *lineEditAutoComplete_Cmd2Send_8;

    QTimer *g_TimerForRepeatCommande;

//    void InitializeUIFromCSL( void );
//    void InitializeOTATables( );
    void InitializeProfileTab( void );
    void InitializeReadersTab( void );
    void InitializeLineEditAutoCompleteCmd2Send();

    QAbstractItemModel *modelFromFile_Cmd(const QString& fileName);
    void completeCompletionList_Cmd(QLineEdit *lineEdit);

    void ConvertASCIToHex( unsigned char *pStringBuf, unsigned inLen, unsigned char *pOutData, unsigned long *pu32OutLen );
    void InitializeTerminalsTab();
    void TermsTabDisplay( char *pData, bool bDisplay );
    void TermsTreeViewDisplay(void *ptReader, bool bDisplay );
    void ScriptCmdSendToCsl(QString aReaderName,  QString aCmdToSend);

    // ne sert plus
    void SuppressLineEditInCompletionList(int aTab, QString aLineEditCmdName);

    QCompleter *completerLineEdit_RelayAdress;
    QAbstractItemModel *modelFromFile_RelayAdress(const QString &fileName);
    void completeCompletionList_RelayAddress(QLineEdit *lineEdit);
    void SendCommandSetRTC(int aTab);

    void ObjectsDisable();
    void ObjectsEnable();

    QString loadStyleSheet(const QString &sheetName);
    void SetStatusMessage(QString aColor, QString aMessage);
    void on_pushButton_Cmd2Send_RepeatMode_clicked( int aTab );
    void on_pushButton_Cmd2Send_clicked( int aTab );
    void on_tableWidget_History_Scripting_doubleClicked(const QModelIndex &index);
    void on_pushButton_Script_clicked(int aScript, int aTab);
    void on_pushButton_Script_contextMenu_Open(int aScript);
    void on_pushButton_Script_contextMenu_Refresh(int aScript);
};

#endif // CLOVERVIEWER_H
