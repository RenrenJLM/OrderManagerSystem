#include "databasemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
const char kConnectionName[] = "ordermanager_connection";
const char kDatabaseFileName[] = "OrderManagerSystem.db";
}

DatabaseManager::DatabaseManager() {}

bool DatabaseManager::initialize()
{
    m_lastError.clear();

    return openDatabase() && enableForeignKeys() && createTables();
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

bool DatabaseManager::openDatabase()
{
    QSqlDatabase database;
    if (QSqlDatabase::contains(kConnectionName)) {
        database = QSqlDatabase::database(kConnectionName);
    } else {
        database = QSqlDatabase::addDatabase("QSQLITE", kConnectionName);
    }

    const QString databasePath =
        QDir(QCoreApplication::applicationDirPath()).filePath(kDatabaseFileName);
    database.setDatabaseName(databasePath);

    if (!database.open()) {
        m_lastError = database.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::enableForeignKeys()
{
    return executeStatement(QStringLiteral("PRAGMA foreign_keys = ON;"));
}

bool DatabaseManager::createTables()
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS product_models ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "created_at TEXT NOT NULL"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS option_templates ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "product_model_id INTEGER NOT NULL,"
            "name TEXT NOT NULL,"
            "created_at TEXT NOT NULL,"
            "FOREIGN KEY(product_model_id) REFERENCES product_models(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS option_template_components ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "option_template_id INTEGER NOT NULL,"
            "component_name TEXT NOT NULL,"
            "quantity_per_set INTEGER NOT NULL,"
            "FOREIGN KEY(option_template_id) REFERENCES option_templates(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS order_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_date TEXT NOT NULL,"
            "customer_name TEXT NOT NULL,"
            "product_model TEXT NOT NULL,"
            "quantity_sets INTEGER NOT NULL,"
            "unit_price REAL NOT NULL DEFAULT 0,"
            "configuration_name TEXT NOT NULL,"
            "shipped_sets INTEGER NOT NULL DEFAULT 0,"
            "unshipped_sets INTEGER NOT NULL,"
            "created_at TEXT NOT NULL"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS order_item_components ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_item_id INTEGER NOT NULL,"
            "component_name TEXT NOT NULL,"
            "quantity_per_set INTEGER NOT NULL,"
            "total_required_quantity INTEGER NOT NULL,"
            "shipped_quantity INTEGER NOT NULL DEFAULT 0,"
            "unshipped_quantity INTEGER NOT NULL,"
            "source_type TEXT NOT NULL,"
            "FOREIGN KEY(order_item_id) REFERENCES order_items(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS shipment_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_item_id INTEGER NOT NULL,"
            "order_item_component_id INTEGER,"
            "shipment_date TEXT NOT NULL,"
            "shipment_quantity INTEGER NOT NULL,"
            "note TEXT,"
            "created_at TEXT NOT NULL,"
            "FOREIGN KEY(order_item_id) REFERENCES order_items(id),"
            "FOREIGN KEY(order_item_component_id) REFERENCES order_item_components(id)"
            ");")
    };

    for (const QString &statement : statements) {
        if (!executeStatement(statement)) {
            return false;
        }
    }

    return true;
}

bool DatabaseManager::executeStatement(const QString &statement)
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    if (!query.exec(statement)) {
        m_lastError = query.lastError().text();
        return false;
    }

    return true;
}
