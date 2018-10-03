/*--------------------------------------------------------------------------*\
 * SoDevLog - 2015 - BRy - SLineEditAutoComplete
 * Control Utilisateur d'Autocompletion, dérivé de QLineEdit
 *--------------------------------------------------------------------------*
 * Je souhaite récupérer l'event quand l'utilisateur clique pour afficher la
 * liste non triée. En suite dès qu'un car est tapé, je souhaite faire
 * un tri.
 *--------------------------------------------------------------------------*
 * Remarques :
 * Difficile à mettre au point par exemple l'utilisation de
 * comp->setWidget( this ); ne tombe pas sous le sens et il faut le faire
 * après les modifications de "comp"
 * Ce control utilisateur est basé sur les exemples "completer" de Qt
 * completer & customcompleter
 *--------------------------------------------------------------------------*
 * 03/12/2015 - Reverse words
 * l'append d'une nouvelle ligne se passe toujours en fin de fichier, elle
 * se retrouve en fin de liste donc on inverse
 * BRY_08012016 : je souhaite que l'on ne puisse supprimer une ligne
 * d'autocompletion que quand la popup est visible
\*--------------------------------------------------------------------------*/

#include "slineeditautocomplete.h"
#include <QtWidgets>
#include <QCompleter>
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QtDebug>
#include <QApplication>
#include <QModelIndex>
#include <QAbstractItemModel>
#include <QScrollBar>

SLineEditAutoComplete::SLineEditAutoComplete( QWidget *parent, QString fileName ) : QLineEdit( parent ), comp(0)
{
    this->completionListFileName = QString( fileName );
}

SLineEditAutoComplete::~SLineEditAutoComplete()
{

}

void SLineEditAutoComplete::setCompleter( QCompleter *completer )
{
    if ( comp )
        QObject::disconnect( comp, 0, this, 0 );

    comp = completer;
    comp->setCompletionMode( QCompleter::UnfilteredPopupCompletion );
    comp->setWidget( this );

    // Keep default behavior
    QLineEdit::setCompleter( comp );
}

QCompleter *SLineEditAutoComplete::completer() const
{
    return comp;
}

void SLineEditAutoComplete::mousePressEvent( QMouseEvent *e )
{
    QLineEdit::mousePressEvent(e);

    if ( comp->popup()->isVisible() )
    {
        e->ignore();
        return; // let the completer do default behavior
    }

    if ( this->text().isEmpty() == false )
    {
        e->ignore();
        return; // let the completer do default behavior
    }

    // BRY_08012016
    if ( this->text().isEmpty() == true )
    {
        // Set QCompleter's mode to Unfiltered
        comp->setCompletionMode( QCompleter::UnfilteredPopupCompletion );
        comp->setWidget( this );
        QLineEdit::setCompleter( comp );
    }

    QRect cr = cursorRect();
    cr.setWidth( this->width() );

    comp->complete( cr ); // popup it up!
}

void SLineEditAutoComplete::keyPressEvent( QKeyEvent *e )
{
    if ( comp->popup()->isVisible() == true && this->text().isEmpty() == false )
    {
        // The following keys are forwarded by the completer to the widget
        switch ( e->key() )
        {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                e->ignore();
                return; // let the completer do default behavior

            // BRY_08012016
            case Qt::Key_Delete:
                deleteCompletionLine();
                e->ignore();
//                emit keyPressEnteredSignal();
                break;

            default:
                break;
        }
    }

    //
    // comp->popup()->isVisible() == false signifit que this->text()
    // n'existe pas dans la liste et comme il n'est pas vide,
    // on peut essayer de l'ajouter dans la liste
    //
    if ( comp->popup()->isVisible() == false && this->text().isEmpty() == false )
    {
        switch ( e->key() )
        {
            case Qt::Key_Enter:
            case Qt::Key_Return:
                addCompletionLine();
                emit keyPressEnteredSignal();
                return;
        }
    }

    // BRY_08012016
//    if ( this->text().isEmpty() == false )
//    {
//        // The following keys are forwarded by the completer to the widget
//        switch ( e->key() )
//        {
//            case Qt::Key_Enter:
//            case Qt::Key_Return:
//                emit keyPressEnteredSignal();
//                break;
//            case Qt::Key_Delete:
//                deleteCompletionLine();
//                break;
//        }
//    }

    if ( this->text().isEmpty() == true )
    {
        // Set QCompleter's mode to Unfiltered
        comp->setCompletionMode( QCompleter::UnfilteredPopupCompletion );
        comp->setWidget( this );
    }
    else
    {
        comp->setCompletionMode( QCompleter::PopupCompletion );
        comp->setWidget( this );
    }
    QLineEdit::setCompleter( comp );

    QLineEdit::keyPressEvent(e);
}

void SLineEditAutoComplete::setModelFromFile()
{
    QFile file( this->completionListFileName );

    //
    // En fait ici, si on arrive pas à ouvrir le fichier
    // c'est qu'il à déjà été ouvert précédemment et lu.
    // Alors on retourne la liste.
    //
    if ( file.open( QFile::ReadOnly ) == false )
    {
        comp->setModel( new QStringListModel( comp ) );
    }

#ifndef QT_NO_CURSOR
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
#endif
    QStringList words;

    while ( !file.atEnd() )
    {
        QByteArray line = file.readLine();
        if (!line.isEmpty())
            words << line.trimmed();
    }

#ifndef QT_NO_CURSOR
    QApplication::restoreOverrideCursor();
#endif

    //
    // Si ici je ferme le fichier, alors on fera la lecture à chaque fois
    //
    file.close();

    // Inverser la list words
    QStringList words_inverse;
    QListIterator<QString> iter( words );
    iter.toBack();
    while( iter.hasPrevious() )
    {
        words_inverse << iter.previous();
    }

    comp->setModel( new QStringListModel( words_inverse, comp ) );
}

void SLineEditAutoComplete::deleteCompletionLine()
{
    QFile file( this->completionListFileName );
    file.open( QIODevice::ReadWrite | QIODevice::Text );

    QString lineToRemove = this->text();

    QString s;
    QTextStream text(&file);
    while( !text.atEnd() )
    {
        QString line = text.readLine();
        if( QString::compare( lineToRemove, line, Qt::CaseSensitive ) != 0 )
        {
            s.append( line + "\n" );
        }
    }
    file.resize(0);
    text << s;
    file.close();

    // Empty the UI string
    this->setText(QString(""));

    //
    // Load the new model
    //
    this->setModelFromFile();
}

void SLineEditAutoComplete::addCompletionLine()
{
    QFile file( this->completionListFileName );

    QString lineToAdd = this->text().trimmed();

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
                if ( QString::compare( lineToAdd, line.trimmed() ) == 0 )
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
        //DEBUG_PRINTF2("addLine: can't open file: ERROR");
    }

    QTextStream out(&file);
    out << lineToAdd << "\n";

    file.close();

    //
    // Load the new model
    //
    this->setModelFromFile();
}
