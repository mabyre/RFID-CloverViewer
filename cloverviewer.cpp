#include "cloverviewer.h"
#include "ui_cloverviewer.h"

#include <QtWidgets>
#include "readerslist.h"
#include "ASTrace/ASTrace.h"
#include "cXMem.h"
#include "pmEnv.h"
#include "pmTrace.h"

// BUG_10122015 : "La charge CPU augmente"
// BRY_20151208 : l'application d'une image de fond est un véritable fiasco
// BRY_02122015 : pour la suite, conserver  l'aspect dynamique de la combo "Reader_ConnectionType_List"
// BRY_25112015 : Utiliser le controle SLineEditAutoComplete
// BRY_20150922 : Eviter d'afficher le Tab d'un reader que l'on ne peut pas connecter
// BRY_20150921 : Afficher une icone verte et rouge pour la commande Send
// BRY_0923 : Correction of a SIGSEGV Segmentation Fault

extern "C"
{
    #include "csl.h"
    e_Result cl_ReaderRemoveFromList( t_Reader *pReader );

    #include "csl_component.h"

    e_Result ClViewer_Data2Read_cb( clvoid *pReader, clvoid *pTuple, e_Result eStatus );
    e_Result ClViewer_SendDataDone_cb( clvoid *pReader, clvoid *pTuple, e_Result status );
    e_Result ClViewer_IOStateChanged_cb( clvoid *pCtxt, clvoid *ptReader, clvoid *ptDevice, e_Result result );
    e_Result ClViewer_OTAProgress_cb( clu32 eState, clvoid *ptReader, clu32 u32Progress );

    #define COM_TABLE_OFFSET_FRIENDLY_NAME  0
    #define COM_TABLE_OFFSET_PORT           1
    #define COM_TABLE_OFFSET_BAUDRATE       2
//    #define COM_TABLE_OFFSET_RADIO_ADDRESS  3
//    #define COM_TABLE_OFFSET_PROFILE_TX     4
//    #define COM_TABLE_OFFSET_PROFILE_RX     5
    #define COM_TABLE_OFFSET_STATUS         3
    #define COM_TABLE_OFFSET_CONNECT        4

    #define IP_TABLE_OFFSET_FRIENDLY_NAME   0
    #define IP_TABLE_OFFSET_IP_ADDRESS      1
    #define IP_TABLE_OFFSET_PORT            2
    #define IP_TABLE_OFFSET_RADIO_ADDRESS   3
    #define IP_TABLE_OFFSET_PROFILE_TX      4
    #define IP_TABLE_OFFSET_PROFILE_RX      5
    #define IP_TABLE_OFFSET_STATUS          6
    #define IP_TABLE_OFFSET_CONNECT         7

    #define BT_TABLE_OFFSET_FRIENDLY_NAME   0
    #define BT_TABLE_OFFSET_PORT            1
    #define BT_TABLE_OFFSET_BAUDRATE        2
    #define BT_TABLE_OFFSET_RADIO_ADDRESS   3
    #define BT_TABLE_OFFSET_PROFILE_TX      4
    #define BT_TABLE_OFFSET_PROFILE_RX      5
    #define BT_TABLE_OFFSET_STATUS          6
    #define BT_TABLE_OFFSET_CONNECT         7

    #define SCRIPT_COLUMN_READER_NAME       0
    #define SCRIPT_COLUMN_TIME              1
    #define SCRIPT_COLUMN_DIRECTION         2
    #define SCRIPT_COLUMN_DATA              3
    #define SCRIPT_GENERAL_TO_READER_COLUMN_OFFSET  (- 1)

    #define OTA_ENCRYPTION_KEY_READER_TYPE      0
    #define OTA_ENCRYPTION_KEY_FRIENDLY_NAME    1
    #define OTA_ENCRYPTION_KEY_PROPERTY         2
    #define OTA_ENCRYPTION_KEY_SELECT           3
}

/*--------------------------------------------------------------------------*/

#define DEBUG_PRINTF2 pm_trace2

CloverViewer *g_pClVw = CL_NULL;

char FullExeFileNameWithDirectory[1024] = "";

#define WAITING_READ_COMPLETE_TIMEOUT 3500 //4000

const QString Completion_List_Cmd_File_Name = QString("completion_list_cmd.txt");
const QString Completion_List_RelayAdress_File_Name = QString("completion_list_relay.txt");

/*--------------------------------------------------------------------------*/

QIcon g_Icon_Arrow_Green;
QIcon g_Icon_Arrow_Red;

QString g_styleSheetForQTableWidgetItem;

//
// List of Trace's files to log Readers
//
QStringList g_FileName_SaveTracesForReaders;

int g_CurrentTabIndexReader;
int g_CurrentTabIndexReader_UI;

int g_CurrentTabIndexGlobal;

int g_CounterRepeatMode = 0;
int g_TabInRepeatMode = 0;

bool g_ExecutingScriptMode = false;
bool g_QuitScriptMode = false;

/*--------------------------------------------------------------------------*/

CloverViewer::CloverViewer(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::CloverViewer)
{
    as_trace_init(NULL);

    e_Result    status         = CL_ERROR;

    // setup UI
    ui->setupUi(this);
    ui->mainToolBar->hide();

    // for specific customer usage, callbacks are sur-implemented to drive outputs from CSL to
    g_tCallbacks.fnIOData2Read_cb       =   ClViewer_Data2Read_cb;
    g_tCallbacks.fnIOSendDataDone_cb    =   ClViewer_SendDataDone_cb;
    g_tCallbacks.fnIOState_cb           =   ClViewer_IOStateChanged_cb;
    g_tCallbacksDiscover.fnIOState_cb   =   ClViewer_IOStateChanged_cb;
    g_tCallbacks.fnOTAProgress_cb       =   ClViewer_OTAProgress_cb;

    g_tHalFunc.fnTrace0 = pm_trace0;
    g_tHalFunc.fnTrace1 = pm_trace1;
    g_tHalFunc.fnAllocMem = csl_AllocDebug;
    g_tHalFunc.fnFreeMem = csl_FreeDebug;
    g_tHalFunc.fnFreeMemSafely = csl_FreeSafeDebug;

    g_tCSLDefaultReader.tSync.u32Retries = 2;

    status = csl_InitGlobalContext
    (
        (e_StackSupport)( COM_STACK_SUPPORT ),
        (t_clSys_HalFunctions *)&g_tHalFunc,
        (t_clSys_CallBackFunctions *)&g_tCallbacks,
        (t_clSys_CallBackFunctions *)CL_NULL,
        &g_tCSLDefaultReader,
        &g_IPReaderDefaultHAL,
        CL_NULL,
        &g_COMReaderDefaultHAL,
        CL_NULL,
        CL_NULL,
        CL_NULL,
        (t_MenuFileDef *)&g_tMenuFileDef,
        FullExeFileNameWithDirectory
    );
    if ( CL_SUCCESS( status ) )
    {
        DEBUG_PRINTF2( "csl_InitGlobalContext: SUCCESS" );
    }
    else
    {
        DEBUG_PRINTF2( "csl_InitGlobalContext: FAILED" );
    }

    //
    // Read the reader's list file and add it to reader's list
    //
    int nbErrorsParsing = ReadersList::InitializeReadersListFromFile();
    if ( nbErrorsParsing != 0 )
    {
        QString messageParsing = QString("Errors parsing file in reader's list: %1").arg( QString::number( nbErrorsParsing ) );
        ui->label_StatusMessage_ReadersList->setText( messageParsing );
    }
    else
    {
        ui->label_StatusMessage_ReadersList->setText( "Parsing reader's list file Ok" );
    }

    // prepare signal to load UI with parameters from file handled by CSL
    connect(this, SIGNAL(window_loaded()), this, SLOT( onWindowLoaded_InitializeUIValues() ));
    QTimer::singleShot(0, this, SIGNAL(window_loaded()));

    g_TimerForRepeatCommande = new QTimer(this);
    g_TimerForRepeatCommande->connect(g_TimerForRepeatCommande, SIGNAL(timeout()),this, SLOT(timerForRepeatCommandeSlot()));

    // signal that data is received from CSL to Script Editor
    connect( this, SIGNAL( updateCSLDataActivitySignal( bool , QString , QString, QString, QString, QString )) \
             ,this, SLOT( updateCSLDataActivitySlot( bool , QString , QString , QString , QString , QString) ), Qt::QueuedConnection );

    connect( this, SIGNAL( updateScriptEditorSignal(int, int, QTableWidget *, QTableWidgetItem *)), this, SLOT( updateScriptEditorWindow( int, int,  QTableWidget *, QTableWidgetItem *) ), Qt::QueuedConnection );

    // connect a signal to update packets counters
    connect( this, SIGNAL( updateDataCounterSignal(qint32, bool)), this, SLOT( updateDataCounterDisplays( qint32, bool ) ), Qt::QueuedConnection );

    // signal that state changed from reader/device/csl from CSL to Clover Viewer
    connect( this, SIGNAL( updateClViewReaderDeviceSgnl(void *, void *, void *)), this, SLOT( updateClViewReaderDeviceWindow( void *, void *, void * ) ), Qt::QueuedConnection );

    // signal that state has changed from a reader to update list of available readers used in OTA
//    connect( this, SIGNAL( updateClViewOTAReaderSgnl(void *, void *, void *)), this, SLOT( updateClViewOTAReaderWindow( void *, void *, void * ) ),Qt::QueuedConnection );

    // connect a signal to progress bar for OTA
//    connect( this, SIGNAL( updateOTAProgressBarSignal( qint32, qint32)), this, SLOT( updateDataProgressBarSlot( qint32, qint32 ) ), Qt::QueuedConnection );

    // Signal that data is received from CSL to LOG this data
    connect( this, SIGNAL( LogSignal( bool , QString , QString, QString, QString, QString ) ),
             this, SLOT( LogSlot( bool , QString , QString , QString , QString, QString ) ), Qt::QueuedConnection );

    // general pointer to CloverViewer object to enable to enter data coming from CSL (aka C calls) in QT framework
    g_pClVw = this;

    g_pClVw->m_iPacketsReceived     =   0;
    g_pClVw->m_iPacketsSent         =   0;

    //
    // Tab Index
    //
    g_CurrentTabIndexReader = ui->tabWidget_Readers->currentIndex();

    // Fucking history of this software development, for further information see FD
    // every object of UI have been named like beginning by "All" and following by 2 (not 1) !
    // so index for UI is +1
    g_CurrentTabIndexReader_UI = g_CurrentTabIndexReader + 1;

    g_CurrentTabIndexGlobal = ui->GlobalSetupWidget->currentIndex();

    ObjectsDisable();

    //
    // Test purpose
    //
    ui->label_Empty_3->setText( "" ); // delete empty label content

    //
    // Save path for all trace's files
    //
    QString currentPath = QDir::currentPath();
    currentPath.append("/log");

    g_FileName_SaveTracesForReaders << "$APP_DIR/traces_all.txt"
                                    << "$APP_DIR/traces_1.txt"
                                    << "$APP_DIR/traces_2.txt"
                                    << "$APP_DIR/traces_3.txt"
                                    << "$APP_DIR/traces_4.txt"
                                    << "$APP_DIR/traces_5.txt"
                                    << "$APP_DIR/traces_6.txt"
                                    << "$APP_DIR/traces_7.txt";

    g_FileName_SaveTracesForReaders.replaceInStrings( "$APP_DIR", currentPath );
    ui->lineEdit_SaveLogFileName->setText( "traces_all.txt" );

    // Set the text of sript's button
    on_pushButton_Script_contextMenu_Refresh( 1 );
    on_pushButton_Script_contextMenu_Refresh( 2 );
    on_pushButton_Script_contextMenu_Refresh( 3 );
    on_pushButton_Script_contextMenu_Refresh( 4 );

    //---
    //--- SoDevLog Graphique Control - SLineEditAutoComplete
    //---
    InitializeLineEditAutoCompleteCmd2Send();

    //---
    //--- Auto Completion for RelayAdress
    //---
    completerLineEdit_RelayAdress = new QCompleter(this);
    completerLineEdit_RelayAdress->setModel( modelFromFile_Cmd( Completion_List_RelayAdress_File_Name ) );
    completerLineEdit_RelayAdress->setCompletionMode(  QCompleter::PopupCompletion );

    ui->lineEdit_AddressMacTarget->setCompleter( completerLineEdit_RelayAdress );
    ui->lineEdit_AddressRelay_1->setCompleter( completerLineEdit_RelayAdress );
    ui->lineEdit_AddressRelay_2->setCompleter( completerLineEdit_RelayAdress );

//    ui->lineEdit_AddressMacTarget_3->setCompleter( completerLineEdit_RelayAdress );
//    ui->lineEdit_AddressRelay_1_Reader_3->setCompleter( completerLineEdit_RelayAdress );
//    ui->lineEdit_AddressRelay_2_Reader_3->setCompleter( completerLineEdit_RelayAdress );

//    ui->lineEdit_AddressMacTarget_5->setCompleter( completerLineEdit_RelayAdress );
//    ui->lineEdit_AddressRelay_1_Reader_5->setCompleter( completerLineEdit_RelayAdress );
//    ui->lineEdit_AddressRelay_2_Reader_5->setCompleter( completerLineEdit_RelayAdress );

    //---
    //--- Set the widht of the TableWidget's columns
    //---
    ui->tableWidget_History_Scripting_All->setColumnWidth(0,70);
    ui->tableWidget_History_Scripting_All->setColumnWidth(1,160);
    ui->tableWidget_History_Scripting_All->setColumnWidth(2,24);
    ui->tableWidget_History_Scripting_All->setColumnWidth(3,430);

    ui->tableWidget_History_Scripting_2->setColumnWidth(0,160);
    ui->tableWidget_History_Scripting_2->setColumnWidth(1,24);
    ui->tableWidget_History_Scripting_2->setColumnWidth(2,430);

    ui->tableWidget_History_Scripting_3->setColumnWidth(0,160);
    ui->tableWidget_History_Scripting_3->setColumnWidth(1,24);
    ui->tableWidget_History_Scripting_3->setColumnWidth(2,430);

    ui->tableWidget_History_Scripting_4->setColumnWidth(0,160);
    ui->tableWidget_History_Scripting_4->setColumnWidth(1,24);
    ui->tableWidget_History_Scripting_4->setColumnWidth(2,430);

    ui->tableWidget_History_Scripting_5->setColumnWidth(0,160);
    ui->tableWidget_History_Scripting_5->setColumnWidth(1,24);
    ui->tableWidget_History_Scripting_5->setColumnWidth(2,430);

    ui->tableWidget_History_Scripting_6->setColumnWidth(0,160);
    ui->tableWidget_History_Scripting_6->setColumnWidth(1,24);
    ui->tableWidget_History_Scripting_6->setColumnWidth(2,430);

    ui->tableWidget_History_Scripting_7->setColumnWidth(0,160);
    ui->tableWidget_History_Scripting_7->setColumnWidth(1,24);
    ui->tableWidget_History_Scripting_7->setColumnWidth(2,430);

    ui->tableWidget_History_Scripting_8->setColumnWidth(0,160);
    ui->tableWidget_History_Scripting_8->setColumnWidth(1,24);
    ui->tableWidget_History_Scripting_8->setColumnWidth(2,430);

    // TableWidget ReaderCOM
    ui->ReadersCOMTableWidget->setColumnWidth(COM_TABLE_OFFSET_FRIENDLY_NAME, 150);
    ui->ReadersCOMTableWidget->setColumnWidth(COM_TABLE_OFFSET_PORT, 90);
    ui->ReadersCOMTableWidget->setColumnWidth(COM_TABLE_OFFSET_BAUDRATE, 160);
    ui->ReadersCOMTableWidget->setColumnWidth(COM_TABLE_OFFSET_STATUS, 160);
    ui->ReadersCOMTableWidget->setColumnWidth(COM_TABLE_OFFSET_CONNECT, 110);

    // Try to disable combobox pfff seulement le premier item de la combo est disable !
//    QModelIndex index = ui->Reader_Serial_DataBits_List->model()->index(1, 0);
//    QVariant v(0);
//    ui->Reader_Serial_DataBits_List->model()->setData(index, v, Qt::UserRole - 1);

    //
    // Read Style Sheet
    //
    g_styleSheetForQTableWidgetItem = loadStyleSheet( "QSSTableWidget" );
    ui->tableWidget_History_Scripting_2->setStyleSheet( g_styleSheetForQTableWidgetItem );
    ui->tableWidget_History_Scripting_3->setStyleSheet( g_styleSheetForQTableWidgetItem );
    ui->tableWidget_History_Scripting_4->setStyleSheet( g_styleSheetForQTableWidgetItem );
    ui->tableWidget_History_Scripting_All->setStyleSheet( g_styleSheetForQTableWidgetItem );

    //
    // Set button's icon from ressources
    //
    // BRY_20150921
    g_Icon_Arrow_Green = QIcon(":/Icons/arrow_green.ico");
    g_Icon_Arrow_Red = QIcon(":/Icons/arrow_red.ico");

    //---
    //--- Load the main Style Sheet
    //---
    QString css_main = loadStyleSheet( "MainStyleSheet" );
    qApp->setStyleSheet( css_main );
}

CloverViewer::~CloverViewer()
{
    e_Result status = CL_ERROR;

    DEBUG_PRINTF("CloverViewer -----------------------------------------> Closing");

    status = csl_Close();

    DEBUG_PRINTF("CloverViewer: Close status: %s", status == 0 ? "OK" : "ERROR");

    c_xmemdbg_dump_state();
    as_trace_close();

    delete ui;
}

/*--------------------------------------------------------------------------*/

void CloverViewer::InitializeLineEditAutoCompleteCmd2Send()
{
    //---
    //--- SoDevLog Graphique Control - SLineEditAutoComplete
    //---

    //---
    //--- Auto Completion for lineEditAutoComplete_Cmd2Send_2
    //---
    completerLineEdit_Cmd2Send_2 = new QCompleter( this );
    completerLineEdit_Cmd2Send_2->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    completerLineEdit_Cmd2Send_2->setCompletionMode(  QCompleter::PopupCompletion );

    lineEditAutoComplete_Cmd2Send_2 = new SLineEditAutoComplete( this, Completion_List_Cmd_File_Name );
    lineEditAutoComplete_Cmd2Send_2->setCompleter( completerLineEdit_Cmd2Send_2 );
    lineEditAutoComplete_Cmd2Send_2->setModelFromFile();
    lineEditAutoComplete_Cmd2Send_2->setObjectName(tr("lineEditAutoComplete_Cmd2Send_2"));

    // Apply Style Sheet
    lineEditAutoComplete_Cmd2Send_2->setStyleSheet( loadStyleSheet( "StyleSheetLineEdit" ) );
    lineEditAutoComplete_Cmd2Send_2->setMinimumHeight( 25 );

    // Do like the send button is clicked
    connect( lineEditAutoComplete_Cmd2Send_2, SIGNAL( keyPressEnteredSignal() ), this, SLOT( on_pushButton_Cmd2Send_2_clicked() ) );
    ui->horizontalLayout_SendCmd_2->addWidget( lineEditAutoComplete_Cmd2Send_2 );

    //---
    //--- Auto Completion for lineEditAutoComplete_Cmd2Send_3
    //---
    completerLineEdit_Cmd2Send_3 = new QCompleter( this );
    completerLineEdit_Cmd2Send_3->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    completerLineEdit_Cmd2Send_3->setCompletionMode(  QCompleter::PopupCompletion );

    lineEditAutoComplete_Cmd2Send_3 = new SLineEditAutoComplete( this, Completion_List_Cmd_File_Name );
    lineEditAutoComplete_Cmd2Send_3->setCompleter( completerLineEdit_Cmd2Send_3 );
    lineEditAutoComplete_Cmd2Send_3->setModelFromFile();
    lineEditAutoComplete_Cmd2Send_3->setObjectName(tr("lineEditAutoComplete_Cmd2Send_3"));

    // Apply Style Sheet
    lineEditAutoComplete_Cmd2Send_3->setStyleSheet( loadStyleSheet( "StyleSheetLineEdit" ) );
    lineEditAutoComplete_Cmd2Send_3->setMinimumHeight( 25 );

    // Do like the send button is clicked
    connect( lineEditAutoComplete_Cmd2Send_3, SIGNAL( keyPressEnteredSignal() ), this, SLOT( on_pushButton_Cmd2Send_3_clicked() ) );
    ui->horizontalLayout_SendCmd_3->addWidget( lineEditAutoComplete_Cmd2Send_3 );

    //---
    //--- Auto Completion for lineEditAutoComplete_Cmd2Send_4
    //---
    completerLineEdit_Cmd2Send_4 = new QCompleter( this );
    completerLineEdit_Cmd2Send_4->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    completerLineEdit_Cmd2Send_4->setCompletionMode(  QCompleter::PopupCompletion );

    lineEditAutoComplete_Cmd2Send_4 = new SLineEditAutoComplete( this, Completion_List_Cmd_File_Name );
    lineEditAutoComplete_Cmd2Send_4->setCompleter( completerLineEdit_Cmd2Send_4 );
    lineEditAutoComplete_Cmd2Send_4->setModelFromFile();
    lineEditAutoComplete_Cmd2Send_4->setObjectName(tr("lineEditAutoComplete_Cmd2Send_4"));

    // Apply Style Sheet
    lineEditAutoComplete_Cmd2Send_4->setStyleSheet( loadStyleSheet( "StyleSheetLineEdit" ) );
    lineEditAutoComplete_Cmd2Send_4->setMinimumHeight( 25 );

    // Do like the send button is clicked
    connect( lineEditAutoComplete_Cmd2Send_4, SIGNAL( keyPressEnteredSignal() ), this, SLOT( on_pushButton_Cmd2Send_4_clicked() ) );
    ui->horizontalLayout_SendCmd_4->addWidget( lineEditAutoComplete_Cmd2Send_4 );

    //---
    //--- Auto Completion for lineEditAutoComplete_Cmd2Send_5
    //---
    completerLineEdit_Cmd2Send_5 = new QCompleter( this );
    completerLineEdit_Cmd2Send_5->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    completerLineEdit_Cmd2Send_5->setCompletionMode(  QCompleter::PopupCompletion );

    lineEditAutoComplete_Cmd2Send_5 = new SLineEditAutoComplete( this, Completion_List_Cmd_File_Name );
    lineEditAutoComplete_Cmd2Send_5->setCompleter( completerLineEdit_Cmd2Send_5 );
    lineEditAutoComplete_Cmd2Send_5->setModelFromFile();
    lineEditAutoComplete_Cmd2Send_5->setObjectName(tr("lineEditAutoComplete_Cmd2Send_5"));

    // Apply Style Sheet
    lineEditAutoComplete_Cmd2Send_5->setStyleSheet( loadStyleSheet( "StyleSheetLineEdit" ) );
    lineEditAutoComplete_Cmd2Send_5->setMinimumHeight( 25 );

    // Do like the send button is clicked
    connect( lineEditAutoComplete_Cmd2Send_5, SIGNAL( keyPressEnteredSignal() ), this, SLOT( on_pushButton_Cmd2Send_5_clicked() ) );
    ui->horizontalLayout_SendCmd_5->addWidget( lineEditAutoComplete_Cmd2Send_5 );

    //---
    //--- Auto Completion for lineEditAutoComplete_Cmd2Send_6
    //---
    completerLineEdit_Cmd2Send_6 = new QCompleter( this );
    completerLineEdit_Cmd2Send_6->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    completerLineEdit_Cmd2Send_6->setCompletionMode(  QCompleter::PopupCompletion );

    lineEditAutoComplete_Cmd2Send_6 = new SLineEditAutoComplete( this, Completion_List_Cmd_File_Name );
    lineEditAutoComplete_Cmd2Send_6->setCompleter( completerLineEdit_Cmd2Send_6 );
    lineEditAutoComplete_Cmd2Send_6->setModelFromFile();
    lineEditAutoComplete_Cmd2Send_6->setObjectName(tr("lineEditAutoComplete_Cmd2Send_6"));

    // Apply Style Sheet
    lineEditAutoComplete_Cmd2Send_6->setStyleSheet( loadStyleSheet( "StyleSheetLineEdit" ) );
    lineEditAutoComplete_Cmd2Send_6->setMinimumHeight( 25 );

    // Do like the send button is clicked
    connect( lineEditAutoComplete_Cmd2Send_6, SIGNAL( keyPressEnteredSignal() ), this, SLOT( on_pushButton_Cmd2Send_6_clicked() ) );
    ui->horizontalLayout_SendCmd_6->addWidget( lineEditAutoComplete_Cmd2Send_6 );

    //---
    //--- Auto Completion for lineEditAutoComplete_Cmd2Send_7
    //---
    completerLineEdit_Cmd2Send_7 = new QCompleter( this );
    completerLineEdit_Cmd2Send_7->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    completerLineEdit_Cmd2Send_7->setCompletionMode(  QCompleter::PopupCompletion );

    lineEditAutoComplete_Cmd2Send_7 = new SLineEditAutoComplete( this, Completion_List_Cmd_File_Name );
    lineEditAutoComplete_Cmd2Send_7->setCompleter( completerLineEdit_Cmd2Send_7 );
    lineEditAutoComplete_Cmd2Send_7->setModelFromFile();
    lineEditAutoComplete_Cmd2Send_7->setObjectName(tr("lineEditAutoComplete_Cmd2Send_7"));

    // Apply Style Sheet
    lineEditAutoComplete_Cmd2Send_7->setStyleSheet( loadStyleSheet( "StyleSheetLineEdit" ) );
    lineEditAutoComplete_Cmd2Send_7->setMinimumHeight( 25 );

    // Do like the send button is clicked
    connect( lineEditAutoComplete_Cmd2Send_7, SIGNAL( keyPressEnteredSignal() ), this, SLOT( on_pushButton_Cmd2Send_7_clicked() ) );
    ui->horizontalLayout_SendCmd_7->addWidget( lineEditAutoComplete_Cmd2Send_7 );

    //---
    //--- Auto Completion for lineEditAutoComplete_Cmd2Send_8
    //---
    completerLineEdit_Cmd2Send_8 = new QCompleter( this );
    completerLineEdit_Cmd2Send_8->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    completerLineEdit_Cmd2Send_8->setCompletionMode(  QCompleter::PopupCompletion );

    lineEditAutoComplete_Cmd2Send_8 = new SLineEditAutoComplete( this, Completion_List_Cmd_File_Name );
    lineEditAutoComplete_Cmd2Send_8->setCompleter( completerLineEdit_Cmd2Send_8 );
    lineEditAutoComplete_Cmd2Send_8->setModelFromFile();
    lineEditAutoComplete_Cmd2Send_8->setObjectName(tr("lineEditAutoComplete_Cmd2Send_8"));

    // Apply Style Sheet
    lineEditAutoComplete_Cmd2Send_8->setStyleSheet( loadStyleSheet( "StyleSheetLineEdit" ) );
    lineEditAutoComplete_Cmd2Send_8->setMinimumHeight( 25 );

    // Do like the send button is clicked
    connect( lineEditAutoComplete_Cmd2Send_8, SIGNAL( keyPressEnteredSignal() ), this, SLOT( on_pushButton_Cmd2Send_8_clicked() ) );
    ui->horizontalLayout_SendCmd_8->addWidget( lineEditAutoComplete_Cmd2Send_8 );
}

