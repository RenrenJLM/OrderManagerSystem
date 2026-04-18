#include "databasemanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QStringList>
#include <QtGlobal>

namespace {
const char kConnectionName[] = "ordermanager_connection";
const char kDatabaseFileName[] = "OrderManagerSystem.db";
const char kBodySourceType[] = "system_body";
constexpr double kDefaultNonZeroUnitPrice = 1.0;

QString currentTimestamp()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString inventoryKey(const QString &componentName,
                     const QString &componentSpec,
                     const QString &material,
                     const QString &color)
{
    return componentName.trimmed() + QLatin1Char('\x1f')
           + componentSpec.trimmed() + QLatin1Char('\x1f')
           + material.trimmed() + QLatin1Char('\x1f')
           + color.trimmed();
}

QString structuredInventoryKey(int productCategoryId,
                               const QString &componentName,
                               const QString &componentSpec,
                               const QString &material,
                               const QString &color)
{
    return QString::number(productCategoryId) + QLatin1Char('\x1f')
           + inventoryKey(componentName, componentSpec, material, color);
}
}

DatabaseManager::DatabaseManager() {}

bool DatabaseManager::initialize()
{
    m_lastError.clear();

    return openDatabase() && enableForeignKeys() && createTables() && repairShipmentData();
}

bool DatabaseManager::ensureMinimumDemoData()
{
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.isValid() || !database.isOpen()) {
        m_lastError = QStringLiteral("Database connection is not available.");
        return false;
    }

    if (!ensureMinimumStructuredDemoData(database)) {
        return false;
    }

    QSqlQuery countQuery(database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM product_models;")) || !countQuery.next()) {
        m_lastError = countQuery.lastError().text();
        return false;
    }

    if (countQuery.value(0).toInt() > 0) {
        return ensureMinimumComponentCatalogData(database);
    }

    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery insertProduct(database);
    insertProduct.prepare(QStringLiteral(
        "INSERT INTO product_models (name, default_price, created_at) VALUES (?, ?, ?);"));

    insertProduct.bindValue(0, QStringLiteral("OMS-标准灯箱"));
    insertProduct.bindValue(1, 1200.0);
    insertProduct.bindValue(2, currentTimestamp());
    if (!insertProduct.exec()) {
        m_lastError = insertProduct.lastError().text();
        database.rollback();
        return false;
    }
    const int lightBoxModelId = insertProduct.lastInsertId().toInt();

    insertProduct.bindValue(0, QStringLiteral("OMS-展示架"));
    insertProduct.bindValue(1, 800.0);
    insertProduct.bindValue(2, currentTimestamp());
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
        "(option_template_id, component_name, quantity_per_set, unit_price) VALUES (?, ?, ?, ?);"));

    const QList<OrderComponentData> standardAComponents = {
        {QStringLiteral("光源"), 2, 90.0, QString()},
        {QStringLiteral("灯罩"), 4, 25.0, QString()},
        {QStringLiteral("电源"), 1, 180.0, QString()}
    };
    const QList<OrderComponentData> standardBComponents = {
        {QStringLiteral("光源"), 3, 90.0, QString()},
        {QStringLiteral("灯罩"), 6, 25.0, QString()},
        {QStringLiteral("白色机头"), 1, 220.0, QString()}
    };
    const QList<OrderComponentData> standComponents = {
        {QStringLiteral("主架"), 1, 160.0, QString()},
        {QStringLiteral("连接件"), 6, 18.0, QString()},
        {QStringLiteral("底座"), 2, 45.0, QString()}
    };

    const auto insertComponents = [&](int templateId, const QList<OrderComponentData> &components) {
        for (const OrderComponentData &component : components) {
            insertComponent.bindValue(0, templateId);
            insertComponent.bindValue(1, component.componentName);
            insertComponent.bindValue(2, component.quantityPerSet);
            insertComponent.bindValue(3, component.unitPrice);
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

    if (!ensureMinimumComponentCatalogData(database)) {
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
        "SELECT id, name, default_price FROM product_models ORDER BY name ASC;"));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return models;
    }

    while (query.next()) {
        ProductModelOption model;
        model.id = query.value(0).toInt();
        model.name = query.value(1).toString();
        model.defaultPrice = query.value(2).toDouble();
        models.append(model);
    }

    return models;
}

QList<ProductComponentOption> DatabaseManager::productModelComponents(int productModelId)
{
    QList<ProductComponentOption> components;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, component_name, unit_price "
        "FROM product_model_components "
        "WHERE product_model_id = ? "
        "ORDER BY component_name ASC;"));
    query.addBindValue(productModelId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return components;
    }

    while (query.next()) {
        ProductComponentOption component;
        component.id = query.value(0).toInt();
        component.name = query.value(1).toString();
        component.unitPrice = query.value(2).toDouble();
        components.append(component);
    }

    return components;
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
        "SELECT component_name, quantity_per_set, unit_price "
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
        component.unitPrice = query.value(2).toDouble();
        component.sourceType = QStringLiteral("template");
        components.append(component);
    }

    return components;
}

bool DatabaseManager::saveOrder(const OrderSaveData &orderData,
                                const QList<OrderComponentData> &components)
{
    QList<OrderComponentData> normalizedComponents = mergedComponents(components);
    if (normalizedComponents.isEmpty()) {
        m_lastError = QStringLiteral("Order components cannot be empty.");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (orderData.bodyUnitPrice < 0.0) {
        m_lastError = QStringLiteral("主体单价不能小于 0。");
        return false;
    }

    OrderComponentData bodyComponent;
    bodyComponent.componentName = bodyComponentName(orderData.productModelName);
    bodyComponent.quantityPerSet = 1;
    bodyComponent.unitPrice = orderData.bodyUnitPrice;
    bodyComponent.sourceType = QString::fromLatin1(kBodySourceType);
    normalizedComponents.prepend(bodyComponent);

    double orderUnitPrice = 0.0;
    for (const OrderComponentData &component : normalizedComponents) {
        orderUnitPrice += static_cast<double>(component.quantityPerSet) * component.unitPrice;
    }

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
    orderQuery.bindValue(4, orderUnitPrice);
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
        "shipped_quantity, unshipped_quantity, unit_price, total_price, source_type) "
        "VALUES (?, ?, ?, ?, 0, ?, ?, ?, ?);"));

    for (const OrderComponentData &component : normalizedComponents) {
        const int totalRequiredQuantity = component.quantityPerSet * orderData.quantitySets;
        const double totalPrice = static_cast<double>(totalRequiredQuantity) * component.unitPrice;
        componentQuery.bindValue(0, orderItemId);
        componentQuery.bindValue(1, component.componentName);
        componentQuery.bindValue(2, component.quantityPerSet);
        componentQuery.bindValue(3, totalRequiredQuantity);
        componentQuery.bindValue(4, totalRequiredQuantity);
        componentQuery.bindValue(5, component.unitPrice);
        componentQuery.bindValue(6, totalPrice);
        componentQuery.bindValue(7, component.sourceType);

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

QList<ShipmentOrderSummary> DatabaseManager::queryOrders(const QString &customerKeyword,
                                                         const QString &productModelName,
                                                         bool onlyUnfinished)
{
    QList<ShipmentOrderSummary> orders;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));

    QString statement = QStringLiteral(
        "SELECT oi.id, oi.order_date, oi.customer_name, oi.product_model, "
        "oi.configuration_name, oi.quantity_sets, oi.shipped_sets, oi.unshipped_sets, "
        "oi.unit_price, "
        "CASE "
        "WHEN COALESCE((SELECT MAX(unshipped_quantity) "
        "               FROM order_item_components oic "
        "               WHERE oic.order_item_id = oi.id), 0) = 0 "
        "THEN 1 ELSE 0 END AS is_completed "
        "FROM order_items oi");

    QStringList conditions;
    QList<QVariant> bindValues;

    if (!customerKeyword.trimmed().isEmpty()) {
        conditions.append(QStringLiteral("oi.customer_name LIKE ?"));
        bindValues.append(QStringLiteral("%") + customerKeyword.trimmed() + QStringLiteral("%"));
    }

    if (!productModelName.trimmed().isEmpty()) {
        conditions.append(QStringLiteral("oi.product_model = ?"));
        bindValues.append(productModelName.trimmed());
    }

    if (onlyUnfinished) {
        conditions.append(QStringLiteral(
            "COALESCE((SELECT MAX(unshipped_quantity) "
            "          FROM order_item_components oic "
            "          WHERE oic.order_item_id = oi.id), 0) > 0"));
    }

    if (!conditions.isEmpty()) {
        statement += QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
    }
    statement += QStringLiteral(" ORDER BY oi.id DESC;");

    query.prepare(statement);
    for (const QVariant &value : bindValues) {
        query.addBindValue(value);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return orders;
    }

    while (query.next()) {
        ShipmentOrderSummary order;
        order.id = query.value(0).toInt();
        order.orderDate = query.value(1).toString();
        order.customerName = query.value(2).toString();
        order.productModelName = query.value(3).toString();
        order.configurationName = query.value(4).toString();
        order.quantitySets = query.value(5).toInt();
        order.shippedSets = query.value(6).toInt();
        order.unshippedSets = query.value(7).toInt();
        order.unitPrice = query.value(8).toDouble();
        order.isCompleted = query.value(9).toInt() == 1;
        order.totalPrice = static_cast<double>(order.quantitySets) * order.unitPrice;
        orders.append(order);
    }

    return orders;
}

QList<ShipmentComponentStatus> DatabaseManager::orderComponents(int orderItemId)
{
    return shipmentComponents(orderItemId);
}

QList<OrderShipmentRecord> DatabaseManager::orderShipments(int orderItemId)
{
    QList<OrderShipmentRecord> records;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT shipment_date, "
        "CASE WHEN order_item_component_id IS NULL THEN '订单级' ELSE '组件级' END, "
        "shipment_quantity, COALESCE(note, '') "
        "FROM shipment_records "
        "WHERE order_item_id = ? "
        "ORDER BY id ASC;"));
    query.addBindValue(orderItemId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return records;
    }

    while (query.next()) {
        OrderShipmentRecord record;
        record.shipmentDate = query.value(0).toString();
        record.shipmentType = query.value(1).toString();
        record.shipmentQuantity = query.value(2).toInt();
        record.note = query.value(3).toString();
        records.append(record);
    }

    return records;
}

QList<ShipmentOrderSummary> DatabaseManager::shipmentOrders()
{
    QList<ShipmentOrderSummary> orders;
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id, order_date, customer_name, product_model, configuration_name, "
        "quantity_sets, shipped_sets, unshipped_sets, unit_price "
        "FROM order_items ORDER BY id DESC;"));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return orders;
    }

    while (query.next()) {
        ShipmentOrderSummary order;
        order.id = query.value(0).toInt();
        order.orderDate = query.value(1).toString();
        order.customerName = query.value(2).toString();
        order.productModelName = query.value(3).toString();
        order.configurationName = query.value(4).toString();
        order.quantitySets = query.value(5).toInt();
        order.shippedSets = query.value(6).toInt();
        order.unshippedSets = query.value(7).toInt();
        order.unitPrice = query.value(8).toDouble();
        order.totalPrice = static_cast<double>(order.quantitySets) * order.unitPrice;
        QString errorMessage;
        order.availableSetShipments = availableSetShipmentsForOrder(database, order.id, &errorMessage);
        if (!errorMessage.isEmpty()) {
            m_lastError = errorMessage;
            return {};
        }
        QSqlQuery statusQuery(database);
        statusQuery.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(unshipped_quantity), 0) "
            "FROM order_item_components WHERE order_item_id = ?;"));
        statusQuery.addBindValue(order.id);
        if (!statusQuery.exec() || !statusQuery.next()) {
            m_lastError = statusQuery.lastError().text();
            return {};
        }
        order.isCompleted = statusQuery.value(0).toInt() == 0;
        orders.append(order);
    }

    return orders;
}

QList<ShipmentComponentStatus> DatabaseManager::shipmentComponents(int orderItemId)
{
    QList<ShipmentComponentStatus> components;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, order_item_id, component_name, quantity_per_set, total_required_quantity, "
        "shipped_quantity, unshipped_quantity, unit_price, total_price, source_type "
        "FROM order_item_components WHERE order_item_id = ? ORDER BY id ASC;"));
    query.addBindValue(orderItemId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return components;
    }

    while (query.next()) {
        ShipmentComponentStatus component;
        component.id = query.value(0).toInt();
        component.orderItemId = query.value(1).toInt();
        component.componentName = query.value(2).toString();
        component.quantityPerSet = query.value(3).toInt();
        component.totalRequiredQuantity = query.value(4).toInt();
        component.shippedQuantity = query.value(5).toInt();
        component.unshippedQuantity = query.value(6).toInt();
        component.unitPrice = query.value(7).toDouble();
        component.totalPrice = query.value(8).toDouble();
        component.sourceType = query.value(9).toString();
        component.isBodyComponent = component.sourceType == QString::fromLatin1(kBodySourceType);
        components.append(component);
    }

    return components;
}

