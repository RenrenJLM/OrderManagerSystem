#include "dataimporter.h"

#include <QFile>
#include <QHash>
#include <QSet>
#include <initializer_list>
#include <QStringConverter>
#include <QTextStream>

namespace {
struct ImportHeaders
{
    QString name;
    QString isActive;

    QString productCategoryName;
    QString skuName;
    QString lampshadeName;
    QString lampshadeUnitPrice;

    QString configCode;
    QString configName;
    QString configPrice;
    QString sortOrder;

    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    QString quantity;
    QString unitAmount;

    QString unitPrice;
    QString inboundQuantity;
    QString outboundQuantity;
    QString currentQuantity;
    QString note;
};

const ImportHeaders kHeaders{
    QStringLiteral("名称"),
    QStringLiteral("是否启用"),

    QStringLiteral("产品类型"),
    QStringLiteral("具体型号"),
    QStringLiteral("默认灯罩"),
    QStringLiteral("灯罩单价"),

    QStringLiteral("配置代码"),
    QStringLiteral("配置名称"),
    QStringLiteral("配置价格"),
    QStringLiteral("排序"),

    QStringLiteral("组件名称"),
    QStringLiteral("规格"),
    QStringLiteral("材质"),
    QStringLiteral("颜色"),
    QStringLiteral("单位"),
    QStringLiteral("数量"),
    QStringLiteral("单价"),

    QStringLiteral("单价"),
    QStringLiteral("入库数量"),
    QStringLiteral("出库数量"),
    QStringLiteral("当前库存"),
    QStringLiteral("备注")
};

QString normalizedCell(const QStringList &row, int index)
{
    if (index < 0 || index >= row.size()) {
        return QString();
    }
    return row.at(index).trimmed();
}

QString configGroupKey(int productCategoryId, const QString &configCode)
{
    return QString::number(productCategoryId) + QLatin1Char('\x1f') + configCode.trimmed();
}

QString productSkuIdentityKey(int productCategoryId,
                              const QString &skuName,
                              const QString &lampshadeName,
                              double lampshadeUnitPrice)
{
    return QString::number(productCategoryId) + QLatin1Char('\x1f')
           + skuName.trimmed() + QLatin1Char('\x1f')
           + lampshadeName.trimmed() + QLatin1Char('\x1f')
           + QString::number(lampshadeUnitPrice, 'f', 4);
}

QString joinColumnNames(std::initializer_list<QString> columnNames)
{
    QStringList parts;
    for (const QString &columnName : columnNames) {
        parts.append(columnName);
    }
    return parts.join(QStringLiteral("、"));
}
}

QString DataImporter::ImportResult::summaryText() const
{
    return QStringLiteral("成功 %1，跳过 %2，失败 %3")
        .arg(successCount)
        .arg(skippedCount)
        .arg(failedCount);
}

DataImporter::DataImporter(DatabaseManager *databaseManager)
    : m_databaseManager(databaseManager)
{
}

DataImporter::ImportResult DataImporter::importCsv(ImportTarget target, const QString &filePath)
{
    ImportResult result;
    if (m_databaseManager == nullptr) {
        result.failedCount = 1;
        result.failureReasons.append(QStringLiteral("数据库管理器不可用。"));
        return result;
    }

    QString errorMessage;
    const ParsedCsv csv = parseCsvFile(filePath, &errorMessage);
    if (!errorMessage.isEmpty()) {
        result.failedCount = 1;
        result.failureReasons.append(errorMessage);
        return result;
    }

    switch (target) {
    case ImportTarget::ProductCategories:
        return importProductCategories(csv);
    case ImportTarget::ProductSkus:
        return importProductSkus(csv);
    case ImportTarget::BaseConfigurations:
        return importBaseConfigurations(csv);
    case ImportTarget::BaseConfigurationBom:
        return importBaseConfigurationBom(csv);
    case ImportTarget::InventoryItems:
        return importInventoryItems(csv);
    }

    result.failedCount = 1;
    result.failureReasons.append(QStringLiteral("不支持的导入类型。"));
    return result;
}

