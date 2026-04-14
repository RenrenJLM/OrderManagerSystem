#include "databasemanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QStringList>

namespace {
const char kConnectionName[] = "ordermanager_connection";
const char kDatabaseFileName[] = "OrderManagerSystem.db";

QString currentTimestamp()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}
}

DatabaseManager::DatabaseManager() {}

bool DatabaseManager::initialize()
{
    m_lastError.clear();

    return openDatabase() && enableForeignKeys() && createTables();
}

bool DatabaseManager::ensureMinimumDemoData()
{
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.isValid() || !database.isOpen()) {
        m_lastError = QStringLiteral("Database connection is not available.");
        return false;
    }

    QSqlQuery countQuery(database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM product_models;")) || !countQuery.next()) {
        m_lastError = countQuery.lastError().text();
        return false;
    }

    if (countQuery.value(0).toInt() > 0) {
        return true;
    }

    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery insertProduct(database);
    insertProduct.prepare(QStringLiteral(
        "INSERT INTO product_models (name, created_at) VALUES (?, ?);"));

    insertProduct.bindValue(0, QStringLiteral("OMS-标准灯箱"));
    insertProduct.bindValue(1, currentTimestamp());
    if (!insertProduct.exec()) {
        m_lastError = insertProduct.lastError().text();
        database.rollback();
        return false;
    }
    const int lightBoxModelId = insertProduct.lastInsertId().toInt();

    insertProduct.bindValue(0, QStringLiteral("OMS-展示架"));
    insertProduct.bindValue(1, currentTimestamp());
    if (!insertProduct.exec()) {
        m_lastError = insertProduct.lastError().text();
        database.rollback();
        return false;
    }
    const int standModelId = insertProduct.lastInsertId().toInt();

    QSqlQuery insertTemplate(database);
    insertTemplate.prepare(QStringLiteral(
        "INSERT INTO option_templates (product_model_id, name, created_at) VALUES (?, ?, ?);"));

    insertTemplate.bindValue(0, lightBoxModelId);
    insertTemplate.bindValue(1, QStringLiteral("标准A配置"));
    insertTemplate.bindValue(2, currentTimestamp());
    if (!insertTemplate.exec()) {
        m_lastError = insertTemplate.lastError().text();
        database.rollback();
        return false;
    }
    const int standardATemplateId = insertTemplate.lastInsertId().toInt();

    insertTemplate.bindValue(0, lightBoxModelId);
    insertTemplate.bindValue(1, QStringLiteral("标准B配置"));
    insertTemplate.bindValue(2, currentTimestamp());
    if (!insertTemplate.exec()) {
        m_lastError = insertTemplate.lastError().text();
        database.rollback();
        return false;
    }
    const int standardBTemplateId = insertTemplate.lastInsertId().toInt();

    insertTemplate.bindValue(0, standModelId);
    insertTemplate.bindValue(1, QStringLiteral("展示架基础配置"));
    insertTemplate.bindValue(2, currentTimestamp());
    if (!insertTemplate.exec()) {
        m_lastError = insertTemplate.lastError().text();
        database.rollback();
        return false;
    }
    const int standTemplateId = insertTemplate.lastInsertId().toInt();

    QSqlQuery insertComponent(database);
    insertComponent.prepare(QStringLiteral(
        "INSERT INTO option_template_components "
        "(option_template_id, component_name, quantity_per_set) VALUES (?, ?, ?);"));

    const QList<OrderComponentData> standardAComponents = {
        {QStringLiteral("光源"), 2, QString()},
        {QStringLiteral("灯罩"), 4, QString()},
        {QStringLiteral("电源"), 1, QString()}
    };
    const QList<OrderComponentData> standardBComponents = {
        {QStringLiteral("光源"), 3, QString()},
        {QStringLiteral("灯罩"), 6, QString()},
        {QStringLiteral("白色机头"), 1, QString()}
    };
    const QList<OrderComponentData> standComponents = {
        {QStringLiteral("主架"), 1, QString()},
        {QStringLiteral("连接件"), 6, QString()},
        {QStringLiteral("底座"), 2, QString()}
    };

    const auto insertComponents = [&](int templateId, const QList<OrderComponentData> &components) {
        for (const OrderComponentData &component : components) {
            insertComponent.bindValue(0, templateId);
            insertComponent.bindValue(1, component.componentName);
            insertComponent.bindValue(2, component.quantityPerSet);
            if (!insertComponent.exec()) {
                m_lastError = insertComponent.lastError().text();
                return false;
            }
        }
        return true;
    };

    if (!insertComponents(standardATemplateId, standardAComponents)
        || !insertComponents(standardBTemplateId, standardBComponents)
        || !insertComponents(standTemplateId, standComponents)) {
        database.rollback();
        return false;
    }

    if (!database.commit()) {
        m_lastError = database.lastError().text();
        database.rollback();
        return false;
    }

    return true;
}

