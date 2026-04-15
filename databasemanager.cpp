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
#include <QtGlobal>

namespace {
const char kConnectionName[] = "ordermanager_connection";
const char kDatabaseFileName[] = "OrderManagerSystem.db";
const char kBodySourceType[] = "system_body";

QString currentTimestamp()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
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

    if (!database.commit()) {
        m_lastError = database.lastError().text();
        database.rollback();
        return false;
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