void CloverViewer::showEvent( QShowEvent *event )
{
    QMainWindow::showEvent( event );
}

/*--------------------------------------------------------------------------*/

void CloverViewer::ObjectsDisable()
{
    ui->pushButton_SetLocalRTC->setEnabled( false );
    ui->pushButton_SetRTCDistant->setEnabled( false );
    ui->pushButton_SetRoute->setEnabled( false );

    ui->pushButton_Script_1->setEnabled( false );
    ui->pushButton_Script_2->setEnabled( false );
    ui->pushButton_Script_3->setEnabled( false );
    ui->pushButton_Script_4->setEnabled( false );
}

/*--------------------------------------------------------------------------*/

void CloverViewer::ObjectsEnable()
{
    ui->pushButton_SetLocalRTC->setEnabled( true );
    ui->pushButton_SetRTCDistant->setEnabled( true );
    ui->pushButton_SetRoute->setEnabled( true );

    ui->pushButton_Script_1->setEnabled( true );
    ui->pushButton_Script_2->setEnabled( true );
    ui->pushButton_Script_3->setEnabled( true );
    ui->pushButton_Script_4->setEnabled( true );
}

/********************************************************************************/
/* Name :  e_Result ClViewer_Data2Read_cb( void **ppReader, void **ppTuple )    */
/* Description : custom callback on data reception for Reader 1                 */
/*                a copy of data shall be done at t                             */
/*                                                                              */
/********************************************************************************/
/* Parameters:                                                                  */
/*  --------------                                                              */
/*  In : void *pSem : semaphore to unlock                                       */
/* ---------------                                                              */
/*  Out: none                                                                   */
/* Return value: e_Result                                                       */
/*                          * OK                                                */
/*                          * ERROR: return if semaphore was not existing       */
/********************************************************************************/
e_Result ClViewer_Data2Read_cb( void *pReader, void *pTuple, e_Result eStatus )
{   
    if ( g_pClVw == CL_NULL )
        return ( CL_ERROR );

    DEBUG_PRINTF("ClViewer_Data2Read_cb: BEGIN");

    // signal data received to UI
    g_pClVw->UpdateScriptEditorDataReceived( pReader, pTuple, eStatus );

    DEBUG_PRINTF(" ClViewer_Data2Read_cb: END");
    return ( CL_OK );
}

/*****************************************************************************/
/* @fn :  e_Result ClViewer_SendDataDone_cb(void *pReader, clvoid *pTuple, e_Result status)
*
*   @brief : Data completion callback sent by porting layer
*
*  @param     *pReader                    ( In ) Reader which issued the data
*  @param    *pTuple                        ( In ) Tuple send to network
*  @param    status                        ( In ) status of the send to network
*  @return e_Result
* \n OK                        :  Result is OK
* \n ERROR,                    : Failure on execution
* \n MEM_ERR,                  :  Failure on memory management (failure,
* \n                                 allocation ....)
* \n PARAMS_ERR,               :  Inconsistent parameters
* \n TIMEOUT_ERR,              :  Overrun on timing
* ************************************************************************** */
e_Result ClViewer_SendDataDone_cb(void *ptReader, clvoid *ptTuple, e_Result status)
{
    DEBUG_PRINTF("<< ClViewer_SendDataDone_cb\n");
    if ( g_pClVw == CL_NULL )
        return ( CL_ERROR );

//    g_pClVw->UpdateScriptEditorWriteComplete( pReader, pTuple, status );

    t_Reader *pReader = (t_Reader *)ptReader;
    t_Tuple *pTuple = (t_Tuple *)ptTuple;
    //e_Result status = CL_ERROR;
    bool bFirstLoop = true;
    clu8 ucNibble;
    clu8    *pData = CL_NULL;

    bool isAnAckReceived = false;

//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

    /* check incoming parameters */
    if ( pReader == CL_NULL )
        return ( CL_ERROR );

    if ( pTuple == CL_NULL )
        return ( CL_ERROR );


    if ( pReader->tType == COM_READER_TYPE )
    {
        pData = pReader->tCOMParams.aucPortName;
    }
    if (( pReader->tType == IP_READER_TYPE ) | ( pReader->tType == LANTRONIX_READER_TYPE ))
        pData = pReader->tIPParams.aucIpAddr;

    if ( pReader->tType == BT_READER_TYPE )
        pData = pReader->aucLabel;

    if ( pData == CL_NULL )
        return ( CL_ERROR );

    QString *pSReaderInfo = new QString( (const cl8*)pData );
    if ( !pSReaderInfo) return ( CL_ERROR );
    if ( pSReaderInfo->isEmpty() == true )
    {
        DEBUG_PRINTF("Clover received Reader info... empty!! =Exit\n");
        return ( CL_ERROR );
    }


    /* parse tuples coming from underlayers */
//    while ( pTuple != CL_NULL )
    {
        if ( pTuple->ptBuf == CL_NULL)
            return ( CL_ERROR );

        if ( pTuple->ptBuf->pData == CL_NULL )
            return ( CL_ERROR );

        if ( pTuple->ptBuf->ulLen > 512 )
        {
            DEBUG_PRINTF("CAUTION!!! UpdateScriptEditorWriteComplete: PB in Buffer len %d\n", pTuple->ptBuf->ulLen );
            return( CL_ERROR );
        }

        if ( pTuple->ptBuf->ulLen == 0)
        {
            DEBUG_PRINTF("Clover Error: length of data is null.. Exit\n");
            return( CL_ERROR );
        }

        if ( pTuple->ptBuf->ulLen == 1 /*&& ui->checkBox_AckDisplay->isChecked() == false*/ )
        {
            DEBUG_PRINTF("ClViewer_SendDataDone_cb: ACK_RECEIVED: %d", pTuple->ptBuf->ulLen);
            isAnAckReceived = true;
        }

        /* Reader name in general view*/
        QString *pSReaderName = new QString( (const char *)pData );
        if ( !pSReaderName ) return ( CL_ERROR );
        if ( pSReaderName->isEmpty() == true )
        {
            DEBUG_PRINTF("Clover received Reader name... empty!! =Exit\n");
            return( CL_ERROR );
        }

        /*  Convert Data for display */
        QString *pSData2Display = new QString;
        g_pClVw->ConvertByteToAscii( pTuple->ptBuf->pData, pTuple->ptBuf->ulLen, pSData2Display );
        if ( pSData2Display->isEmpty() == true )
        {
            DEBUG_PRINTF("Clover Error: no data received.. Exit\n");
            return( CL_ERROR );
        }
        QByteArray  BA = pSData2Display->toLocal8Bit();
        const char *pData2Display = BA.data();

        /* Tuple Time Tagged */
        QString *pSTimeString = new QString( (const char *)pTuple->cl8Time );
        QByteArray BATime = pSTimeString->toLocal8Bit();
        const char *pTime2Display = BATime.data();

        DEBUG_PRINTF("the reader %s send %d \n", pReader->aucLabel, pTuple->ptBuf->ulLen );

        if ( QMetaObject::invokeMethod( g_pClVw, "updateCSLDataActivitySignal", Qt::QueuedConnection, \
                                        Q_ARG( bool, isAnAckReceived),
                                        Q_ARG( QString,     (const char *)pReader->aucLabel), /* SReaderInfo */\
                                        Q_ARG( QString ,    (const char *)pData), /* Reader name */ \
                                        Q_ARG( QString,     (const char *)pData2Display), /* SData2Display */ \
                                        Q_ARG( QString ,    (const char *)pTuple->cl8Time), /* STime2Display */ \
                                        Q_ARG( QString ,    (const char *)"Tx" ) \
                                        ) == true )
        {
            DEBUG_PRINTF("Method correct");
        }
        else
        {
            DEBUG_PRINTF("Failed to call méthode");
        }

 //       emit g_pClVw->updateCSLDataActivitySignal( false, pSReaderInfo, pSReaderName, pSData2Display,  pSTimeString, pSDirection );
         if ( QMetaObject::invokeMethod( g_pClVw, "updateDataCounterSignal", Qt::QueuedConnection, Q_ARG( qint32, g_pClVw->m_iPacketsSent++), Q_ARG( bool, false) ) == true )
         {
             DEBUG_PRINTF("Method correct\n");
         }
         else
         {
             DEBUG_PRINTF("Failed to call méthode\n");
         }

//        emit g_pClVw->updateDataCounterSignal( (int)g_pClVw->m_iPacketsSent++, (bool)false );

         if ( g_pClVw->LogMode() == true )
         {
             if ( QMetaObject::invokeMethod
                  (
                        g_pClVw, "LogSignal", Qt::QueuedConnection,
                        Q_ARG( bool, isAnAckReceived),
                        Q_ARG( QString,     (const char *)pReader->aucLabel), /* ReaderName */
                        Q_ARG( QString ,    (const char *)pData), /* ReaderInfo */
                        Q_ARG( QString,     (const char *)pData2Display), /* Data2Display */
                        Q_ARG( QString ,    (const char *)pTime2Display), /* TimeString */
                        Q_ARG( QString ,    (const char *)"Tx") /* Direction */
                  ) == true
                )
             {
                 DEBUG_PRINTF("QMetaObject::invokeMethod: LogSignal: OK");
             }
             else
             {
                 DEBUG_PRINTF("QMetaObject::invokeMethod: LogSignal: FAILED !");
             }
         }
    }
    DEBUG_PRINTF("ClViewer_SendDataDone_cb >>\n");
    return ( CL_OK );
}

/*--------------------------------------------------------------------------*\
 * Draw the two TableWidgets :
 * the one for "All readers" that have an extra column : ReaderName
 * the one for the "Concerned reader"
 *--------------------------------------------------------------------------*
 * Remarques :
 * On ne peut pas créer qu'un seul QTableWidgetItem et l'affecter dans les
 * 2 TableWidget le résultat est alors "vide"
 * Surprise :
 * QTableWidgetItem n'a pas de styleSheet !
\*--------------------------------------------------------------------------*/
void CloverViewer::updateCSLDataActivitySlot( bool bDataReceived, QString SReaderInfo, QString SReaderName, QString SData2Display, QString STimeString, QString SDirection )
{
    int iRowsCount;
    int iReaderRowsCount;
    QTableWidget *pReaderTableWidget ;

    DEBUG_PRINTF2("updateCSLDataActivitySlot: BEGIN");

    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    if ( bDataReceived == true && ui->checkBox_AckDisplay->isChecked() == false ) // DoNotDisplayAckReceived
        return;

    QTableWidget *pTableWidget_All = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_All");
    if ( !pTableWidget_All ) return;

    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
    if ( !pTabTerms )
        return;

    if ( pTabTerms )
    {
        bool bTabFound = false;
        cl8 cl8Index = 0;
        for ( int i = 1; i < pTabTerms->count(); i++ )
        {
            if ( !QString::compare( pTabTerms->tabText(i), SReaderInfo, Qt::CaseInsensitive) )
            {
                bTabFound = true;
                cl8Index = i;

                break;
            }
        }

        if ( bTabFound == true )
        {
            switch ( cl8Index )
            {
                case 1:
                {
                    pReaderTableWidget = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_2");
                    break;
                }
                case 2:
                {
                    pReaderTableWidget = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_3"); break;
                }
                case 3:
                {
                    pReaderTableWidget = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_4"); break;
                }
                case 4:
                {
                    pReaderTableWidget = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_5"); break;
                }
                case 5:
                {
                    pReaderTableWidget = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_6"); break;
                }
                case 6:
                {
                    pReaderTableWidget = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_7"); break;
                }
                case 7:
                {
                    pReaderTableWidget = pWin->findChild<QTableWidget*>("tableWidget_History_Scripting_8"); break;
                }
                default:
                {
                    pReaderTableWidget = CL_NULL; break;
                }
            }
        }
    }

    //-----------------------------------------------
    // BRY_02122015 - Log Data in MainLog File
    // if directory "log" does not exist nothing
    // happend
    //-----------------------------------------------
    if ( g_FileName_SaveTracesForReaders[ 0 ] != "" )
    {
        QString fileName  = QString( g_FileName_SaveTracesForReaders[ 0 ] );
        QFile file( fileName );

        // If file don't exit it's created otherwise appended
        file.open(QIODevice::WriteOnly | QIODevice::Append);

        // Remove white spaces
        QString theTime = STimeString;
        QString theReaderName = SReaderInfo;
        theReaderName.replace(" ", "");
        QString theDirection = SDirection;
    //    theDirection.replace(" ", ""); inutile avec le changement " E " en "Tx"
        QString theData2Display = SData2Display;
        theData2Display.replace(" ", "");

        QString line_file = QString("[%1]:%2:%3:%4\r\n").arg( theTime, theReaderName, theDirection, theData2Display );
        file.write(line_file.toLatin1());

        file.close();
    }

    //-- End of - Log Data in MainLog File ----------

    iRowsCount = pTableWidget_All->rowCount();
    pTableWidget_All->insertRow( iRowsCount ) ;

    if ( pReaderTableWidget != CL_NULL )
    {
        iReaderRowsCount = pReaderTableWidget->rowCount();
        pReaderTableWidget->insertRow( iReaderRowsCount ) ;
    }

    // Create color for Tx row
    QColor color_background = QColor("#FFFFFF");
    if ( QString::compare( "Tx", SDirection ) == 0 )
    {
        color_background = QColor("#D5D5D5");
    }

    //
    // SReaderInfo for All
    //
    QTableWidgetItem *item_0 = new QTableWidgetItem( SReaderInfo );
    item_0->setFlags( item_0->flags() & ~Qt::ItemIsEditable ); // set item non-editable
    item_0->setBackground( color_background );
    pTableWidget_All->setItem( iRowsCount, SCRIPT_COLUMN_READER_NAME, item_0);

    //
    // SData2Display
    //
    QTableWidgetItem *item_1 = new QTableWidgetItem( SData2Display );
    item_1->setFlags( item_1->flags() & ~Qt::ItemIsEditable ); // set item non-editable
    item_1->setBackground( color_background );
    pReaderTableWidget->setItem( iReaderRowsCount, (SCRIPT_COLUMN_DATA + SCRIPT_GENERAL_TO_READER_COLUMN_OFFSET), item_1 );
    QTableWidgetItem *item_All_1 = new QTableWidgetItem( SData2Display );
    item_All_1->setFlags( item_All_1->flags() & ~Qt::ItemIsEditable ); // set item non-editable
    item_All_1->setBackground( color_background );
    pTableWidget_All->setItem( iRowsCount, SCRIPT_COLUMN_DATA, item_All_1);

    //
    // STimeString
    //
    QTableWidgetItem *item_2 = new QTableWidgetItem( STimeString );
    item_2->setFlags( item_2->flags() & ~Qt::ItemIsEditable ); // set item non-editable
    item_2->setBackground( color_background );
    pReaderTableWidget->setItem( iReaderRowsCount, (SCRIPT_COLUMN_TIME + SCRIPT_GENERAL_TO_READER_COLUMN_OFFSET), item_2 );
    QTableWidgetItem *item_All_2 = new QTableWidgetItem( STimeString );
    item_All_2->setFlags( item_All_2->flags() & ~Qt::ItemIsEditable ); // set item non-editable
    item_All_2->setBackground( color_background );
    pTableWidget_All->setItem( iRowsCount, SCRIPT_COLUMN_TIME, item_All_2 );

    //
    // SDirection
    //
    QTableWidgetItem *item_3 = new QTableWidgetItem( SDirection );
    item_3->setFlags( item_3->flags() & ~Qt::ItemIsEditable ); // set item non-editable
    item_3->setBackground( color_background );
    pReaderTableWidget->setItem( iReaderRowsCount, (SCRIPT_COLUMN_DIRECTION + SCRIPT_GENERAL_TO_READER_COLUMN_OFFSET), item_3 );
    QTableWidgetItem *item_All_3 = new QTableWidgetItem( SDirection );
    item_All_3->setFlags( item_All_3->flags() & ~Qt::ItemIsEditable ); // set item non-editable
    item_All_3->setBackground( color_background );
    pTableWidget_All->setItem( iRowsCount, SCRIPT_COLUMN_DIRECTION, item_All_3 );

    // Scroll down the two TableWidget
    pTableWidget_All->scrollToBottom();
    pReaderTableWidget->scrollToBottom();

    DEBUG_PRINTF2("updateCSLDataActivitySlot: END");
}

/********************************************************************************/
/* Name :  e_Result ClViewer_OTAProgress_cb ( clu32 eState, clvoid *pReader, clu32 u32Progress ) */
/* Description : ota progress   :    if u32Progress = 0xFF => OTA Failed        */
/*                                                                                */
/********************************************************************************/
/* Parameters:                                                                    */
/*  --------------                                                                */
/*                                                                              */
/* ---------------                                                                */
/*  Out: none                                                                     */
/* Return value: e_Result                                                          */
/*                          * OK                                                  */
/*                          * ERROR: return if semaphore was not existing         */
/********************************************************************************/
e_Result ClViewer_OTAProgress_cb( clu32 eState, clvoid *ptReader, clu32 u32Progress )
{
    e_Result     eStatus = CL_ERROR;
    t_Reader     *pReader = ( t_Reader *)ptReader;

    DEBUG_PRINTF("<< ClViewer_OTAProgress_cb %d\n", u32Progress);

    if ( u32Progress == 16 )
    {
        DEBUG_PRINTF("<< ClViewer_OTAProgress_cb %d\n", u32Progress);
    }
    if ( g_pClVw == CL_NULL )
        return ( CL_ERROR );

    qint32 q32State = eState;
    qint32 q32Progress = u32Progress;
    if ( QMetaObject::invokeMethod( g_pClVw, "updateOTAProgressBarSignal", Qt::QueuedConnection, \
                                    Q_ARG( qint32, q32State), \
                                    Q_ARG( qint32, q32Progress) \
                                    ) == true )
    {
        DEBUG_PRINTF("Method correct\n");
    }
    else
    {
        DEBUG_PRINTF("Failed to call méthode\n");
    }


    // signal data received to UI
//    g_pClVw->UpdateClViewOTAProgress( eState, ptReader, u32Progress );

    DEBUG_PRINTF("ClViewer_OTAProgress_cb >>\n");
    return( CL_OK );
}