QList<ProductModelOption> DatabaseManager::productModels()
{
    QList<ProductModelOption> models;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, name FROM product_models ORDER BY name ASC;"));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return models;
    }

    while (query.next()) {
        ProductModelOption model;
        model.id = query.value(0).toInt();
        model.name = query.value(1).toString();
        models.append(model);
    }

    return models;
}

QList<TemplateOption> DatabaseManager::optionTemplatesForProduct(int productModelId)
{
    QList<TemplateOption> templates;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, name FROM option_templates "
        "WHERE product_model_id = ? ORDER BY name ASC;"));
    query.addBindValue(productModelId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return templates;
    }

    while (query.next()) {
        TemplateOption option;
        option.id = query.value(0).toInt();
        option.name = query.value(1).toString();
        templates.append(option);
    }

    return templates;
}

QList<OrderComponentData> DatabaseManager::templateComponents(int templateId)
{
    QList<OrderComponentData> components;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT component_name, quantity_per_set "
        "FROM option_template_components "
        "WHERE option_template_id = ? ORDER BY id ASC;"));
    query.addBindValue(templateId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return components;
    }

    while (query.next()) {
        OrderComponentData component;
        component.componentName = query.value(0).toString();
        component.quantityPerSet = query.value(1).toInt();
        component.sourceType = QStringLiteral("template");
        components.append(component);
    }

    return components;
}

bool DatabaseManager::saveOrder(const OrderSaveData &orderData,
                                const QList<OrderComponentData> &components)
{
    const QList<OrderComponentData> normalizedComponents = mergedComponents(components);
    if (normalizedComponents.isEmpty()) {
        m_lastError = QStringLiteral("Order components cannot be empty.");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery orderQuery(database);
    orderQuery.prepare(QStringLiteral(
        "INSERT INTO order_items "
        "(order_date, customer_name, product_model, quantity_sets, unit_price, "
        "configuration_name, shipped_sets, unshipped_sets, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?);"));
    orderQuery.bindValue(0, orderData.orderDate);
    orderQuery.bindValue(1, orderData.customerName);
    orderQuery.bindValue(2, orderData.productModelName);
    orderQuery.bindValue(3, orderData.quantitySets);
    orderQuery.bindValue(4, orderData.unitPrice);
    orderQuery.bindValue(5, orderData.configurationName);
    orderQuery.bindValue(6, orderData.quantitySets);
    orderQuery.bindValue(7, currentTimestamp());

    if (!orderQuery.exec()) {
        m_lastError = orderQuery.lastError().text();
        database.rollback();
        return false;
    }

    const int orderItemId = orderQuery.lastInsertId().toInt();

    QSqlQuery componentQuery(database);
    componentQuery.prepare(QStringLiteral(
        "INSERT INTO order_item_components "
        "(order_item_id, component_name, quantity_per_set, total_required_quantity, "
        "shipped_quantity, unshipped_quantity, source_type) "
        "VALUES (?, ?, ?, ?, 0, ?, ?);"));

    for (const OrderComponentData &component : normalizedComponents) {
        const int totalRequiredQuantity = component.quantityPerSet * orderData.quantitySets;
        componentQuery.bindValue(0, orderItemId);
        componentQuery.bindValue(1, component.componentName);
        componentQuery.bindValue(2, component.quantityPerSet);
        componentQuery.bindValue(3, totalRequiredQuantity);
        componentQuery.bindValue(4, totalRequiredQuantity);
        componentQuery.bindValue(5, component.sourceType);

        if (!componentQuery.exec()) {
            m_lastError = componentQuery.lastError().text();
            database.rollback();
            return false;
        }
    }

    if (!database.commit()) {
        m_lastError = database.lastError().text();
        database.rollback();
        return false;
    }

    return true;
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

QList<OrderComponentData> DatabaseManager::mergedComponents(
    const QList<OrderComponentData> &components) const
{
    QList<OrderComponentData> normalized;
    QHash<QString, int> indexByName;

    for (const OrderComponentData &component : components) {
        const QString normalizedName = component.componentName.trimmed();
        if (normalizedName.isEmpty() || component.quantityPerSet <= 0) {
            continue;
        }

        const QString key = normalizedName.toLower();
        if (indexByName.contains(key)) {
            OrderComponentData &existing = normalized[indexByName.value(key)];
            existing.quantityPerSet += component.quantityPerSet;
            if (existing.sourceType != component.sourceType) {
                existing.sourceType = QStringLiteral("manual");
            }
            continue;
        }

        OrderComponentData mergedComponent = component;
        mergedComponent.componentName = normalizedName;
        if (mergedComponent.sourceType.isEmpty()) {
            mergedComponent.sourceType = QStringLiteral("manual");
        }
        indexByName.insert(key, normalized.size());
        normalized.append(mergedComponent);
    }

    return normalized;
}
