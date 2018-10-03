#include "cloverviewer.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    CloverViewer w;
    w.show();
    return a.exec();
}