DataImporter::ParsedCsv DataImporter::parseCsvFile(const QString &filePath,
                                                   QString *errorMessage) const
{
    ParsedCsv csv;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法打开文件：%1").arg(filePath);
        }
        return csv;
    }

    QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#else
    stream.setEncoding(QStringConverter::Utf8);
#endif

    bool headerParsed = false;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (!line.isEmpty() && line.at(0) == QChar(0xFEFF)) {
            line.remove(0, 1);
        }
        if (!headerParsed) {
            if (line.trimmed().isEmpty()) {
                continue;
            }
            csv.headers = parseCsvLine(line);
            for (QString &header : csv.headers) {
                header = header.trimmed();
            }
            headerParsed = true;
            continue;
        }
        csv.rows.append(parseCsvLine(line));
    }

    if (!headerParsed) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("CSV 文件为空。");
        }
        return ParsedCsv();
    }

    return csv;
}

QStringList DataImporter::parseCsvLine(const QString &line) const
{
    QStringList fields;
    QString currentField;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (inQuotes && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                currentField += QLatin1Char('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == QLatin1Char(',') && !inQuotes) {
            fields.append(currentField);
            currentField.clear();
            continue;
        }

        currentField += ch;
    }

    fields.append(currentField);
    return fields;
}

int DataImporter::columnIndex(const QStringList &headers, const QString &columnName) const
{
    for (int i = 0; i < headers.size(); ++i) {
        if (headers.at(i).trimmed() == columnName) {
            return i;
        }
    }
    return -1;
}

bool DataImporter::parseBooleanField(const QString &value, bool defaultValue, bool *ok) const
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        if (ok != nullptr) {
            *ok = true;
        }
        return defaultValue;
    }
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true")) {
        if (ok != nullptr) {
            *ok = true;
        }
        return true;
    }
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false")) {
        if (ok != nullptr) {
            *ok = true;
        }
        return false;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return defaultValue;
}

bool DataImporter::parseIntegerField(const QString &value, int *result) const
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    if (ok && result != nullptr) {
        *result = parsed;
    }
    return ok;
}

bool DataImporter::parseNonNegativeIntegerField(const QString &value, int *result) const
{
    int parsed = 0;
    if (!parseIntegerField(value, &parsed) || parsed < 0) {
        return false;
    }
    if (result != nullptr) {
        *result = parsed;
    }
    return true;
}

bool DataImporter::parseDoubleField(const QString &value, double *result) const
{
    bool ok = false;
    const double parsed = value.trimmed().toDouble(&ok);
    if (ok && result != nullptr) {
        *result = parsed;
    }
    return ok;
}

bool DataImporter::parseNonNegativeDoubleField(const QString &value, double *result) const
{
    double parsed = 0.0;
    if (!parseDoubleField(value, &parsed) || parsed < 0.0) {
        return false;
    }
    if (result != nullptr) {
        *result = parsed;
    }
    return true;
}

bool DataImporter::normalizeOptionalMiddleColumns(QStringList *columns, int expectedCount) const
{
    if (columns == nullptr || expectedCount < 0) {
        return false;
    }

    while (columns->size() > expectedCount && !columns->isEmpty() && columns->first().trimmed().isEmpty()) {
        columns->removeFirst();
    }

    if (columns->size() > expectedCount) {
        return false;
    }

    while (columns->size() < expectedCount) {
        columns->append(QString());
    }

    return true;
}

QString DataImporter::formatRowError(int rowNumber, const QString &reason) const
{
    return QStringLiteral("第 %1 行：%2").arg(rowNumber).arg(reason);
}

DataImporter::ImportResult DataImporter::importProductCategories(const ParsedCsv &csv)
{
    ImportResult result;
    const int nameIndex = columnIndex(csv.headers, kHeaders.name);
    const int isActiveIndex = columnIndex(csv.headers, kHeaders.isActive);
    if (nameIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(QStringLiteral("列头必须包含 %1。").arg(kHeaders.name));
        return result;
    }

    for (int i = 0; i < csv.rows.size(); ++i) {
        const QStringList &row = csv.rows.at(i);
        const int rowNumber = i + 2;
        const QString name = normalizedCell(row, nameIndex);
        if (name.isEmpty()) {
            ++result.skippedCount;
            continue;
        }

        bool boolOk = true;
        const bool isActive = parseBooleanField(normalizedCell(row, isActiveIndex), true, &boolOk);
        if (!boolOk) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 只能是 0/1/true/false。").arg(kHeaders.isActive)));
            continue;
        }

        if (!m_databaseManager->upsertProductCategoryByName(name, isActive)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, m_databaseManager->lastError()));
            continue;
        }

        ++result.successCount;
    }

    return result;
}

