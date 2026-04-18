#ifndef DATAIMPORTER_H
#define DATAIMPORTER_H

#include <QString>
#include <QStringList>

#include "databasemanager.h"

class DataImporter
{
public:
    enum class ImportTarget
    {
        ProductCategories,
        ProductSkus,
        BaseConfigurations,
        BaseConfigurationBom,
        InventoryItems
    };

    struct ImportResult
    {
        int successCount = 0;
        int skippedCount = 0;
        int failedCount = 0;
        QStringList failureReasons;
        QString summaryText() const;
    };

    explicit DataImporter(DatabaseManager *databaseManager);

    ImportResult importCsv(ImportTarget target, const QString &filePath);

private:
    struct ParsedCsv
    {
        QStringList headers;
        QList<QStringList> rows;
    };

    ParsedCsv parseCsvFile(const QString &filePath, QString *errorMessage) const;
    QStringList parseCsvLine(const QString &line) const;
    int columnIndex(const QStringList &headers, const QString &columnName) const;
    bool parseBooleanField(const QString &value, bool defaultValue, bool *ok) const;
    bool parseIntegerField(const QString &value, int *result) const;
    bool parseNonNegativeIntegerField(const QString &value, int *result) const;
    bool parseDoubleField(const QString &value, double *result) const;
    bool parseNonNegativeDoubleField(const QString &value, double *result) const;
    bool normalizeOptionalMiddleColumns(QStringList *columns, int expectedCount) const;
    QString formatRowError(int rowNumber, const QString &reason) const;

    ImportResult importProductCategories(const ParsedCsv &csv);
    ImportResult importProductSkus(const ParsedCsv &csv);
    ImportResult importBaseConfigurations(const ParsedCsv &csv);
    ImportResult importBaseConfigurationBom(const ParsedCsv &csv);
    ImportResult importInventoryItems(const ParsedCsv &csv);

    DatabaseManager *m_databaseManager = nullptr;
};

#endif // DATAIMPORTER_H