bool DatabaseManager::saveOrderShipment(int orderItemId,
                                        const QString &shipmentDate,
                                        int shipmentSets,
                                        const QString &note)
{
    if (orderItemId <= 0) {
        m_lastError = QStringLiteral("请选择有效订单。");
        return false;
    }

    if (shipmentSets <= 0) {
        m_lastError = QStringLiteral("订单发货套数必须大于 0。");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    QString availableSetError;
    const int availableSetShipments =
        availableSetShipmentsForOrder(database, orderItemId, &availableSetError);
    if (!availableSetError.isEmpty()) {
        m_lastError = availableSetError;
        return false;
    }
    if (shipmentSets > availableSetShipments) {
        m_lastError = QStringLiteral("发货套数超过当前订单可整套发货数量。");
        return false;
    }

    struct ComponentUpdate
    {
        int id = 0;
        int quantityPerSet = 0;
    };

    QList<ComponentUpdate> components;
    QSqlQuery componentQuery(database);
    componentQuery.prepare(QStringLiteral(
        "SELECT id, quantity_per_set "
        "FROM order_item_components WHERE order_item_id = ? ORDER BY id ASC;"));
    componentQuery.addBindValue(orderItemId);

    if (!componentQuery.exec()) {
        m_lastError = componentQuery.lastError().text();
        return false;
    }

    while (componentQuery.next()) {
        ComponentUpdate component;
        component.id = componentQuery.value(0).toInt();
        component.quantityPerSet = componentQuery.value(1).toInt();
        components.append(component);
    }

    if (components.isEmpty()) {
        m_lastError = QStringLiteral("当前订单没有可发货组件。");
        return false;
    }

    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery insertShipment(database);
    insertShipment.prepare(QStringLiteral(
        "INSERT INTO shipment_records "
        "(order_item_id, order_item_component_id, shipment_date, shipment_quantity, note, created_at) "
        "VALUES (?, NULL, ?, ?, ?, ?);"));
    insertShipment.bindValue(0, orderItemId);
    insertShipment.bindValue(1, shipmentDate);
    insertShipment.bindValue(2, shipmentSets);
    insertShipment.bindValue(3, note.trimmed());
    insertShipment.bindValue(4, currentTimestamp());

    if (!insertShipment.exec()) {
        m_lastError = insertShipment.lastError().text();
        database.rollback();
        return false;
    }

    QSqlQuery updateComponent(database);
    updateComponent.prepare(QStringLiteral(
        "UPDATE order_item_components "
        "SET shipped_quantity = shipped_quantity + ?, "
        "unshipped_quantity = unshipped_quantity - ? "
        "WHERE id = ?;"));

    for (const ComponentUpdate &component : components) {
        const int delta = shipmentSets * component.quantityPerSet;
        updateComponent.bindValue(0, delta);
        updateComponent.bindValue(1, delta);
        updateComponent.bindValue(2, component.id);
        if (!updateComponent.exec()) {
            m_lastError = updateComponent.lastError().text();
            database.rollback();
            return false;
        }
    }

    QSqlQuery updateOrder(database);
    updateOrder.prepare(QStringLiteral(
        "UPDATE order_items "
        "SET shipped_sets = shipped_sets + ?, unshipped_sets = unshipped_sets - ? "
        "WHERE id = ? AND unshipped_sets >= ?;"));
    updateOrder.bindValue(0, shipmentSets);
    updateOrder.bindValue(1, shipmentSets);
    updateOrder.bindValue(2, orderItemId);
    updateOrder.bindValue(3, shipmentSets);

    if (!updateOrder.exec() || updateOrder.numRowsAffected() != 1) {
        m_lastError = updateOrder.lastError().text();
        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("订单机体未发数量不足。");
        }
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

bool DatabaseManager::saveComponentShipment(int orderItemId,
                                            int componentId,
                                            const QString &shipmentDate,
                                            int shipmentQuantity,
                                            const QString &note)
{
    if (orderItemId <= 0 || componentId <= 0) {
        m_lastError = QStringLiteral("请选择有效组件。");
        return false;
    }

    if (shipmentQuantity <= 0) {
        m_lastError = QStringLiteral("组件发货数量必须大于 0。");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    QSqlQuery componentQuery(database);
    componentQuery.prepare(QStringLiteral(
        "SELECT unshipped_quantity, source_type FROM order_item_components "
        "WHERE id = ? AND order_item_id = ?;"));
    componentQuery.addBindValue(componentId);
    componentQuery.addBindValue(orderItemId);

    if (!componentQuery.exec() || !componentQuery.next()) {
        m_lastError = componentQuery.lastError().isValid()
                          ? componentQuery.lastError().text()
                          : QStringLiteral("未找到对应订单组件。");
        return false;
    }

    const int unshippedQuantity = componentQuery.value(0).toInt();
    const QString sourceType = componentQuery.value(1).toString();
    const bool isBodyComponent = sourceType == QString::fromLatin1(kBodySourceType);
    if (shipmentQuantity > unshippedQuantity) {
        m_lastError = QStringLiteral("组件发货数量超过未发数量。");
        return false;
    }

    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery insertShipment(database);
    insertShipment.prepare(QStringLiteral(
        "INSERT INTO shipment_records "
        "(order_item_id, order_item_component_id, shipment_date, shipment_quantity, note, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?);"));
    insertShipment.bindValue(0, orderItemId);
    insertShipment.bindValue(1, componentId);
    insertShipment.bindValue(2, shipmentDate);
    insertShipment.bindValue(3, shipmentQuantity);
    insertShipment.bindValue(4, note.trimmed());
    insertShipment.bindValue(5, currentTimestamp());

    if (!insertShipment.exec()) {
        m_lastError = insertShipment.lastError().text();
        database.rollback();
        return false;
    }

    QSqlQuery updateComponent(database);
    updateComponent.prepare(QStringLiteral(
        "UPDATE order_item_components "
        "SET shipped_quantity = shipped_quantity + ?, "
        "unshipped_quantity = unshipped_quantity - ? "
        "WHERE id = ? AND order_item_id = ?;"));
    updateComponent.bindValue(0, shipmentQuantity);
    updateComponent.bindValue(1, shipmentQuantity);
    updateComponent.bindValue(2, componentId);
    updateComponent.bindValue(3, orderItemId);

    if (!updateComponent.exec()) {
        m_lastError = updateComponent.lastError().text();
        database.rollback();
        return false;
    }

    if (isBodyComponent) {
        QSqlQuery updateOrder(database);
        updateOrder.prepare(QStringLiteral(
            "UPDATE order_items "
            "SET shipped_sets = shipped_sets + ?, unshipped_sets = unshipped_sets - ? "
            "WHERE id = ? AND unshipped_sets >= ?;"));
        updateOrder.bindValue(0, shipmentQuantity);
        updateOrder.bindValue(1, shipmentQuantity);
        updateOrder.bindValue(2, orderItemId);
        updateOrder.bindValue(3, shipmentQuantity);

        if (!updateOrder.exec() || updateOrder.numRowsAffected() != 1) {
            m_lastError = updateOrder.lastError().text();
            if (m_lastError.isEmpty()) {
                m_lastError = QStringLiteral("订单机体未发数量不足。");
            }
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

QList<ProductCategoryOption> DatabaseManager::productCategories()
{
    QList<ProductCategoryOption> categories;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, name, is_active FROM product_categories ORDER BY name ASC;"));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return categories;
    }

    while (query.next()) {
        ProductCategoryOption category;
        category.id = query.value(0).toInt();
        category.name = query.value(1).toString();
        category.isActive = query.value(2).toInt() == 1;
        categories.append(category);
    }

    return categories;
}

bool DatabaseManager::saveProductCategory(const ProductCategoryOption &category)
{
    if (category.name.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("产品类型名称不能为空。");
        return false;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    const QString timestamp = currentTimestamp();
    if (category.id > 0) {
        query.prepare(QStringLiteral(
            "UPDATE product_categories "
            "SET name = ?, is_active = ?, updated_at = ? "
            "WHERE id = ?;"));
        query.bindValue(0, category.name.trimmed());
        query.bindValue(1, category.isActive ? 1 : 0);
        query.bindValue(2, timestamp);
        query.bindValue(3, category.id);
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO product_categories (name, is_active, created_at, updated_at) "
            "VALUES (?, ?, ?, ?);"));
        query.bindValue(0, category.name.trimmed());
        query.bindValue(1, category.isActive ? 1 : 0);
        query.bindValue(2, timestamp);
        query.bindValue(3, timestamp);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    return true;
}

int DatabaseManager::productCategoryIdByName(const QString &categoryName)
{
    const QString normalizedName = categoryName.trimmed();
    if (normalizedName.isEmpty()) {
        return 0;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral("SELECT id FROM product_categories WHERE name = ? LIMIT 1;"));
    query.addBindValue(normalizedName);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return 0;
    }

    return query.next() ? query.value(0).toInt() : 0;
}

bool DatabaseManager::upsertProductCategoryByName(const QString &categoryName, bool isActive)
{
    ProductCategoryOption category;
    category.id = productCategoryIdByName(categoryName);
    category.name = categoryName.trimmed();
    category.isActive = isActive;
    if (category.name.isEmpty()) {
        m_lastError = QStringLiteral("产品类型名称不能为空。");
        return false;
    }

    return saveProductCategory(category);
}

QList<ProductSkuOption> DatabaseManager::productSkus(int productCategoryId)
{
    QList<ProductSkuOption> skus;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    QString statement = QStringLiteral(
        "SELECT ps.id, ps.product_category_id, pc.name, ps.sku_name, "
        "ps.lampshade_name, ps.lampshade_unit_price, ps.is_active "
        "FROM product_skus ps "
        "JOIN product_categories pc ON pc.id = ps.product_category_id");

    if (productCategoryId > 0) {
        statement += QStringLiteral(" WHERE ps.product_category_id = ?");
    }
    statement += QStringLiteral(" ORDER BY ps.sku_name ASC;");

    query.prepare(statement);
    if (productCategoryId > 0) {
        query.addBindValue(productCategoryId);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return skus;
    }

    while (query.next()) {
        ProductSkuOption sku;
        sku.id = query.value(0).toInt();
        sku.productCategoryId = query.value(1).toInt();
        sku.productCategoryName = query.value(2).toString();
        sku.skuName = query.value(3).toString();
        sku.lampshadeName = query.value(4).toString();
        sku.lampshadeUnitPrice = query.value(5).toDouble();
        sku.isActive = query.value(6).toInt() == 1;
        skus.append(sku);
    }

    return skus;
}

bool DatabaseManager::saveProductSku(const ProductSkuOption &sku)
{
    if (sku.productCategoryId <= 0) {
        m_lastError = QStringLiteral("请先选择所属产品类型。");
        return false;
    }
    if (sku.skuName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("具体型号名称不能为空。");
        return false;
    }
    if (sku.lampshadeName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("默认灯罩名称不能为空。");
        return false;
    }
    if (sku.lampshadeUnitPrice < 0.0) {
        m_lastError = QStringLiteral("灯罩单价不能小于 0。");
        return false;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    const QString timestamp = currentTimestamp();
    if (sku.id > 0) {
        query.prepare(QStringLiteral(
            "UPDATE product_skus "
            "SET product_category_id = ?, sku_name = ?, lampshade_name = ?, "
            "lampshade_unit_price = ?, is_active = ?, updated_at = ? "
            "WHERE id = ?;"));
        query.bindValue(0, sku.productCategoryId);
        query.bindValue(1, sku.skuName.trimmed());
        query.bindValue(2, sku.lampshadeName.trimmed());
        query.bindValue(3, sku.lampshadeUnitPrice);
        query.bindValue(4, sku.isActive ? 1 : 0);
        query.bindValue(5, timestamp);
        query.bindValue(6, sku.id);
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO product_skus "
            "(product_category_id, sku_name, lampshade_name, lampshade_unit_price, "
            "is_active, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);"));
        query.bindValue(0, sku.productCategoryId);
        query.bindValue(1, sku.skuName.trimmed());
        query.bindValue(2, sku.lampshadeName.trimmed());
        query.bindValue(3, sku.lampshadeUnitPrice);
        query.bindValue(4, sku.isActive ? 1 : 0);
        query.bindValue(5, timestamp);
        query.bindValue(6, timestamp);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::upsertProductSkuByNaturalKey(const ProductSkuOption &sku)
{
    if (sku.productCategoryId <= 0) {
        m_lastError = QStringLiteral("请先选择所属产品类型。");
        return false;
    }

    ProductSkuOption normalizedSku = sku;
    normalizedSku.skuName = sku.skuName.trimmed();

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id FROM product_skus WHERE product_category_id = ? AND sku_name = ? LIMIT 1;"));
    query.addBindValue(normalizedSku.productCategoryId);
    query.addBindValue(normalizedSku.skuName);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    if (query.next()) {
        normalizedSku.id = query.value(0).toInt();
    }

    return saveProductSku(normalizedSku);
}

QList<BaseConfigurationOption> DatabaseManager::baseConfigurationsForCategory(int productCategoryId)
{
    QList<BaseConfigurationOption> configurations;
    if (productCategoryId <= 0) {
        return configurations;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, product_category_id, config_code, config_name, config_price, sort_order, is_active "
        "FROM base_configurations "
        "WHERE product_category_id = ? "
        "ORDER BY sort_order ASC, id ASC;"));
    query.addBindValue(productCategoryId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return configurations;
    }

    while (query.next()) {
        BaseConfigurationOption configuration;
        configuration.id = query.value(0).toInt();
        configuration.productCategoryId = query.value(1).toInt();
        configuration.configCode = query.value(2).toString();
        configuration.configName = query.value(3).toString();
        configuration.configPrice = query.value(4).toDouble();
        configuration.sortOrder = query.value(5).toInt();
        configuration.isActive = query.value(6).toInt() == 1;
        configurations.append(configuration);
    }

    return configurations;
}

bool DatabaseManager::saveBaseConfiguration(const BaseConfigurationOption &configuration)
{
    if (configuration.productCategoryId <= 0) {
        m_lastError = QStringLiteral("请先选择所属产品类型。");
        return false;
    }
    if (configuration.configCode.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("基础配置代码不能为空。");
        return false;
    }
    if (configuration.configName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("基础配置名称不能为空。");
        return false;
    }
    if (configuration.configPrice < 0.0) {
        m_lastError = QStringLiteral("配置价格不能小于 0。");
        return false;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    const QString timestamp = currentTimestamp();
    if (configuration.id > 0) {
        query.prepare(QStringLiteral(
            "UPDATE base_configurations "
            "SET product_category_id = ?, config_code = ?, config_name = ?, "
            "config_price = ?, is_active = ?, sort_order = ?, updated_at = ? "
            "WHERE id = ?;"));
        query.bindValue(0, configuration.productCategoryId);
        query.bindValue(1, configuration.configCode.trimmed());
        query.bindValue(2, configuration.configName.trimmed());
        query.bindValue(3, configuration.configPrice);
        query.bindValue(4, configuration.isActive ? 1 : 0);
        query.bindValue(5, configuration.sortOrder > 0 ? configuration.sortOrder : configuration.id);
        query.bindValue(6, timestamp);
        query.bindValue(7, configuration.id);
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO base_configurations "
            "(product_category_id, config_code, config_name, config_price, "
            "is_active, sort_order, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);"));
        query.bindValue(0, configuration.productCategoryId);
        query.bindValue(1, configuration.configCode.trimmed());
        query.bindValue(2, configuration.configName.trimmed());
        query.bindValue(3, configuration.configPrice);
        query.bindValue(4, configuration.isActive ? 1 : 0);
        query.bindValue(5, configuration.sortOrder > 0 ? configuration.sortOrder : 9999);
        query.bindValue(6, timestamp);
        query.bindValue(7, timestamp);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    return true;
}

int DatabaseManager::baseConfigurationIdByCategoryAndCode(int productCategoryId, const QString &configCode)
{
    if (productCategoryId <= 0 || configCode.trimmed().isEmpty()) {
        return 0;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id FROM base_configurations "
        "WHERE product_category_id = ? AND config_code = ? "
        "LIMIT 1;"));
    query.addBindValue(productCategoryId);
    query.addBindValue(configCode.trimmed());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return 0;
    }

    return query.next() ? query.value(0).toInt() : 0;
}

bool DatabaseManager::upsertBaseConfigurationByNaturalKey(
    const BaseConfigurationOption &configuration)
{
    if (configuration.productCategoryId <= 0) {
        m_lastError = QStringLiteral("请先选择所属产品类型。");
        return false;
    }

    BaseConfigurationOption normalizedConfiguration = configuration;
    normalizedConfiguration.configCode = configuration.configCode.trimmed();
    normalizedConfiguration.id = baseConfigurationIdByCategoryAndCode(
        configuration.productCategoryId, normalizedConfiguration.configCode);
    return saveBaseConfiguration(normalizedConfiguration);
}

QList<BaseConfigurationComponentData> DatabaseManager::baseConfigurationComponents(int baseConfigurationId)
{
    QList<BaseConfigurationComponentData> components;
    if (baseConfigurationId <= 0) {
        return components;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, base_configuration_id, component_name, component_spec, material, color, "
        "unit_name, quantity, unit_amount, line_amount, sort_order, is_active "
        "FROM base_configuration_components "
        "WHERE base_configuration_id = ? "
        "ORDER BY sort_order ASC, id ASC;"));
    query.addBindValue(baseConfigurationId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return components;
    }

    while (query.next()) {
        BaseConfigurationComponentData component;
        component.id = query.value(0).toInt();
        component.baseConfigurationId = query.value(1).toInt();
        component.componentName = query.value(2).toString();
        component.componentSpec = query.value(3).toString();
        component.material = query.value(4).toString();
        component.color = query.value(5).toString();
        component.unitName = query.value(6).toString();
        component.quantity = query.value(7).toInt();
        component.unitAmount = query.value(8).toDouble();
        component.lineAmount = query.value(9).toDouble();
        component.sortOrder = query.value(10).toInt();
        component.isActive = query.value(11).toInt() == 1;
        components.append(component);
    }

    return components;
}

bool DatabaseManager::replaceBaseConfigurationComponents(
    int baseConfigurationId, const QList<BaseConfigurationComponentData> &components)
{
    if (baseConfigurationId <= 0) {
        m_lastError = QStringLiteral("请选择有效的基础配置。");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery deleteQuery(database);
    deleteQuery.prepare(QStringLiteral(
        "DELETE FROM base_configuration_components WHERE base_configuration_id = ?;"));
    deleteQuery.addBindValue(baseConfigurationId);
    if (!deleteQuery.exec()) {
        m_lastError = deleteQuery.lastError().text();
        database.rollback();
        return false;
    }

    const QString timestamp = currentTimestamp();
    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO base_configuration_components "
        "(base_configuration_id, component_name, component_spec, material, color, unit_name, "
        "quantity, unit_amount, line_amount, sort_order, is_active, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?);"));

    int sortOrder = 1;
    for (const BaseConfigurationComponentData &component : components) {
        if (component.componentName.trimmed().isEmpty() || component.quantity <= 0) {
            continue;
        }

        const QString unitName =
            component.unitName.trimmed().isEmpty() ? QStringLiteral("件") : component.unitName.trimmed();
        const double unitAmount = component.unitAmount;
        const double lineAmount = static_cast<double>(component.quantity) * unitAmount;

        insertQuery.bindValue(0, baseConfigurationId);
        insertQuery.bindValue(1, component.componentName.trimmed());
        insertQuery.bindValue(2, component.componentSpec.trimmed());
        insertQuery.bindValue(3, component.material.trimmed());
        insertQuery.bindValue(4, component.color.trimmed());
        insertQuery.bindValue(5, unitName);
        insertQuery.bindValue(6, component.quantity);
        insertQuery.bindValue(7, unitAmount);
        insertQuery.bindValue(8, lineAmount);
        insertQuery.bindValue(9, component.sortOrder > 0 ? component.sortOrder : sortOrder);
        insertQuery.bindValue(10, timestamp);
        insertQuery.bindValue(11, timestamp);

        if (!insertQuery.exec()) {
            m_lastError = insertQuery.lastError().text();
            database.rollback();
            return false;
        }

        ++sortOrder;
    }

    if (!database.commit()) {
        m_lastError = database.lastError().text();
        database.rollback();
        return false;
    }

    return true;
}