DataImporter::ImportResult DataImporter::importProductSkus(const ParsedCsv &csv)
{
    ImportResult result;
    const int categoryNameIndex = columnIndex(csv.headers, kHeaders.productCategoryName);
    const int skuNameIndex = columnIndex(csv.headers, kHeaders.skuName);
    const int lampshadeNameIndex = columnIndex(csv.headers, kHeaders.lampshadeName);
    const int lampshadePriceIndex = columnIndex(csv.headers, kHeaders.lampshadeUnitPrice);
    const int isActiveIndex = columnIndex(csv.headers, kHeaders.isActive);
    if (categoryNameIndex < 0 || skuNameIndex < 0 || lampshadeNameIndex < 0 || lampshadePriceIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(QStringLiteral("列头必须包含 %1。")
                                         .arg(joinColumnNames({kHeaders.productCategoryName,
                                                               kHeaders.skuName,
                                                               kHeaders.lampshadeName,
                                                               kHeaders.lampshadeUnitPrice})));
        return result;
    }

    QSet<QString> importedIdentityKeys;
    for (int i = 0; i < csv.rows.size(); ++i) {
        const QStringList &row = csv.rows.at(i);
        const int rowNumber = i + 2;
        const QString categoryName = normalizedCell(row, categoryNameIndex);
        const QString skuName = normalizedCell(row, skuNameIndex);
        const QString lampshadeName = normalizedCell(row, lampshadeNameIndex);
        if (categoryName.isEmpty() && skuName.isEmpty() && lampshadeName.isEmpty()) {
            ++result.skippedCount;
            continue;
        }
        if (categoryName.isEmpty() || skuName.isEmpty() || lampshadeName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型、具体型号、默认灯罩不能为空。")));
            continue;
        }

        double lampshadeUnitPrice = 0.0;
        if (!parseNonNegativeDoubleField(normalizedCell(row, lampshadePriceIndex), &lampshadeUnitPrice)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("灯罩单价必须是大于等于 0 的数字。")));
            continue;
        }

        const int categoryId = m_databaseManager->productCategoryIdByName(categoryName);
        if (categoryId <= 0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型不存在：%1").arg(categoryName)));
            continue;
        }

        bool boolOk = true;
        const bool isActive = parseBooleanField(normalizedCell(row, isActiveIndex), true, &boolOk);
        if (!boolOk) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 只能是 0/1/true/false。").arg(kHeaders.isActive)));
            continue;
        }

        ProductSkuOption sku;
        sku.productCategoryId = categoryId;
        sku.skuName = skuName;
        sku.lampshadeName = lampshadeName;
        sku.lampshadeUnitPrice = lampshadeUnitPrice;
        sku.isActive = isActive;
        const QString identityKey =
            productSkuIdentityKey(categoryId, sku.skuName, sku.lampshadeName, sku.lampshadeUnitPrice);
        if (importedIdentityKeys.contains(identityKey)) {
            ++result.skippedCount;
            continue;
        }
        importedIdentityKeys.insert(identityKey);
        if (!m_databaseManager->upsertProductSkuByNaturalKey(sku)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, m_databaseManager->lastError()));
            continue;
        }

        ++result.successCount;
    }

    return result;
}

