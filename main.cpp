#include "databasemanager.h"
#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QMessageBox>
#include <QTranslator>

#include <memory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("订单管理系统"));

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
                              QStringLiteral("数据库初始化失败"),
                              databaseManager.lastError());
        return 1;
    }

    auto window = std::make_unique<MainWindow>();
    window->show();
    return QCoreApplication::exec();
}