bool DatabaseManager::saveStructuredOrder(const StructuredOrderSaveData &orderData,
                                          const QList<StructuredOrderComponentData> &components)
{
    if (orderData.customerName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("客户名称不能为空。");
        return false;
    }
    if (orderData.productCategoryId <= 0 || orderData.productSkuId <= 0
        || orderData.baseConfigurationId <= 0) {
        m_lastError = QStringLiteral("请先选择完整的产品、型号和基础配置。");
        return false;
    }
    if (orderData.orderQuantity <= 0) {
        m_lastError = QStringLiteral("订单数量必须大于 0。");
        return false;
    }
    if (components.isEmpty()) {
        m_lastError = QStringLiteral("订单组件不能为空。");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    const QString timestamp = currentTimestamp();
    QSqlQuery orderQuery(database);
    orderQuery.prepare(QStringLiteral(
        "INSERT INTO orders "
        "(order_date, customer_name, product_category_id, product_category_name, "
        "product_sku_id, product_sku_name, base_configuration_id, base_configuration_name, "
        "order_quantity, lampshade_name, lampshade_unit_price, config_price, status, remark, "
        "created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'open', ?, ?, ?);"));
    orderQuery.bindValue(0, orderData.orderDate);
    orderQuery.bindValue(1, orderData.customerName.trimmed());
    orderQuery.bindValue(2, orderData.productCategoryId);
    orderQuery.bindValue(3, orderData.productCategoryName.trimmed());
    orderQuery.bindValue(4, orderData.productSkuId);
    orderQuery.bindValue(5, orderData.productSkuName.trimmed());
    orderQuery.bindValue(6, orderData.baseConfigurationId);
    orderQuery.bindValue(7, orderData.baseConfigurationName.trimmed());
    orderQuery.bindValue(8, orderData.orderQuantity);
    orderQuery.bindValue(9, orderData.lampshadeName.trimmed());
    orderQuery.bindValue(10, orderData.lampshadeUnitPrice);
    orderQuery.bindValue(11, orderData.configPrice);
    orderQuery.bindValue(12, orderData.remark.trimmed());
    orderQuery.bindValue(13, timestamp);
    orderQuery.bindValue(14, timestamp);

    if (!orderQuery.exec()) {
        m_lastError = orderQuery.lastError().text();
        database.rollback();
        return false;
    }

    const int orderId = orderQuery.lastInsertId().toInt();
    QSqlQuery componentQuery(database);
    componentQuery.prepare(QStringLiteral(
        "INSERT INTO order_components "
        "(order_id, source_component_id, component_name, component_spec, material, color, "
        "unit_name, quantity_per_set, required_quantity, shipped_quantity, unshipped_quantity, "
        "unit_amount, line_amount, source_type, adjustment_type, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?, ?);"));

    for (const StructuredOrderComponentData &component : components) {
        if (component.componentName.trimmed().isEmpty() || component.quantityPerSet <= 0) {
            m_lastError = QStringLiteral("订单组件名称不能为空且数量必须大于 0。");
            database.rollback();
            return false;
        }

        const int requiredQuantity = component.quantityPerSet * orderData.orderQuantity;
        const double lineAmount =
            static_cast<double>(requiredQuantity) * component.unitAmount;

        componentQuery.bindValue(0, orderId);
        componentQuery.bindValue(1, component.sourceComponentId > 0 ? QVariant(component.sourceComponentId)
                                                                   : QVariant());
        componentQuery.bindValue(2, component.componentName.trimmed());
        componentQuery.bindValue(3, component.componentSpec.trimmed());
        componentQuery.bindValue(4, component.material.trimmed());
        componentQuery.bindValue(5, component.color.trimmed());
        componentQuery.bindValue(6, component.unitName.trimmed().isEmpty()
                                        ? QStringLiteral("件")
                                        : component.unitName.trimmed());
        componentQuery.bindValue(7, component.quantityPerSet);
        componentQuery.bindValue(8, requiredQuantity);
        componentQuery.bindValue(9, requiredQuantity);
        componentQuery.bindValue(10, component.unitAmount);
        componentQuery.bindValue(11, lineAmount);
        componentQuery.bindValue(12, component.sourceType.trimmed().isEmpty()
                                         ? QStringLiteral("base_bom")
                                         : component.sourceType.trimmed());
        componentQuery.bindValue(13, component.adjustmentType.trimmed().isEmpty()
                                         ? QStringLiteral("none")
                                         : component.adjustmentType.trimmed());
        componentQuery.bindValue(14, timestamp);
        componentQuery.bindValue(15, timestamp);

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

QList<StructuredOrderSummary> DatabaseManager::structuredOrders(bool onlyOpen)
{
    QList<StructuredOrderSummary> orders;
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    QSqlQuery query(database);
    QString statement = QStringLiteral(
        "SELECT id, order_date, customer_name, product_category_name, product_sku_name, "
        "base_configuration_name, order_quantity, lampshade_name, lampshade_unit_price, "
        "config_price, status "
        "FROM orders");

    if (onlyOpen) {
        statement += QStringLiteral(" WHERE status != 'completed'");
    }
    statement += QStringLiteral(" ORDER BY id DESC;");

    query.prepare(statement);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return orders;
    }

    while (query.next()) {
        StructuredOrderSummary order;
        order.id = query.value(0).toInt();
        order.orderDate = query.value(1).toString();
        order.customerName = query.value(2).toString();
        order.productCategoryName = query.value(3).toString();
        order.productSkuName = query.value(4).toString();
        order.baseConfigurationName = query.value(5).toString();
        order.orderQuantity = query.value(6).toInt();
        order.lampshadeName = query.value(7).toString();
        order.lampshadeUnitPrice = query.value(8).toDouble();
        order.configPrice = query.value(9).toDouble();
        order.status = query.value(10).toString();
        QString errorMessage;
        order.availableSetShipments =
            availableSetShipmentsForStructuredOrder(database, order.id, &errorMessage);
        if (!errorMessage.isEmpty()) {
            m_lastError = errorMessage;
            return {};
        }
        order.isCompleted = order.status == QStringLiteral("completed");
        order.shipmentReady = isStructuredOrderShipmentReady(order.id);
        orders.append(order);
    }

    return orders;
}

QList<StructuredOrderComponentSnapshot> DatabaseManager::structuredOrderComponents(int orderId)
{
    QList<StructuredOrderComponentSnapshot> components;
    if (orderId <= 0) {
        return components;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT id, order_id, COALESCE(source_component_id, 0), component_name, "
        "COALESCE(component_spec, ''), COALESCE(material, ''), COALESCE(color, ''), "
        "COALESCE(unit_name, '件'), quantity_per_set, required_quantity, shipped_quantity, "
        "unshipped_quantity, unit_amount, line_amount, source_type, adjustment_type "
        "FROM order_components "
        "WHERE order_id = ? "
        "ORDER BY id ASC;"));
    query.addBindValue(orderId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return components;
    }

    while (query.next()) {
        StructuredOrderComponentSnapshot component;
        component.id = query.value(0).toInt();
        component.orderId = query.value(1).toInt();
        component.sourceComponentId = query.value(2).toInt();
        component.componentName = query.value(3).toString();
        component.componentSpec = query.value(4).toString();
        component.material = query.value(5).toString();
        component.color = query.value(6).toString();
        component.unitName = query.value(7).toString();
        component.quantityPerSet = query.value(8).toInt();
        component.requiredQuantity = query.value(9).toInt();
        component.shippedQuantity = query.value(10).toInt();
        component.unshippedQuantity = query.value(11).toInt();
        component.unitAmount = query.value(12).toDouble();
        component.lineAmount = query.value(13).toDouble();
        component.sourceType = query.value(14).toString();
        component.adjustmentType = query.value(15).toString();
        components.append(component);
    }

    return components;
}

QList<OrderShipmentRecord> DatabaseManager::structuredOrderShipments(int orderId)
{
    QList<OrderShipmentRecord> records;
    if (orderId <= 0) {
        return records;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT shipment_date, shipment_type, shipment_quantity, COALESCE(note, '') "
        "FROM structured_shipment_records "
        "WHERE order_id = ? "
        "ORDER BY id ASC;"));
    query.addBindValue(orderId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return records;
    }

    while (query.next()) {
        OrderShipmentRecord record;
        record.shipmentDate = query.value(0).toString();
        record.shipmentType = query.value(1).toString() == QStringLiteral("order")
                                  ? QStringLiteral("订单级")
                                  : QStringLiteral("组件级");
        record.shipmentQuantity = query.value(2).toInt();
        record.note = query.value(3).toString();
        records.append(record);
    }

    return records;
}