DataImporter::ImportResult DataImporter::importBaseConfigurations(const ParsedCsv &csv)
{
    ImportResult result;
    const int categoryNameIndex = columnIndex(csv.headers, kHeaders.productCategoryName);
    const int configCodeIndex = columnIndex(csv.headers, kHeaders.configCode);
    const int configNameIndex = columnIndex(csv.headers, kHeaders.configName);
    const int sortOrderIndex = columnIndex(csv.headers, kHeaders.sortOrder);
    const int isActiveIndex = columnIndex(csv.headers, kHeaders.isActive);
    if (categoryNameIndex < 0 || configNameIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(
            QStringLiteral("列头必须包含 %1。%2、%3 可留空。")
                .arg(joinColumnNames({kHeaders.productCategoryName, kHeaders.configName}),
                     kHeaders.configCode,
                     kHeaders.configPrice));
        return result;
    }

    for (int i = 0; i < csv.rows.size(); ++i) {
        const QStringList &row = csv.rows.at(i);
        const int rowNumber = i + 2;
        const QString categoryName = normalizedCell(row, categoryNameIndex);
        const QString configCode = normalizedCell(row, configCodeIndex);
        const QString configName = normalizedCell(row, configNameIndex);
        if (categoryName.isEmpty() && configCode.isEmpty() && configName.isEmpty()) {
            ++result.skippedCount;
            continue;
        }
        if (categoryName.isEmpty() || configName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型、配置名称不能为空。")));
            continue;
        }

        const int categoryId = m_databaseManager->productCategoryIdByName(categoryName);
        if (categoryId <= 0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型不存在：%1").arg(categoryName)));
            continue;
        }

        int sortOrder = i + 1;
        const QString sortOrderText = normalizedCell(row, sortOrderIndex);
        if (!sortOrderText.isEmpty() && !parseNonNegativeIntegerField(sortOrderText, &sortOrder)) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 必须是大于等于 0 的整数。").arg(kHeaders.sortOrder)));
            continue;
        }

        bool boolOk = true;
        const bool isActive = parseBooleanField(normalizedCell(row, isActiveIndex), true, &boolOk);
        if (!boolOk) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 只能是 0/1/true/false。").arg(kHeaders.isActive)));
            continue;
        }

        BaseConfigurationOption configuration;
        configuration.productCategoryId = categoryId;
        configuration.configCode = configCode;
        configuration.configName = configName;
        configuration.configPrice = 0.0;
        configuration.sortOrder = sortOrder;
        configuration.isActive = isActive;
        if (!m_databaseManager->upsertBaseConfigurationByNaturalKey(configuration)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, m_databaseManager->lastError()));
            continue;
        }

        ++result.successCount;
    }

    return result;
}