/********************************************************************************/
/* Name :  e_Result ClViewer_IOStateChanged_cb( void **ppReader, void **ppTuple ) */
/* Description : reader status changed (CONNECT/DISCONNECT/ERROR....)           */
/*                                                                                */
/********************************************************************************/
/* Parameters:                                                                    */
/*  --------------                                                                */
/*                                                                              */
/* ---------------                                                                */
/*  Out: none                                                                     */
/* Return value: e_Result                                                          */
/*                          * OK                                                  */
/*                          * ERROR: return if semaphore was not existing         */
/********************************************************************************/
e_Result ClViewer_IOStateChanged_cb( clvoid *pCtxt, clvoid *ptReader, clvoid *ptDevice, e_Result result )
{            

    e_Result     eStatus = CL_ERROR;
    t_Reader     *pReader = ( t_Reader *)ptReader;
    t_Device     *pDevice = ( t_Device *)ptDevice;
    cl8            *pDesc    =    CL_NULL;

    DEBUG_PRINTF("<< ClViewer_IOStateChanged_cb");
    if ( ( !pCtxt ) & ( !pReader ) & ( !pDevice ) )
        return ( CL_ERROR );

    // check if the state change is coming from the framework
    if ( pCtxt )
        DEBUG_PRINTF("State update from Framework");

    if ( g_pClVw == CL_NULL )
        return ( CL_ERROR );

    // signal data received to UI
    g_pClVw->UpdateClViewReaderDeviceState( pCtxt, pReader, pDevice );

    DEBUG_PRINTF("ClViewer_IOStateChanged_cb >>");
    return( CL_OK );
}

/*--------------------------------------------------------------------------*/

void CloverViewer::UpdateClViewReaderDeviceState( void *pCtxt, void *ptReader, void *ptDevice )
{
    t_Reader *pReader = ( t_Reader *)ptReader;
    t_Device *pDevice = ( t_Device *)ptDevice;
    cl8            *pDesc    =    CL_NULL;

    if ( ( pReader == CL_NULL ) & (pCtxt == CL_NULL) & ( pDevice == CL_NULL )) return;

    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    if (pReader->tType == COM_READER_TYPE )
        pDesc = (cl8 *)pReader->tCOMParams.aucPortName;

    if ( pReader->tType == IP_READER_TYPE )
        pDesc = (cl8 *) pReader->aucLabel;


    //
    if ( pReader )
    {
        switch ( pReader->eState )
        {
            case ( STATE_INIT ):            /// indicates a reader is added to a list
                DEBUG_PRINTF2("UpdateClViewReaderDeviceState: Reader %s STATE_INIT", pDesc );
                break;
            case ( STATE_CONNECT ):        /// asks to connect a reader to its IOs layer, indicates that a reader was successfully connected to its IOs layer
                DEBUG_PRINTF2("UpdateClViewReaderDeviceState: Reader %s: STATE CONNECT", pDesc );
                break;
            case ( STATE_DISCONNECT ):    /// asks to disconnect a reader from its IOs layer, indicates that a reader was successfully disconnected from its IOs layer
                DEBUG_PRINTF2("UpdateClViewReaderDeviceState: Reader  %s: STATE DISCONNECT", pDesc );
                break;
            case ( STATE_ERROR ):        /// indicates that a reader got an error from its underlaying IOs layer
                DEBUG_PRINTF2("UpdateClViewReaderDeviceState: Reader  %s: STATE ERROR", pDesc );
                break;
            case ( STATE_DISCOVER ):
                DEBUG_PRINTF2("UpdateClViewReaderDeviceState: Reader  %s: STATE DISCOVER", pDesc );
                break;
            case ( STATE_OTA ):
                DEBUG_PRINTF2("UpdateClViewReaderDeviceState: Reader  %s: STATE OTA", pDesc );
                break;

            default:
                DEBUG_PRINTF2("UpdateClViewReaderDeviceState: Unknown State from Reader %s", pDesc );
                break;
        }
        // FD_STUFF_SHIT_STINK
        DEBUG_PRINTF2("UpdateClViewReaderDeviceState: EMIT");
        emit g_pClVw->updateClViewReaderDeviceSgnl( pCtxt, ptReader, ptDevice );
        emit g_pClVw->updateClViewOTAReaderSgnl( pCtxt, ptReader, ptDevice );
    }

    // check if the state change is coming from a device
    if ( pDevice )
    {
        switch ( pDevice->eState )
        {
        case ( STATE_INIT ):            /// indicates a reader is added to a list
            DEBUG_PRINTF2("State update from Device %s STATE_INIT", pDevice->aucLabel );
            break;
        case ( STATE_CONNECT ):        /// asks to connect a reader to its IOs layer, indicates that a reader was successfully connected to its IOs layer
            DEBUG_PRINTF2("State update from Device %s: STATE CONNECT", pDevice->aucLabel );
            break;
        case ( STATE_DISCONNECT ):    /// asks to disconnect a reader from its IOs layer, indicates that a reader was successfully disconnected from its IOs layer
            DEBUG_PRINTF2("State update from Device %s : STATE_DISCONNECT", pDevice->aucLabel );
            break;
        case ( STATE_ERROR ):        /// indicates that a reader got an error from its underlaying IOs layer
            DEBUG_PRINTF2("State update from Device %s: STATE_ERROR", pDevice->aucLabel );
            break;
        default:
            DEBUG_PRINTF2("Unknown State update from Device %s", pDevice->aucLabel );
            break;
        }
    }

}

/**************************************************************************************************/
/* update Clover Viewer Reader's list view and OTA view on event of connection/disconnection      */
/**************************************************************************************************/
void CloverViewer::updateClViewReaderDeviceWindow( void *pCtxt, void *ptReader, void *ptDevice )
{
    int iIndex = 0;
    bool bFound = false;

    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    QTableWidget    *pTableView                         =   CL_NULL;


    t_Reader    *pReader = ( t_Reader *) ptReader ;

    // check if the state change is coming from a reader
    if ( pReader )
    {
        DEBUG_PRINTF2("updateClViewReaderDeviceWindow: Slot_BEGIN");

        // if no radio address for this reader... do not display
/*        bool bRadioAddressFound = false;
        for ( int i=0; i < sizeof( pReader->tCloverSense.au8RadioAddress) ; i++ )
        {
            if ( pReader->tCloverSense.au8RadioAddress[i] != 0)
            {
                 bRadioAddressFound = true;
                 break;
            }
        }
        if ( bRadioAddressFound == false)
            return;
*/

        // get table widget encryption key reader to add/remove the reader used in OTA
//        pTableWidgetEncryptionKeyReader = pWin->findChild<QTableWidget *>("tableWidget_OTAEncryptionDongle");
//        if ( !pTableWidgetEncryptionKeyReader )
//            return;

        // get table widget for transmission reader used in OTA
//        pTableWidgetTransmissionReader = pWin->findChild<QTableWidget *>("tableWidget_OTATransmissionReader");
//        if ( !pTableWidgetTransmissionReader)
//            return;

        // for Serial reader
        if ( pReader->tType == COM_READER_TYPE )
        {
            /* get COM reader table pointer */
            pTableView = pWin->findChild<QTableWidget *>("ReadersCOMTableWidget");
            if ( !pTableView ) return;

            //
            QByteArray BA4Display = QByteArray( (const cl8*) pReader->tCOMParams.aucPortName, strlen( (const cl8*)pReader->tCOMParams.aucPortName));

            // check if reader is already in the list. If not, add it
            QString st2Compare = QString(BA4Display);
            if ( st2Compare.isEmpty() == true )
            {
                DEBUG_PRINTF("Clover error!! We received a reader with no name => exit\n");
                return;
            }
            for ( int i = 0; i < pTableView->rowCount(); i++ )
            {
                // we found the reader for this message => change information
                if ( QString::compare( st2Compare, pTableView->item(i, COM_TABLE_OFFSET_PORT )->text()) == 0)
                {
                    bFound = true;
                    iIndex = i;
                    break;
                }
            }

            int jIndex = 0;
            if ( bFound == false )
            {
                iIndex = pTableView->rowCount();
                jIndex = pTableView->columnCount() - 1;
                pTableView->insertRow( iIndex );
            }

            QTableWidgetItem *newItem = CL_NULL;

            // add memory place holder for the new row
            for ( int k = 0; k < pTableView->columnCount(); k++)
            {
                newItem =  new QTableWidgetItem( QTableWidgetItem::Type );
                pTableView->setItem( iIndex,k , newItem );
            }

            //-------------
            // set properties for current row
            //-------------
            // set friendly name
            pTableView->item( iIndex, COM_TABLE_OFFSET_FRIENDLY_NAME)->setText( (const QString &) QString( QByteArray((const cl8*)pReader->aucLabel, strlen( (const cl8*)pReader->aucLabel) ) ));

            // set friendly name
            pTableView->item( iIndex, COM_TABLE_OFFSET_PORT)->setText( (const QString &) QString( QByteArray( (const cl8*)pReader->tCOMParams.aucPortName, strlen( (const cl8*)pReader->tCOMParams.aucPortName) ) ));

            // set baudrate
            switch (pReader->tCOMParams.eBaudRate)
            {
                case (CL_COM_BAUDRATE_4800):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_BAUDRATE)->setText( (const QString &) QString("4800"));
                    break;
                }
                case (CL_COM_BAUDRATE_9600):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_BAUDRATE)->setText( (const QString &) QString("9600"));
                    break;
                }
                case (CL_COM_BAUDRATE_19200):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_BAUDRATE)->setText( (const QString &) QString("19200"));
                    break;
                }
                case (CL_COM_BAUDRATE_38400):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_BAUDRATE)->setText( (const QString &) QString("38400"));
                    break;
                }
                case (CL_COM_BAUDRATE_57600):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_BAUDRATE)->setText( (const QString &) QString("57600"));
                    break;
                }
                case (CL_COM_BAUDRATE_115200):
                default:
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_BAUDRATE)->setText( (const QString &) QString("115200"));
                    break;
                }
            }

            // set communication status
            switch ( pReader->eState )
            {
                case ( STATE_DEFAULT ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("Default"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Unchecked);
                    break;
                }
                case ( STATE_INIT ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("Init"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Unchecked);
                    break;
                }
                case ( STATE_CONNECT ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("Connected"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Checked);
                    break;
                }
                case ( STATE_DISCONNECT ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("Disconnected"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Unchecked);
                    break;
                }
                case ( STATE_DISCOVER ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("Discover"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Unchecked);
                    break;
                }
                case ( STATE_ERROR ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("Error"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Unchecked);
                    break;
                }
                case ( STATE_OK ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("Ok"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Unchecked);
                    break;
                }
                case ( STATE_OTA ):
                {
                    pTableView->item( iIndex, COM_TABLE_OFFSET_STATUS)->setText(QString("in OTA"));
                    pTableView->item( iIndex, COM_TABLE_OFFSET_CONNECT)->setCheckState(Qt::Checked);
                    break;
                }
            }

            //
            // Update flags for non editable TableWidget Items
            //
            for ( int row = 0; row < pTableView->rowCount(); row ++ )
            {
                for ( int col = 0; col < pTableView->columnCount(); col ++ )
                {
                    QTableWidgetItem *item = pTableView->item( row, col );
                    item->setFlags( item->flags() & ~Qt::ItemIsEditable );
                }
            }
        }
    }
}

/*--------------------------------------------------------------------------*/

void CloverViewer::InitializeReadersTab()
{
    e_Result    status  = CL_ERROR;
    t_Reader    *pReader    =   CL_NULL;
    t_Reader    *pNextReader = CL_NULL;

    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPAddress");
    pLineEdit->hide();

    pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPPort");
    pLineEdit->hide();

    QComboBox *pComboBox = pWin->findChild<QComboBox*>("comboBox_ReaderSerialPort");
    pComboBox->hide();

    pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_BaudRate_List");
    pComboBox->hide();

    pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_DataBits_List");
    pComboBox->hide();

    pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_ParityBits_List");
    pComboBox->hide();

    pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_StopBits_List");
    pComboBox->hide();

    QLabel *pLabel = pWin->findChild<QLabel*>("Reader_IPAddress_Label");
    pLabel->hide();

    pLabel = pWin->findChild<QLabel*>("Reader_IPTcpPort_Label");
    pLabel->hide();

    pLabel = pWin->findChild<QLabel*>("Reader_SerialBaudRate_Label");
    pLabel->hide();

    pLabel = pWin->findChild<QLabel*>("Reader_SerialCOMPort_Label");
    pLabel->hide();

    pLabel = pWin->findChild<QLabel*>("Reader_SerialDataBits_Label");
    pLabel->hide();

    pLabel = pWin->findChild<QLabel*>("Reader_SerialParityBits_Label");
    pLabel->hide();

    pLabel = pWin->findChild<QLabel*>("Reader_SerialStopBits_Label");
    pLabel->hide();

    // update UI
    status = cl_getReader_List( &pReader );

    if ( CL_SUCCESS( status ) )
    {
        while ( pReader)
        {
            pNextReader = pReader->pNext;

            updateClViewReaderDeviceWindow( CL_NULL, pReader, CL_NULL );
            pReader = pReader->pNext ;
        }
    }

    //
    // Set the only available communication mode : Serial Reader
    //
    // BRY_02122015
    CloverViewer::on_Reader_ConnectionType_List_activated( QString("Serial Reader"));
    ui->Reader_ConnectionType_List->setCurrentIndex( 1 );
    ui->Reader_ConnectionType_List->setItemData( 0, "DISABLE", Qt::UserRole - 1 );
    ui->Reader_ConnectionType_List->setItemData( 2, "DISABLE", Qt::UserRole - 1 );
    ui->Reader_ConnectionType_List->setItemData( 3, "DISABLE", Qt::UserRole - 1 );
}

//void CloverViewer::InitializeOTATables( )
//{
//    e_Result status     = CL_ERROR;
//    clu8 *pValue        = CL_NULL;
//    clu32 u32ValueLen   = 0;

//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

///*    QTableView *pTableView = pWin->findChild<QTableView*>("tableView_CompatibleTargetFWVersion");
//    clLineRollingListDelegate *delegate = new clLineRollingListDelegate( this );
//    clLineRollinglistModel rollingLinesModel( this, 2, 3 );

//    QItemSelectionModel *m = pTableView->selectionModel();  // get former model
//    pTableView->setModel( &rollingLinesModel );                  // apply new on
//    delete m;
//    QModelIndex nIndex = rollingLinesModel.index(0,0);
//    QVariant DataToSet("toto");
//    rollingLinesModel.setData( nIndex, DataToSet, Qt::EditRole );

//    for ( i = 0; i < rollingLinesModel.rowCount(); i++)
//        pTableView->openPersistentEditor( rollingLinesModel.index( i, 1) );

//    QWidget *pFileWidget = pWin->findChild<QWidget*>("FileOTA");

//*/

//    //    rollingLines.setData( );

//    //    pTableView->setRootIndex(&rollingLines.index());
//    //    pTableView->dataChanged();// free memory of the former model
//        //pTableView->updateGeometry();
//        //emit dataChanged( );

//    //*********************************
//    // OTA : File Tab init
//    //*********************************
//    /* save encryption file name */
//    QLabel *pOTA_FWFile = pWin->findChild<QLabel*>("label_OTA_FirmwareFile");
//    if (!pOTA_FWFile)  return;
//    status = cl_GetParams( (clu8*)"encryption_file_name", strlen("encryption_file_name"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    pOTA_FWFile->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));
//    cl_FreeMem( pValue );


//    /* Platformm identifier : target firmware number*/
//    QLineEdit *pLineToSet = pWin->findChild<QLineEdit*>("PlatformIdentifier");
//    if ( !pLineToSet ) return;

//    status = cl_GetParams( (clu8*)"target_device_firmware_number", strlen("target_device_firmware_number"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    pLineToSet->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));
//    cl_FreeMem( pValue );

//    /* */
//    pLineToSet = pWin->findChild<QLineEdit*>("FW_version");
//    if ( !pLineToSet ) return;

//    status = cl_GetParams( (clu8*)"new_firmware_version", strlen("new_firmware_version"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    pLineToSet->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));
//    cl_FreeMem( pValue );

//    /* use as rescue */
//    status = cl_GetParams( (clu8*)"use_as_rescue", strlen("use_as_rescue"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;

//    QCheckBox *pCheckBox = pWin->findChild<QCheckBox*>("use_as_rescue");
//    if (!pCheckBox) return;

//    if ( *pValue=='1')
//        pCheckBox->setEnabled( true );
//    else
//        pCheckBox->setEnabled( false );
//    cl_FreeMem( pValue );

//    /* device_current_version_start_range */
//    QTableWidget *pTableView = pWin->findChild<QTableWidget *>("tableWidget_CompatibleTargetFWVersion");

//    status = cl_GetParams( (clu8*)"device_current_version_start_range", strlen("device_current_version_start_range"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    /* set value in the table */
//    QTableWidgetItem *newItem = new QTableWidgetItem(QString(QByteArray((const char *)pValue, (int)u32ValueLen)));
//    pTableView->setItem(0, 0, newItem);
//    cl_FreeMem( pValue );


//    /* get ota_password */
//    QLineEdit *pPlatformId = pWin->findChild<QLineEdit*>("ota_password");
//    if ( !pPlatformId ) return;

//    status = cl_GetParams( (clu8*)"ota_password", strlen("ota_password"), &pValue, &u32ValueLen, CL_OK );
//    QByteArray BA4Display((const cl8*)pValue, u32ValueLen);
//    QString String4Display(BA4Display);
//    pPlatformId->setText((const QString &)String4Display);
//    cl_FreeMem( pValue );


//    /* device_current_version_end_range */
//    status = cl_GetParams( (clu8*)"device_current_version_end_range", strlen("device_current_version_end_range"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;

//    /* set value in the table */
//    newItem = new QTableWidgetItem(QString(QByteArray((const char *)pValue, (int)u32ValueLen)));
//    pTableView->setItem(0, 1, newItem);
//    cl_FreeMem( pValue );


//    pTableView->verticalHeader()->hide();

//    //*********************************
//    // OTA : Encrypt tab
//    //*********************************

//    pLineToSet = pWin->findChild<QLineEdit*>("EncryptionPassword");
//    if ( !pLineToSet ) return;

//    status = cl_GetParams( (clu8*)"encryption_password", strlen("encryption_password"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    pLineToSet->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));
//    cl_FreeMem( pValue );

//    pLineToSet = pWin->findChild<QLineEdit*>("encryption_master_certificate_validity");
//    if ( !pLineToSet ) return;
//    cl_FreeMem( pValue );

//    status = cl_GetParams( (clu8*)"encryption_master_certificate_validity", strlen("encryption_master_certificate_validity"), &pValue, &u32ValueLen, CL_OK );
//    pLineToSet->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));
//    if (!pValue)
//        return;
//    cl_FreeMem( pValue );

//    pLineToSet = pWin->findChild<QLineEdit*>("encryption_slave_certificate");
//    if ( !pLineToSet ) return;
//    status = cl_GetParams( (clu8*)"encryption_slave_certificate", strlen("encryption_slave_certificate"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    pLineToSet->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));
//    cl_FreeMem( pValue );


//    /* encryption_master_certificate_start_address*/
//    pTableView = pWin->findChild<QTableWidget *>("tableWidget_MasterAddressRange");

//    status = cl_GetParams( (clu8*)"encryption_master_certificate_start_address", strlen("encryption_master_certificate_start_address"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    /* set value in the table */
//    newItem = new QTableWidgetItem(QString(QByteArray((const char *)pValue, (int)u32ValueLen)));
//    pTableView->setItem(0, 0, newItem);
//    cl_FreeMem( pValue );

//    /* encryption_master_certificate_end_address */
//    status = cl_GetParams( (clu8*)"encryption_master_certificate_end_address", strlen("encryption_master_certificate_end_address"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    /* set value in the table */
//    newItem = new QTableWidgetItem(QString(QByteArray((const char *)pValue, (int)u32ValueLen)));
//    pTableView->setItem(0, 1, newItem);
//    cl_FreeMem( pValue );

//    pTableView->verticalHeader()->hide();

//    /* encryption_slave_certificate_start_address*/
//    pTableView = pWin->findChild<QTableWidget *>("tableWidget_SlaveAddressRange");

//    status = cl_GetParams( (clu8*)"encryption_slave_certificate_start_address", strlen("encryption_slave_certificate_start_address"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    /* set value in the table */
//    newItem = new QTableWidgetItem(QString(QByteArray((const char *)pValue, (int)u32ValueLen)));
//    pTableView->setItem(0, 0, newItem);
//    cl_FreeMem( pValue );

//    /* encryption_slave_certificate_end_address */
//    status = cl_GetParams( (clu8*)"encryption_slave_certificate_end_address", strlen("encryption_slave_certificate_end_address"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    /* set value in the table */
//    newItem = new QTableWidgetItem(QString(QByteArray((const char *)pValue, (int)u32ValueLen)));
//    pTableView->setItem(0, 1, newItem);
//    cl_FreeMem( pValue );

//    pTableView->verticalHeader()->hide();



//    //*********************************
//    // OTA : Transmission tab
//    //*********************************
//    QComboBox *pComboBox = pWin->findChild<QComboBox*>("to_forward_to_rf_mode");
//    if ( pComboBox )
//    {
//        if ( CL_SUCCESS( status =  cl_GetParams( (clu8*)"to_forward_to_rf_mode", strlen("to_forward_to_rf_mode"), &pValue, &u32ValueLen, CL_OK )) )
//        {
//            if (u32ValueLen == 1)
//            {
//                pComboBox->setCurrentIndex( *pValue );
//            }
//            cl_FreeMem( pValue );
//        }

//        pComboBox->hide();
//    }

//    QLabel *pLabel  =  pWin->findChild<QLabel*>("Label_to_forward_to_rf_mode");
//    pLabel->hide();

//    pComboBox = pWin->findChild<QComboBox*>("tx_channel_to_forward_to");
//    if ( pComboBox )
//    {
//        if ( CL_SUCCESS( status =  cl_GetParams( (clu8*)"tx_channel_to_forward_to", strlen("tx_channel_to_forward_to"), &pValue, &u32ValueLen, CL_OK )) )
//        {
//            pComboBox->setCurrentIndex( *pValue );
//            cl_FreeMem( pValue );
//        }

//        pComboBox->hide();
//    }
//    pLabel  =  pWin->findChild<QLabel*>("Label_tx_channel_to_forward_to");
//    if ( pLabel)
//        pLabel->hide();

//    pComboBox = pWin->findChild<QComboBox*>("ota_forward_addressing_type");
//    if ( pComboBox )
//    {
//        if ( CL_SUCCESS( status =  cl_GetParams( (clu8*)"ota_forward_addressing_type", strlen("ota_forward_addressing_type"), &pValue, &u32ValueLen, CL_OK )) )
//        {
//            pComboBox->setCurrentIndex( *pValue );
//            cl_FreeMem( pValue );
//        }

//        pComboBox->hide();
//    }
//    pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_addressing_type");
//    if ( pLabel)
//        pLabel->hide();