bool DatabaseManager::saveStructuredOrderShipment(int orderId,
                                                  const QString &shipmentDate,
                                                  int shipmentSets,
                                                  const QString &note)
{
    if (orderId <= 0) {
        m_lastError = QStringLiteral("请选择有效订单。");
        return false;
    }
    if (shipmentSets <= 0) {
        m_lastError = QStringLiteral("订单发货套数必须大于 0。");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    QString errorMessage;
    const int availableSetShipments =
        availableSetShipmentsForStructuredOrder(database, orderId, &errorMessage);
    if (!errorMessage.isEmpty()) {
        m_lastError = errorMessage;
        return false;
    }
    if (shipmentSets > availableSetShipments) {
        m_lastError = QStringLiteral("发货套数超过当前订单可整套发货数量。");
        return false;
    }

    const QList<StructuredOrderComponentSnapshot> components = structuredOrderComponents(orderId);

    if (components.isEmpty()) {
        m_lastError = QStringLiteral("当前订单没有可发货组件。");
        return false;
    }

    for (const StructuredOrderComponentSnapshot &component : components) {
        const int delta = shipmentSets * component.quantityPerSet;
        if (delta > component.unshippedQuantity) {
            m_lastError = QStringLiteral("存在组件未发数量不足，无法整套发货。");
            return false;
        }
    }

    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery insertShipment(database);
    insertShipment.prepare(QStringLiteral(
        "INSERT INTO structured_shipment_records "
        "(order_id, order_component_id, shipment_date, shipment_type, shipment_quantity, note, created_at) "
        "VALUES (?, NULL, ?, 'order', ?, ?, ?);"));
    insertShipment.bindValue(0, orderId);
    insertShipment.bindValue(1, shipmentDate);
    insertShipment.bindValue(2, shipmentSets);
    insertShipment.bindValue(3, note.trimmed());
    insertShipment.bindValue(4, currentTimestamp());
    if (!insertShipment.exec()) {
        m_lastError = insertShipment.lastError().text();
        database.rollback();
        return false;
    }

    QSqlQuery updateComponent(database);
    updateComponent.prepare(QStringLiteral(
        "UPDATE order_components "
        "SET shipped_quantity = shipped_quantity + ?, "
        "unshipped_quantity = unshipped_quantity - ? "
        "WHERE id = ? AND order_id = ?;"));

    for (const StructuredOrderComponentSnapshot &component : components) {
        const int delta = shipmentSets * component.quantityPerSet;
        updateComponent.bindValue(0, delta);
        updateComponent.bindValue(1, delta);
        updateComponent.bindValue(2, component.id);
        updateComponent.bindValue(3, orderId);
        if (!updateComponent.exec()) {
            m_lastError = updateComponent.lastError().text();
            database.rollback();
            return false;
        }
        if (!deductStructuredInventory(database, orderId, component, delta)) {
            database.rollback();
            return false;
        }
    }

    if (!syncStructuredOrderStatus(database, orderId)) {
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

bool DatabaseManager::saveStructuredComponentShipment(int orderId,
                                                      int componentId,
                                                      const QString &shipmentDate,
                                                      int shipmentQuantity,
                                                      const QString &note)
{
    if (orderId <= 0 || componentId <= 0) {
        m_lastError = QStringLiteral("请选择有效组件。");
        return false;
    }
    if (shipmentQuantity <= 0) {
        m_lastError = QStringLiteral("组件发货数量必须大于 0。");
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    QSqlQuery componentQuery(database);
    componentQuery.prepare(QStringLiteral(
        "SELECT unshipped_quantity FROM order_components "
        "WHERE id = ? AND order_id = ?;"));
    componentQuery.addBindValue(componentId);
    componentQuery.addBindValue(orderId);

    if (!componentQuery.exec() || !componentQuery.next()) {
        m_lastError = componentQuery.lastError().isValid()
                          ? componentQuery.lastError().text()
                          : QStringLiteral("未找到对应订单组件。");
        return false;
    }

    const int unshippedQuantity = componentQuery.value(0).toInt();
    if (shipmentQuantity > unshippedQuantity) {
        m_lastError = QStringLiteral("组件发货数量超过未发数量。");
        return false;
    }

    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery insertShipment(database);
    insertShipment.prepare(QStringLiteral(
        "INSERT INTO structured_shipment_records "
        "(order_id, order_component_id, shipment_date, shipment_type, shipment_quantity, note, created_at) "
        "VALUES (?, ?, ?, 'component', ?, ?, ?);"));
    insertShipment.bindValue(0, orderId);
    insertShipment.bindValue(1, componentId);
    insertShipment.bindValue(2, shipmentDate);
    insertShipment.bindValue(3, shipmentQuantity);
    insertShipment.bindValue(4, note.trimmed());
    insertShipment.bindValue(5, currentTimestamp());
    if (!insertShipment.exec()) {
        m_lastError = insertShipment.lastError().text();
        database.rollback();
        return false;
    }

    QSqlQuery updateComponent(database);
    updateComponent.prepare(QStringLiteral(
        "UPDATE order_components "
        "SET shipped_quantity = shipped_quantity + ?, "
        "unshipped_quantity = unshipped_quantity - ? "
        "WHERE id = ? AND order_id = ?;"));
    updateComponent.bindValue(0, shipmentQuantity);
    updateComponent.bindValue(1, shipmentQuantity);
    updateComponent.bindValue(2, componentId);
    updateComponent.bindValue(3, orderId);

    if (!updateComponent.exec()) {
        m_lastError = updateComponent.lastError().text();
        database.rollback();
        return false;
    }

    StructuredOrderComponentSnapshot targetComponent;
    bool found = false;
    for (const StructuredOrderComponentSnapshot &component : structuredOrderComponents(orderId)) {
        if (component.id == componentId) {
            targetComponent = component;
            found = true;
            break;
        }
    }
    if (!found) {
        m_lastError = QStringLiteral("未找到对应订单组件。");
        database.rollback();
        return false;
    }
    if (!deductStructuredInventory(database, orderId, targetComponent, shipmentQuantity)) {
        database.rollback();
        return false;
    }

    if (!syncStructuredOrderStatus(database, orderId)) {
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

QList<OrderMaterialDemandData> DatabaseManager::unshippedOrderMaterialDemands()
{
    QList<OrderMaterialDemandData> demands;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT oc.component_name, COALESCE(oc.component_spec, ''), COALESCE(oc.material, ''), "
        "COALESCE(oc.color, ''), COALESCE(oc.unit_name, '件'), SUM(oc.unshipped_quantity) "
        "FROM order_components oc "
        "JOIN orders o ON o.id = oc.order_id "
        "WHERE oc.unshipped_quantity > 0 AND o.status != 'completed' "
        "GROUP BY oc.component_name, COALESCE(oc.component_spec, ''), "
        "COALESCE(oc.material, ''), COALESCE(oc.color, ''), COALESCE(oc.unit_name, '件') "
        "ORDER BY oc.component_name ASC, oc.component_spec ASC;"));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return demands;
    }

    while (query.next()) {
        OrderMaterialDemandData demand;
        demand.componentName = query.value(0).toString();
        demand.componentSpec = query.value(1).toString();
        demand.material = query.value(2).toString();
        demand.color = query.value(3).toString();
        demand.unitName = query.value(4).toString();
        demand.totalUnshippedQuantity = query.value(5).toInt();
        demands.append(demand);
    }

    return demands;
}

QList<InventoryItemData> DatabaseManager::inventoryItems()
{
    QList<InventoryItemData> items;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT ii.id, COALESCE(ii.product_category_id, 0), COALESCE(pc.name, ''), "
        "ii.component_name, ii.component_spec, ii.material, ii.color, ii.unit_name, "
        "COALESCE(ii.unit_price, 0), ii.current_quantity, COALESCE(ii.note, '') "
        "FROM inventory_items ii "
        "LEFT JOIN product_categories pc ON pc.id = ii.product_category_id "
        "ORDER BY ii.component_name ASC, ii.id ASC;"));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return items;
    }

    while (query.next()) {
        InventoryItemData item;
        item.id = query.value(0).toInt();
        item.productCategoryId = query.value(1).toInt();
        item.productCategoryName = query.value(2).toString();
        item.componentName = query.value(3).toString();
        item.componentSpec = query.value(4).toString();
        item.material = query.value(5).toString();
        item.color = query.value(6).toString();
        item.unitName = query.value(7).toString();
        item.unitPrice = query.value(8).toDouble();
        item.currentQuantity = query.value(9).toInt();
        item.note = query.value(10).toString();
        items.append(item);
    }

    return items;
}

QList<ProductComponentOption> DatabaseManager::inventoryComponentOptions(int productCategoryId)
{
    QList<ProductComponentOption> options;
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    QString statement = QStringLiteral(
        "SELECT ii.id, ii.component_name, COALESCE(ii.component_spec, ''), "
        "COALESCE(ii.material, ''), COALESCE(ii.color, ''), COALESCE(ii.unit_name, '件'), "
        "COALESCE(ii.unit_price, 0), COALESCE(ii.product_category_id, 0), COALESCE(pc.name, '') "
        "FROM inventory_items ii "
        "LEFT JOIN product_categories pc ON pc.id = ii.product_category_id "
        "WHERE COALESCE(ii.unit_price, 0) > 0");
    if (productCategoryId > 0) {
        statement += QStringLiteral(" AND (ii.product_category_id = ? OR ii.product_category_id IS NULL)");
    }
    statement += QStringLiteral(" ORDER BY ii.component_name ASC, ii.component_spec ASC, ii.id ASC;");

    query.prepare(statement);
    if (productCategoryId > 0) {
        query.addBindValue(productCategoryId);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return options;
    }

    QSet<QString> seenVariants;
    while (query.next()) {
        ProductComponentOption option;
        option.id = query.value(0).toInt();
        option.name = query.value(1).toString();
        option.componentSpec = query.value(2).toString();
        option.material = query.value(3).toString();
        option.color = query.value(4).toString();
        option.unitName = query.value(5).toString();
        option.unitPrice = query.value(6).toDouble();
        option.productCategoryId = query.value(7).toInt();
        option.productCategoryName = query.value(8).toString();

        const QString variantKey = QString::number(option.productCategoryId)
                                   + QLatin1Char('\x1f') + option.name.trimmed()
                                   + QLatin1Char('\x1f') + option.componentSpec.trimmed()
                                   + QLatin1Char('\x1f') + option.material.trimmed()
                                   + QLatin1Char('\x1f') + option.color.trimmed()
                                   + QLatin1Char('\x1f') + option.unitName.trimmed()
                                   + QLatin1Char('\x1f')
                                   + QString::number(option.unitPrice, 'f', 4);
        if (seenVariants.contains(variantKey)) {
            continue;
        }
        seenVariants.insert(variantKey);
        options.append(option);
    }

    return options;
}

