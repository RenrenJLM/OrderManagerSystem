#include "dataimporter.h"

#include <QFile>
#include <QHash>
#include <QStringConverter>
#include <QTextStream>

namespace {
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
    const int nameIndex = columnIndex(csv.headers, QStringLiteral("name"));
    const int isActiveIndex = columnIndex(csv.headers, QStringLiteral("is_active"));
    if (nameIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(QStringLiteral("列头必须包含 name。"));
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
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("is_active 只能是 0/1/true/false。")));
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
    const int categoryNameIndex = columnIndex(csv.headers, QStringLiteral("product_category_name"));
    const int skuNameIndex = columnIndex(csv.headers, QStringLiteral("sku_name"));
    const int lampshadeNameIndex = columnIndex(csv.headers, QStringLiteral("lampshade_name"));
    const int lampshadePriceIndex = columnIndex(csv.headers, QStringLiteral("lampshade_unit_price"));
    const int isActiveIndex = columnIndex(csv.headers, QStringLiteral("is_active"));
    if (categoryNameIndex < 0 || skuNameIndex < 0 || lampshadeNameIndex < 0 || lampshadePriceIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(
            QStringLiteral("列头必须包含 product_category_name, sku_name, lampshade_name, lampshade_unit_price。"));
        return result;
    }

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
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("is_active 只能是 0/1/true/false。")));
            continue;
        }

        ProductSkuOption sku;
        sku.productCategoryId = categoryId;
        sku.skuName = skuName;
        sku.lampshadeName = lampshadeName;
        sku.lampshadeUnitPrice = lampshadeUnitPrice;
        sku.isActive = isActive;
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
    const int categoryNameIndex = columnIndex(csv.headers, QStringLiteral("product_category_name"));
    const int configCodeIndex = columnIndex(csv.headers, QStringLiteral("config_code"));
    const int configNameIndex = columnIndex(csv.headers, QStringLiteral("config_name"));
    const int configPriceIndex = columnIndex(csv.headers, QStringLiteral("config_price"));
    const int sortOrderIndex = columnIndex(csv.headers, QStringLiteral("sort_order"));
    const int isActiveIndex = columnIndex(csv.headers, QStringLiteral("is_active"));
    if (categoryNameIndex < 0 || configCodeIndex < 0 || configNameIndex < 0 || configPriceIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(
            QStringLiteral("列头必须包含 product_category_name, config_code, config_name, config_price。"));
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
        if (categoryName.isEmpty() || configCode.isEmpty() || configName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型、配置代码、配置名称不能为空。")));
            continue;
        }

        double configPrice = 0.0;
        if (!parseNonNegativeDoubleField(normalizedCell(row, configPriceIndex), &configPrice)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("配置价格必须是大于等于 0 的数字。")));
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
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("sort_order 必须是大于等于 0 的整数。")));
            continue;
        }

        bool boolOk = true;
        const bool isActive = parseBooleanField(normalizedCell(row, isActiveIndex), true, &boolOk);
        if (!boolOk) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("is_active 只能是 0/1/true/false。")));
            continue;
        }

        BaseConfigurationOption configuration;
        configuration.productCategoryId = categoryId;
        configuration.configCode = configCode;
        configuration.configName = configName;
        configuration.configPrice = configPrice;
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
    const int categoryNameIndex = columnIndex(csv.headers, QStringLiteral("product_category_name"));
    const int configCodeIndex = columnIndex(csv.headers, QStringLiteral("config_code"));
    const int componentNameIndex = columnIndex(csv.headers, QStringLiteral("component_name"));
    const int quantityIndex = columnIndex(csv.headers, QStringLiteral("quantity"));
    const int unitAmountIndex = columnIndex(csv.headers, QStringLiteral("unit_amount"));
    if (categoryNameIndex < 0 || configCodeIndex < 0 || componentNameIndex < 0 || quantityIndex < 0
        || unitAmountIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(
            QStringLiteral("列头必须包含 product_category_name, config_code, component_name, quantity, unit_amount。"));
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
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("unit_name 不能为空。")));
            hasValidationError = true;
            continue;
        }

        int quantity = 0;
        if (!parseIntegerField(quantityText, &quantity) || quantity <= 0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("quantity 必须是大于 0 的整数。")));
            hasValidationError = true;
            continue;
        }

        double unitAmount = 0.0;
        if (!parseNonNegativeDoubleField(unitAmountText, &unitAmount)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("unit_amount 必须是大于等于 0 的数字。")));
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
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("sort_order 必须是大于等于 0 的整数。")));
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
    const int categoryNameIndex = columnIndex(csv.headers, QStringLiteral("product_category_name"));
    const int componentNameIndex = columnIndex(csv.headers, QStringLiteral("component_name"));
    const int unitPriceIndex = columnIndex(csv.headers, QStringLiteral("unit_price"));
    const int quantityIndex = columnIndex(csv.headers, QStringLiteral("current_quantity"));
    if (categoryNameIndex < 0 || componentNameIndex < 0 || unitPriceIndex < 0 || quantityIndex < 0) {
        result.failedCount = 1;
        result.failureReasons.append(
            QStringLiteral("列头必须包含 product_category_name, component_name, unit_price, current_quantity。"));
        return result;
    }

    for (int i = 0; i < csv.rows.size(); ++i) {
        const QStringList &row = csv.rows.at(i);
        const int rowNumber = i + 2;
        const QString categoryName = normalizedCell(row, categoryNameIndex);
        const QString componentName = normalizedCell(row, componentNameIndex);
        if (categoryName.isEmpty() && componentName.isEmpty()) {
            ++result.skippedCount;
            continue;
        }
        if (categoryName.isEmpty() || componentName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型、物料名称不能为空。")));
            continue;
        }

        if (row.size() < 5) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("库存行缺少关键列，至少需要物料名称、单位、单价、库存。")));
            continue;
        }

        QString note;
        QStringList middleColumns;
        QString unitName;
        QString unitPriceText;
        QString quantityText;
        if (row.size() >= 6) {
            note = row.at(row.size() - 1).trimmed();
            quantityText = row.at(row.size() - 2).trimmed();
            unitPriceText = row.at(row.size() - 3).trimmed();
            unitName = row.at(row.size() - 4).trimmed();
            middleColumns = row.mid(2, row.size() - 6);
        } else {
            quantityText = row.at(row.size() - 1).trimmed();
            unitPriceText = row.at(row.size() - 2).trimmed();
            unitName = row.at(row.size() - 3).trimmed();
            middleColumns = row.mid(2, row.size() - 5);
        }

        if (!normalizeOptionalMiddleColumns(&middleColumns, 3)) {
            ++result.failedCount;
            result.failureReasons.append(
                formatRowError(rowNumber, QStringLiteral("库存描述列过多，无法确定规格、材质、颜色的映射。")));
            continue;
        }

        if (unitName.isEmpty()) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("unit_name 不能为空。")));
            continue;
        }

        double unitPrice = 0.0;
        if (!parseDoubleField(unitPriceText, &unitPrice) || unitPrice <= 0.0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("unit_price 必须是大于 0 的数字。")));
            continue;
        }

        int currentQuantity = 0;
        if (!parseNonNegativeIntegerField(quantityText, &currentQuantity)) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("current_quantity 必须是大于等于 0 的整数。")));
            continue;
        }

        const int categoryId = m_databaseManager->productCategoryIdByName(categoryName);
        if (categoryId <= 0) {
            ++result.failedCount;
            result.failureReasons.append(formatRowError(rowNumber, QStringLiteral("产品类型不存在：%1").arg(categoryName)));
            continue;
        }

        InventoryItemData item;
        item.productCategoryId = categoryId;
        item.componentName = componentName;
        item.componentSpec = middleColumns.value(0).trimmed();
        item.material = middleColumns.value(1).trimmed();
        item.color = middleColumns.value(2).trimmed();
        item.unitName = unitName;
        item.unitPrice = unitPrice;
        item.currentQuantity = currentQuantity;
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