//    QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("ota_destination_address_to_forward_to");
//    if ( pLineEdit )
//    {
//        if ( CL_SUCCESS( status =  cl_GetParams( (clu8*)"destination_address_to_forward_to", strlen("destination_address_to_forward_to"), &pValue, &u32ValueLen, CL_OK )) )
//        {
//            pLineEdit->setText( QString( QByteArray((const cl8*)pValue, u32ValueLen ) ) );
//            cl_FreeMem( pValue );
//        }
//        pLineEdit->hide();
//    }

//    pLabel  =  pWin->findChild<QLabel*>("label_ota_destination_address_to_forward_to");
//    if ( pLabel)
//        pLabel->hide();

//    pLineEdit = pWin->findChild<QLineEdit*>("ota_forward_to_group_multi_cast");
//    if ( pLineEdit )
//    {
//        if ( CL_SUCCESS( status =  cl_GetParams( (clu8*)"ota_forward_to_group_multi_cast", strlen("ota_forward_to_group_multi_cast"), &pValue, &u32ValueLen, CL_OK )) )
//        {
//            pLineEdit->setText( QString( QByteArray((const cl8*)pValue, u32ValueLen ) ) );
//            cl_FreeMem( pValue );
//        }
//        pLineEdit->hide();
//    }

//    pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_to_group_multi_cast");
//    if ( pLabel)
//        pLabel->hide();


//    QProgressBar    *pBar = pWin->findChild<QProgressBar*>("OTA_progressBar");
//    if ( pBar )
//    {
//        pBar->setValue( 0 );
//        pBar->hide();
//    }
//    pLabel = pWin->findChild<QLabel*>("Label_OTAInProgress");
//    if ( pLabel)
//        pLabel->hide();



//    //*********************************
//    // OTA : Modem tab
//    //*********************************
//    pLineToSet = pWin->findChild<QLineEdit*>("ota_transaction_id");
//    if ( !pLineToSet ) return;

//    status = cl_GetParams( (clu8*)"ota_transaction_id", strlen("ota_transaction_id"), &pValue, &u32ValueLen, CL_OK );
//    pLineToSet->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));

//    pCheckBox = pWin->findChild<QCheckBox *>("ota_allow_downgrade");
//    status = cl_GetParams( (clu8*)"ota_allow_downgrade", strlen("ota_allow_downgrade"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    if ( *pValue =='1')
//         pCheckBox->setChecked(true);
//     else
//         pCheckBox->setChecked(false);
//    cl_FreeMem( pValue );


//    pCheckBox = pWin->findChild<QCheckBox *>("encrypt_firmware");
//    status = cl_GetParams( (clu8*)"ota_activate_encryption", strlen("ota_activate_encryption"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    if ( *pValue =='1')
//         pCheckBox->setChecked(true);
//     else
//         pCheckBox->setChecked(false);
//    cl_FreeMem( pValue );


//    //*********************************
//    // OTA : Target tab
//    //*********************************
//    pLineToSet = pWin->findChild<QLineEdit*>("ota_transaction_id");
//    if ( !pLineToSet ) return;

//    status = cl_GetParams( (clu8*)"ota_transaction_id", strlen("ota_transaction_id"), &pValue, &u32ValueLen, CL_OK );
//    pLineToSet->setText((const QString &)QString(QByteArray((const cl8*)pValue, u32ValueLen )));

//    pCheckBox = pWin->findChild<QCheckBox *>("ota_allow_downgrade");
//    status = cl_GetParams( (clu8*)"ota_allow_downgrade", strlen("ota_allow_downgrade"), &pValue, &u32ValueLen, CL_OK );
//    if (!pValue)
//        return;
//    if ( *pValue =='1')
//         pCheckBox->setChecked(true);
//     else
//         pCheckBox->setChecked(false);
//    cl_FreeMem( pValue );

//}

void CloverViewer::InitializeTerminalsTab()
{
    e_Result status     = CL_ERROR;
    clu8 *pValue        = CL_NULL;
    clu32 u32ValueLen   = 0;

    // get pointer on tab
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;
    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");


    // select TAB with all
    pTabTerms->setCurrentIndex( 0 );

    pTabTerms->setTabEnabled( 1, false);
    pTabTerms->setTabEnabled( 2, false);
    pTabTerms->setTabEnabled( 3, false);
    pTabTerms->setTabEnabled( 4, false);
    pTabTerms->setTabEnabled( 5, false);
    pTabTerms->setTabEnabled( 6, false);
    pTabTerms->setTabEnabled( 7, false);

    // select TAB with all
    pTabTerms->setCurrentIndex( 0 );
}

void CloverViewer::onWindowLoaded_InitializeUIValues(void)
{
    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    this->setWindowIcon( QIcon(":/Icons/ineo.ico") );

    // hide-display correct fields in reader tab
    InitializeReadersTab();

    // hide-display correct fields in OTA tab
//    InitializeOTATables();

    // hide-display correct fields in Profiles tab
//    InitializeProfileTab();

    // select hyperterminal tab per default       
    InitializeTerminalsTab();

}

/*--------------------------------------------------------------------------*/

void CloverViewer::on_GlobalSetupWidget_currentChanged( int index )
{
    g_CurrentTabIndexGlobal = ui->GlobalSetupWidget->currentIndex();

    if ( g_CurrentTabIndexGlobal != 1 )
    {
        ObjectsDisable();
    }
    else
    {
        if ( g_CurrentTabIndexReader != 0 )
        {
            ObjectsEnable();
        }
    }
}

/*--------------------------------------------------------------------------*\
 * Maintain the current tabIndex
 *--------------------------------------------------------------------------*
 * 0 : is for Tab "All Readers"
 * 1 : is for reader_1
 * ...
 *
 * And maintain UI QWidget in accordance whith current tabIndex
\*--------------------------------------------------------------------------*/
void CloverViewer::on_tabWidget_Readers_currentChanged( int index )
{
    g_CurrentTabIndexReader = index;
    g_CurrentTabIndexReader_UI = index + 1; // TabIndex for UI
                                            // reader_1's index is 2
    // Display fileName in friendly way
    QStringList listFileName = g_FileName_SaveTracesForReaders[ g_CurrentTabIndexReader ].split("/");
    QString fileNameToDisplay = listFileName[ listFileName.count() - 1 ];
    ui->lineEdit_SaveLogFileName->setText( fileNameToDisplay );

    if ( g_CurrentTabIndexReader == 0 )
    {
        ObjectsDisable();
    }
    else
    {
        ObjectsEnable();
    }
}

//void CloverViewer::on_ModemRFMode_currentIndexChanged(int index)
//{
//    e_Result status = CL_ERROR;
//    clu8 cValue=0;
//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

//    switch (index )
//    {
//        case 0: cValue = 0;break;
//        case 1: cValue = 1;break;
//        case 2: cValue = 2;break;
//        case 3: cValue = 3;break;
//        case 4: cValue = 4;break;
//        case 5: cValue = 5;break;
//        case 6: cValue = 6;break;
//        case 7: cValue = 7;break;
//        case 8: cValue = 8;break;
//        case 9: cValue = 9;break;
//        case 10: cValue = 10;break;
//        case 11: cValue = 11;break;
//        case 12: cValue = 12;break;
//        case 13: cValue = 13;break;
//        case 14: cValue = 14;break;
//        case 15: cValue = 15;break;
//        case 16: cValue = 16;break;
//        default:break;
//    }

//    QString NewString2Display;
//    this->ConvertByteToAscii( (unsigned char *)&cValue, sizeof( cValue ), &NewString2Display );
//    cl_SetParams( (unsigned char *)"default_rf_mode", strlen("default_rf_mode"), (clu8 *)NewString2Display.toUtf8().data(), NewString2Display.toUtf8().length() );
//}

//void CloverViewer::on_ModemTxChannel_editingFinished()
//{
//    e_Result status = CL_ERROR;
//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;
//    QLineEdit *pPlatformId = pWin->findChild<QLineEdit*>("ModemTxChannel");
//    if (!pPlatformId)  return;
//    QByteArray StringUTF8 = pPlatformId->text().toUtf8();
//    status = cl_SetParams( (unsigned char *)"default_tx_channel", strlen("default_tx_channel"), (clu8 *)(StringUTF8.data()), StringUTF8.count() );
//}

//void CloverViewer::on_ModemScanPeriod_editingFinished()
//{
//    e_Result status = CL_ERROR;
//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;
//    QLineEdit *pPlatformId = pWin->findChild<QLineEdit*>("ModemScanPeriod");
//    if (!pPlatformId)  return;
//    QByteArray StringUTF8 = pPlatformId->text().toUtf8();
//    status = cl_SetParams( (unsigned char *)"default_scan_period", strlen("default_scan_period"), (clu8 *)(StringUTF8.data()), StringUTF8.count() );

//}

//void CloverViewer::on_ModemWUPLen_editingFinished()
//{
//    e_Result status = CL_ERROR;
//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;
//    QLineEdit *pPlatformId = pWin->findChild<QLineEdit*>("ModemWUPLen");
//    if (!pPlatformId)  return;
//    QByteArray StringUTF8 = pPlatformId->text().toUtf8();
//    status = cl_SetParams( (unsigned char *)"default_wup_length", strlen("default_wup_length"), (clu8 *)(StringUTF8.data()), StringUTF8.count() );
//}

/* when script line enter is pressed, send data to lower layer */
//void CloverViewer::on_Script_Cmd2Send_returnPressed()
//{
//    e_Result    status      = CL_ERROR;
//    t_clContext *pCtxt      = CL_NULL;
//    t_Tuple     *pTuple2Send = CL_NULL;
//    clu32       clu32TsfNb;
//    bool        ok          = false;
//    clu8        *pBuffForNet = CL_NULL;
//    t_Reader    *ptReader   ;

//    // get pointer on list
//    status = cl_getReader_List( &ptReader );

//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

//    // get item
//    QLineEdit *pEltId = pWin->findChild<QLineEdit*>("Script_Cmd2Send");

//    // get data to send
//    QByteArray StringUTF8 = pEltId->text().toUtf8();
//    QByteArray Data2Send = QByteArray::fromHex( StringUTF8 );
//    clu8    *pData = (clu8*)(Data2Send.data());
//    if ( !pData ) return;

//    // get context to have access to function pointers for memory/thread management on platform
//    if ( CL_FAILED(status =  cl_GetContext( &pCtxt )) )
//        return;

//    // check input parameters
//    if ( pCtxt->ptHalFuncs == CL_NULL ) return;

//    if ( pCtxt->ptHalFuncs->fnAllocMem == CL_NULL ) return;

//    // allocate buffer for data to send
//    if ( csl_malloc( (clvoid **) &pBuffForNet, Data2Send.count())) return;

//    // allocate tuple which holds the data
//    if ( CL_FAILED( csl_malloc( (clvoid **) &pTuple2Send, sizeof(t_Tuple)))) return;

//    // save data in buffer
//    memcpy( pBuffForNet, pData, Data2Send.count() );

//    // initialize a tuple default flags with memory
//    if ( CL_SUCCESS( cl_initTuple( pTuple2Send, CL_NULL, &pBuffForNet, Data2Send.count() ) ) )
//    {
//        // now send to the device Dev4Test connected an unknown reader
//        //cl_sendData( CL_NULL, ptReader, pTuple2Send, NON_BLOCKING, &clu32TsfNb );

//        // BRY_07052015
//        //
//        // If p_TplList2Send is not empty, insert new Tuple at the end of p_TplList2Send
//        //
//        t_Tuple *tuple = ptReader->p_TplList2Send;
//        if ( tuple != CL_NULL )
//        {
//            while ( tuple->pNext != CL_NULL )
//            {
//                tuple = tuple->pNext;
//            };
//            tuple->pNext = pTuple2Send;
//        }
//        else
//        {
//            ptReader->p_TplList2Send = pTuple2Send;
//        }

//        DEBUG_PRINTF2("SemaphoreRelease: pSgl4Write");
//        pCtxt->ptHalFuncs->fnSemaphoreRelease( ptReader->tSync.pSgl4Write );
//    }

//    /* clean up the interface */
//    pEltId->setText(QString(""));
//}

/*----------------------------------------------------------------------------------------------*/

void CloverViewer::UpdateScriptEditorDataReceived( clvoid *ptReader, clvoid *ptTuple, int eStatus )
{
    e_Result    status      = CL_ERROR;
    bool        bFirstLoop  = true;
    t_Reader    *pReader    = ( t_Reader *)ptReader;
    t_Tuple     *pTuple     = ( t_Tuple *)ptTuple;
    clu8        *pData      =   CL_NULL;

    bool isAckReceived = false;

    // BRY_19062015 : fait planter l'application car une demande de mise à jour de l'UI
    // est effectuée par une autre thread que l'UI !
//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

    /* check incoming parameters */
    if ( pReader == CL_NULL )
        return;

    if ( pTuple == CL_NULL )
        return;

    if ( pReader->tType == COM_READER_TYPE )
        pData = pReader->tCOMParams.aucPortName;

    if (( pReader->tType == IP_READER_TYPE ) | ( pReader->tType == LANTRONIX_READER_TYPE ))
        pData = pReader->tIPParams.aucIpAddr;

    if ( pReader->tType == BT_READER_TYPE )
        pData = pReader->aucLabel;

    if ( pData == CL_NULL )
        return;

    QString *pSReaderInfo = new QString((const char*)pData );
    if ( !pSReaderInfo ) return;

    if ( pSReaderInfo->isEmpty() == true )
    {
        DEBUG_PRINTF("Clover received empty reader!!");
        return;
    }

    /* parse tuples coming from underlayers */
    //while ( pTuple != CL_NULL )
    {
        if ( pTuple->ptBuf == CL_NULL)
            return;

        if ( pTuple->ptBuf->pData == CL_NULL )
            return;

        if ( pTuple->ptBuf->ulLen > 512 )
        {
            DEBUG_PRINTF("UpdateScriptEditorDataReceived: pTuple->ptBuf->ulLen: [%d] CAUTION!!!", pTuple->ptBuf->ulLen );
            return;
        }

        if ( pTuple->ptBuf->ulLen == 0)
        {
            DEBUG_PRINTF("UpdateScriptEditorDataReceived: received 0 bytes to display... error : exit\n");
            return;
        }

        if ( pTuple->ptBuf->ulLen == 1 /*&& ui->checkBox_AckDisplay->isChecked() == false*/ )
        {
            DEBUG_PRINTF("UpdateScriptEditorDataReceived: ACK_RECEIVED: %d", pTuple->ptBuf->ulLen);
            isAckReceived = true;
        }

        /* set reader name in general view*/
        //QString SReaderName = new QString();

        QString *pSReaderName= new QString( (const char *)pData );
        if ( !pSReaderName ) return;

        if ( pSReaderName->isEmpty() == true )
        {
            DEBUG_PRINTF("UpdateScriptEditorDataReceived: received a Null Reader Name....  error : exit");
            return;
        }

        /*  send data for display */
        QString *pSData2Display = new QString;
        if ( !pSData2Display ) return;
        this->ConvertByteToAscii( pTuple->ptBuf->pData, pTuple->ptBuf->ulLen, pSData2Display );
        if ( pSData2Display->isEmpty() == true )
        {
            DEBUG_PRINTF("UpdateScriptEditorDataReceived: received a no data....  error : exit");
            return;
        }
        QByteArray BA = pSData2Display->toLocal8Bit();
        const char *pData2Display = BA.data();

        /* set related time */
        QString *pSTimeString = new QString( (const char *)pTuple->cl8Time );
        if ( !pSTimeString ) return;
        if ( pSTimeString->isEmpty() == true )
        {
            DEBUG_PRINTF("CLOVER received no time....  error : exit");
            return;
        }
        QByteArray BATime = pSTimeString->toLocal8Bit();
        const char *pTime2Display = BATime.data();

        DEBUG_PRINTF("UpdateScriptEditorDataReceived: reader %s received %d oct", pReader->aucLabel, pTuple->ptBuf->ulLen );

        if ( QMetaObject::invokeMethod( g_pClVw, "updateCSLDataActivitySignal", Qt::QueuedConnection, \
                                        Q_ARG( bool, isAckReceived), \
                                        Q_ARG( QString,     (const char *)pReader->aucLabel), /* SReaderName */ \
                                        Q_ARG( QString ,    (const char *)pData), /* SReaderInfo */ \
                                        Q_ARG( QString,     (const char *)pData2Display), /* SData2Display */\
                                        Q_ARG( QString ,    (const char *)pTime2Display), /* STimeString */\
                                        Q_ARG( QString ,    (const char *)"Rx") /* Direction */ \
                                        ) == true )
        {
            DEBUG_PRINTF("Method correct");
        }
        else
        {
            DEBUG_PRINTF("Failed to call methode");
        }


 //       emit g_pClVw->updateCSLDataActivitySignal( false, pSReaderInfo, pSReaderName, pSData2Display,  pSTimeString, pSDirection );
         if ( QMetaObject::invokeMethod( g_pClVw, "updateDataCounterSignal", Qt::QueuedConnection, Q_ARG( qint32, g_pClVw->m_iPacketsReceived++), \
                                         Q_ARG( bool, true)) == true )
         {
             DEBUG_PRINTF("Method correct");
         }
         else
         {
             DEBUG_PRINTF("Failed to call méthode");
         }

         if ( g_pClVw->LogMode() == true )
         {
             if ( QMetaObject::invokeMethod
                  (
                        g_pClVw, "LogSignal", Qt::QueuedConnection,
                        Q_ARG( bool, isAckReceived),
                        Q_ARG( QString,     (const char *)pReader->aucLabel), /* ReaderName */
                        Q_ARG( QString ,    (const char *)pData), /* ReaderInfo */
                        Q_ARG( QString,     (const char *)pData2Display), /* Data2Display */
                        Q_ARG( QString ,    (const char *)pTime2Display), /* TimeString */
                        Q_ARG( QString ,    (const char *)"Rx") /* Direction */
                  ) == true
                )
             {
                 DEBUG_PRINTF("QMetaObject::invokeMethod: LogSignal: OK");
             }
             else
             {
                 DEBUG_PRINTF("QMetaObject::invokeMethod: LogSignal: FAILED !");
             }
         }

//        emit g_pClVw->updateCSLDataActivitySignal( true, pSReaderInfo, pSReaderName, pSData2Display,  pSTimeString, pSDirection );
//        emit g_pClVw->updateDataCounterSignal( (int)g_pClVw->m_iPacketsReceived++, (bool)true );
    }
    return;
}
/************************************************************************************************/
/************************************************************************************************/
void CloverViewer::UpdateScriptEditorWriteComplete( void *ptReader, void *ptTuple, int eStatus )
{
    t_Reader *pReader = (t_Reader *)ptReader;
    t_Tuple *pTuple = (t_Tuple *)ptTuple;
    e_Result status = CL_ERROR;
    bool bFirstLoop = true;
    clu8 ucNibble;
    clu8    *pData = CL_NULL;

//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

    /* check incoming parameters */
    if ( pReader == CL_NULL )
        return;

    if ( pTuple == CL_NULL )
        return;

    if ( pReader->tType == COM_READER_TYPE )
    {
        pData = pReader->tCOMParams.aucPortName;
    }
    if (( pReader->tType == IP_READER_TYPE ) | ( pReader->tType == LANTRONIX_READER_TYPE ))
        pData = pReader->tIPParams.aucIpAddr;

    if ( pReader->tType == BT_READER_TYPE )
        pData = pReader->aucLabel;

    if ( pData == CL_NULL )
        return;

    QString *pSReaderInfo = new QString( (const cl8*)pData );
    if ( !pSReaderInfo) return;
    if ( pSReaderInfo->isEmpty() == true )
    {
        DEBUG_PRINTF("Clover received Reader info... empty: Exit");
        return;
    }


    /* parse tuples coming from underlayers */
//    while ( pTuple != CL_NULL )
    {
        if ( pTuple->ptBuf == CL_NULL)
            return;

        if ( pTuple->ptBuf->pData == CL_NULL )
            return;

        if ( pTuple->ptBuf->ulLen > 512 )
        {
            DEBUG_PRINTF("CAUTION!!! UpdateScriptEditorWriteComplete: PB in Buffer len %d\n", pTuple->ptBuf->ulLen );
            return;
        }
        if ( pTuple->ptBuf->ulLen == 0)
        {
            DEBUG_PRINTF("Clover Error: length of data is null.. Exit");
            return;
        }
        /* set reader name in general view*/
        QString *pSReaderName = new QString( (const char *)pData );
        if ( !pSReaderName ) return;
        if ( pSReaderName->isEmpty() == true )
        {
            DEBUG_PRINTF("Clover received Reader name... empty!! =Exit");
            return;
        }

        /*  send data for display */
        QString *pSData2Display = new QString;
        this->ConvertByteToAscii( pTuple->ptBuf->pData, pTuple->ptBuf->ulLen, pSData2Display );
        if ( pSData2Display->isEmpty() == true )
        {
            DEBUG_PRINTF("Clover Error: no data received.. Exit");
            return;
        }
        QByteArray  BA = pSData2Display->toLocal8Bit();
        const char *pData2Display = BA.data();

        /* set related time */
        QString *pSTimeString = new QString( (const char *)pTuple->cl8Time );
        if ( !pSTimeString ) return;
        if ( pSTimeString->isEmpty() )
        {
            DEBUG_PRINTF("Clover Error: time is empty!.. Exit");
            return;
        }

// FD_SHIT
//        /* Set direction */
//        QString *pSDirection = new QString("R -> D");

        DEBUG_PRINTF("the reader %s send %d \n", pReader->aucLabel, pTuple->ptBuf->ulLen );

        if ( QMetaObject::invokeMethod( g_pClVw, "updateCSLDataActivitySignal", Qt::QueuedConnection, \
                                        Q_ARG( bool, false),
                                        Q_ARG( QString,     (const char *)pData), /* Reader Name */\
                                        Q_ARG( QString ,    (const char *)pData), /* Reader Info */ \
                                        Q_ARG( QString,     (const char *)pData2Display), /* SData2Display */ \
                                        Q_ARG( QString ,    (const char *)pTuple->cl8Time), /* STime2Display */ \
                                        Q_ARG( QString ,    (const char *)"R -> D" ) \
                                        ) == true )
        {
            DEBUG_PRINTF("Method correct");
        }
        else
        {
            DEBUG_PRINTF("Failed to call méthode");
        }


 //       emit g_pClVw->updateCSLDataActivitySignal( false, pSReaderInfo, pSReaderName, pSData2Display,  pSTimeString, pSDirection );
         if ( QMetaObject::invokeMethod( g_pClVw, "updateDataCounterSignal", Qt::QueuedConnection, Q_ARG( qint32, g_pClVw->m_iPacketsSent++), Q_ARG( bool, false) ) == true )
         {
             DEBUG_PRINTF("Method correct\n");
         }
         else
         {
             DEBUG_PRINTF("Failed to call méthode");
         }

//        emit g_pClVw->updateDataCounterSignal( (int)g_pClVw->m_iPacketsSent++, (bool)false );
    }
    return;
}