bool DatabaseManager::saveInventoryItem(const InventoryItemData &item)
{
    if (item.productCategoryId <= 0) {
        m_lastError = QStringLiteral("请选择适用产品类型。");
        return false;
    }
    if (item.componentName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("库存物料名称不能为空。");
        return false;
    }
    if (item.currentQuantity < 0) {
        m_lastError = QStringLiteral("库存数量不能小于 0。");
        return false;
    }
    if (item.unitPrice <= 0.0) {
        m_lastError = QStringLiteral("库存单价必须大于 0。");
        return false;
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    const QString timestamp = currentTimestamp();
    if (item.id > 0) {
        query.prepare(QStringLiteral(
            "UPDATE inventory_items "
            "SET product_category_id = ?, component_name = ?, component_spec = ?, material = ?, "
            "color = ?, unit_name = ?, unit_price = ?, current_quantity = ?, note = ?, updated_at = ? "
            "WHERE id = ?;"));
        query.bindValue(0, item.productCategoryId);
        query.bindValue(1, item.componentName.trimmed());
        query.bindValue(2, item.componentSpec.trimmed());
        query.bindValue(3, item.material.trimmed());
        query.bindValue(4, item.color.trimmed());
        query.bindValue(5, item.unitName.trimmed().isEmpty()
                                ? QStringLiteral("件")
                                : item.unitName.trimmed());
        query.bindValue(6, item.unitPrice);
        query.bindValue(7, item.currentQuantity);
        query.bindValue(8, item.note.trimmed());
        query.bindValue(9, timestamp);
        query.bindValue(10, item.id);
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO inventory_items "
            "(product_category_id, component_name, component_spec, material, color, unit_name, unit_price, current_quantity, note, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"));
        query.bindValue(0, item.productCategoryId);
        query.bindValue(1, item.componentName.trimmed());
        query.bindValue(2, item.componentSpec.trimmed());
        query.bindValue(3, item.material.trimmed());
        query.bindValue(4, item.color.trimmed());
        query.bindValue(5, item.unitName.trimmed().isEmpty()
                                ? QStringLiteral("件")
                                : item.unitName.trimmed());
        query.bindValue(6, item.unitPrice);
        query.bindValue(7, item.currentQuantity);
        query.bindValue(8, item.note.trimmed());
        query.bindValue(9, timestamp);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    return true;
}

int DatabaseManager::inventoryItemIdByIdentity(QSqlDatabase &database,
                                               const InventoryIdentityData &identity,
                                               bool *duplicateFound)
{
    if (duplicateFound != nullptr) {
        *duplicateFound = false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id FROM inventory_items "
        "WHERE product_category_id = ? "
        "AND component_name = ? "
        "AND COALESCE(component_spec, '') = ? "
        "AND COALESCE(material, '') = ? "
        "AND COALESCE(color, '') = ? "
        "ORDER BY id ASC;"));
    query.addBindValue(identity.productCategoryId);
    query.addBindValue(identity.componentName.trimmed());
    query.addBindValue(identity.componentSpec.trimmed());
    query.addBindValue(identity.material.trimmed());
    query.addBindValue(identity.color.trimmed());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return 0;
    }

    int matchedId = 0;
    int matchCount = 0;
    while (query.next()) {
        ++matchCount;
        if (matchCount == 1) {
            matchedId = query.value(0).toInt();
        }
    }

    if (duplicateFound != nullptr && matchCount > 1) {
        *duplicateFound = true;
    }
    return matchCount == 1 ? matchedId : 0;
}

bool DatabaseManager::upsertInventoryItemByNaturalKey(const InventoryItemData &item)
{
    InventoryItemData normalizedItem = item;
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    InventoryIdentityData identity;
    identity.productCategoryId = item.productCategoryId;
    identity.componentName = item.componentName.trimmed();
    identity.componentSpec = item.componentSpec.trimmed();
    identity.material = item.material.trimmed();
    identity.color = item.color.trimmed();

    bool duplicateFound = false;
    normalizedItem.id = inventoryItemIdByIdentity(database, identity, &duplicateFound);
    if (duplicateFound) {
        m_lastError = QStringLiteral("库存存在重复自然键记录，无法自动覆盖：%1 / %2 / %3 / %4 / %5")
                          .arg(QString::number(identity.productCategoryId),
                               identity.componentName,
                               identity.componentSpec,
                               identity.material,
                               identity.color);
        return false;
    }

    return saveInventoryItem(normalizedItem);
}

bool DatabaseManager::isStructuredOrderShipmentReady(int orderId)
{
    if (orderId <= 0) {
        return false;
    }

    int productCategoryId = 0;
    QSqlQuery orderQuery(QSqlDatabase::database(kConnectionName));
    orderQuery.prepare(QStringLiteral("SELECT product_category_id FROM orders WHERE id = ?;"));
    orderQuery.addBindValue(orderId);
    if (!orderQuery.exec() || !orderQuery.next()) {
        m_lastError = orderQuery.lastError().isValid() ? orderQuery.lastError().text()
                                                       : QStringLiteral("未找到订单。");
        return false;
    }
    productCategoryId = orderQuery.value(0).toInt();

    QHash<QString, int> inventoryByKey;
    for (const InventoryItemData &item : inventoryItems()) {
        if (productCategoryId > 0 && item.productCategoryId > 0
            && item.productCategoryId != productCategoryId) {
            continue;
        }
        const QString key =
            inventoryKey(item.componentName, item.componentSpec, item.material, item.color);
        inventoryByKey.insert(key, inventoryByKey.value(key, 0) + item.currentQuantity);
    }

    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral(
        "SELECT component_name, COALESCE(component_spec, ''), COALESCE(material, ''), "
        "COALESCE(color, ''), unshipped_quantity "
        "FROM order_components "
        "WHERE order_id = ? AND unshipped_quantity > 0;"));
    query.addBindValue(orderId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    bool hasPendingComponent = false;
    while (query.next()) {
        hasPendingComponent = true;
        const QString key = inventoryKey(query.value(0).toString(),
                                         query.value(1).toString(),
                                         query.value(2).toString(),
                                         query.value(3).toString());
        if (inventoryByKey.value(key, 0) < query.value(4).toInt()) {
            return false;
        }
    }

    return hasPendingComponent;
}

