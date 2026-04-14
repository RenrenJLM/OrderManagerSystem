#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>

class DatabaseManager
{
public:
    DatabaseManager();

    bool initialize();
    QString lastError() const;

private:
    bool openDatabase();
    bool enableForeignKeys();
    bool createTables();
    bool executeStatement(const QString &statement);

    QString m_lastError;
};

#endif // DATABASEMANAGER_H