/* display data in correct terminal in an asynchronous QT format */
void CloverViewer::updateScriptEditorWindow( int rows, int columns, QTableWidget *pTableWidget, QTableWidgetItem *pTableWidgetItem)
{
    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    if (!pTableWidget) return;

    //DEBUG_PRINTF("<-- updateScriptEditorWindow rows : %d, columns %d\n", rows, columns);

    pTableWidget->setItem( rows, columns, pTableWidgetItem);

    pTableWidget->setCurrentCell( rows, columns );

    //pTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents );
//    DEBUG_PRINTF(" updateScriptEditorWindow -->\n");
}

void CloverViewer::on_Reader_ConnectionType_List_activated(const QString &arg1)
{
    e_Result status = CL_ERROR;
    t_Reader *pReader = CL_NULL;

    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    if ( QString::compare(QString("Serial Reader"), arg1) == 0)
    {
        QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPAddress");
        pLineEdit->hide();

        pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPPort");
        pLineEdit->hide();

        // OLD things FD_SHIT_STUFF_THAT_STINKS very hard
//        // get updated reader list coming from registration thread
//        // and update entry in the list according to the list of reader
//        status = cl_getReader_List( &pReader );

//        QComboBox *pComboBox = pWin->findChild<QComboBox*>("comboBox_ReaderSerialPort");
//        if ( pComboBox )
//        {
//            while ( pReader)
//            {
//                if ( pReader->tType == COM_READER_TYPE )
//                {
//                    pComboBox->addItem( QString((const cl8 *)pReader->tCOMParams.aucPortName));

//                }
//                pReader = pReader->pNext ;
//            };

//            pComboBox->show();
//        }

        QComboBox *pComboBox = pWin->findChild<QComboBox*>("comboBox_ReaderSerialPort");
        QString port = QString("COM");
        for ( int i = 1; i <= 99; i++ )
        {
            pComboBox->addItem( port + QString::number(i) );
        }
        pComboBox->show();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_BaudRate_List");
        pComboBox->show();

        // Nein, nein, nein ! ça sert à rien
//        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_DataBits_List");
//        pComboBox->show();

//        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_ParityBits_List");
//        pComboBox->show();

//        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_StopBits_List");
//        pComboBox->show();

        QLabel *pLabel = pWin->findChild<QLabel*>("Reader_IPAddress_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_IPTcpPort_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialBaudRate_Label");
        pLabel->show();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialCOMPort_Label");
        pLabel->show();

        // Nein !
//        pLabel = pWin->findChild<QLabel*>("Reader_SerialDataBits_Label");
//        pLabel->show();

//        pLabel = pWin->findChild<QLabel*>("Reader_SerialParityBits_Label");
//        pLabel->show();

//        pLabel = pWin->findChild<QLabel*>("Reader_SerialStopBits_Label");
//        pLabel->show();
    }

    if ( QString::compare(QString("IP Reader"), arg1) == 0)
    {
        QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPAddress");
        pLineEdit->show();

        pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPPort");
        pLineEdit->show();

        QComboBox *pComboBox = pWin->findChild<QComboBox*>("comboBox_ReaderSerialPort");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_BaudRate_List");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_DataBits_List");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_ParityBits_List");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_StopBits_List");
        pComboBox->hide();

        QLabel *pLabel = pWin->findChild<QLabel*>("Reader_IPAddress_Label");
        pLabel->show();

        pLabel = pWin->findChild<QLabel*>("Reader_IPTcpPort_Label");
        pLabel->show();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialBaudRate_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialCOMPort_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialDataBits_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialParityBits_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialStopBits_Label");
        pLabel->hide();
    }

    if ( QString::compare(QString("Bluetooth Reader"), arg1) == 0)
    {
        QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPAddress");
        pLineEdit->hide();

        pLineEdit = pWin->findChild<QLineEdit*>("Reader_IPPort");
        pLineEdit->hide();

        QComboBox *pComboBox = pWin->findChild<QComboBox*>("comboBox_ReaderSerialPort");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_BaudRate_List");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_DataBits_List");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_ParityBits_List");
        pComboBox->hide();

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_StopBits_List");
        pComboBox->hide();

        QLabel *pLabel = pWin->findChild<QLabel*>("Reader_IPAddress_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_IPTcpPort_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialBaudRate_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialCOMPort_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialDataBits_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialParityBits_Label");
        pLabel->hide();

        pLabel = pWin->findChild<QLabel*>("Reader_SerialStopBits_Label");
        pLabel->hide();
    }
}

void CloverViewer::ConvertByteToAscii( unsigned char *pData, unsigned long u32Len, QString *pString )
{
    if (!pString )
        return;

    if (!pData)
        return;

    /* convert from byte to ASCII */
    size_t BufSize;
    char buf[(u32Len *3 +2*3)];
    clu8 ucNibble = 0;

    memset( buf, 0, sizeof(buf));

    for ( unsigned long i=0; i< u32Len; i++ )
    {
        // get nibble MSB
        ucNibble = (((pData[i])>>4)&0x0F);
        if (( ucNibble >=0x00) & ( ucNibble <=0x09))
            buf[i*3]    =   0x30+ucNibble;
        if (( ucNibble >=0x0A) & ( ucNibble <=0x0F))
            buf[i*3]    =   0x41+(ucNibble-0x0A);

        ucNibble =  ((pData[i])&0x0F);
        if (( ucNibble >=0x00) & ( ucNibble <=0x09))
            buf[i*3+1]    =   0x30+ucNibble;
        if (( ucNibble >=0x0A) & ( ucNibble <=0x0F))
            buf[i*3+1]    =   0x41+(ucNibble-0x0A);

        buf[i*3+2]    =   0x20;
    }
    buf[ u32Len * 3] = 0;

    QString LocalString = QString::fromUtf8( buf );
    pString->append( LocalString);
}

void CloverViewer::ConvertASCIToHex( unsigned char *pStringBuf, unsigned inLen, unsigned char *pOutData, unsigned long *pu32OutLen )
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

void CloverViewer::on_ReadersCOMTableWidget_cellChanged(int row, int column)
{
    e_Result    status = CL_ERROR;
    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;
    QTableWidget *pTableView = CL_NULL;
//    t_Reader *pReaderFilter;
    t_Reader *pReaderFromList = CL_NULL;
    QTableWidgetItem *pItem = CL_NULL;
    char   *cData = CL_NULL;

    DEBUG_PRINTF1( "on_ReadersCOMTableWidget_cellChanged: row[%d] column[%d]", row, column );

    if ( column == COM_TABLE_OFFSET_CONNECT )
    {
        /* get IP reader table pointer */
        pTableView = pWin->findChild<QTableWidget *>("ReadersCOMTableWidget");
        if (!pTableView) return;

//        if ( CL_FAILED( cl_ReaderFillWithDefaultFields( &pReaderFilter, COM_READER_TYPE ) ) )
//            return;

//        // check radio address: if NULL, this is not a valid reader
//        pItem = pTableView->item( row, COM_TABLE_OFFSET_RADIO_ADDRESS );
//        if ( !pItem ) return;
//        QByteArray ByteArray2UTF8 = pItem->text().toUtf8();
//        cData = ByteArray2UTF8.data();
//        if (!cData)
//            return;

//        if ( ByteArray2UTF8.length() == 0)
//            return;

//        int len = ByteArray2UTF8.length();
        // if there is no radio address linked to this reader, exit
//        if ( !memcmp("00 00 00 00 00 00 ", pData, ByteArray2UTF8.length() ) )
//             return;

        // set Friendly Name
        pItem = pTableView->item( row, COM_TABLE_OFFSET_FRIENDLY_NAME );
        if ( !pItem ) return;
        if ( pItem->text().isEmpty() ) return;

        QByteArray ByteArray2UTF8 = pItem->text().toUtf8();
        cData = ByteArray2UTF8.data();
        if ( !cData )
        {
            DEBUG_PRINTF2( "on_ReadersCOMTableWidget_cellChanged: !cData" );
            return;
        }

        DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: Reader [%s]", cData);

        status = cl_readerFindInListByFriendlyName( &pReaderFromList, cData );
        if ( status == CL_ERROR )
        {
            DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: not founded ERROR !");
        }

        // FD_STUFF_SHIT_THAT_STINK
//        ByteArray2UTF8 = pItem->text().toUtf8();
//        pData = (clu8*)(ByteArray2UTF8.data());
//        if ( !pData ) return;
//        memset( pReaderFilter.aucLabel, 0, sizeof( pReaderFilter.aucLabel ) );
//        memcpy( pReaderFilter.aucLabel, pData, ByteArray2UTF8.length() );

        // set baudrate
//        pItem = pTableView->item( row, COM_TABLE_OFFSET_BAUDRATE );
//        if ( !pItem ) return;
//        QString Serial_BaudRate = pItem->text();
//        pReaderFilter.tCOMParams.eBaudRate = CL_COM_BAUDRATE_115200;
//        if ( QString::compare(QString("4800"), Serial_BaudRate) == 0)
//            pReaderFilter.tCOMParams.eBaudRate = CL_COM_BAUDRATE_4800;
//        if ( QString::compare(QString("9600"), Serial_BaudRate) == 0)
//            pReaderFilter.tCOMParams.eBaudRate = CL_COM_BAUDRATE_9600;
//        if ( QString::compare(QString("19200"), Serial_BaudRate) == 0)
//            pReaderFilter.tCOMParams.eBaudRate = CL_COM_BAUDRATE_19200;
//        if ( QString::compare(QString("38400"), Serial_BaudRate) == 0)
//            pReaderFilter.tCOMParams.eBaudRate = CL_COM_BAUDRATE_38400;
//        if ( QString::compare(QString("57600"), Serial_BaudRate) == 0)
//            pReaderFilter.tCOMParams.eBaudRate = CL_COM_BAUDRATE_57600;
//        if ( QString::compare(QString("115200"), Serial_BaudRate) == 0)
//            pReaderFilter.tCOMParams.eBaudRate = CL_COM_BAUDRATE_115200;
/*
        // set databits
        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_DataBits_List");
        QString Serial_DataBits = pComboBox->currentText();
        NewReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_8BITS;
        if ( QString::compare(QString("8 Bits"), Serial_DataBits) == 0)
            NewReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_8BITS;
        if ( QString::compare(QString("7 Bits"), Serial_DataBits) == 0)
            NewReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_7BITS;
        if ( QString::compare(QString("6 Bits"), Serial_DataBits) == 0)
            NewReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_6BITS;
        if ( QString::compare(QString("5 Bits"), Serial_DataBits) == 0)
            NewReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_5BITS ;

        // Parity bits
        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_ParityBits_List");
        QString Serial_ParityBits = pComboBox->currentText();
        NewReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_NONE;
        if ( QString::compare(QString("None"), Serial_ParityBits) == 0)
            NewReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_NONE;
        if ( QString::compare(QString("Even"), Serial_ParityBits) == 0)
            NewReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_EVEN;
        if ( QString::compare(QString("Odd"), Serial_ParityBits) == 0)
            NewReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_ODD;
        if ( QString::compare(QString("Mark"), Serial_ParityBits) == 0)
            NewReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_MARK;
        if ( QString::compare(QString("Space"), Serial_ParityBits) == 0)
            NewReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_SPACE;

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_StopBits_List");
        QString Serial_StopBits = pComboBox->currentText();
        NewReader.tCOMParams.eStopBits = CL_COM_STOPBITS_10BIT;
        if ( QString::compare(QString("1 Bit"), Serial_StopBits) == 0)
            NewReader.tCOMParams.eStopBits = CL_COM_STOPBITS_10BIT;
        if ( QString::compare(QString("1,5 Bits"), Serial_StopBits) == 0)
            NewReader.tCOMParams.eStopBits = CL_COM_STOPBITS_15BIT;
        if ( QString::compare(QString("2 Bits"), Serial_StopBits) == 0)
            NewReader.tCOMParams.eStopBits= CL_COM_STOPBITS_20BIT;
*/
//        pReaderFilter.tCOMParams.eByteSize = CL_COM_BYTESIZE_8BITS;
//        pReaderFilter.tCOMParams.eParityBits = CL_COM_PARITYBIT_NONE;
//        pReaderFilter.tCOMParams.eStopBits = CL_COM_STOPBITS_10BIT;

        // set port
//        pItem = pTableView->item( row, COM_TABLE_OFFSET_PORT );
//        ByteArray2UTF8 = pItem->text().toUtf8();
//        pData = (clu8*)(ByteArray2UTF8.data());
//        if ( !pData ) return;
//        memset( pReaderFilter.tCOMParams.aucPortName, 0, sizeof( pReaderFilter.tCOMParams.aucPortName ) );
//        memcpy( (clvoid*)pReaderFilter.tCOMParams.aucPortName, (clvoid*)pData, strlen( (const cl8*)pData ) );

//        if ( CL_FAILED( status = cl_readerFindInList( &pReaderFromList, &pReaderFilter ) ) )
//        {
//            DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: cl_readerFindInList: %s FAILED", pReaderFromList->tCOMParams.aucPortName);
//            DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: cl_readerFindInList: FAILED" );
//            return;
//        }
//        else
//        {
//            DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: cl_readerFindInList: %s OK", pReaderFromList->tCOMParams.aucPortName);
//            DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: cl_readerFindInList: OK" );
//        }

        if ( pReaderFromList == CL_NULL )
        {
            DEBUG_PRINTF2("Error: pReaderFromList == CL_NULL");
            return;
        }

        // set port
        pItem = pTableView->item( row, COM_TABLE_OFFSET_CONNECT );

        // FD_STUFF_SHIT_THAT_STINK il confond & et && !!!???
//        if ( ( pItem->checkState() == Qt::Checked ) & ( pReaderFromList->eState != STATE_CONNECT ) )
//        {
//            // display the data in the correct tab of the Terminal
//            TermsTabDisplay( (cl8 *)pReaderFromList->tCOMParams.aucPortName, true );

//            // change reader state
//            if ( pReaderFromList->eState != STATE_OTA )
//            {
//                cl_ReaderSetState( pReaderFromList, STATE_CONNECT);
//                DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: STATE_CONNECT");
//            }

//        }
//        else
//        {
//            if ( ( pItem->checkState() == Qt::Unchecked ) & ( pReaderFromList->eState != STATE_DISCONNECT ) & ( pReaderFromList->eState != STATE_DISCOVER ) )
//            {
//                // disable display of this reader in the correct tab of the Terminal
//                TermsTabDisplay( (cl8 *)pReaderFromList->tCOMParams.aucPortName, false );

//                // disconnect from lower layer
//                cl_ReaderSetState( pReaderFromList, STATE_DISCONNECT );
//                DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: STATE_DISCONNECT");
//            }
//        }


//FD_STUFF_SHIT_THAT_STINK ce con execute un TermsTabDisplay(true) meme si le reader est en erreur !!!
//        if ( ( pItem->checkState() == Qt::Checked ) && ( pReaderFromList->eState != STATE_CONNECT ) )
//        {
//            TermsTabDisplay( (cl8 *)pReaderFromList->aucLabel, true );

//            TermsTreeViewDisplay( pReaderFromList, true);

//            // change reader state
//            if ( pReaderFromList->eState != STATE_OTA )
//            {
//                cl_ReaderSetState( pReaderFromList, STATE_CONNECT);
//                DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: STATE_CONNECT");
//            }
//        }

//        if ( ( pItem->checkState() == Qt::Unchecked ) && ( pReaderFromList->eState == STATE_CONNECT ) )
//        {
//            TermsTabDisplay( (cl8 *)pReaderFromList->aucLabel, false );

//            // disconnect from lower layer
//            cl_ReaderSetState( pReaderFromList, STATE_DISCONNECT );
//            DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: STATE_DISCONNECT");
//        }

        //
        // BRY_20150922
        //
        if ( ( pItem->checkState() == Qt::Checked ) && ( pReaderFromList->eState != STATE_CONNECT ) )
        {
            // if reader is in STATE_DISCOVER
            // it will be removed then added to ReadersList
            // GRRRR FD_IS_REAL_SHIT so :
            pReaderFromList->eState = STATE_DEFAULT;

            status = cl_ReaderSetState( pReaderFromList, STATE_CONNECT);

            if ( pReaderFromList->eState == STATE_CONNECT )
            {
                TermsTabDisplay( (cl8 *)pReaderFromList->aucLabel, true );
                TermsTreeViewDisplay( pReaderFromList, true);

                DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: STATE_CONNECT");
            }

            if ( CL_SUCCESS( status ) )
            {
                // BUG_10122015 We start Threads only the fisrt time
                if ( pReaderFromList->tSync.tThreadId4Read == NULL && pReaderFromList->tSync.tThreadId4Write == NULL )
                {
                    status = cl_ReaderStartThreads( pReaderFromList );
                    DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: cl_ReaderSetState %s: -------> STARTED", pReaderFromList->tCOMParams.aucPortName );
                }
                else
                {
                    DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: cl_ReaderSetState %s: -------> ALLREADY STARTED", pReaderFromList->tCOMParams.aucPortName );
                }
            }
        }

        if ( ( pItem->checkState() == Qt::Unchecked ) && ( pReaderFromList->eState == STATE_CONNECT ) )
        {
            cl_ReaderSetState( pReaderFromList, STATE_DISCONNECT );

            TermsTabDisplay( (cl8 *)pReaderFromList->aucLabel, false );

            DEBUG_PRINTF2("on_ReadersCOMTableWidget_cellChanged: STATE_DISCONNECT");
        }
    }
}

/*--------------------------------------------------------------------------*/

void CloverViewer::TermsTreeViewDisplay( void *ptReader, bool bDisplay )
{
    t_Reader *pReader = (t_Reader *)ptReader;
    const char *readerName = (char *)pReader->aucLabel;

    QWidget *pWin = QApplication::activeWindow();
    if ( !pWin ) return;

    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
    if ( pTabTerms )
    {
        bool bTabFound = false;
        cl8 cl8Index = 0;
        for ( int i = 1; i < pTabTerms->count(); i++ )
        {
            if ( !QString::compare( pTabTerms->tabText(i), QString( readerName ), Qt::CaseInsensitive) )
            {
                bTabFound = true;
                cl8Index = i;
                break;
            }
        }

        if ( bTabFound == true )
        {
            QString *treeName = new QString("treeWidget_");
            treeName->append( QString::number(cl8Index + 1) );
            QTreeWidget *treeWidget = new QTreeWidget;
            treeWidget = pWin->findChild<QTreeWidget *>(treeName->toUtf8());

            if ( treeWidget == NULL )
            {
                DEBUG_PRINTF2("TermsTreeViewDisplay: treeView: NULL");
            }
            else
            {
                DEBUG_PRINTF2("TermsTreeViewDisplay: treeView: Ok");
                treeWidget->setColumnWidth( 0, 110 );

                QTreeWidgetItem *item;
                QList<QTreeWidgetItem *> found = treeWidget->findItems( "Mac", Qt::MatchExactly );
                foreach ( item, found )
                {
                    item->setText(1, (char *)pReader->tCOMParams.aucPortName);
                    item->setText(2, readerName );
                }
            }
        }
    }
}

/*--------------------------------------------------------------------------*\
 * Display data in the correct tab of the Terminal
\*--------------------------------------------------------------------------*/
void CloverViewer::TermsTabDisplay( cl8 *pData, bool bDisplay )
{
    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    if ( bDisplay == true )
    {
        // enabled the terminal tab (look first if not already existing, then check which is the next empty)
        QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
        if ( pTabTerms)
        {
            bool bTabFound = false;
            cl8 cl8Index = 0;
            for ( int i=1; i< pTabTerms->count(); i++)
            {
                if ( !QString::compare( pTabTerms->tabText(i), QString( (const cl8*)pData), Qt::CaseInsensitive))
                {
                    bTabFound = true;
                    cl8Index = i;
                    break;
                }
            }
            if ( bTabFound == true )
            {
                pTabTerms->setTabEnabled( cl8Index, true );
                pTabTerms->setTabText( cl8Index, (const cl8 *)pData );
            }
            else
            {
                for ( int i=1; i< pTabTerms->count(); i++)
                {
                    if ( !QString::compare( pTabTerms->tabText(i), QString("No Reader"), Qt::CaseInsensitive))
                    {
                        bTabFound = true;
                        cl8Index = i;
                        break;
                    }
                }
                if ( bTabFound == true )
                {
                    pTabTerms->setTabEnabled( cl8Index, true );
                    pTabTerms->setTabText( cl8Index, (const cl8 *)pData );
                }
            }
        }
    }
    else
    {
        // enabled the terminal tab (look first if not already existing, then check which is the next empty)
        QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
        if ( pTabTerms)
        {
            bool bTabFound = false;
            cl8 cl8Index = 0;
            for ( int i=1; i< pTabTerms->count(); i++)
            {
                if ( !QString::compare( pTabTerms->tabText(i), QString( (const cl8*)pData), Qt::CaseInsensitive))
                {
                    bTabFound = true;
                    cl8Index = i;
                    break;
                }
            }
            if ( bTabFound == true )
            {
                pTabTerms->setTabEnabled( cl8Index, false );
                // BRY_20150922 pTabTerms->setTabText( cl8Index, "No Reader" );
            }
        }

    }
}

/*--------------------------------------------------------------------------*\
 * Very specifique function to log the data on a Reader
 *--------------------------------------------------------------------------*
 * The log file name to save in is in the "ui" at the place
 * of "lineEdit_SaveFileName_Reader_X"
 * So the aim of this function is to retreive the final name replacing "X",
 * in the good reader corresponding tab and to log the reader->data
 * into this file
\*--------------------------------------------------------------------------*/
void CloverViewer::LogSlot
(
        bool isAckReceived,
        QString aReaderName,
        QString aPortName,
        QString aData2Display,
        QString aTime,
        QString aDirection
)
{
    if ( isAckReceived == true && ui->checkBox_AckDisplay->isChecked() == false ) // DoNotDisplayAckReceived
        return;

    if ( g_FileName_SaveTracesForReaders[ g_CurrentTabIndexReader ] != "" )
    {
        DEBUG_PRINTF2( "CloverViewer::Log: OK" );

        QString fileName  = QString( g_FileName_SaveTracesForReaders[ g_CurrentTabIndexReader ] );
        QFile file( fileName );

        // If file don't exit it's created otherwise appended
        file.open(QIODevice::WriteOnly | QIODevice::Append);

        // Remove white spaces
        aReaderName.replace(" ", "");
        aDirection.replace(" ", "");
        aData2Display.replace(" ", "");

        // \n : works with Notepad++
        // \r\n : must be set with notepad (microsoft's shit)
        //QString line_file = QString("[%1]:%2:%3:%4:%5\n").arg(QDateTime::currentDateTime().toString(),aTime,aPortName,aDirection,aData2Display);
        QString line_file = QString("[%1]:%2:%3:%4\r\n").arg( aTime, aReaderName, aDirection, aData2Display );
        file.write(line_file.toLatin1());

        file.close();
    }
}