bool DatabaseManager::repairShipmentData()
{
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.isValid() || !database.isOpen()) {
        m_lastError = QStringLiteral("Database connection is not available.");
        return false;
    }

    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery orderQuery(database);
    orderQuery.prepare(QStringLiteral(
        "SELECT id, product_model, quantity_sets FROM order_items ORDER BY id ASC;"));
    if (!orderQuery.exec()) {
        m_lastError = orderQuery.lastError().text();
        database.rollback();
        return false;
    }

    QSqlQuery findBodyComponent(database);
    findBodyComponent.prepare(QStringLiteral(
        "SELECT id FROM order_item_components "
        "WHERE order_item_id = ? AND source_type = ? LIMIT 1;"));
    QSqlQuery insertBodyComponent(database);
    insertBodyComponent.prepare(QStringLiteral(
        "INSERT INTO order_item_components "
        "(order_item_id, component_name, quantity_per_set, total_required_quantity, "
        "shipped_quantity, unshipped_quantity, unit_price, total_price, source_type) "
        "VALUES (?, ?, 1, ?, 0, ?, ?, ?, ?);"));
    QSqlQuery sumOrderShipments(database);
    sumOrderShipments.prepare(QStringLiteral(
        "SELECT "
        "COALESCE(SUM(CASE WHEN order_item_component_id IS NULL THEN shipment_quantity ELSE 0 END), 0) + "
        "COALESCE(SUM(CASE WHEN order_item_component_id = ? THEN shipment_quantity ELSE 0 END), 0) "
        "FROM shipment_records WHERE order_item_id = ?;"));
    QSqlQuery updateBodyComponent(database);
    updateBodyComponent.prepare(QStringLiteral(
        "UPDATE order_item_components "
        "SET component_name = ?, total_required_quantity = ?, shipped_quantity = ?, "
        "unshipped_quantity = ?, unit_price = ?, total_price = ?, source_type = ? "
        "WHERE id = ?;"));
    QSqlQuery updateOrder(database);
    updateOrder.prepare(QStringLiteral(
        "UPDATE order_items SET shipped_sets = ?, unshipped_sets = ? WHERE id = ?;"));
    QSqlQuery findBodyPrice(database);
    findBodyPrice.prepare(QStringLiteral(
        "SELECT default_price FROM product_models WHERE name = ? LIMIT 1;"));
    QSqlQuery updateProductPrices(database);
    updateProductPrices.prepare(QStringLiteral(
        "UPDATE product_models "
        "SET default_price = CASE name "
        "WHEN 'OMS-标准灯箱' THEN 1200 "
        "WHEN 'OMS-展示架' THEN 800 "
        "ELSE default_price END "
        "WHERE default_price = 0;"));
    QSqlQuery updateTemplateComponentPrices(database);
    updateTemplateComponentPrices.prepare(QStringLiteral(
        "UPDATE option_template_components "
        "SET unit_price = CASE component_name "
        "WHEN '光源' THEN 90 "
        "WHEN '灯罩' THEN 25 "
        "WHEN '电源' THEN 180 "
        "WHEN '白色机头' THEN 220 "
        "WHEN '主架' THEN 160 "
        "WHEN '连接件' THEN 18 "
        "WHEN '底座' THEN 45 "
        "ELSE unit_price END "
        "WHERE unit_price = 0;"));
    QSqlQuery updateNonBodyComponentPrices(database);
    updateNonBodyComponentPrices.prepare(QStringLiteral(
        "UPDATE order_item_components "
        "SET unit_price = CASE component_name "
        "WHEN '光源' THEN 90 "
        "WHEN '灯罩' THEN 25 "
        "WHEN '电源' THEN 180 "
        "WHEN '白色机头' THEN 220 "
        "WHEN '主架' THEN 160 "
        "WHEN '连接件' THEN 18 "
        "WHEN '底座' THEN 45 "
        "ELSE unit_price END "
        "WHERE source_type != ? AND unit_price = 0;"));
    QSqlQuery updateAllComponentTotals(database);
    updateAllComponentTotals.prepare(QStringLiteral(
        "UPDATE order_item_components "
        "SET total_price = unit_price * total_required_quantity "
        "WHERE order_item_id = ?;"));
    QSqlQuery updateOrderUnitPrice(database);
    updateOrderUnitPrice.prepare(QStringLiteral(
        "UPDATE order_items "
        "SET unit_price = COALESCE((SELECT SUM(quantity_per_set * unit_price) "
        "FROM order_item_components WHERE order_item_id = ?), 0) "
        "WHERE id = ?;"));

    if (!updateProductPrices.exec()) {
        m_lastError = updateProductPrices.lastError().text();
        database.rollback();
        return false;
    }
    if (!updateTemplateComponentPrices.exec()) {
        m_lastError = updateTemplateComponentPrices.lastError().text();
        database.rollback();
        return false;
    }
    updateNonBodyComponentPrices.bindValue(0, QString::fromLatin1(kBodySourceType));
    if (!updateNonBodyComponentPrices.exec()) {
        m_lastError = updateNonBodyComponentPrices.lastError().text();
        database.rollback();
        return false;
    }

    while (orderQuery.next()) {
        const int orderItemId = orderQuery.value(0).toInt();
        const QString productModelName = orderQuery.value(1).toString();
        const int quantitySets = orderQuery.value(2).toInt();
        const QString bodyName = bodyComponentName(productModelName);
        double bodyUnitPrice = 0.0;
        findBodyPrice.bindValue(0, productModelName);
        if (!findBodyPrice.exec()) {
            m_lastError = findBodyPrice.lastError().text();
            database.rollback();
            return false;
        }
        if (findBodyPrice.next()) {
            bodyUnitPrice = findBodyPrice.value(0).toDouble();
        }

        findBodyComponent.bindValue(0, orderItemId);
        findBodyComponent.bindValue(1, QString::fromLatin1(kBodySourceType));
        int bodyComponentId = 0;
        if (!findBodyComponent.exec()) {
            m_lastError = findBodyComponent.lastError().text();
            database.rollback();
            return false;
        }
        if (findBodyComponent.next()) {
            bodyComponentId = findBodyComponent.value(0).toInt();
        }

        if (bodyComponentId == 0) {
            insertBodyComponent.bindValue(0, orderItemId);
            insertBodyComponent.bindValue(1, bodyName);
            insertBodyComponent.bindValue(2, quantitySets);
            insertBodyComponent.bindValue(3, quantitySets);
            insertBodyComponent.bindValue(4, bodyUnitPrice);
            insertBodyComponent.bindValue(5, static_cast<double>(quantitySets) * bodyUnitPrice);
            insertBodyComponent.bindValue(6, QString::fromLatin1(kBodySourceType));
            if (!insertBodyComponent.exec()) {
                m_lastError = insertBodyComponent.lastError().text();
                database.rollback();
                return false;
            }
            bodyComponentId = insertBodyComponent.lastInsertId().toInt();
        }

        sumOrderShipments.bindValue(0, bodyComponentId);
        sumOrderShipments.bindValue(1, orderItemId);
        if (!sumOrderShipments.exec() || !sumOrderShipments.next()) {
            m_lastError = sumOrderShipments.lastError().text();
            database.rollback();
            return false;
        }

        const int shippedSets = qBound(0, sumOrderShipments.value(0).toInt(), quantitySets);
        const int unshippedSets = quantitySets - shippedSets;

        updateBodyComponent.bindValue(0, bodyName);
        updateBodyComponent.bindValue(1, quantitySets);
        updateBodyComponent.bindValue(2, shippedSets);
        updateBodyComponent.bindValue(3, unshippedSets);
        updateBodyComponent.bindValue(4, bodyUnitPrice);
        updateBodyComponent.bindValue(5, static_cast<double>(quantitySets) * bodyUnitPrice);
        updateBodyComponent.bindValue(6, QString::fromLatin1(kBodySourceType));
        updateBodyComponent.bindValue(7, bodyComponentId);
        if (!updateBodyComponent.exec()) {
            m_lastError = updateBodyComponent.lastError().text();
            database.rollback();
            return false;
        }

        updateOrder.bindValue(0, shippedSets);
        updateOrder.bindValue(1, unshippedSets);
        updateOrder.bindValue(2, orderItemId);
        if (!updateOrder.exec()) {
            m_lastError = updateOrder.lastError().text();
            database.rollback();
            return false;
        }

        updateAllComponentTotals.bindValue(0, orderItemId);
        if (!updateAllComponentTotals.exec()) {
            m_lastError = updateAllComponentTotals.lastError().text();
            database.rollback();
            return false;
        }

        updateOrderUnitPrice.bindValue(0, orderItemId);
        updateOrderUnitPrice.bindValue(1, orderItemId);
        if (!updateOrderUnitPrice.exec()) {
            m_lastError = updateOrderUnitPrice.lastError().text();
            database.rollback();
            return false;
        }
    }

    if (!repairStructuredInventoryUnitPrices(database)) {
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

bool DatabaseManager::repairStructuredInventoryUnitPrices(QSqlDatabase &database)
{
    QHash<QString, double> priceByKey;

    QSqlQuery configQuery(database);
    configQuery.prepare(QStringLiteral(
        "SELECT bc.product_category_id, bcc.component_name, COALESCE(bcc.component_spec, ''), "
        "COALESCE(bcc.material, ''), COALESCE(bcc.color, ''), bcc.unit_amount "
        "FROM base_configuration_components bcc "
        "JOIN base_configurations bc ON bc.id = bcc.base_configuration_id "
        "WHERE bcc.unit_amount > 0;"));
    if (!configQuery.exec()) {
        m_lastError = configQuery.lastError().text();
        return false;
    }
    while (configQuery.next()) {
        const QString key = structuredInventoryKey(configQuery.value(0).toInt(),
                                                   configQuery.value(1).toString(),
                                                   configQuery.value(2).toString(),
                                                   configQuery.value(3).toString(),
                                                   configQuery.value(4).toString());
        priceByKey.insert(key, qMax(priceByKey.value(key, 0.0), configQuery.value(5).toDouble()));
    }

    QSqlQuery lampshadeQuery(database);
    lampshadeQuery.prepare(QStringLiteral(
        "SELECT product_category_id, lampshade_name, lampshade_unit_price "
        "FROM product_skus WHERE lampshade_unit_price > 0;"));
    if (!lampshadeQuery.exec()) {
        m_lastError = lampshadeQuery.lastError().text();
        return false;
    }
    while (lampshadeQuery.next()) {
        const QString key = structuredInventoryKey(lampshadeQuery.value(0).toInt(),
                                                   lampshadeQuery.value(1).toString(),
                                                   QString(),
                                                   QString(),
                                                   QString());
        priceByKey.insert(key, qMax(priceByKey.value(key, 0.0), lampshadeQuery.value(2).toDouble()));
    }

    QSqlQuery zeroPriceQuery(database);
    zeroPriceQuery.prepare(QStringLiteral(
        "SELECT id, COALESCE(product_category_id, 0), component_name, "
        "COALESCE(component_spec, ''), COALESCE(material, ''), COALESCE(color, '') "
        "FROM inventory_items "
        "WHERE COALESCE(unit_price, 0) <= 0 "
        "ORDER BY id ASC;"));
    if (!zeroPriceQuery.exec()) {
        m_lastError = zeroPriceQuery.lastError().text();
        return false;
    }

    QSqlQuery updatePrice(database);
    updatePrice.prepare(QStringLiteral(
        "UPDATE inventory_items SET unit_price = ?, updated_at = ? WHERE id = ?;"));
    const QString timestamp = currentTimestamp();

    while (zeroPriceQuery.next()) {
        const int id = zeroPriceQuery.value(0).toInt();
        const QString key = structuredInventoryKey(zeroPriceQuery.value(1).toInt(),
                                                   zeroPriceQuery.value(2).toString(),
                                                   zeroPriceQuery.value(3).toString(),
                                                   zeroPriceQuery.value(4).toString(),
                                                   zeroPriceQuery.value(5).toString());
        const double repairedPrice = qMax(priceByKey.value(key, 0.0), kDefaultNonZeroUnitPrice);
        updatePrice.bindValue(0, repairedPrice);
        updatePrice.bindValue(1, timestamp);
        updatePrice.bindValue(2, id);
        if (!updatePrice.exec()) {
            m_lastError = updatePrice.lastError().text();
            return false;
        }
    }

    return true;
}

int DatabaseManager::availableSetShipmentsForOrder(QSqlDatabase &database,
                                                   int orderItemId,
                                                   QString *errorMessage) const
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT MIN(unshipped_quantity / quantity_per_set) "
        "FROM order_item_components "
        "WHERE order_item_id = ? AND quantity_per_set > 0;"));
    query.addBindValue(orderItemId);

    if (!query.exec() || !query.next()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return 0;
    }

    if (query.value(0).isNull()) {
        return 0;
    }

    return query.value(0).toInt();
}

int DatabaseManager::availableSetShipmentsForStructuredOrder(QSqlDatabase &database,
                                                             int orderId,
                                                             QString *errorMessage) const
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT MIN(unshipped_quantity / quantity_per_set) "
        "FROM order_components "
        "WHERE order_id = ? AND quantity_per_set > 0;"));
    query.addBindValue(orderId);

    if (!query.exec() || !query.next()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return 0;
    }

    if (query.value(0).isNull()) {
        return 0;
    }

    return query.value(0).toInt();
}

bool DatabaseManager::syncStructuredOrderStatus(QSqlDatabase &database, int orderId)
{
    QSqlQuery stateQuery(database);
    stateQuery.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM order_components "
        "WHERE order_id = ? AND unshipped_quantity > 0;"));
    stateQuery.addBindValue(orderId);

    if (!stateQuery.exec() || !stateQuery.next()) {
        m_lastError = stateQuery.lastError().text();
        return false;
    }

    const QString status = stateQuery.value(0).toInt() == 0 ? QStringLiteral("completed")
                                                            : QStringLiteral("open");
    QSqlQuery updateOrder(database);
    updateOrder.prepare(QStringLiteral(
        "UPDATE orders SET status = ?, updated_at = ? WHERE id = ?;"));
    updateOrder.bindValue(0, status);
    updateOrder.bindValue(1, currentTimestamp());
    updateOrder.bindValue(2, orderId);

    if (!updateOrder.exec()) {
        m_lastError = updateOrder.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::deductStructuredInventory(QSqlDatabase &database,
                                                int orderId,
                                                const StructuredOrderComponentSnapshot &component,
                                                int deductionQuantity)
{
    if (deductionQuantity <= 0) {
        return true;
    }

    QSqlQuery orderQuery(database);
    orderQuery.prepare(QStringLiteral(
        "SELECT product_category_id FROM orders WHERE id = ? LIMIT 1;"));
    orderQuery.addBindValue(orderId);
    if (!orderQuery.exec() || !orderQuery.next()) {
        m_lastError = orderQuery.lastError().isValid() ? orderQuery.lastError().text()
                                                       : QStringLiteral("未找到对应订单。");
        return false;
    }
    const int productCategoryId = orderQuery.value(0).toInt();

    struct InventoryCandidate
    {
        int id = 0;
        int currentQuantity = 0;
    };

    QList<InventoryCandidate> candidates;
    int totalAvailable = 0;
    QSqlQuery inventoryQuery(database);
    inventoryQuery.prepare(QStringLiteral(
        "SELECT id, current_quantity "
        "FROM inventory_items "
        "WHERE product_category_id = ? "
        "AND component_name = ? "
        "AND COALESCE(component_spec, '') = ? "
        "AND COALESCE(material, '') = ? "
        "AND COALESCE(color, '') = ? "
        "ORDER BY id ASC;"));
    inventoryQuery.bindValue(0, productCategoryId);
    inventoryQuery.bindValue(1, component.componentName.trimmed());
    inventoryQuery.bindValue(2, component.componentSpec.trimmed());
    inventoryQuery.bindValue(3, component.material.trimmed());
    inventoryQuery.bindValue(4, component.color.trimmed());

    if (!inventoryQuery.exec()) {
        m_lastError = inventoryQuery.lastError().text();
        return false;
    }

    while (inventoryQuery.next()) {
        InventoryCandidate candidate;
        candidate.id = inventoryQuery.value(0).toInt();
        candidate.currentQuantity = inventoryQuery.value(1).toInt();
        totalAvailable += candidate.currentQuantity;
        candidates.append(candidate);
    }

    if (totalAvailable < deductionQuantity) {
        m_lastError = QStringLiteral("库存不足：组件“%1”可扣减库存为 %2，当前发货需要 %3。")
                          .arg(component.componentName)
                          .arg(totalAvailable)
                          .arg(deductionQuantity);
        return false;
    }

    QSqlQuery updateInventory(database);
    updateInventory.prepare(QStringLiteral(
        "UPDATE inventory_items SET current_quantity = ? WHERE id = ?;"));

    int remaining = deductionQuantity;
    for (const InventoryCandidate &candidate : candidates) {
        if (remaining <= 0) {
            break;
        }

        const int deduction = qMin(candidate.currentQuantity, remaining);
        updateInventory.bindValue(0, candidate.currentQuantity - deduction);
        updateInventory.bindValue(1, candidate.id);
        if (!updateInventory.exec()) {
            m_lastError = updateInventory.lastError().text();
            return false;
        }
        remaining -= deduction;
    }

    return remaining == 0;
}

double DatabaseManager::productDefaultPrice(QSqlDatabase &database,
                                            const QString &productModelName,
                                            QString *errorMessage) const
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT default_price FROM product_models WHERE name = ? LIMIT 1;"));
    query.addBindValue(productModelName);
    if (!query.exec() || !query.next()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().isValid()
                                ? query.lastError().text()
                                : QStringLiteral("未找到产品型号默认价格。");
        }
        return 0.0;
    }

    return query.value(0).toDouble();
}

