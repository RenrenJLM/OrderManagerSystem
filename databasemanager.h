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
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    double unitPrice = 0.0;
    int productCategoryId = 0;
    QString productCategoryName;
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

struct ProductCategoryOption
{
    int id = 0;
    QString name;
    bool isActive = true;
};

struct ProductSkuOption
{
    int id = 0;
    int productCategoryId = 0;
    QString productCategoryName;
    QString skuName;
    QString lampshadeName;
    double lampshadeUnitPrice = 0.0;
    bool isActive = true;
};

struct BaseConfigurationOption
{
    int id = 0;
    int productCategoryId = 0;
    QString configCode;
    QString configName;
    double configPrice = 0.0;
    int sortOrder = 0;
    bool isActive = true;
};

struct BaseConfigurationComponentData
{
    int id = 0;
    int baseConfigurationId = 0;
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    int quantity = 0;
    double unitAmount = 0.0;
    double lineAmount = 0.0;
    int sortOrder = 0;
    bool isActive = true;
};

struct StructuredOrderComponentData
{
    int sourceComponentId = 0;
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    int quantityPerSet = 0;
    double unitAmount = 0.0;
    QString sourceType;
    QString adjustmentType;
};

struct StructuredOrderSaveData
{
    QString orderDate;
    QString customerName;
    int productCategoryId = 0;
    QString productCategoryName;
    int productSkuId = 0;
    QString productSkuName;
    int baseConfigurationId = 0;
    QString baseConfigurationName;
    int orderQuantity = 0;
    QString lampshadeName;
    double lampshadeUnitPrice = 0.0;
    double configPrice = 0.0;
    QString remark;
};

struct InventoryItemData
{
    int id = 0;
    int productCategoryId = 0;
    QString productCategoryName;
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    double unitPrice = 0.0;
    int currentQuantity = 0;
    QString note;
};

struct InventoryIdentityData
{
    int productCategoryId = 0;
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
};

struct OrderMaterialDemandData
{
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    int totalUnshippedQuantity = 0;
};

struct StructuredOrderSummary
{
    int id = 0;
    QString orderDate;
    QString customerName;
    int productCategoryId = 0;
    QString productCategoryName;
    QString productSkuName;
    QString baseConfigurationName;
    int orderQuantity = 0;
    QString lampshadeName;
    double lampshadeUnitPrice = 0.0;
    double configPrice = 0.0;
    QString status;
    int availableSetShipments = 0;
    bool isCompleted = false;
    bool hasShipmentRecord = false;
    bool shipmentReady = false;
};

struct StructuredOrderQueryFilter
{
    QString startDate;
    QString endDate;
    QString customerKeyword;
    int productSkuId = 0;
    bool onlyOpen = false;
};

struct StructuredOrderComponentSnapshot
{
    int id = 0;
    int orderId = 0;
    int sourceComponentId = 0;
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    int quantityPerSet = 0;
    int requiredQuantity = 0;
    int shippedQuantity = 0;
    int unshippedQuantity = 0;
    double unitAmount = 0.0;
    double lineAmount = 0.0;
    QString sourceType;
    QString adjustmentType;
};

struct InventoryDemandSummaryRow
{
    int productCategoryId = 0;
    QString productCategoryName;
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    int totalDemandQuantity = 0;
    int currentInventoryQuantity = 0;
    int shortageQuantity = 0;
};

class DatabaseManager
{
public:
    DatabaseManager();

    bool initialize();
    bool ensureRequiredReferenceData();
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
    QList<ProductCategoryOption> productCategories();
    bool saveProductCategory(const ProductCategoryOption &category);
    QList<ProductSkuOption> productSkus(int productCategoryId = 0);
    bool saveProductSku(const ProductSkuOption &sku);
    QList<BaseConfigurationOption> baseConfigurationsForCategory(int productCategoryId);
    bool saveBaseConfiguration(const BaseConfigurationOption &configuration);
    QList<BaseConfigurationComponentData> baseConfigurationComponents(int baseConfigurationId);
    bool replaceBaseConfigurationComponents(
        int baseConfigurationId, const QList<BaseConfigurationComponentData> &components);
    bool saveStructuredOrder(const StructuredOrderSaveData &orderData,
                             const QList<StructuredOrderComponentData> &components);
    QList<StructuredOrderSummary> structuredOrders(bool onlyOpen = false);
    QList<StructuredOrderSummary> structuredOrders(const StructuredOrderQueryFilter &filter);
    QList<StructuredOrderComponentSnapshot> structuredOrderComponents(int orderId);
    QList<OrderShipmentRecord> structuredOrderShipments(int orderId);
    bool saveStructuredOrderShipment(int orderId,
                                     const QString &shipmentDate,
                                     int shipmentSets,
                                     const QString &note);
    bool saveStructuredComponentShipment(int orderId,
                                         int componentId,
                                         const QString &shipmentDate,
                                         int shipmentQuantity,
                                         const QString &note);
    QList<OrderMaterialDemandData> unshippedOrderMaterialDemands();
    QList<InventoryDemandSummaryRow> inventoryDemandSummary();
    QList<InventoryItemData> inventoryItems();
    QList<ProductComponentOption> inventoryComponentOptions(int productCategoryId = 0);
    bool saveInventoryItem(const InventoryItemData &item);
    bool isStructuredOrderShipmentReady(int orderId);
    int productCategoryIdByName(const QString &categoryName);
    int baseConfigurationIdByCategoryAndCode(int productCategoryId, const QString &configCode);
    bool upsertProductCategoryByName(const QString &categoryName, bool isActive = true);
    bool upsertProductSkuByNaturalKey(const ProductSkuOption &sku);
    bool upsertBaseConfigurationByNaturalKey(const BaseConfigurationOption &configuration);
    bool upsertInventoryItemByNaturalKey(const InventoryItemData &item);
    QString lastError() const;

private:
    bool openDatabase();
    bool enableForeignKeys();
    bool createTables();
    bool repairShipmentData();
    bool repairStructuredInventoryUnitPrices(QSqlDatabase &database);
    bool ensureMinimumComponentCatalogData(QSqlDatabase &database);
    bool ensureStructuredReferenceData(QSqlDatabase &database);
    bool ensureColumnExists(const QString &tableName,
                            const QString &columnName,
                            const QString &columnDefinition);
    bool executeStatement(const QString &statement);
    QList<OrderComponentData> mergedComponents(const QList<OrderComponentData> &components) const;
    int availableSetShipmentsForOrder(QSqlDatabase &database, int orderItemId, QString *errorMessage = nullptr) const;
    int availableSetShipmentsForStructuredOrder(QSqlDatabase &database,
                                                int orderId,
                                                QString *errorMessage = nullptr) const;
    bool syncStructuredOrderStatus(QSqlDatabase &database, int orderId);
    bool deductStructuredInventory(QSqlDatabase &database,
                                   int orderId,
                                   const StructuredOrderComponentSnapshot &component,
                                   int deductionQuantity);
    int inventoryItemIdByIdentity(QSqlDatabase &database,
                                  const InventoryIdentityData &identity,
                                  bool *duplicateFound = nullptr);
    QString bodyComponentName(const QString &productModelName) const;
    double productDefaultPrice(QSqlDatabase &database, const QString &productModelName, QString *errorMessage = nullptr) const;

    QString m_lastError;
};

#endif // DATABASEMANAGER_H