/*--------------------------------------------------------------------------*\
 * RTC Local
 *--------------------------------------------------------------------------*
 * Use specifique command "010109" to set RTC to chip
\*--------------------------------------------------------------------------*/
void CloverViewer::on_pushButton_SetLocalRTC_clicked()
{
    //
    // DEBUG PURPOSE verify RTC calculation
    //
//    QDateTime dateTimeTest(QDate(2015, 7, 2), QTime(19, 0));
//    qint64 nbSectest = startDateTime.secsTo( dateTimeTest ); // 173 556 000 est le bon résultat
    // Fin DEBUG

    QWidget *pWin = QApplication::activeWindow();
    QDateTime startDateTime(QDate(2010, 1, 1), QTime(0, 0, 0));
    QDateTime currentDateTime1 = QDateTime::currentDateTime();

    qint64 numberOfSeconds = startDateTime.secsTo( currentDateTime1 );
    QString cmd2Send = QString(tr("010109"));

    // Format is 00000000 (8 digits)
    QByteArray baNumberOfSeconds = QByteArray::number(numberOfSeconds, 16);
    int numberOfZeroToPrepend = 8 - baNumberOfSeconds.length();
    QByteArray baToPrepend = QByteArray(numberOfZeroToPrepend, '0');
    baNumberOfSeconds.prepend(baToPrepend);

    QString strNumberOfSeconds = QString( baNumberOfSeconds );

    cmd2Send.append( strNumberOfSeconds.toUpper() );

    // Send Command to Reader correspondind to the Tab
    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
    QString theTab = pTabTerms->tabText( g_CurrentTabIndexReader );

    ScriptCmdSendToCsl( theTab, cmd2Send );

    SetStatusMessage( "G", "Local RTC set" );
}

/*--------------------------------------------------------------------------*\
 * Functions Designed for All Tabs Readers
 *--------------------------------------------------------------------------*
 * Parameters are the name of Objects declared in the UI
\*--------------------------------------------------------------------------*/
void CloverViewer::SuppressLineEditInCompletionList( int aTab, QString aLineEditCmdName )
{
    QWidget *pWin = QApplication::activeWindow();
    QFile file( Completion_List_Cmd_File_Name );

    QLineEdit *lineEdit_CmdName = new QLineEdit;
    lineEdit_CmdName = pWin->findChild<QLineEdit *>( aLineEditCmdName );

    // Retreive the Label Status Message
    QString *labelStatusMessageName = new QString("label_StatusMessage_Reader_");
    labelStatusMessageName->append(QString::number( aTab ) );
    QLabel *labelStatusMessage = new QLabel;
    labelStatusMessage = pWin->findChild<QLabel *>( labelStatusMessageName->toUtf8() );

    if ( lineEdit_CmdName == NULL )
    {
        DEBUG_PRINTF2("SuppressLineEditInCompletionListt: lineEdit_CmdName == NULL");
        return;
    }

    if ( lineEdit_CmdName->text().isEmpty() == false )
    {
        QString lineToRemove = lineEdit_CmdName->text();

        file.open( QIODevice::ReadWrite | QIODevice::Text );

        QString s;
        QTextStream text(&file);
        while( !text.atEnd() )
        {
            QString line = text.readLine();
            if( !line.contains( lineToRemove.toUtf8() ) )
            {
                s.append(line + "\n");
            }
            else
            {
                labelStatusMessage->setText( QString("Autocomplete line removed : %1").arg( lineToRemove ) );
            }
        }
        file.resize(0);
        text << s;
        file.close();

        //
        // Load the new model
        //
        completerLineEdit_Cmd2Send_2->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
    }
    else
    {
        labelStatusMessage->setText( tr("Command to send is emtpy.") );
    }
}

/*--------------------------------------------------------------------------*/

void CloverViewer::SendCommandSetRTC( int aTab )
{
    QWidget *pWin = QApplication::activeWindow();
    QDateTime startDateTime(QDate(2010, 1, 1), QTime(0, 0, 0));
    QDateTime currentDateTime1 = QDateTime::currentDateTime();

    qint64 numberOfSeconds = startDateTime.secsTo( currentDateTime1 );
    QString cmd2Send = QString(tr("010109"));

    // Format is 00000000 (8 digits)
    QByteArray baNumberOfSeconds = QByteArray::number(numberOfSeconds, 16);
    int numberOfZeroToPrepend = 8 - baNumberOfSeconds.length();
    QByteArray baToPrepend = QByteArray(numberOfZeroToPrepend, '0');
    baNumberOfSeconds.prepend(baToPrepend);

    QString strNumberOfSeconds = QString( baNumberOfSeconds );

    cmd2Send.append( strNumberOfSeconds.toUpper() );

    // Send Command to Reader correspondind to the Tab
    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
    QString theTab = pTabTerms->tabText( aTab - 1 );
    ScriptCmdSendToCsl( theTab, cmd2Send );

    // Retreive the label for Status Message
    QString *labelName = new QString("label_StatusMessage_Reader_");
    labelName->append(QString::number( aTab ));
    QLabel *labelStatusMessage = new QLabel;
    labelStatusMessage = pWin->findChild<QLabel *>( labelName->toUtf8() );

    labelStatusMessage->setText( tr("RTC set.") );
}

/*--------------------------------------------------------------------------*\
 * RTC Distant, there is "RTC" in script's file
 * replace it with RTC calculation
\*--------------------------------------------------------------------------*/
void CloverViewer::on_pushButton_SetRTCDistant_clicked()
{
    QString fileName = QDir::currentPath();
    fileName.append( QString( "/scripts/script_rtc.txt" ) );

    if ( QFile::exists( fileName ) == true )
    {
        // Read the fisrt line of the script's file
        QFile file( fileName );
        file.open( QFile::ReadOnly | QIODevice::Text );
        QTextStream text( &file );
        QString cmd2Send = text.readLine(); // first line is made with button's label

        //
        // Calculate RTC
        //
        QDateTime startDateTime(QDate(2010, 1, 1), QTime(0, 0, 0));
        QDateTime currentDateTime1 = QDateTime::currentDateTime();
        qint64 numberOfSeconds = startDateTime.secsTo( currentDateTime1 );

        // Format is 00000000 (8 digits)
        QByteArray baNumberOfSeconds = QByteArray::number(numberOfSeconds, 16);
        int numberOfZeroToPrepend = 8 - baNumberOfSeconds.length();
        QByteArray baToPrepend = QByteArray(numberOfZeroToPrepend, '0');
        baNumberOfSeconds.prepend(baToPrepend);

        QString strNumberOfSeconds = QString( baNumberOfSeconds ).toUpper();

        // Format the cmd2send
        cmd2Send.replace( "\"RTC\"", strNumberOfSeconds );

        // Send Command to Reader correspondind to the Tab
        QTabWidget *pTabTerms = g_pClVw->findChild<QTabWidget*>("tabWidget_Readers");
        QString theTab = pTabTerms->tabText( g_CurrentTabIndexReader );

        ScriptCmdSendToCsl( theTab, cmd2Send );

        SetStatusMessage( "G", "RTC distant is set" );
    }
    else
    {
        SetStatusMessage( "G", QString("Error: File does not exist : %1").arg( fileName ) );
    }
}

void CloverViewer::on_pushButton_SetRTCDistant_customContextMenuRequested(const QPoint &pos)
{
    QMenu *menu = new QMenu( this );
    menu->addAction( QString("Open"), this, SLOT( on_SetRTCDistant_Open() ) );
    menu->popup( QCursor::pos() );
}

void CloverViewer::on_SetRTCDistant_Open()
{
    QString fileName = QDir::currentPath();
    fileName.append( QString( "/scripts/script_rtc.txt" ) );

    if ( QFile::exists( fileName ) == true )
    {
        QDesktopServices::openUrl( QUrl::fromUserInput( fileName ) );
    }
    else
    {
        SetStatusMessage( "G", QString("Error: File does not exist : %1").arg( fileName ) );
    }
}

/*--------------------------------------------------------------------------*\
 *
 *   UI functions for All Tabs -> SendCommand with Repeat Mode
 *
\*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*\
 * Transferer le contenu de la cellule dans la lineEdit Commande to Send
\*--------------------------------------------------------------------------*/
void CloverViewer::on_tableWidget_History_Scripting_doubleClicked(const QModelIndex &index)
{
    QWidget *pWin = QApplication::activeWindow();
    QString *objName;

    // Retreive tableWidget_History_Scripting
    objName = new QString("tableWidget_History_Scripting_");
    objName->append( QString::number( g_CurrentTabIndexReader_UI ));
    QTableWidget *tableWidget = new QTableWidget();
    tableWidget = pWin->findChild<QTableWidget *>( objName->toUtf8() );

    // Retreive lineEditAutoComplete_Cmd2Send
    objName = new QString("lineEditAutoComplete_Cmd2Send_");
    objName->append( QString::number( g_CurrentTabIndexReader_UI ));
    SLineEditAutoComplete *sLineEdit = new SLineEditAutoComplete();
    sLineEdit = pWin->findChild<SLineEditAutoComplete *>( objName->toUtf8() );

    if ( index.column() != 2 )
        return;

    QVariant value = tableWidget->model()->data( index, 0 );
    QString val = QString( "" );
    if ( value.isValid() )
    {
        val = value.toString();
    }

    val.replace( " ", "" );
    sLineEdit->setText( val );
}

/*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_RepeatMode_clicked( int aTab )
{
    QWidget *pWin = QApplication::activeWindow();
    QString *objName;

    // Retrieve pusButton_Cmd2Send
    objName = new QString("pushButton_Cmd2Send_");
    objName->append( QString::number( aTab ));
    QPushButton *pushButton_Cmd2Send = pWin->findChild<QPushButton *>( objName->toUtf8() );

    // Retrieve lineEdit_RepeatTime
    objName = new QString("lineEdit_RepeatTime_Reader_");
    objName->append( QString::number( aTab ));
    QLineEdit *lineEdit_RepeatTime = pWin->findChild<QLineEdit *>( objName->toUtf8() );

    // If in Repeat Mode
    if ( g_CounterRepeatMode != 0 && g_TabInRepeatMode == aTab )
    {
        g_CounterRepeatMode = 0;
        g_TabInRepeatMode = 0;
        pushButton_Cmd2Send->setIcon( g_Icon_Arrow_Green );
        g_TimerForRepeatCommande->stop();
        SetStatusMessage( "G", "Repeat mode stopped" );
        return;
    }

    // BRY_20150921
    pushButton_Cmd2Send->setIcon( g_Icon_Arrow_Red );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 1000 );

    // Send Command to Reader correspondind to the Tab
    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>( "tabWidget_Readers" );
    QString theTab = pTabTerms->tabText( aTab - 1 );

    // Retreive lineEditAutoComplete_Cmd2Send
    objName = new QString("lineEditAutoComplete_Cmd2Send_");
    objName->append( QString::number( aTab ));
    SLineEditAutoComplete *sLineEdit_Cmd2Send = new SLineEditAutoComplete();
    sLineEdit_Cmd2Send = pWin->findChild<SLineEditAutoComplete *>( objName->toUtf8() );

    if ( sLineEdit_Cmd2Send->text().isEmpty() == false )
    {
        // Check Repeat Mode
        int repeatTime = 0;
        repeatTime = lineEdit_RepeatTime->text().toInt();
        if ( repeatTime != 0 )
        {
            repeatTime = repeatTime * 100;
            SetStatusMessage( "R", "Repeat mode started");

            g_CounterRepeatMode = 1;
            g_TabInRepeatMode = aTab;
            g_TimerForRepeatCommande->start( repeatTime );
        }
        else if ( lineEdit_RepeatTime->text().isEmpty() == false )
        {
            SetStatusMessage( "R", "Error: Enter a multiple of 100 millisecondes" );
            pushButton_Cmd2Send->setIcon( g_Icon_Arrow_Green );
            return;
        }

        ScriptCmdSendToCsl( theTab, sLineEdit_Cmd2Send->text() );
    }
    else
    {
        SetStatusMessage( "R", "Error: Command line is empty !");
        pushButton_Cmd2Send->setIcon( g_Icon_Arrow_Green );
        return;
    }

    if ( g_CounterRepeatMode == 0 )
    {
        SetStatusMessage( "G", "Command sent");
        pushButton_Cmd2Send->setIcon( g_Icon_Arrow_Green );
    }
}

/*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_clicked( int aTab )
{
    QWidget *pWin = QApplication::activeWindow();
    QString *objName;

    // Retrieve pusButton_Cmd2Send
    objName = new QString("pushButton_Cmd2Send_");
    objName->append( QString::number( aTab ));
    QPushButton *pushButton_Cmd2Send = new QPushButton();
    pushButton_Cmd2Send = pWin->findChild<QPushButton *>( objName->toUtf8() );

    // BRY_20150921
    pushButton_Cmd2Send->setIcon( g_Icon_Arrow_Red );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 1000 );

    // Send Command to Reader correspondind to the Tab
    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>( "tabWidget_Readers" );
    QString theTab = pTabTerms->tabText( aTab - 1 );

    // Retreive lineEditAutoComplete_Cmd2Send
    objName = new QString("lineEditAutoComplete_Cmd2Send_");
    objName->append( QString::number( aTab ));
    SLineEditAutoComplete *sLineEdit_Cmd2Send = new SLineEditAutoComplete();
    sLineEdit_Cmd2Send = pWin->findChild<SLineEditAutoComplete *>( objName->toUtf8() );

    if ( sLineEdit_Cmd2Send->text().isEmpty() == false )
    {
        ScriptCmdSendToCsl( theTab, sLineEdit_Cmd2Send->text() );
        SetStatusMessage( "G", "Command sent");
    }
    else
    {
        SetStatusMessage( "R", "Error: Command line is empty !");
    }

    pushButton_Cmd2Send->setIcon( g_Icon_Arrow_Green );
}

/*--------------------------------------------------------------------------*\
 *--- UI functions -> Tab 2 ------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_2_clicked()
{
    int currTab = 2;

    if ( g_TabInRepeatMode == 0 )
    {
        on_pushButton_Cmd2Send_RepeatMode_clicked( currTab );
    }
    else if ( g_TabInRepeatMode == currTab )
    {
        on_pushButton_Cmd2Send_RepeatMode_clicked( currTab );
    }
    else
    {
        on_pushButton_Cmd2Send_clicked( currTab );
    }
}

void CloverViewer::on_tableWidget_History_Scripting_2_doubleClicked(const QModelIndex &index)
{
    on_tableWidget_History_Scripting_doubleClicked( index );
}

/*--------------------------------------------------------------------------*\
 *--- UI functions -> Tab 3 ------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_3_clicked()
{
    int currTab = 3;

    if ( g_TabInRepeatMode == 0 )
    {
        on_pushButton_Cmd2Send_RepeatMode_clicked( currTab );
    }
    else if ( g_TabInRepeatMode == currTab )
    {
        on_pushButton_Cmd2Send_RepeatMode_clicked( currTab );
    }
    else
    {
        on_pushButton_Cmd2Send_clicked( currTab );
    }
}

void CloverViewer::on_tableWidget_History_Scripting_3_doubleClicked(const QModelIndex &index)
{
    on_tableWidget_History_Scripting_doubleClicked( index );
}

/*--------------------------------------------------------------------------*\
 *--- UI functions -> Tab 4 ------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_4_clicked()
{
    on_pushButton_Cmd2Send_clicked( 4 );
}

void CloverViewer::on_tableWidget_History_Scripting_4_doubleClicked(const QModelIndex &index)
{
    on_tableWidget_History_Scripting_doubleClicked( index );
}

/*--------------------------------------------------------------------------*\
 *--- UI functions -> Tab 5 ------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_5_clicked()
{
    on_pushButton_Cmd2Send_clicked( 5 );
}

void CloverViewer::on_tableWidget_History_Scripting_5_doubleClicked(const QModelIndex &index)
{
    on_tableWidget_History_Scripting_doubleClicked( index );
}

/*--------------------------------------------------------------------------*\
 *--- UI functions -> Tab 6 ------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_6_clicked()
{
    on_pushButton_Cmd2Send_clicked( 6 );
}

void CloverViewer::on_tableWidget_History_Scripting_6_doubleClicked(const QModelIndex &index)
{
    on_tableWidget_History_Scripting_doubleClicked( index );
}

/*--------------------------------------------------------------------------*\
 *--- UI functions -> Tab 7 ------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_7_clicked()
{
    on_pushButton_Cmd2Send_clicked( 7 );
}

void CloverViewer::on_tableWidget_History_Scripting_7_doubleClicked(const QModelIndex &index)
{
    on_tableWidget_History_Scripting_doubleClicked( index );
}

/*--------------------------------------------------------------------------*\
 *--- UI functions -> Tab 8 ------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Cmd2Send_8_clicked()
{
    on_pushButton_Cmd2Send_clicked( 8 );
}

void CloverViewer::on_tableWidget_History_Scripting_8_doubleClicked(const QModelIndex &index)
{
    on_tableWidget_History_Scripting_doubleClicked( index );
}

/*--------------------------------------------------------------------------*\
 * The Tab has for text : aReaderName
\*--------------------------------------------------------------------------*/
void CloverViewer::ScriptCmdSendToCsl( QString aReaderName,  QString aCmdToSend )
{
    bool         readerConnected = false;
    t_Reader     *pReader;
    t_clContext *pCtxt = CL_NULL;
    e_Result     status = CL_ERROR;
    t_Buffer    *pBuffForNet = CL_NULL;
    t_Tuple     *pTuple2Send = CL_NULL;

    status = cl_readerFindInListByFriendlyName( &pReader, aReaderName.toLatin1().data() );
    if ( status == CL_PARAMS_ERR )
    {
        DEBUG_PRINTF2("ScriptCmdSendToCsl: Reader not foud in readerlist file !");
    }

    if ( pReader )
    {
        if ( pReader->eState == STATE_CONNECT )
        {
            DEBUG_PRINTF2("ScriptCmdSendToCsl: Reader CONNECTED");
            readerConnected = true;
        }
        else
        {
            DEBUG_PRINTF2("ScriptCmdSendToCsl: Reader NOT CONNECTED");
        }
    }

    if ( readerConnected == true )
    {
        QByteArray stringUTF8 = aCmdToSend.toUtf8();
        QByteArray dataToSend = QByteArray::fromHex( stringUTF8 );
        clu8 *pData = (clu8*)(dataToSend.data());

        // Get context to have access to function pointers for memory/thread management on platform
        if ( CL_FAILED(status =  cl_GetContext( &pCtxt )) )
            return;

        // Check context
        if ( pCtxt->ptHalFuncs == CL_NULL ) return;
        if ( pCtxt->ptHalFuncs->fnAllocMem == CL_NULL ) return;

        // BRY_0923
        // Allocate buffer for data to send : 1 - 2 - 3
        // 1
        csl_pmalloc( (clvoid **)&pBuffForNet, sizeof(t_Buffer) );
        if ( pBuffForNet == CL_NULL )
        {
            DEBUG_PRINTF( "ScriptCmdSendToCsl: malloc pBuffForNet: ERROR");
            return;
        }

        // 2
        pBuffForNet->pData = CL_NULL;

        csl_pmalloc( (clvoid **)&pBuffForNet->pData, dataToSend.count());
        if ( pBuffForNet->pData == CL_NULL )
        {
            DEBUG_PRINTF( "ScriptCmdSendToCsl: malloc pBuffForNet->pData: ERROR");
            csl_FreeSafeDebug( (clvoid **)&pBuffForNet );
            return;
        }

        // 3 - Allocate tuple which holds the data
        csl_pmalloc( (clvoid **) &pTuple2Send, sizeof(t_Tuple));
        if ( pTuple2Send == CL_NULL )
        {
            DEBUG_PRINTF( "ScriptCmdSendToCsl: malloc pTuple2Send: ERROR");
            csl_FreeSafeDebug( (clvoid **)&pBuffForNet->pData );
            csl_FreeSafeDebug( (clvoid **)&pBuffForNet );
            return;
        }

        //
        // Save data in buffer to send
        //
        memcpy( pBuffForNet->pData, pData, dataToSend.count() );

        //
        // Initialize a tuple default flags with memory
        //
        if ( CL_SUCCESS( cl_initTuple( pTuple2Send, pBuffForNet, &pBuffForNet->pData, dataToSend.count() ) ) )
        {
            pReader->p_TplList2Send = pTuple2Send;
            pCtxt->ptHalFuncs->fnSemaphoreRelease( pReader->tSync.pSgl4Write );

            DEBUG_PRINTF("ScriptCmdSendToCsl: ---------> Waiting_for_Write_To_Completed ...");

            status = pCtxt->ptHalFuncs->fnSemaphoreWait( pReader->tSync.pSgl4WriteComplete, WAITING_READ_COMPLETE_TIMEOUT );
            if ( status == CL_OK )
            {
                pm_trace0("ScriptCmdSendToCsl: Write_Complete: OK");
            }
            if ( status == CL_TIMEOUT_ERR )
            {
                pm_trace0("ScriptCmdSendToCsl: Write_Complete: TIMEOUT");
            }

            DEBUG_PRINTF("ScriptCmdSendToCsl: ---------> New_Write_Can_Beging ...");
        }
        else
        {
            DEBUG_PRINTF("ScriptCmdSendToCsl: ---------> ERROR_INIT_TUPLE");
        }
    }
}

// update the counter of incoming/outgoing packets in the left of the UI
void CloverViewer::updateDataCounterDisplays( qint32 iPckts, bool bPacketsReceived )
{
    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    if ( bPacketsReceived == true )
    {
        QLabel *pLabel = pWin->findChild<QLabel *>("label_PacketsReceived");
        if ( pLabel )
        {
            pLabel->setText( QString::number( iPckts ) );
        }
    }
    else
    {
        QLabel *pLabel = pWin->findChild<QLabel *>("label_PacketSent");
        if ( pLabel )
            pLabel->setText( QString::number( iPckts ) );
    }
}