QString DatabaseManager::bodyComponentName(const QString &productModelName) const
{
    return QStringLiteral("%1机体").arg(productModelName.trimmed());
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

bool DatabaseManager::ensureMinimumComponentCatalogData(QSqlDatabase &database)
{
    QSqlQuery productQuery(database);
    productQuery.prepare(QStringLiteral("SELECT id, name FROM product_models ORDER BY id ASC;"));
    if (!productQuery.exec()) {
        m_lastError = productQuery.lastError().text();
        return false;
    }

    QHash<QString, int> productIds;
    while (productQuery.next()) {
        productIds.insert(productQuery.value(1).toString(), productQuery.value(0).toInt());
    }

    QSqlQuery insertComponent(database);
    insertComponent.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO product_model_components "
        "(product_model_id, component_name, unit_price) VALUES (?, ?, ?);"));

    const QList<QPair<QString, QList<OrderComponentData>>> catalogEntries = {
        {QStringLiteral("OMS-标准灯箱"),
         {{QStringLiteral("光源"), 0, 90.0, QString()},
          {QStringLiteral("灯罩"), 0, 25.0, QString()},
          {QStringLiteral("电源"), 0, 180.0, QString()},
          {QStringLiteral("白色机头"), 0, 220.0, QString()}}},
        {QStringLiteral("OMS-展示架"),
         {{QStringLiteral("主架"), 0, 160.0, QString()},
          {QStringLiteral("连接件"), 0, 18.0, QString()},
          {QStringLiteral("底座"), 0, 45.0, QString()}}}
    };

    for (const auto &entry : catalogEntries) {
        const int productModelId = productIds.value(entry.first);
        if (productModelId <= 0) {
            continue;
        }

        for (const OrderComponentData &component : entry.second) {
            insertComponent.bindValue(0, productModelId);
            insertComponent.bindValue(1, component.componentName);
            insertComponent.bindValue(2, component.unitPrice);
            if (!insertComponent.exec()) {
                m_lastError = insertComponent.lastError().text();
                return false;
            }
        }
    }

    return true;
}

