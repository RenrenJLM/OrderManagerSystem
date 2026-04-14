#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QList>
#include <QString>

struct ProductModelOption
{
    int id = 0;
    QString name;
};

struct TemplateOption
{
    int id = 0;
    QString name;
};

struct OrderComponentData
{
    QString componentName;
    int quantityPerSet = 0;
    QString sourceType;
};

struct OrderSaveData
{
    QString orderDate;
    QString customerName;
    QString productModelName;
    int quantitySets = 0;
    double unitPrice = 0.0;
    QString configurationName;
};

class DatabaseManager
{
public:
    DatabaseManager();

    bool initialize();
    bool ensureMinimumDemoData();
    QList<ProductModelOption> productModels();
    QList<TemplateOption> optionTemplatesForProduct(int productModelId);
    QList<OrderComponentData> templateComponents(int templateId);
    bool saveOrder(const OrderSaveData &orderData, const QList<OrderComponentData> &components);
    QString lastError() const;

private:
    bool openDatabase();
    bool enableForeignKeys();
    bool createTables();
    bool executeStatement(const QString &statement);
    QList<OrderComponentData> mergedComponents(const QList<OrderComponentData> &components) const;

    QString m_lastError;
};

#endif // DATABASEMANAGER_H