void CloverViewer::on_pushButton_AddReader_clicked()
{
    t_Reader newReader;
    e_Result status = CL_ERROR;
    t_Reader *pReader = CL_NULL;

    QApplication::setActiveWindow( g_pClVw );
    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    QString CurrentReaderType = pWin->findChild<QComboBox*>("Reader_ConnectionType_List")->currentText();

    if ( QString::compare(QString(""), CurrentReaderType ) == 0 )
    {
        ui->label_StatusMessage_ReadersList->setText("Choose a connection type.");
        return;
    }

    if ( QString::compare(QString("Serial Reader"), CurrentReaderType ) == 0 )
    {
        // fill reader with all necessary function pointers...
        cl_ReaderFillWithDefaultFields( &newReader, COM_READER_TYPE );

        // get name of readers
        QLineEdit *lineEdit = pWin->findChild<QLineEdit*>("lineEdit_ReaderName");
        QString readerName = lineEdit->text();
        readerName = readerName.toUpper();
        lineEdit->setText(readerName);
        QByteArray readerNameInUTF8 = readerName.toUtf8();
        clu8 *pData = (clu8*)(readerNameInUTF8.data());
        if ( !pData ) return;

        // Check format
        if( ReadersList::IsReaderFormatedOk( readerName ) == false )
        {
            ui->label_StatusMessage_ReadersList->setText("Error : Friendly name in bad format.");
            return;
        }

        memset( (clvoid *)newReader.aucLabel, 0, sizeof( newReader.aucLabel) );
        memcpy( (clvoid *)newReader.aucLabel, (clvoid *)pData, strlen(( const cl8*)pData));

        if ( ReadersList::IsReaderInFile( "Serial;" + readerName ) )
        {
            ui->label_StatusMessage_ReadersList->setText("Reader is already in the file.");
            return;
        }

        // Find the reader in list
        status = cl_readerFindInListByFriendlyName( &pReader, readerName.toLatin1().data() );
        if ( status == CL_OK )
        {
            ui->label_StatusMessage_ReadersList->setText("Reader is already in the list.");
            return;
        }

        // set serial port name
        QComboBox *pComboBox = pWin->findChild<QComboBox*>("comboBox_ReaderSerialPort");
        readerName = pComboBox->currentText();
        readerNameInUTF8 = readerName.toUtf8();
        pData = (clu8*)(readerNameInUTF8.data());
        if ( !pData ) return;

        // Look if a reader with same COM port already exist ?
        QTableWidget *pTableView = pWin->findChild<QTableWidget *>("ReadersCOMTableWidget");
        for ( int i = 0; i < pTableView->rowCount(); i++ )
        {
            if ( QString::compare( readerNameInUTF8, pTableView->item(i, COM_TABLE_OFFSET_PORT )->text()) == 0 )
            {
                ui->label_StatusMessage_ReadersList->setText(QString("A reader with Serial Port: %1 already exist.").arg(readerName));
                return;
            }
        }

        // BRY_13032015 - remove "COM128" that have been set by default
        memset( (clvoid*)newReader.tCOMParams.aucPortName, (unsigned char)0, strlen("COM128"));
        memcpy( (clvoid*)newReader.tCOMParams.aucPortName, (clvoid*)pData, strlen( (const cl8*)pData ) );

        // set baudrate
        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_BaudRate_List");
        QString Serial_BaudRate = pComboBox->currentText();
        newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_115200;
        if ( QString::compare(QString("4800"), Serial_BaudRate) == 0)
            newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_4800;
        if ( QString::compare(QString("9600"), Serial_BaudRate) == 0)
            newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_9600;
        if ( QString::compare(QString("19200"), Serial_BaudRate) == 0)
            newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_19200;
        if ( QString::compare(QString("38400"), Serial_BaudRate) == 0)
            newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_38400;
        if ( QString::compare(QString("57600"), Serial_BaudRate) == 0)
            newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_57600;
        if ( QString::compare(QString("115200"), Serial_BaudRate) == 0)
            newReader.tCOMParams.eBaudRate = CL_COM_BAUDRATE_115200;

        // set databits
        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_DataBits_List");
        QString Serial_DataBits = pComboBox->currentText();
        newReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_8BITS;
        if ( QString::compare(QString("8 Bits"), Serial_DataBits) == 0)
            newReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_8BITS;
        if ( QString::compare(QString("7 Bits"), Serial_DataBits) == 0)
            newReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_7BITS;
        if ( QString::compare(QString("6 Bits"), Serial_DataBits) == 0)
            newReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_6BITS;
        if ( QString::compare(QString("5 Bits"), Serial_DataBits) == 0)
            newReader.tCOMParams.eByteSize = CL_COM_BYTESIZE_5BITS ;

        // Parity bits
        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_ParityBits_List");
        QString Serial_ParityBits = pComboBox->currentText();
        newReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_NONE;
        if ( QString::compare(QString("None"), Serial_ParityBits) == 0)
            newReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_NONE;
        if ( QString::compare(QString("Even"), Serial_ParityBits) == 0)
            newReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_EVEN;
        if ( QString::compare(QString("Odd"), Serial_ParityBits) == 0)
            newReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_ODD;
        if ( QString::compare(QString("Mark"), Serial_ParityBits) == 0)
            newReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_MARK;
        if ( QString::compare(QString("Space"), Serial_ParityBits) == 0)
            newReader.tCOMParams.eParityBits = CL_COM_PARITYBIT_SPACE;

        pComboBox = pWin->findChild<QComboBox*>("Reader_Serial_StopBits_List");
        QString Serial_StopBits = pComboBox->currentText();
        newReader.tCOMParams.eStopBits = CL_COM_STOPBITS_10BIT;
        if ( QString::compare(QString("1 Bit"), Serial_StopBits) == 0)
            newReader.tCOMParams.eStopBits = CL_COM_STOPBITS_10BIT;
        if ( QString::compare(QString("1,5 Bits"), Serial_StopBits) == 0)
            newReader.tCOMParams.eStopBits = CL_COM_STOPBITS_15BIT;
        if ( QString::compare(QString("2 Bits"), Serial_StopBits) == 0)
            newReader.tCOMParams.eStopBits= CL_COM_STOPBITS_20BIT;

        if ( CL_FAILED( status = cl_readerAddToList( &newReader ) ) )
        {
//            QMessageBox MsgBox;
//            MsgBox.setText( QString( "Failed to add COM reader to list") );

            ui->label_StatusMessage_ReadersList->setText("Add COM Reader to the list failed !");
            return;
        }
        else
        {
            // non non et non !!!
//            // update list of readers in persistent storage of CloverViewer.
//            if ( CL_FAILED( status = cl_ReaderSetState( &newReader, STATE_CONNECT ) ) )
//            {
//                QMessageBox MsgBox;
//                MsgBox.setText(QString("Failed to connect COM reader to port"));
//            }
        }

        // Refresh ReadersCOMTableWidget
        //emit g_pClVw->updateClViewReaderDeviceSgnl( NULL, &newReader, NULL ); // SLOT : updateClViewReaderDeviceWindow
        updateClViewReaderDeviceWindow( NULL, &newReader, NULL );

        ReadersList::AddReaderToFile( &newReader );
        ui->label_StatusMessage_ReadersList->setText("Reader added to the list.");
    }

    if ( QString::compare(QString("IP Reader"), CurrentReaderType) == 0 )
    {

    }

    if ( QString::compare(QString("Bluetooth Reader"), CurrentReaderType) == 0 )
    {

    }
}

void CloverViewer::on_pushButton_RemoveReader_clicked()
{
    t_Reader *pReader = CL_NULL;
    e_Result     status = CL_ERROR;
    QString friendlyName;
    int rowSelectedReader = -1;

    QWidget *pWin = QApplication::activeWindow();
    if (!pWin) return;

    // Get COM reader Table pointer
    QTableWidget *pTableWidget = pWin->findChild<QTableWidget *>("ReadersCOMTableWidget");

    // Find the row selected by the user
    for ( int i = 0; i < pTableWidget->rowCount(); i++ )
    {
        if ( pTableWidget->item( i, 0 )->isSelected() )
        {
            rowSelectedReader = i;
            break;
        }
    }

    // User did'nt select Reader's row
    if ( rowSelectedReader == -1 )
    {
        ui->label_StatusMessage_ReadersList->setText("You must select a reader in reader's list.");
        return;
    }

    // Get the friendly Name of the Reader
    friendlyName = QString( pTableWidget->item( rowSelectedReader, COM_TABLE_OFFSET_FRIENDLY_NAME )->text() );

    // Initialise the Reader to be removed in list
    status = cl_readerFindInListByFriendlyName( &pReader, friendlyName.toLatin1().data() );
    if ( status == CL_PARAMS_ERR )
    {
        ui->label_StatusMessage_ReadersList->setText("Error: Reader not founded in list.");
        return;
    }

    // If checkbox checked, the user must deconnect the reader to update GUI
    if ( pReader->eState == STATE_CONNECT  )
    {
        ui->label_StatusMessage_ReadersList->setText("Error: You must first disconnect reader.");
        return;
    }

    // BRY_11122015  
//    // Ok the reader is disconnected but we must say cl_ReaderRemoveFromList it is connected
//    // for it to close the connection
//    pReader->eState = STATE_CONNECT;

    // Remove the reader from the chained list of readers
    status = cl_ReaderRemoveFromList( pReader );
    if ( status != CL_OK )
    {
        ui->label_StatusMessage_ReadersList->setText("Error: Can't remove Reader from list.");
        return;
    }

    ReadersList::RemoveReaderFromFile( "Serial;" + friendlyName );

    // Refresh ReadersCOMTableWidget
    //emit g_pClVw->updateClViewReaderDeviceSgnl( NULL, &newReader, NULL );
    // pfffff ce n'est pas la bonne fonction à utiliser pour ajouter ou retirer, je vais devoir tout refaire ...
    // pfffff en fait pour ajouter ca fonctionne ggggrrrrrrrr

    pTableWidget->removeRow( rowSelectedReader );

    QString readerRemoved = QString("Reader removed from the list : %1").arg( friendlyName );
    ui->label_StatusMessage_ReadersList->setText( readerRemoved );
}

//void CloverViewer::on_ota_password_editingFinished()
//{
//    e_Result status = CL_ERROR;
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;
//    QLineEdit *pPlatformId = pWin->findChild<QLineEdit*>("ota_password");
//    if (!pPlatformId) return;
//    QByteArray StringUTF8 = pPlatformId->text().toUtf8();
//    status = cl_SetParams( (unsigned char *)"ota_password", strlen("ota_password"), (clu8 *)(StringUTF8.data()), StringUTF8.count() );

//}

//void CloverViewer::on_comboBox_OTATransmissionType_currentIndexChanged(int index)
//{
//    e_Result status = CL_ERROR;
//    clu8    u8TransferType  =   0;

//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

//    QComboBox *pOtaTransmissionType = pWin->findChild<QComboBox*>("comboBox_OTATransmissionType");
//    if (!pOtaTransmissionType) return;


//    QString String2Comp = pOtaTransmissionType->currentText();


//    if ( !QString::compare( QString("Upgrade Reader"), String2Comp, Qt::CaseInsensitive))
//    {
//        u8TransferType  =   0;
//        // we ask for a local upgrade
//        QString NewString2Display;
//        this->ConvertByteToAscii( (unsigned char *)&u8TransferType, sizeof( u8TransferType ), &NewString2Display );

//        status = cl_SetParams( (unsigned char *)"session_type", strlen("session_type"), (clu8 *)NewString2Display.toUtf8().data(), NewString2Display.toUtf8().length() );

//        QComboBox *pComboBox = pWin->findChild<QComboBox*>("to_forward_to_rf_mode");
//        if ( pComboBox )
//            pComboBox->hide();

////        QLabel *pLabel  =  pWin->findChild<QLabel*>("Label_to_forward_to_rf_mode");
////        pLabel->hide();

//        pComboBox = pWin->findChild<QComboBox*>("tx_channel_to_forward_to");
//        if ( pComboBox )
//            pComboBox->hide();

////        pLabel  =  pWin->findChild<QLabel*>("Label_tx_channel_to_forward_to");
////        if ( pLabel)
////            pLabel->hide();

//        pComboBox = pWin->findChild<QComboBox*>("ota_forward_addressing_type");
//        if ( pComboBox )
//            pComboBox->hide();

////        pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_addressing_type");
////        if ( pLabel)
////            pLabel->hide();

//        QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("ota_destination_address_to_forward_to");
//        if ( pLineEdit )
//            pLineEdit->hide();

//        pLabel  =  pWin->findChild<QLabel*>("label_ota_destination_address_to_forward_to");
//        if ( pLabel)
//            pLabel->hide();

//        pLineEdit = pWin->findChild<QLineEdit*>("ota_forward_to_group_multi_cast");
//        if ( pLineEdit )
//            pLineEdit->hide();

//        pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_to_group_multi_cast");
//        if ( pLabel)
//            pLabel->hide();
//    }
//    else
//    {   // we ask for a forward firmware to
//        u8TransferType  =   4;
//        QString NewString2Display;
//        this->ConvertByteToAscii( (unsigned char *)&u8TransferType, sizeof( u8TransferType ), &NewString2Display );
//        status = cl_SetParams( (unsigned char *)"session_type", strlen("session_type"), (clu8 *)NewString2Display.toUtf8().data(), NewString2Display.toUtf8().length() );

//        QComboBox *pComboBox = pWin->findChild<QComboBox*>("to_forward_to_rf_mode");
//        if ( pComboBox )
//            pComboBox->show();

////        QLabel *pLabel  =  pWin->findChild<QLabel*>("Label_to_forward_to_rf_mode");
////        pLabel->show();

//        pComboBox = pWin->findChild<QComboBox*>("tx_channel_to_forward_to");
//        if ( pComboBox )
//            pComboBox->show();

////        pLabel  =  pWin->findChild<QLabel*>("Label_tx_channel_to_forward_to");
////        if ( pLabel)
////            pLabel->show();

//        pComboBox = pWin->findChild<QComboBox*>("ota_forward_addressing_type");
//        if ( pComboBox )
//            pComboBox->show();

//        pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_addressing_type");
//        if ( pLabel)
//            pLabel->show();

//        QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("ota_destination_address_to_forward_to");
//        if ( pLineEdit )
//            pLineEdit->show();

//        pLabel  =  pWin->findChild<QLabel*>("label_ota_destination_address_to_forward_to");
//        if ( pLabel)
//            pLabel->show();

//        pLineEdit = pWin->findChild<QLineEdit*>("ota_forward_to_group_multi_cast");
//        if ( pLineEdit )
//            pLineEdit->show();

//        pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_to_group_multi_cast");
//        if ( pLabel)
//            pLabel->show();
//    }
//}

//void CloverViewer::on_to_forward_to_rf_mode_currentIndexChanged(const QString &arg1)
//{
//    clu8    u8RfMode =  0;
//    e_Result status = CL_ERROR;
//    clu8    au8Data[]={0,0};

//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

//    if ( !QString::compare( arg1, QString("868Mhz Mono channel 9600 bauds RF NRZ")) )
//         u8RfMode   =   0x00;
//    if ( !QString::compare( arg1, QString("868Mhz Frequency hopping 9600 bauds RF  NRZ")) )
//         u8RfMode   =   0x01;
//    if ( !QString::compare( arg1, QString("869Mhz Mono channel 500mw 19200 bauds NRZ")) )
//         u8RfMode   =   0x02;
//    if ( !QString::compare( arg1, QString("868Mhz  Frequency hopping 19200 bauds RF NRZ") ) )
//         u8RfMode   =   0x03;
//    if ( !QString::compare( arg1, QString("Reserved(4)")) )
//         u8RfMode   =   0x04;
//    if ( !QString::compare( arg1, QString("Reserved(5)")) )
//         u8RfMode   =   0x05;
//    if ( !QString::compare( arg1, QString("868Mhz Mono channel 19200 bauds RF NRZ")) )
//         u8RfMode   =   0x06;
//    if ( !QString::compare( arg1, QString("433Mhz Mono channel 9600 bauds RF NRZ")) )
//         u8RfMode   =   0x07;
//    if ( !QString::compare( arg1, QString("433Mhz Frequency hopping 9600 bauds RF NRZ")) )
//         u8RfMode   =   0x08;
//    if ( !QString::compare( arg1, QString("915Mhz Mono channel 19200 bauds RF NRZ FCC15.427 (US)")) )
//         u8RfMode   =   0x09;
//    if ( !QString::compare( arg1, QString("915Mhz Mono channel 19200 bauds RF NRZ FCC15.427 (Australia)")) )
//         u8RfMode   =   0x0A;
//    if ( !QString::compare( arg1, QString("915Mhz Frequency hopping 19200 bauds RF NRZ FCC15.427 (US)")) )
//         u8RfMode   =   0x0B;
//    if ( !QString::compare( arg1, QString("915Mhz Mono channel 19200 bauds RF NRZ FCC15.427 (Australia)")) )
//         u8RfMode   =   0x0C;
//    if ( !QString::compare( arg1, QString("866Mhz Mono channel 9600 bauds RF NRZ (India)")) )
//         u8RfMode   =   0x0D;
//    if ( !QString::compare( arg1, QString("866Mhz Frequency hopping 9600 bauds RF  NRZ (India)")) )
//         u8RfMode   =   0x0E;
//    if ( !QString::compare( arg1, QString("433Mhz Mono channel 19200 bauds RF NRZ")) )
//         u8RfMode   =   0x0F;
//    if ( !QString::compare( arg1, QString("433Mhz  Frequency hopping 19200 bauds RF NRZ")) )
//         u8RfMode   =   0x10;

//    // save parameters to file
//    QString NewString2Display;
//    this->ConvertByteToAscii( (unsigned char *)&u8RfMode, sizeof( u8RfMode ), &NewString2Display );
//    status = cl_SetParams( (unsigned char *)"to_forward_to_rf_mode", strlen("to_forward_to_rf_mode"), (clu8 *)NewString2Display.toUtf8().data(), NewString2Display.toUtf8().length() );
//}

//void CloverViewer::on_tx_channel_to_forward_to_currentIndexChanged(int index)
//{
//    e_Result status = CL_ERROR;
//    clu8    au8Data[]={0,0};
//    clu8 u8Channel = (clu8) index;

//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

//    //
//    if ( index >= 0 )
//    {
//        QString NewString2Display;
//        this->ConvertByteToAscii( (unsigned char *)&u8Channel, sizeof( u8Channel ), &NewString2Display );
//        status = cl_SetParams( (unsigned char *)"tx_channel_to_forward_to", strlen("tx_channel_to_forward_to"), (clu8 *)NewString2Display.toUtf8().data(), NewString2Display.toUtf8().length() );
//    }
//}

//void CloverViewer::on_ota_forward_addressing_type_currentIndexChanged(const QString &arg1)
//{
//    clu8    u8OtaAddressingType =  0;
//    e_Result status = CL_ERROR;

//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;

//    if ( !QString::compare( arg1, QString("Unicast")) )
//    {
//         u8OtaAddressingType   =   CL_NET_TRANSMISSION_UNICAST;
//         QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("ota_destination_address_to_forward_to");
//         if ( pLineEdit )
//             pLineEdit->show();

//         QLabel *pLabel  =  pWin->findChild<QLabel*>("label_ota_destination_address_to_forward_to");
//         if ( pLabel)
//             pLabel->show();

//         pLineEdit = pWin->findChild<QLineEdit*>("ota_forward_to_group_multi_cast");
//         if ( pLineEdit )
//             pLineEdit->hide();

//         pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_to_group_multi_cast");
//         if ( pLabel)
//             pLabel->hide();

//    }
//    if ( !QString::compare( arg1, QString("Multicast")) )
//    {
//         u8OtaAddressingType   =   CL_NET_TRANSMISSION_MULTICAST;

//         QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("ota_destination_address_to_forward_to");
//         if ( pLineEdit )
//             pLineEdit->show();

//         QLabel *pLabel  =  pWin->findChild<QLabel*>("label_ota_destination_address_to_forward_to");
//         if ( pLabel)
//             pLabel->show();

//         pLineEdit = pWin->findChild<QLineEdit*>("ota_forward_to_group_multi_cast");
//         if ( pLineEdit )
//             pLineEdit->show();

//         pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_to_group_multi_cast");
//         if ( pLabel)
//             pLabel->show();

//    }
//    if ( !QString::compare( arg1, QString("Broadcast")) )
//    {
//         u8OtaAddressingType   =   CL_NET_TRANSMISSION_BROADCAST;

//         QLineEdit *pLineEdit = pWin->findChild<QLineEdit*>("ota_destination_address_to_forward_to");
//         if ( pLineEdit )
//             pLineEdit->hide();

//         QLabel *pLabel  =  pWin->findChild<QLabel*>("label_ota_destination_address_to_forward_to");
//         if ( pLabel)
//             pLabel->hide();

//         pLineEdit = pWin->findChild<QLineEdit*>("ota_forward_to_group_multi_cast");
//         if ( pLineEdit )
//             pLineEdit->show();

//         pLabel  =  pWin->findChild<QLabel*>("Label_ota_forward_to_group_multi_cast");
//         if ( pLabel)
//             pLabel->show();

//    }

//    if ( ( u8OtaAddressingType == CL_NET_TRANSMISSION_UNICAST) | ( u8OtaAddressingType == CL_NET_TRANSMISSION_MULTICAST) | ( u8OtaAddressingType == CL_NET_TRANSMISSION_BROADCAST) )
//    {
//        QString NewString2Display;
//        this->ConvertByteToAscii( (unsigned char *)&u8OtaAddressingType, sizeof( u8OtaAddressingType ), &NewString2Display );
//        status = cl_SetParams( (unsigned char *)"ota_addressing_type", strlen("ota_addressing_type"), (clu8 *)NewString2Display.toUtf8().data(), NewString2Display.toUtf8().length() );
//    }

//}

/**********************************************************************************************/
/* send a signal to QT to update the progress bar                                           */
/**********************************************************************************************/
void CloverViewer::UpdateClViewOTAProgress( int eState, void *pReader, int u32Progress )
{
//    QApplication::setActiveWindow( g_pClVw );
//    QWidget *pWin = QApplication::activeWindow();
//    if (!pWin) return;
    //QEvent *pEvent = new QEvent()
    //QCoreApplication::postEvent( g_pClVw, )

    qint32 q32State = eState;
    qint32 q32Progress = u32Progress;
    if ( QMetaObject::invokeMethod( g_pClVw, "updateOTAProgressBarSignal", Qt::QueuedConnection, \
                                    Q_ARG( qint32, q32State), \
                                    Q_ARG( qint32, q32Progress) \
                                    ) == true )
    {
        DEBUG_PRINTF("Method correct\n");
    }
    else
    {
        DEBUG_PRINTF("Failed to call méthode\n");
    }


//    emit g_pClVw->updateOTAProgressBarSignal( eState, pReader, u32Progress );
}

/*--------------------------------------------------------------------------*\
 * Auto Completion functions for LineEdit Cmd
\*--------------------------------------------------------------------------*/

QAbstractItemModel *CloverViewer::modelFromFile_Cmd( const QString& fileName )
{
    QFile file( fileName );

    if ( file.open(QFile::ReadOnly) == false )
    {
        return new QStringListModel( completerLineEdit_Cmd2Send_2 );
    }

#ifndef QT_NO_CURSOR
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
#endif
    QStringList words;

    while (!file.atEnd())
    {
        QByteArray line = file.readLine();
        if (!line.isEmpty())
            words << line.trimmed();
    }

#ifndef QT_NO_CURSOR
    QApplication::restoreOverrideCursor();
#endif

    file.close();

    return new QStringListModel(words, completerLineEdit_Cmd2Send_2);
}