bool DatabaseManager::ensureMinimumStructuredDemoData(QSqlDatabase &database)
{
    if (!database.isValid() || !database.isOpen()) {
        m_lastError = QStringLiteral("Database connection is not available.");
        return false;
    }

    const QString timestamp = currentTimestamp();
    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }

    QSqlQuery insertCategory(database);
    insertCategory.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO product_categories "
        "(name, is_active, created_at, updated_at) VALUES (?, 1, ?, ?);"));
    const QStringList categoryNames = {QStringLiteral("飞月"),
                                       QStringLiteral("金牛"),
                                       QStringLiteral("仿派"),
                                       QStringLiteral("今慕光语")};
    for (const QString &categoryName : categoryNames) {
        insertCategory.bindValue(0, categoryName);
        insertCategory.bindValue(1, timestamp);
        insertCategory.bindValue(2, timestamp);
        if (!insertCategory.exec()) {
            m_lastError = insertCategory.lastError().text();
            database.rollback();
            return false;
        }
    }

    QHash<QString, int> categoryIds;
    QSqlQuery categoryQuery(database);
    categoryQuery.prepare(QStringLiteral("SELECT id, name FROM product_categories;"));
    if (!categoryQuery.exec()) {
        m_lastError = categoryQuery.lastError().text();
        database.rollback();
        return false;
    }
    while (categoryQuery.next()) {
        categoryIds.insert(categoryQuery.value(1).toString(), categoryQuery.value(0).toInt());
    }

    const int feiyueCategoryId = categoryIds.value(QStringLiteral("飞月"));
    if (feiyueCategoryId <= 0) {
        m_lastError = QStringLiteral("未找到飞月产品类型。");
        database.rollback();
        return false;
    }

    QSqlQuery insertSku(database);
    insertSku.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO product_skus "
        "(product_category_id, sku_name, lampshade_name, lampshade_unit_price, is_active, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, 1, ?, ?);"));
    const QList<ProductSkuOption> feiyueSkus = {
        {0, feiyueCategoryId, QStringLiteral("飞月"), QStringLiteral("飞月185款"), QStringLiteral("亮普家灯罩"), 43.0, true},
        {0, feiyueCategoryId, QStringLiteral("飞月"), QStringLiteral("飞月186款"), QStringLiteral("通用灯罩"), 22.0, true},
        {0, feiyueCategoryId, QStringLiteral("飞月"), QStringLiteral("飞月187款"), QStringLiteral("通用灯罩"), 14.0, true},
        {0, feiyueCategoryId, QStringLiteral("飞月"), QStringLiteral("飞月188款"), QStringLiteral("通用灯罩"), 14.0, true},
        {0, feiyueCategoryId, QStringLiteral("飞月"), QStringLiteral("飞月190款"), QStringLiteral("通用灯罩"), 10.0, true}
    };
    for (const ProductSkuOption &sku : feiyueSkus) {
        insertSku.bindValue(0, sku.productCategoryId);
        insertSku.bindValue(1, sku.skuName);
        insertSku.bindValue(2, sku.lampshadeName);
        insertSku.bindValue(3, sku.lampshadeUnitPrice);
        insertSku.bindValue(4, timestamp);
        insertSku.bindValue(5, timestamp);
        if (!insertSku.exec()) {
            m_lastError = insertSku.lastError().text();
            database.rollback();
            return false;
        }
    }

    QSqlQuery insertBaseConfiguration(database);
    insertBaseConfiguration.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO base_configurations "
        "(product_category_id, config_code, config_name, config_price, is_active, sort_order, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, 1, ?, ?, ?);"));
    const QList<BaseConfigurationOption> feiyueConfigurations = {
        {0, feiyueCategoryId, QStringLiteral("A"), QStringLiteral("A配置"), 115.50, 1, true},
        {0, feiyueCategoryId, QStringLiteral("B"), QStringLiteral("B配置"), 100.00, 2, true},
        {0, feiyueCategoryId, QStringLiteral("C"), QStringLiteral("C配置"), 107.79, 3, true},
        {0, feiyueCategoryId, QStringLiteral("D"), QStringLiteral("D配置"), 95.50, 4, true}
    };
    for (const BaseConfigurationOption &configuration : feiyueConfigurations) {
        insertBaseConfiguration.bindValue(0, configuration.productCategoryId);
        insertBaseConfiguration.bindValue(1, configuration.configCode);
        insertBaseConfiguration.bindValue(2, configuration.configName);
        insertBaseConfiguration.bindValue(3, configuration.configPrice);
        insertBaseConfiguration.bindValue(4, configuration.sortOrder);
        insertBaseConfiguration.bindValue(5, timestamp);
        insertBaseConfiguration.bindValue(6, timestamp);
        if (!insertBaseConfiguration.exec()) {
            m_lastError = insertBaseConfiguration.lastError().text();
            database.rollback();
            return false;
        }
    }

    QSqlQuery findConfiguration(database);
    findConfiguration.prepare(QStringLiteral(
        "SELECT id FROM base_configurations "
        "WHERE product_category_id = ? AND config_code = ? LIMIT 1;"));

    QSqlQuery componentCount(database);
    componentCount.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM base_configuration_components WHERE base_configuration_id = ?;"));
    QSqlQuery insertComponent(database);
    insertComponent.prepare(QStringLiteral(
        "INSERT INTO base_configuration_components "
        "(base_configuration_id, component_name, component_spec, material, color, unit_name, "
        "quantity, unit_amount, line_amount, sort_order, is_active, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?);"));

    const auto ensureConfigComponents =
        [&](const QString &configCode, const QList<BaseConfigurationComponentData> &components) {
            findConfiguration.bindValue(0, feiyueCategoryId);
            findConfiguration.bindValue(1, configCode);
            if (!findConfiguration.exec() || !findConfiguration.next()) {
                m_lastError = findConfiguration.lastError().isValid()
                                  ? findConfiguration.lastError().text()
                                  : QStringLiteral("未找到基础配置。");
                return false;
            }

            const int baseConfigurationId = findConfiguration.value(0).toInt();
            componentCount.bindValue(0, baseConfigurationId);
            if (!componentCount.exec() || !componentCount.next()) {
                m_lastError = componentCount.lastError().text();
                return false;
            }

            if (componentCount.value(0).toInt() > 0) {
                return true;
            }

            for (const BaseConfigurationComponentData &component : components) {
                insertComponent.bindValue(0, baseConfigurationId);
                insertComponent.bindValue(1, component.componentName);
                insertComponent.bindValue(2, component.componentSpec);
                insertComponent.bindValue(3, component.material);
                insertComponent.bindValue(4, component.color);
                insertComponent.bindValue(5, component.unitName);
                insertComponent.bindValue(6, component.quantity);
                insertComponent.bindValue(7, component.unitAmount);
                insertComponent.bindValue(8, component.lineAmount);
                insertComponent.bindValue(9, component.sortOrder);
                insertComponent.bindValue(10, timestamp);
                insertComponent.bindValue(11, timestamp);
                if (!insertComponent.exec()) {
                    m_lastError = insertComponent.lastError().text();
                    return false;
                }
            }

            return true;
        };

    const QList<BaseConfigurationComponentData> configAComponents = {
        {0, 0, QStringLiteral("变频电机"), QStringLiteral("9512+语音播报"), QStringLiteral("全铜"), QStringLiteral("镀锌"), QStringLiteral("件"), 1, 21.00, 21.00, 1, true},
        {0, 0, QStringLiteral("控制器"), QStringLiteral("遥控款"), QString(), QString(), QStringLiteral("件"), 1, 26.00, 26.00, 2, true},
        {0, 0, QStringLiteral("直边盘"), QStringLiteral("0.6直边"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 11.50, 11.50, 3, true},
        {0, 0, QStringLiteral("48寸扇叶"), QStringLiteral("ABS扇叶"), QStringLiteral("ABS塑料"), QStringLiteral("透明"), QStringLiteral("件"), 1, 12.50, 12.50, 4, true},
        {0, 0, QStringLiteral("吊架"), QStringLiteral("标准吊架"), QStringLiteral("铁"), QStringLiteral("黑色"), QStringLiteral("件"), 1, 4.80, 4.80, 5, true},
        {0, 0, QStringLiteral("配件包"), QStringLiteral("螺丝/说明书"), QString(), QString(), QStringLiteral("件"), 1, 3.20, 3.20, 6, true}
    };
    const QList<BaseConfigurationComponentData> configBComponents = {
        {0, 0, QStringLiteral("变频电机"), QStringLiteral("9512标准款"), QStringLiteral("全铜"), QStringLiteral("镀锌"), QStringLiteral("件"), 1, 19.50, 19.50, 1, true},
        {0, 0, QStringLiteral("控制器"), QStringLiteral("壁控款"), QString(), QString(), QStringLiteral("件"), 1, 22.00, 22.00, 2, true},
        {0, 0, QStringLiteral("直边盘"), QStringLiteral("0.5直边"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 9.80, 9.80, 3, true},
        {0, 0, QStringLiteral("扇叶盘"), QStringLiteral("0.6扇叶盘"), QStringLiteral("铁"), QStringLiteral("黑色"), QStringLiteral("件"), 1, 4.60, 4.60, 4, true},
        {0, 0, QStringLiteral("48寸扇叶"), QStringLiteral("ABS扇叶"), QStringLiteral("ABS塑料"), QStringLiteral("透明"), QStringLiteral("件"), 1, 11.20, 11.20, 5, true},
        {0, 0, QStringLiteral("配件包"), QStringLiteral("螺丝/说明书"), QString(), QString(), QStringLiteral("件"), 1, 2.80, 2.80, 6, true}
    };
    const QList<BaseConfigurationComponentData> configCComponents = {
        {0, 0, QStringLiteral("变频电机"), QStringLiteral("9512+语音播报"), QStringLiteral("全铜"), QStringLiteral("镀锌"), QStringLiteral("件"), 1, 21.00, 21.00, 1, true},
        {0, 0, QStringLiteral("控制器"), QString(), QString(), QString(), QStringLiteral("件"), 1, 25.50, 25.50, 2, true},
        {0, 0, QStringLiteral("直边盘"), QStringLiteral("0.5直边"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 10.00, 10.00, 3, true},
        {0, 0, QStringLiteral("扇叶盘"), QStringLiteral("0.7扇叶盘黑色"), QStringLiteral("铁"), QStringLiteral("黑色"), QStringLiteral("件"), 1, 5.00, 5.00, 4, true},
        {0, 0, QStringLiteral("48寸扇叶"), QStringLiteral("扇叶"), QStringLiteral("ABS塑料"), QStringLiteral("透明"), QStringLiteral("件"), 1, 11.50, 11.50, 5, true},
        {0, 0, QStringLiteral("吊钟"), QStringLiteral("(白色)下锁式，55孔距，锥形吊钟"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 1.10, 1.10, 6, true},
        {0, 0, QStringLiteral("蒙古包"), QStringLiteral("碗"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 2.00, 2.00, 7, true},
        {0, 0, QStringLiteral("小喇叭"), QString(), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 0.40, 0.40, 8, true},
        {0, 0, QStringLiteral("吊杆"), QStringLiteral("(白色) Φ26.5*T1.5*100mm五孔大吊杆"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 0.45, 0.45, 9, true},
        {0, 0, QStringLiteral("吊杆"), QStringLiteral("(白色) Φ26.5*T1.5*200mm五孔大吊杆"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 0.95, 0.95, 10, true},
        {0, 0, QStringLiteral("三角片"), QStringLiteral("厚三角片"), QStringLiteral("铁"), QStringLiteral("镀锌"), QStringLiteral("件"), 1, 0.60, 0.60, 11, true},
        {0, 0, QStringLiteral("吊架"), QStringLiteral("铁吊架+如意头+吊头"), QStringLiteral("铁"), QStringLiteral("黑色"), QStringLiteral("件"), 1, 4.40, 4.40, 12, true},
        {0, 0, QStringLiteral("泡沫"), QStringLiteral("42-D-（51.5*51.5*15）"), QStringLiteral("泡沫"), QStringLiteral("白色"), QStringLiteral("件"), 1, 5.80, 5.80, 13, true},
        {0, 0, QStringLiteral("纸箱"), QStringLiteral("53.53.27"), QStringLiteral("纸"), QStringLiteral("米黄"), QStringLiteral("件"), 1, 4.42, 4.42, 14, true},
        {0, 0, QStringLiteral("整机配件螺丝"), QStringLiteral("配件包0.7元/机身螺丝1.8"), QString(), QString(), QStringLiteral("件"), 1, 2.50, 2.50, 15, true},
        {0, 0, QStringLiteral("装饰盖"), QString(), QString(), QString(), QStringLiteral("件"), 1, 0.30, 0.30, 16, true},
        {0, 0, QStringLiteral("配件"), QStringLiteral("说明书0.1平衡帖0.1包装袋0.17"), QString(), QString(), QStringLiteral("件"), 1, 0.27, 0.27, 17, true},
        {0, 0, QStringLiteral("电机线"), QString(), QString(), QString(), QStringLiteral("件"), 1, 0.60, 0.60, 18, true},
        {0, 0, QStringLiteral("厂房+利润"), QString(), QString(), QString(), QStringLiteral("项"), 1, 6.00, 6.00, 19, true},
        {0, 0, QStringLiteral("人工"), QString(), QString(), QString(), QStringLiteral("项"), 1, 5.00, 5.00, 20, true}
    };
    const QList<BaseConfigurationComponentData> configDComponents = {
        {0, 0, QStringLiteral("变频电机"), QStringLiteral("9512经济款"), QStringLiteral("全铜"), QStringLiteral("镀锌"), QStringLiteral("件"), 1, 18.50, 18.50, 1, true},
        {0, 0, QStringLiteral("控制器"), QStringLiteral("拉绳款"), QString(), QString(), QStringLiteral("件"), 1, 18.00, 18.00, 2, true},
        {0, 0, QStringLiteral("直边盘"), QStringLiteral("0.5直边"), QStringLiteral("铁"), QStringLiteral("白色"), QStringLiteral("件"), 1, 9.20, 9.20, 3, true},
        {0, 0, QStringLiteral("48寸扇叶"), QStringLiteral("ABS扇叶"), QStringLiteral("ABS塑料"), QStringLiteral("透明"), QStringLiteral("件"), 1, 10.50, 10.50, 4, true},
        {0, 0, QStringLiteral("吊架"), QStringLiteral("标准吊架"), QStringLiteral("铁"), QStringLiteral("黑色"), QStringLiteral("件"), 1, 4.10, 4.10, 5, true},
        {0, 0, QStringLiteral("配件包"), QStringLiteral("螺丝/说明书"), QString(), QString(), QStringLiteral("件"), 1, 2.60, 2.60, 6, true}
    };

    if (!ensureConfigComponents(QStringLiteral("A"), configAComponents)
        || !ensureConfigComponents(QStringLiteral("B"), configBComponents)
        || !ensureConfigComponents(QStringLiteral("C"), configCComponents)
        || !ensureConfigComponents(QStringLiteral("D"), configDComponents)) {
        database.rollback();
        return false;
    }

    QSqlQuery existingInventory(database);
    existingInventory.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM inventory_items "
        "WHERE COALESCE(product_category_id, 0) = ? "
        "AND component_name = ? "
        "AND COALESCE(component_spec, '') = ? "
        "AND COALESCE(material, '') = ? "
        "AND COALESCE(color, '') = ?;"));
    QSqlQuery insertInventory(database);
    insertInventory.prepare(QStringLiteral(
        "INSERT INTO inventory_items "
        "(product_category_id, component_name, component_spec, material, color, unit_name, unit_price, current_quantity, note, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"));

    const auto ensureInventoryItem = [&](const InventoryItemData &item) {
        existingInventory.bindValue(0, item.productCategoryId);
        existingInventory.bindValue(1, item.componentName);
        existingInventory.bindValue(2, item.componentSpec);
        existingInventory.bindValue(3, item.material);
        existingInventory.bindValue(4, item.color);
        if (!existingInventory.exec() || !existingInventory.next()) {
            m_lastError = existingInventory.lastError().text();
            return false;
        }
        if (existingInventory.value(0).toInt() > 0) {
            return true;
        }

        insertInventory.bindValue(0, item.productCategoryId);
        insertInventory.bindValue(1, item.componentName);
        insertInventory.bindValue(2, item.componentSpec);
        insertInventory.bindValue(3, item.material);
        insertInventory.bindValue(4, item.color);
        insertInventory.bindValue(5, item.unitName);
        insertInventory.bindValue(6, item.unitPrice);
        insertInventory.bindValue(7, 20);
        insertInventory.bindValue(8, QStringLiteral("初始化样例库存"));
        insertInventory.bindValue(9, timestamp);
        if (!insertInventory.exec()) {
            m_lastError = insertInventory.lastError().text();
            return false;
        }
        return true;
    };

    const QList<QList<BaseConfigurationComponentData>> configurationGroups = {
        configAComponents, configBComponents, configCComponents, configDComponents};
    for (const QList<BaseConfigurationComponentData> &group : configurationGroups) {
        for (const BaseConfigurationComponentData &component : group) {
            InventoryItemData item;
            item.productCategoryId = feiyueCategoryId;
            item.productCategoryName = QStringLiteral("飞月");
            item.componentName = component.componentName;
            item.componentSpec = component.componentSpec;
            item.material = component.material;
            item.color = component.color;
            item.unitName = component.unitName;
            item.unitPrice = component.unitAmount;
            if (!ensureInventoryItem(item)) {
                database.rollback();
                return false;
            }
        }
    }

    for (const ProductSkuOption &sku : feiyueSkus) {
        if (sku.lampshadeName.trimmed().isEmpty()) {
            continue;
        }
        InventoryItemData item;
        item.productCategoryId = feiyueCategoryId;
        item.productCategoryName = QStringLiteral("飞月");
        item.componentName = sku.lampshadeName;
        item.unitName = QStringLiteral("件");
        item.unitPrice = sku.lampshadeUnitPrice;
        if (!ensureInventoryItem(item)) {
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
            "default_price REAL NOT NULL DEFAULT 0,"
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
            "unit_price REAL NOT NULL DEFAULT 0,"
            "FOREIGN KEY(option_template_id) REFERENCES option_templates(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS product_model_components ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "product_model_id INTEGER NOT NULL,"
            "component_name TEXT NOT NULL,"
            "unit_price REAL NOT NULL DEFAULT 0,"
            "UNIQUE(product_model_id, component_name),"
            "FOREIGN KEY(product_model_id) REFERENCES product_models(id)"
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
            "unit_price REAL NOT NULL DEFAULT 0,"
            "total_price REAL NOT NULL DEFAULT 0,"
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
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS structured_shipment_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_id INTEGER NOT NULL,"
            "order_component_id INTEGER,"
            "shipment_date TEXT NOT NULL,"
            "shipment_type TEXT NOT NULL,"
            "shipment_quantity INTEGER NOT NULL,"
            "note TEXT,"
            "created_at TEXT NOT NULL,"
            "FOREIGN KEY(order_id) REFERENCES orders(id),"
            "FOREIGN KEY(order_component_id) REFERENCES order_components(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS product_categories ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "is_active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS product_skus ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "product_category_id INTEGER NOT NULL,"
            "sku_name TEXT NOT NULL,"
            "lampshade_name TEXT NOT NULL,"
            "lampshade_unit_price REAL NOT NULL DEFAULT 0,"
            "is_active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL,"
            "UNIQUE(product_category_id, sku_name),"
            "FOREIGN KEY(product_category_id) REFERENCES product_categories(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS base_configurations ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "product_category_id INTEGER NOT NULL,"
            "config_code TEXT NOT NULL,"
            "config_name TEXT NOT NULL,"
            "config_price REAL NOT NULL DEFAULT 0,"
            "is_active INTEGER NOT NULL DEFAULT 1,"
            "sort_order INTEGER NOT NULL DEFAULT 0,"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL,"
            "UNIQUE(product_category_id, config_code),"
            "FOREIGN KEY(product_category_id) REFERENCES product_categories(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS base_configuration_components ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "base_configuration_id INTEGER NOT NULL,"
            "component_name TEXT NOT NULL,"
            "component_spec TEXT,"
            "material TEXT,"
            "color TEXT,"
            "unit_name TEXT NOT NULL DEFAULT '件',"
            "quantity INTEGER NOT NULL,"
            "unit_amount REAL NOT NULL DEFAULT 0,"
            "line_amount REAL NOT NULL DEFAULT 0,"
            "sort_order INTEGER NOT NULL DEFAULT 0,"
            "is_active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL,"
            "FOREIGN KEY(base_configuration_id) REFERENCES base_configurations(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS orders ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_date TEXT NOT NULL,"
            "customer_name TEXT NOT NULL,"
            "product_category_id INTEGER NOT NULL,"
            "product_category_name TEXT NOT NULL,"
            "product_sku_id INTEGER NOT NULL,"
            "product_sku_name TEXT NOT NULL,"
            "base_configuration_id INTEGER NOT NULL,"
            "base_configuration_name TEXT NOT NULL,"
            "order_quantity INTEGER NOT NULL,"
            "lampshade_name TEXT NOT NULL,"
            "lampshade_unit_price REAL NOT NULL DEFAULT 0,"
            "config_price REAL NOT NULL DEFAULT 0,"
            "status TEXT NOT NULL DEFAULT 'open',"
            "remark TEXT,"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL,"
            "FOREIGN KEY(product_category_id) REFERENCES product_categories(id),"
            "FOREIGN KEY(product_sku_id) REFERENCES product_skus(id),"
            "FOREIGN KEY(base_configuration_id) REFERENCES base_configurations(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS order_components ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_id INTEGER NOT NULL,"
            "source_component_id INTEGER,"
            "component_name TEXT NOT NULL,"
            "component_spec TEXT,"
            "material TEXT,"
            "color TEXT,"
            "unit_name TEXT NOT NULL DEFAULT '件',"
            "quantity_per_set INTEGER NOT NULL,"
            "required_quantity INTEGER NOT NULL,"
            "shipped_quantity INTEGER NOT NULL DEFAULT 0,"
            "unshipped_quantity INTEGER NOT NULL,"
            "unit_amount REAL NOT NULL DEFAULT 0,"
            "line_amount REAL NOT NULL DEFAULT 0,"
            "source_type TEXT NOT NULL,"
            "adjustment_type TEXT NOT NULL DEFAULT 'none',"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL,"
            "FOREIGN KEY(order_id) REFERENCES orders(id),"
            "FOREIGN KEY(source_component_id) REFERENCES base_configuration_components(id)"
            ");"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS inventory_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "product_category_id INTEGER,"
            "component_name TEXT NOT NULL,"
            "component_spec TEXT,"
            "material TEXT,"
            "color TEXT,"
            "unit_name TEXT NOT NULL DEFAULT '件',"
            "unit_price REAL NOT NULL DEFAULT 0,"
            "current_quantity INTEGER NOT NULL DEFAULT 0,"
            "note TEXT,"
            "updated_at TEXT NOT NULL"
            ");")
    };

    for (const QString &statement : statements) {
        if (!executeStatement(statement)) {
            return false;
        }
    }

    return ensureColumnExists(QStringLiteral("product_models"),
                              QStringLiteral("default_price"),
                              QStringLiteral("REAL NOT NULL DEFAULT 0"))
           && ensureColumnExists(QStringLiteral("option_template_components"),
                                 QStringLiteral("unit_price"),
                                 QStringLiteral("REAL NOT NULL DEFAULT 0"))
           && ensureColumnExists(QStringLiteral("order_item_components"),
                                 QStringLiteral("unit_price"),
                                 QStringLiteral("REAL NOT NULL DEFAULT 0"))
           && ensureColumnExists(QStringLiteral("order_item_components"),
                                 QStringLiteral("total_price"),
                                 QStringLiteral("REAL NOT NULL DEFAULT 0"))
           && ensureColumnExists(QStringLiteral("inventory_items"),
                                 QStringLiteral("product_category_id"),
                                 QStringLiteral("INTEGER"))
           && ensureColumnExists(QStringLiteral("inventory_items"),
                                 QStringLiteral("unit_price"),
                                 QStringLiteral("REAL NOT NULL DEFAULT 0"));
}

bool DatabaseManager::ensureColumnExists(const QString &tableName,
                                         const QString &columnName,
                                         const QString &columnDefinition)
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare(QStringLiteral("PRAGMA table_info(%1);").arg(tableName));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString() == columnName) {
            return true;
        }
    }

    return executeStatement(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3;")
                                .arg(tableName, columnName, columnDefinition));
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
            existing.unitPrice = qMax(existing.unitPrice, component.unitPrice);
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
