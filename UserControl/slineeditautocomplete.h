/*--------------------------------------------------------------------------*\
 * SLineEditAutoComplete derive from QLineEdit to add some features for
 * autocompletion and using of QCompleter
\*--------------------------------------------------------------------------*/

#ifndef SLINEEDITAUTOCOMPLETE_H
#define SLINEEDITAUTOCOMPLETE_H

#include <QLineEdit>

QT_BEGIN_NAMESPACE
class QCompleter;
QT_END_NAMESPACE

class SLineEditAutoComplete : public QLineEdit
{
    Q_OBJECT

public:
    SLineEditAutoComplete( QWidget *parent = 0, QString fileName = QString("") );
    ~SLineEditAutoComplete();

    void setCompleter( QCompleter *completer );
    QCompleter *completer() const;

    void setModelFromFile();

Q_SIGNALS:
    void keyPressEnteredSignal();

protected:
    void keyPressEvent( QKeyEvent *e ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent *e ) Q_DECL_OVERRIDE;

private:
    QCompleter *comp;
    QString completionListFileName;
    void deleteCompletionLine();
    void addCompletionLine();
};

#endif // SLINEEDITAUTOCOMPLETE_H