DataImporter::ImportResult DataImporter::importBaseConfigurationBom(const ParsedCsv &csv)
{
    ImportResult result;
    const int categoryNameIndex = columnIndex(csv.headers, kHeaders.productCategoryName);
    const int configCodeIndex = columnIndex(csv.headers, kHeaders.configCode);
    const int componentNameIndex = columnIndex(csv.headers, kHeaders.componentName);
    const int quantityIndex = columnIndex(csv.headers, kHeaders.quantity);
    const int unitAmountIndex = columnIndex(csv.headers, kHeaders.unitAmount);
    if (categoryNameIndex < 0 || configCodeIndex < 0 || componentNameIndex < 0 || quantityIndex < 0
        || unitAmountIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(QStringLiteral("列头必须包含 %1。")
                                         .arg(joinColumnNames({kHeaders.productCategoryName,
                                                               kHeaders.configCode,
                                                               kHeaders.componentName,
                                                               kHeaders.quantity,
                                                               kHeaders.unitAmount})));
        return result;
    }

    struct BomGroup {
        int baseConfigurationId = 0;
        int firstRowNumber = 0;
        QList<BaseConfigurationComponentData> components;
    };

    QHash<QString, BomGroup> groups;
    bool hasValidationError = false;

    for (int i = 0; i < csv.rows.size(); ++i) {
        const QStringList &row = csv.rows.at(i);
        const int rowNumber = i + 2;
        const QString categoryName = normalizedCell(row, categoryNameIndex);
        const QString configCode = normalizedCell(row, configCodeIndex);
        const QString componentName = normalizedCell(row, componentNameIndex);
        if (categoryName.isEmpty() && configCode.isEmpty() && componentName.isEmpty()) {
            ++result.skippedCount;
            continue;
        }
        if (categoryName.isEmpty() || configCode.isEmpty() || componentName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型、配置代码、组件名称不能为空。")));
            hasValidationError = true;
            continue;
        }

        const int categoryId = m_databaseManager->productCategoryIdByName(categoryName);
        if (categoryId <= 0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型不存在：%1").arg(categoryName)));
            hasValidationError = true;
            continue;
        }

        const int baseConfigurationId =
            m_databaseManager->baseConfigurationIdByCategoryAndCode(categoryId, configCode);
        if (baseConfigurationId <= 0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("基础配置不存在：%1 / %2").arg(categoryName, configCode)));
            hasValidationError = true;
            continue;
        }

        if (row.size() < 7) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("BOM 行缺少关键列，至少需要组件名称、单位、数量、金额、排序。")));
            hasValidationError = true;
            continue;
        }

        QStringList middleColumns = row.mid(3, row.size() - 7);
        if (!normalizeOptionalMiddleColumns(&middleColumns, 3)) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("BOM 描述列过多，无法确定规格、材质、颜色的映射。")));
            hasValidationError = true;
            continue;
        }

        const QString unitName = row.at(row.size() - 4).trimmed();
        const QString quantityText = row.at(row.size() - 3).trimmed();
        const QString unitAmountText = row.at(row.size() - 2).trimmed();
        const QString sortOrderText = row.at(row.size() - 1).trimmed();

        if (unitName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 不能为空。").arg(kHeaders.unitName)));
            hasValidationError = true;
            continue;
        }

        int quantity = 0;
        if (!parseIntegerField(quantityText, &quantity) || quantity <= 0) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 必须是大于 0 的整数。").arg(kHeaders.quantity)));
            hasValidationError = true;
            continue;
        }

        double unitAmount = 0.0;
        if (!parseNonNegativeDoubleField(unitAmountText, &unitAmount)) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 必须是大于等于 0 的数字。").arg(kHeaders.unitAmount)));
            hasValidationError = true;
            continue;
        }

        BaseConfigurationComponentData component;
        component.baseConfigurationId = baseConfigurationId;
        component.componentName = componentName;
        component.componentSpec = middleColumns.value(0).trimmed();
        component.material = middleColumns.value(1).trimmed();
        component.color = middleColumns.value(2).trimmed();
        component.unitName = unitName;
        component.quantity = quantity;
        component.unitAmount = unitAmount;
        if (sortOrderText.isEmpty()) {
            component.sortOrder = 0;
        } else if (!parseNonNegativeIntegerField(sortOrderText, &component.sortOrder)) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 必须是大于等于 0 的整数。").arg(kHeaders.sortOrder)));
            hasValidationError = true;
            continue;
        }

        const QString key = configGroupKey(categoryId, configCode);
        BomGroup &group = groups[key];
        group.baseConfigurationId = baseConfigurationId;
        if (group.firstRowNumber <= 0) {
            group.firstRowNumber = rowNumber;
        }
        group.components.append(component);
    }

    if (hasValidationError) {
        return result;
    }

    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const BomGroup &group = it.value();
        if (group.components.isEmpty()) {
            ++result.skippedCount;
            continue;
        }
        if (!m_databaseManager->replaceBaseConfigurationComponents(group.baseConfigurationId,
                                                                   group.components)) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(group.firstRowNumber, m_databaseManager->lastError()));
            continue;
        }
        result.successCount += group.components.size();
    }

    return result;
}