/*--------------------------------------------------------------------------*/

void CloverViewer::completeCompletionList_Cmd(QLineEdit *lineEdit)
{
    QFile file( Completion_List_Cmd_File_Name );

    //
    // Browse file to see if lineEdit->text() is not already in there
    //
    if ( file.open(QFile::ReadOnly) == true )
    {
        while ( !file.atEnd() )
        {
            QByteArray line = file.readLine();
            if ( line.isEmpty() == false )
            {
                QString labelText = lineEdit->text();
                if ( QString::compare( labelText, line.trimmed() ) == 0 )
                {
                    file.close();
                    return;
                }
            }
        }
    }

    // We are going to reopen in Append Mode
    file.close();

    //
    // This time add lineEdit->text() at the end of file
    //
    file.open( QIODevice::Append | QIODevice::Text );
    if ( file.isOpen() == false )
    {
        DEBUG_PRINTF2("completeCompletionList: can't open file: ERROR");
    }

    QTextStream out(&file);
    out << lineEdit->text() << "\n";

    // For debug purpose
//    QString *text = new QString( ui->lineEdit->text().toUtf8() );
//    text->append(tr("\n"));
//    Debug( ui->label_Result, *text );

    file.close();

    //
    // Load the new model
    //
    completerLineEdit_Cmd2Send_2->setModel( modelFromFile_Cmd( Completion_List_Cmd_File_Name ) );
}

/*--------------------------------------------------------------------------*\
 * Auto Completion functions for Relay Adress
\*--------------------------------------------------------------------------*/

QAbstractItemModel *CloverViewer::modelFromFile_RelayAdress(const QString& fileName)
{
    QFile file(fileName);

    if ( file.open(QFile::ReadOnly) == false )
    {
        return new QStringListModel(completerLineEdit_RelayAdress);
    }

#ifndef QT_NO_CURSOR
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
#endif
    QStringList words;

    while (!file.atEnd())
    {
        QByteArray line = file.readLine();
        if (!line.isEmpty())
            words << line.trimmed();
    }

#ifndef QT_NO_CURSOR
    QApplication::restoreOverrideCursor();
#endif

    file.close();

    return new QStringListModel(words, completerLineEdit_RelayAdress);
}

/*--------------------------------------------------------------------------*/

void CloverViewer::completeCompletionList_RelayAddress(QLineEdit *lineEdit)
{
    QFile file( Completion_List_RelayAdress_File_Name );

    //
    // Browse file to see if lineEdit->text() is not already in there
    //
    if ( file.open( QFile::ReadOnly ) == true )
    {
        while ( !file.atEnd() )
        {
            QByteArray line = file.readLine();
            if ( line.isEmpty() == false )
            {
                QString labelText = lineEdit->text();
                if ( QString::compare( labelText, line.trimmed() ) == 0 )
                {
                    file.close();
                    return;
                }
            }
        }
    }

    // We are going to reopen in Append Mode
    file.close();

    //
    // This time add lineEdit->text() at the end of file
    //
    file.open( QIODevice::Append | QIODevice::Text );
    if ( file.isOpen() == false )
    {
        DEBUG_PRINTF2("completeCompletionList: can't open file: ERROR");
    }

    QTextStream out(&file);
    out << lineEdit->text() << "\n";

    // For debug purpose
//    QString *text = new QString( ui->lineEdit->text().toUtf8() );
//    text->append(tr("\n"));
//    Debug( ui->label_Result, *text );

    file.close();

    //
    // Load the new model
    //
    completerLineEdit_RelayAdress->setModel( modelFromFile_Cmd( Completion_List_RelayAdress_File_Name ) );
}

/*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_ClearCurrentTableWidget_clicked()
{
    QWidget *pWin = g_pClVw;

    // Create the name of the tableWidget
    QString *nameTableWidget = new QString("tableWidget_History_Scripting_");
    if ( g_CurrentTabIndexReader == 0 )
    {
        nameTableWidget->append( "All" );
    }
    else
    {
        nameTableWidget->append( QString::number( g_CurrentTabIndexReader + 1 ) );
    }

    // Get the Object
    QTableWidget *tableWidget = new QTableWidget;
    tableWidget = pWin->findChild<QTableWidget *>( nameTableWidget->toUtf8() );

    // Clear the TableWidget
    tableWidget->setRowCount( 0 );

    // Clear the Status Message
    SetStatusMessage("G", "");
}

/*--------------------------------------------------------------------------*/

QString CloverViewer::loadStyleSheet( const QString &sheetName )
{
    QFile file( ":/StyleSheet/" + sheetName + ".qss" );
    file.open( QFile::ReadOnly );
    QString styleSheet = QString::fromLatin1( file.readAll() );
    file.close();

    return styleSheet;
}

/*--------------------------------------------------------------------------*\
 * Print Status Message in current readers with choosen color
\*--------------------------------------------------------------------------*/

const QString QSSColorRed = QString("color: #AA0000");
const QString QSSColorGreen = QString("color: #00AA00");

/*--------------------------------------------------------------------------*/

void CloverViewer::SetStatusMessage( QString aColor, QString aMessage )
{
    QWidget *pWin = g_pClVw;

    // Retreive the Label Status Message
    QString *objName = new QString("label_StatusMessage_Reader_");
    objName->append( QString::number( g_CurrentTabIndexReader_UI ) );
    QLabel *labelStatusMessage = new QLabel;
    labelStatusMessage = pWin->findChild<QLabel *>( objName->toUtf8() );

    if ( labelStatusMessage == NULL )
        return;

    if ( aColor == "R" )
        labelStatusMessage->setStyleSheet( QSSColorRed );

    if ( aColor == "G" )
        labelStatusMessage->setStyleSheet( QSSColorGreen );

    QDateTime dateTime = dateTime.currentDateTime();
    QString messageDateTime = dateTime.toString("yyyy-MM-dd hh:mm:ss.zzz ");
    messageDateTime.append( aMessage );

    labelStatusMessage->setText( messageDateTime );
}

/*--------------------------------------------------------------------------*/

void CloverViewer::timerForRepeatCommandeSlot()
{
    QWidget *pWin = g_pClVw;

    // Show number of commands sent
    QString counter = "Repeat command ";
    counter.append( QString::number( g_CounterRepeatMode ) );

    // Retreive the Label Status Message
    QString *objName = new QString("label_StatusMessage_Reader_");
    objName->append( QString::number( g_TabInRepeatMode ) );
    QLabel *labelStatusMessage = pWin->findChild<QLabel *>( objName->toUtf8() );
    labelStatusMessage->setText( counter );

    g_CounterRepeatMode = g_CounterRepeatMode + 1;

    // Create the name of the command to send
    QString *nameWidget = new QString("lineEditAutoComplete_Cmd2Send_");
    nameWidget->append( QString::number( g_TabInRepeatMode ) );

    // Get the command
    SLineEditAutoComplete *lineEditAuto = new SLineEditAutoComplete;
    lineEditAuto = pWin->findChild<SLineEditAutoComplete *>( nameWidget->toUtf8() );
    QString commandToSend = lineEditAuto->text();

    // Get the Tab
    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
    QString tabText = pTabTerms->tabText( g_TabInRepeatMode - 1 );

    // Send the Command
    ScriptCmdSendToCsl( tabText, commandToSend );

    QCoreApplication::processEvents( QEventLoop::AllEvents, 1000 );
}

/*--------------------------------------------------------------------------*\
 * Manage Context Menu for SaveLogFileName
\*--------------------------------------------------------------------------*/

void CloverViewer::on_lineEdit_SaveLogFileName_customContextMenuRequested(const QPoint &pos)
{
    QMenu *menu = new QMenu( this );
    menu->addAction( QString("Open"), this, SLOT( on_ContextMenu_Open() ) );
    menu->addAction( QString("Disable"), this, SLOT( on_ContextMenu_Disable() ) );
    menu->popup( QCursor::pos() );
}

void CloverViewer::on_ContextMenu_Open()
{
    QString fileName = g_FileName_SaveTracesForReaders[ g_CurrentTabIndexReader ];
    QDesktopServices::openUrl( QUrl::fromUserInput( fileName ) );
}

void CloverViewer::on_ContextMenu_Disable()
{
    g_FileName_SaveTracesForReaders[ g_CurrentTabIndexReader ] = "";
    ui->lineEdit_SaveLogFileName->setText( "" );
}

bool CloverViewer::LogMode()
{
    if ( QString::compare( ui->lineEdit_SaveLogFileName->text(), QString("") ) == 0 )
    {
        return false;
    }
    return true;
}

/*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_SaveLogFilePath_clicked()
{
    QString fileName = QFileDialog::getSaveFileName
    (
        this,
        tr("Open the file to save traces to"),
        QString(""),
        tr("All Files (*);;Text Files (*.txt);;Log Files (*.log)"),
        0,
        QFileDialog::DontConfirmOverwrite
     );

    if ( !fileName.isEmpty() )
    {
        g_FileName_SaveTracesForReaders[ g_CurrentTabIndexReader ] = fileName;

        // Display fileName in friendly way
        QStringList listFileName = fileName.split("/");
        QString fileNameToDisplay = listFileName[ listFileName.count() - 1 ];
        ui->lineEdit_SaveLogFileName->setText( fileNameToDisplay );

        SetStatusMessage( "G", "Trace file choosen" );
    }
}

/*--------------------------------------------------------------------------*\
 * Set Path Rout for current reader
\*--------------------------------------------------------------------------*/
void CloverViewer::on_pushButton_SetRoute_clicked()
{
    QWidget *pWin = QApplication::activeWindow();

    QLineEdit *lineEdit_Relay0 = ui->lineEdit_AddressMacTarget;
    QLineEdit *lineEdit_Relay1 = ui->lineEdit_AddressRelay_1;
    QLineEdit *lineEdit_Relay2 = ui->lineEdit_AddressRelay_2;

    if ( lineEdit_Relay0 == NULL || lineEdit_Relay1 == NULL || lineEdit_Relay2 == NULL )
    {
        DEBUG_PRINTF2( "SendCommandSetPath: Error parameter == NULL" );
        return;
    }

    //QString emptyRelay = QString(tr("XX XX XX XX")); BRY_25012016
    QString emptyRelay = QString(tr("XXXXXXXX"));
    QString cmd2Send = QString(tr("01040201010D"));
    QString macTarget = lineEdit_Relay0->text();
    QString relay1 = lineEdit_Relay1->text();
    QString relay2 = lineEdit_Relay2->text();
    int addressLength = emptyRelay.length();
    int nbRelay = 0;

    // 1 - Check User's Entry
    if ( QString::compare( emptyRelay, macTarget ) == 0 ||  macTarget.length() != addressLength )
    {
        SetStatusMessage( "R", "Error: No target address" );
        return;
    }

    macTarget.remove( QChar(' ') );

    if ( QString::compare( emptyRelay, relay1 ) != 0 && relay1.length() == addressLength )
    {
        nbRelay = 1;
        relay1.remove( QChar(' ') );
    }

    if ( QString::compare( emptyRelay, relay2 ) != 0 && relay2.length() == addressLength && nbRelay == 1 )
    {
        nbRelay = 2;
        relay2.remove( QChar(' ') );
    }

    // 2 - Build cmd2Send
    if ( nbRelay == 0 )
    {
        cmd2Send.append(tr("00"));
        cmd2Send.append(macTarget);
        cmd2Send.append(tr("0000000000000000"));
    }

    if ( nbRelay == 1 )
    {
        cmd2Send.append(tr("01"));
        cmd2Send.append(macTarget);
        cmd2Send.append(relay1);
        cmd2Send.append(tr("00000000"));
    }

    if ( nbRelay == 2 )
    {
        cmd2Send.append(tr("02"));
        cmd2Send.append(macTarget);
        cmd2Send.append(relay1);
        cmd2Send.append(relay2);
    }

    //
    // 3 - Send Command to Reader correspondind to the Tab
    //
    QTabWidget *pTabTerms = pWin->findChild<QTabWidget*>("tabWidget_Readers");
    QString theTab = pTabTerms->tabText( g_CurrentTabIndexReader );
    ScriptCmdSendToCsl( theTab, cmd2Send );

    //
    // 4 - Update Completion List RelayAdress
    //
    if ( nbRelay == 0 )
    {
        completeCompletionList_RelayAddress( lineEdit_Relay0 );
    }

    if ( nbRelay == 1 )
    {
        completeCompletionList_RelayAddress( lineEdit_Relay0 );
        completeCompletionList_RelayAddress( lineEdit_Relay1 );
    }

    if ( nbRelay == 2 )
    {
        completeCompletionList_RelayAddress( lineEdit_Relay0 );
        completeCompletionList_RelayAddress( lineEdit_Relay1 );
        completeCompletionList_RelayAddress( lineEdit_Relay2 );
    }

    SetStatusMessage( "G", "Target address set" );
}

/*--------------------------------------------------------------------------*\
 *--------------------------------------------------------------------------*
 *--- Button's scripts functions -------------------------------------------*
 *--------------------------------------------------------------------------*
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Script_clicked( int aScript, int aTab )
{
    QString fileName = QDir::currentPath();
    fileName.append( QString( "/scripts/script_%1.txt" ).arg( QString::number( aScript ) ) );

    QFile file( fileName );
    file.open( QIODevice::ReadOnly | QIODevice::Text );

    QTextStream text( &file );
    QString line = text.readLine(); // skipe first line, it's made with button's label
    int cptLine = 2;

    // Manage ExecutingScriptMode
    if ( g_ExecutingScriptMode == true )
    {
        g_ExecutingScriptMode = false;
        g_QuitScriptMode = true;
    }
    else
    {
        g_QuitScriptMode = false;
    }

    while( !text.atEnd() && ( g_QuitScriptMode == false ) )
    {
        line = text.readLine();
        QStringList scriptLine = line.split(";");

        // Format: command;time to repeat;wait for next
        if ( scriptLine.count() != 3 )
        {
            SetStatusMessage( "R", QString("Parsing error line: %1").arg( QString::number( cptLine ) ) );
            return;
        }

        //
        // Parse the script's ligne
        //
        QString command = scriptLine[ 0 ];

        int nbTimes = scriptLine[ 1 ].toInt();
        if ( nbTimes == 0 )
        {
            QString message = QString("Error col2 line %1").arg( QString::number( cptLine ) );
            SetStatusMessage( "R", message );
            return;
        }

        unsigned long sleepNTimesHundredMS = scriptLine[ 2 ].toInt();
        if ( sleepNTimesHundredMS == 0 )
        {
            QString message = QString("Error col3 line %1").arg( QString::number( cptLine ) );
            SetStatusMessage( "R", message );
            return;
        }

        //
        // Find the reader's name to send the command to
        //
        QTabWidget *pTabTerms = g_pClVw->findChild<QTabWidget*>( "tabWidget_Readers" );
        QString theTab = pTabTerms->tabText( aTab );
        if ( QString::compare( theTab, "No Reader", Qt::CaseInsensitive) == 0 )
        {
            SetStatusMessage( "R", "Error: There is no reader !" );
            return;
        }

        //
        // Execute the command
        //
        for ( int i = 1; i <= nbTimes && ( g_QuitScriptMode == false ); i++ )
        {
            g_ExecutingScriptMode = true;

            QString message = QString( "Line %1 Cmd %2 on %3 times, wait: %4").arg
            (
                QString::number( cptLine ),
                QString::number( i ),
                QString::number( nbTimes ),
                QString::number( sleepNTimesHundredMS * 100 )
            );
            DEBUG_PRINTF("SCRIPT_MODE: %s", message.toLatin1().data() );
            SetStatusMessage( "G", message );
            QCoreApplication::processEvents( QEventLoop::AllEvents );

            ScriptCmdSendToCsl( theTab, command );

            // Wait n number of time * 100 ms
            for ( int i = 1; i <= sleepNTimesHundredMS; i++ )
            {
                QCoreApplication::processEvents( QEventLoop::AllEvents );
                QThread::msleep( 100 );
                QCoreApplication::processEvents( QEventLoop::AllEvents );
            }
        }

        cptLine += 1;
    }

    if ( g_QuitScriptMode )
    {
        SetStatusMessage( "R", "Script stopped by user" );
        QCoreApplication::processEvents( QEventLoop::AllEvents );
    }

    g_ExecutingScriptMode = false;

    file.close();
}

/*--------------------------------------------------------------------------*\
 * Open the corresponding script file
\*--------------------------------------------------------------------------*/
void CloverViewer::on_pushButton_Script_contextMenu_Open( int aScript )
{
    QString fileName = QDir::currentPath();
    fileName.append( QString( "/scripts/script_%1.txt" ).arg( QString::number( aScript ) ) );

    if ( QFile::exists( fileName ) == true )
    {
        DEBUG_PRINTF("on_pushButton_Script_contextMenu_Open: fileName: %s", fileName.toLatin1().data() );
        QDesktopServices::openUrl( QUrl::fromUserInput( fileName ) );
    }
    else
    {
        SetStatusMessage( "G", QString("Error: File does not exist : %1").arg( fileName ) );
    }
}

/*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Script_contextMenu_Refresh( int aScript )
{
    QString fileName = QDir::currentPath();
    fileName.append( QString( "/scripts/script_%1.txt" ).arg( QString::number( aScript ) ) );

    // Retrieve pushButton_Script_x to set the text
    QString *objName = new QString("pushButton_Script_");
    objName->append( QString::number( aScript ));
    QPushButton *pushButton = g_pClVw->findChild<QPushButton *>( objName->toUtf8() );

    if ( QFile::exists( fileName ) == true )
    {
        // Read the fisrt line of the script's file
        QFile file( fileName );
        file.open( QFile::ReadOnly | QIODevice::Text );
        QTextStream text( &file );
        QString line = text.readLine(); // first line is for button's label

        QString label;
        if ( line.length() > 50 )
        {
            label = line.left( 50 );
            label.append("...");
        }
        else
        {
            label = line;
        }

        pushButton->setText( label );
        if ( label.length() < 50 )
        {
            // Not working !
            pushButton->setMinimumSize( 0, 0 );
        }
    }
    else
    {
        pushButton->setText( "No script file!" );
    }
}

/*
 * Change button's color while the sript is executing
 */
QString stylesheet_ColorOn = QString("color:red;");
QString stylesheet_ColorOff = QString("color:;");

/*--------------------------------------------------------------------------*\
 *--- Script_1
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Script_1_customContextMenuRequested(const QPoint &pos)
{
    QMenu *menu = new QMenu( this );
    menu->addAction( QString("Open"), this, SLOT( on_pushButton_Script_1_contextMenu_Open() ) );
    menu->addAction( QString("Refresh"), this, SLOT( on_pushButton_Script_1_contextMenu_Refresh() ) );
    menu->popup( QCursor::pos() );
}

void CloverViewer::on_pushButton_Script_1_clicked()
{
    ui->pushButton_Script_1->setStyleSheet( stylesheet_ColorOn );
    on_pushButton_Script_clicked( 1, g_CurrentTabIndexReader );
    ui->pushButton_Script_1->setStyleSheet( stylesheet_ColorOff );
}

void CloverViewer::on_pushButton_Script_1_contextMenu_Open()
{
    on_pushButton_Script_contextMenu_Open( 1 );
}

void CloverViewer::on_pushButton_Script_1_contextMenu_Refresh()
{
    on_pushButton_Script_contextMenu_Refresh( 1 );
}

/*--------------------------------------------------------------------------*\
 *--- Script_2
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Script_2_customContextMenuRequested(const QPoint &pos)
{
    QMenu *menu = new QMenu( this );
    menu->addAction( QString("Open"), this, SLOT( on_pushButton_Script_2_contextMenu_Open() ) );
    menu->addAction( QString("Refresh"), this, SLOT( on_pushButton_Script_2_contextMenu_Refresh() ) );
    menu->popup( QCursor::pos() );
}

void CloverViewer::on_pushButton_Script_2_clicked()
{
    ui->pushButton_Script_2->setStyleSheet( stylesheet_ColorOn );
    on_pushButton_Script_clicked( 2, g_CurrentTabIndexReader );
    ui->pushButton_Script_2->setStyleSheet( stylesheet_ColorOff );
}

void CloverViewer::on_pushButton_Script_2_contextMenu_Open()
{
    on_pushButton_Script_contextMenu_Open( 2 );
}

void CloverViewer::on_pushButton_Script_2_contextMenu_Refresh()
{
    on_pushButton_Script_contextMenu_Refresh( 2 );
}

/*--------------------------------------------------------------------------*\
 *--- Script_3
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Script_3_customContextMenuRequested(const QPoint &pos)
{
    QMenu *menu = new QMenu( this );
    menu->addAction( QString("Open"), this, SLOT( on_pushButton_Script_3_contextMenu_Open() ) );
    menu->addAction( QString("Refresh"), this, SLOT( on_pushButton_Script_3_contextMenu_Refresh() ) );
    menu->popup( QCursor::pos() );
}

void CloverViewer::on_pushButton_Script_3_clicked()
{
    ui->pushButton_Script_3->setStyleSheet( stylesheet_ColorOn );
    on_pushButton_Script_clicked( 3, g_CurrentTabIndexReader );
    ui->pushButton_Script_3->setStyleSheet( stylesheet_ColorOff );
}

void CloverViewer::on_pushButton_Script_3_contextMenu_Open()
{
    on_pushButton_Script_contextMenu_Open( 3 );
}

void CloverViewer::on_pushButton_Script_3_contextMenu_Refresh()
{
    on_pushButton_Script_contextMenu_Refresh( 3 );
}

/*--------------------------------------------------------------------------*\
 *--- Script_4
\*--------------------------------------------------------------------------*/

void CloverViewer::on_pushButton_Script_4_clicked()
{
    ui->pushButton_Script_4->setStyleSheet( stylesheet_ColorOn );
    on_pushButton_Script_clicked( 4, g_CurrentTabIndexReader );
    ui->pushButton_Script_4->setStyleSheet( stylesheet_ColorOff );
}

void CloverViewer::on_pushButton_Script_4_customContextMenuRequested(const QPoint &pos)
{
    QMenu *menu = new QMenu( this );
    menu->addAction( QString("Open"), this, SLOT( on_pushButton_Script_4_contextMenu_Open() ) );
    menu->addAction( QString("Refresh label"), this, SLOT( on_pushButton_Script_4_contextMenu_Refresh() ) );
    menu->popup( QCursor::pos() );
}

void CloverViewer::on_pushButton_Script_4_contextMenu_Open()
{
    on_pushButton_Script_contextMenu_Open( 4 );
}

void CloverViewer::on_pushButton_Script_4_contextMenu_Refresh()
{
    on_pushButton_Script_contextMenu_Refresh( 4 );
}
