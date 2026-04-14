#include "databasemanager.h"
#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QMessageBox>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "OrderManagerSystem_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    DatabaseManager databaseManager;
    if (!databaseManager.initialize()) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("Database Initialization Failed"),
                              databaseManager.lastError());
        return 1;
    }

    MainWindow w;
    w.show();
    return QCoreApplication::exec();
}