DataImporter::ImportResult DataImporter::importInventoryItems(const ParsedCsv &csv)
{
    ImportResult result;
    const int categoryNameIndex = columnIndex(csv.headers, kHeaders.productCategoryName);
    const int componentNameIndex = columnIndex(csv.headers, kHeaders.componentName);
    const int componentSpecIndex = columnIndex(csv.headers, kHeaders.componentSpec);
    const int materialIndex = columnIndex(csv.headers, kHeaders.material);
    const int colorIndex = columnIndex(csv.headers, kHeaders.color);
    const int unitNameIndex = columnIndex(csv.headers, kHeaders.unitName);
    const int unitPriceIndex = columnIndex(csv.headers, kHeaders.unitPrice);
    const int noteIndex = columnIndex(csv.headers, kHeaders.note);
    const int inboundQuantityIndex = columnIndex(csv.headers, kHeaders.inboundQuantity);
    const int outboundQuantityIndex = columnIndex(csv.headers, kHeaders.outboundQuantity);
    const int legacyQuantityIndex = columnIndex(csv.headers, kHeaders.currentQuantity);
    if (componentNameIndex < 0 || unitPriceIndex < 0
        || (inboundQuantityIndex < 0 && outboundQuantityIndex < 0 && legacyQuantityIndex < 0)) {
        result.failedCount = 1;
        result.failureReasons.append(
            QStringLiteral("列头必须包含 %1、%2，且至少包含 %3、%4、%5 之一。")
                .arg(kHeaders.componentName,
                     kHeaders.unitPrice,
                     kHeaders.inboundQuantity,
                     kHeaders.outboundQuantity,
                     kHeaders.currentQuantity));
        return result;
    }

    for (int i = 0; i < csv.rows.size(); ++i) {
        const QStringList &row = csv.rows.at(i);
        const int rowNumber = i + 2;
        QString categoryName = normalizedCell(row, categoryNameIndex);
        const QString componentName = normalizedCell(row, componentNameIndex);
        if (categoryName.isEmpty() && componentName.isEmpty()) {
            ++result.skippedCount;
            continue;
        }
        if (componentName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("物料名称不能为空。")));
            continue;
        }
        if (categoryName.isEmpty()) {
            categoryName = QStringLiteral("通用");
        }
        const QString unitName = normalizedCell(row, unitNameIndex);
        const QString unitPriceText = normalizedCell(row, unitPriceIndex);
        const QString legacyQuantityText =
            legacyQuantityIndex >= 0 ? normalizedCell(row, legacyQuantityIndex) : QString();
        const QString note = normalizedCell(row, noteIndex);

        if (unitName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 不能为空。").arg(kHeaders.unitName)));
            continue;
        }

        double unitPrice = 0.0;
        if (!parseDoubleField(unitPriceText, &unitPrice) || unitPrice <= 0.0) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("%1 必须是大于 0 的数字。").arg(kHeaders.unitPrice)));
            continue;
        }

        int inboundQuantity = 0;
        int outboundQuantity = 0;
        const QString inboundText =
            inboundQuantityIndex >= 0 ? normalizedCell(row, inboundQuantityIndex) : legacyQuantityText;
        const QString outboundText =
            outboundQuantityIndex >= 0 ? normalizedCell(row, outboundQuantityIndex) : QString();

        if (!inboundText.isEmpty() && !parseNonNegativeIntegerField(inboundText, &inboundQuantity)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(
                rowNumber, QStringLiteral("%1 必须是大于等于 0 的整数。").arg(kHeaders.inboundQuantity)));
            continue;
        }
        if (!outboundText.isEmpty() && !parseNonNegativeIntegerField(outboundText, &outboundQuantity)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(
                rowNumber, QStringLiteral("%1 必须是大于等于 0 的整数。").arg(kHeaders.outboundQuantity)));
            continue;
        }

        int categoryId = m_databaseManager->productCategoryIdByName(categoryName);
        if (categoryId <= 0 && !m_databaseManager->upsertProductCategoryByName(categoryName)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, m_databaseManager->lastError()));
            continue;
        }
        categoryId = m_databaseManager->productCategoryIdByName(categoryName);
        if (categoryId <= 0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型不存在：%1").arg(categoryName)));
            continue;
        }

        InventoryItemData item;
        item.productCategoryId = categoryId;
        item.componentName = componentName;
        item.componentSpec = normalizedCell(row, componentSpecIndex);
        item.material = normalizedCell(row, materialIndex);
        item.color = normalizedCell(row, colorIndex);
        item.unitName = unitName;
        item.unitPrice = unitPrice;
        item.currentQuantity = 0;
        item.inboundQuantity = inboundQuantity;
        item.outboundQuantity = outboundQuantity;
        item.note = note;
        if (!m_databaseManager->upsertInventoryItemByNaturalKey(item)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, m_databaseManager->lastError()));
            continue;
        }

        ++result.successCount;
    }

    return result;
}
