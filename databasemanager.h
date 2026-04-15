#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct ProductModelOption
{
    int id = 0;
    QString name;
    double defaultPrice = 0.0;
};

struct TemplateOption
{
    int id = 0;
    QString name;
};

struct ProductComponentOption
{
    int id = 0;
    QString name;
    double unitPrice = 0.0;
};

struct OrderComponentData
{
    QString componentName;
    int quantityPerSet = 0;
    double unitPrice = 0.0;
    QString sourceType;
};

struct OrderSaveData
{
    QString orderDate;
    QString customerName;
    QString productModelName;
    int quantitySets = 0;
    double bodyUnitPrice = 0.0;
    double unitPrice = 0.0;
    QString configurationName;
};

struct ShipmentOrderSummary
{
    int id = 0;
    QString orderDate;
    QString customerName;
    QString productModelName;
    QString configurationName;
    int quantitySets = 0;
    int shippedSets = 0;
    int unshippedSets = 0;
    int availableSetShipments = 0;
    bool isCompleted = false;
    double unitPrice = 0.0;
    double totalPrice = 0.0;
};

struct ShipmentComponentStatus
{
    int id = 0;
    int orderItemId = 0;
    QString componentName;
    int quantityPerSet = 0;
    int totalRequiredQuantity = 0;
    int shippedQuantity = 0;
    int unshippedQuantity = 0;
    double unitPrice = 0.0;
    double totalPrice = 0.0;
    QString sourceType;
    bool isBodyComponent = false;
};

struct OrderShipmentRecord
{
    QString shipmentDate;
    QString shipmentType;
    int shipmentQuantity = 0;
    QString note;
};

class DatabaseManager
{
public:
    DatabaseManager();

    bool initialize();
    bool ensureMinimumDemoData();
    QList<ProductModelOption> productModels();
    QList<ProductComponentOption> productModelComponents(int productModelId);
    QList<TemplateOption> optionTemplatesForProduct(int productModelId);
    QList<OrderComponentData> templateComponents(int templateId);
    bool saveOrder(const OrderSaveData &orderData, const QList<OrderComponentData> &components);
    QList<ShipmentOrderSummary> queryOrders(const QString &customerKeyword,
                                           const QString &productModelName,
                                           bool onlyUnfinished);
    QList<ShipmentComponentStatus> orderComponents(int orderItemId);
    QList<OrderShipmentRecord> orderShipments(int orderItemId);
    QList<ShipmentOrderSummary> shipmentOrders();
    QList<ShipmentComponentStatus> shipmentComponents(int orderItemId);
    bool saveOrderShipment(int orderItemId,
                           const QString &shipmentDate,
                           int shipmentSets,
                           const QString &note);
    bool saveComponentShipment(int orderItemId,
                               int componentId,
                               const QString &shipmentDate,
                               int shipmentQuantity,
                               const QString &note);
    QString lastError() const;

private:
    bool openDatabase();
    bool enableForeignKeys();
    bool createTables();
    bool repairShipmentData();
    bool ensureMinimumComponentCatalogData(QSqlDatabase &database);
    bool ensureColumnExists(const QString &tableName,
                            const QString &columnName,
                            const QString &columnDefinition);
    bool executeStatement(const QString &statement);
    QList<OrderComponentData> mergedComponents(const QList<OrderComponentData> &components) const;
    int availableSetShipmentsForOrder(QSqlDatabase &database, int orderItemId, QString *errorMessage = nullptr) const;
    QString bodyComponentName(const QString &productModelName) const;
    double productDefaultPrice(QSqlDatabase &database, const QString &productModelName, QString *errorMessage = nullptr) const;

    QString m_lastError;
};

#endif // DATABASEMANAGER_H
