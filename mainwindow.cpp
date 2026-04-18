#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QCompleter>
#include <QDate>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPrintDialog>
#include <QPrinter>
#include <QStringConverter>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidgetItem>
#include <QTextDocument>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGlobal>

namespace {
constexpr double kDefaultNonZeroUnitPrice = 1.0;

class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    explicit NumericTableWidgetItem(int value)
        : QTableWidgetItem(QString::number(value))
    {
        setData(Qt::UserRole, value);
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        bool leftOk = false;
        bool rightOk = false;
        const qlonglong leftValue = data(Qt::UserRole).toLongLong(&leftOk);
        const qlonglong rightValue = other.data(Qt::UserRole).toLongLong(&rightOk);
        if (leftOk && rightOk) {
            return leftValue < rightValue;
        }
        return QTableWidgetItem::operator<(other);
    }
};

QTableWidgetItem *createIdTableWidgetItem(int value)
{
    auto *item = new NumericTableWidgetItem(value);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

void applyDefaultAscendingSort(QTableWidget *tableWidget, int column)
{
    if (tableWidget == nullptr) {
        return;
    }

    const bool sortingEnabled = tableWidget->isSortingEnabled();
    if (!sortingEnabled) {
        tableWidget->setSortingEnabled(true);
    }
    tableWidget->sortItems(column, Qt::AscendingOrder);
}

struct InventoryShortageRow
{
    int productCategoryId = 0;
    QString productCategoryName;
    QString componentName;
    QString componentSpec;
    QString material;
    QString color;
    QString unitName;
    int quantity = 0;
};

QString inventoryDemandIdentityKey(int productCategoryId,
                                   const QString &componentName,
                                   const QString &componentSpec,
                                   const QString &material,
                                   const QString &color,
                                   const QString &unitName)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(productCategoryId)
        .arg(componentName.trimmed())
        .arg(componentSpec.trimmed())
        .arg(material.trimmed())
        .arg(color.trimmed())
        .arg(unitName.trimmed());
}

constexpr int kComponentNameColumn = 0;
constexpr int kComponentSpecColumn = 1;
constexpr int kComponentMaterialColumn = 2;
constexpr int kComponentColorColumn = 3;
constexpr int kComponentUnitColumn = 4;
constexpr int kQuantityPerSetColumn = 5;
constexpr int kUnitPriceColumn = 6;
constexpr int kSourceTypeColumn = 7;
constexpr int kTotalRequiredColumn = 8;
constexpr int kComponentTotalPriceColumn = 9;

constexpr int kShipmentComponentNameColumn = 0;
constexpr int kShipmentComponentQuantityPerSetColumn = 1;
constexpr int kShipmentComponentUnitPriceColumn = 2;
constexpr int kShipmentComponentTotalRequiredColumn = 3;
constexpr int kShipmentComponentShippedColumn = 4;
constexpr int kShipmentComponentUnshippedColumn = 5;
constexpr int kShipmentComponentTotalPriceColumn = 6;
constexpr int kShipmentComponentSourceColumn = 7;

constexpr int kQueryOrderIdColumn = 0;
constexpr int kQueryOrderDateColumn = 1;
constexpr int kQueryOrderCustomerColumn = 2;
constexpr int kQueryOrderCategoryColumn = 3;
constexpr int kQueryOrderProductModelColumn = 4;
constexpr int kQueryOrderConfigurationColumn = 5;
constexpr int kQueryOrderQuantitySetsColumn = 6;
constexpr int kQueryOrderUnitPriceColumn = 7;
constexpr int kQueryOrderStatusColumn = 8;
constexpr int kQueryOrderShipmentReadyColumn = 9;

constexpr int kQueryDetailComponentNameColumn = 0;
constexpr int kQueryDetailComponentSpecColumn = 1;
constexpr int kQueryDetailComponentMaterialColumn = 2;
constexpr int kQueryDetailComponentColorColumn = 3;
constexpr int kQueryDetailQuantityPerSetColumn = 4;
constexpr int kQueryDetailTotalRequiredColumn = 5;
constexpr int kQueryDetailUnitPriceColumn = 6;
constexpr int kQueryDetailSourceColumn = 7;

constexpr int kQueryShipmentDateColumn = 0;
constexpr int kQueryShipmentTypeColumn = 1;
constexpr int kQueryShipmentQuantityColumn = 2;
constexpr int kQueryShipmentNoteColumn = 3;

constexpr int kCategoryEditorIdColumn = 0;
constexpr int kCategoryEditorNameColumn = 1;

constexpr int kSkuEditorIdColumn = 0;
constexpr int kSkuEditorNameColumn = 1;
constexpr int kSkuEditorLampshadeColumn = 2;
constexpr int kSkuEditorLampshadePriceColumn = 3;

constexpr int kConfigurationEditorIdColumn = 0;
constexpr int kConfigurationEditorCodeColumn = 1;
constexpr int kConfigurationEditorNameColumn = 2;
constexpr int kConfigurationEditorPriceColumn = 3;
constexpr int kConfigurationEditorSortColumn = 4;

constexpr int kBomEditorIdColumn = 0;
constexpr int kBomEditorNameColumn = 1;
constexpr int kBomEditorSpecColumn = 2;
constexpr int kBomEditorMaterialColumn = 3;
constexpr int kBomEditorColorColumn = 4;
constexpr int kBomEditorUnitColumn = 5;
constexpr int kBomEditorQuantityColumn = 6;
constexpr int kBomEditorUnitPriceColumn = 7;
constexpr int kBomEditorSortColumn = 8;

constexpr int kInventoryListIdColumn = 0;
constexpr int kInventoryListCategoryColumn = 1;
constexpr int kInventoryListNameColumn = 2;
constexpr int kInventoryListSpecColumn = 3;
constexpr int kInventoryListMaterialColumn = 4;
constexpr int kInventoryListColorColumn = 5;
constexpr int kInventoryListUnitColumn = 6;
constexpr int kInventoryListUnitPriceColumn = 7;
constexpr int kInventoryListQuantityColumn = 8;
constexpr int kInventoryListNoteColumn = 9;

constexpr int kDemandSummaryCategoryColumn = 0;
constexpr int kDemandSummaryNameColumn = 1;
constexpr int kDemandSummarySpecColumn = 2;
constexpr int kDemandSummaryMaterialColumn = 3;
constexpr int kDemandSummaryColorColumn = 4;
constexpr int kDemandSummaryUnitColumn = 5;
constexpr int kDemandSummaryDemandColumn = 6;
constexpr int kDemandSummaryInventoryColumn = 7;
constexpr int kDemandSummaryGapColumn = 8;

constexpr int kReadyOrderIdColumn = 0;
constexpr int kReadyOrderCustomerColumn = 1;
constexpr int kReadyOrderCategoryColumn = 2;
constexpr int kReadyOrderSkuColumn = 3;
constexpr int kReadyOrderConfigurationColumn = 4;
constexpr int kReadyOrderQuantityColumn = 5;
constexpr int kReadyOrderStatusColumn = 6;

QString structuredSourceDisplayText(const QString &sourceType)
{
    if (sourceType == QStringLiteral("base_bom")) {
        return QStringLiteral("基础 BOM");
    }
    if (sourceType == QStringLiteral("lampshade")) {
        return QStringLiteral("灯罩");
    }
    if (sourceType == QStringLiteral("extra")) {
        return QStringLiteral("附加新增");
    }
    if (sourceType == QStringLiteral("adjusted_final")) {
        return QStringLiteral("调整后最终");
    }
    return sourceType;
}

QString structuredSourceStorageValue(const QString &displayText)
{
    if (displayText == QStringLiteral("基础 BOM")) {
        return QStringLiteral("base_bom");
    }
    if (displayText == QStringLiteral("灯罩")) {
        return QStringLiteral("lampshade");
    }
    if (displayText == QStringLiteral("附加新增")) {
        return QStringLiteral("extra");
    }
    if (displayText == QStringLiteral("调整后最终")) {
        return QStringLiteral("adjusted_final");
    }
    return displayText.trimmed();
}

QString inventoryOptionIdentityKey(const ProductComponentOption &option)
{
    return option.name.trimmed() + QLatin1Char('\x1f')
           + option.componentSpec.trimmed() + QLatin1Char('\x1f')
           + option.material.trimmed() + QLatin1Char('\x1f')
           + option.color.trimmed() + QLatin1Char('\x1f')
           + option.unitName.trimmed();
}

QString inventoryOptionBaseText(const ProductComponentOption &option)
{
    return option.componentSpec.trimmed().isEmpty()
               ? option.name.trimmed()
               : QStringLiteral("%1 | %2").arg(option.name.trimmed(), option.componentSpec.trimmed());
}

QString inventoryOptionDisplayText(const ProductComponentOption &option,
                                   const QHash<QString, QSet<QString>> &priceVariantsByIdentity)
{
    QString text = inventoryOptionBaseText(option);
    if (priceVariantsByIdentity.value(inventoryOptionIdentityKey(option)).size() > 1) {
        text += QStringLiteral(" | %1").arg(QString::number(option.unitPrice, 'f', 2));
    }
    return text;
}

QString shipmentOrderDisplayText(const ShipmentOrderSummary &order)
{
    return QStringLiteral("#%1 | %2 | %3 | %4 | %5 | %6")
        .arg(order.id)
        .arg(order.orderDate)
        .arg(order.customerName)
        .arg(order.productModelName)
        .arg(order.configurationName)
        .arg(order.isCompleted
                 ? QStringLiteral("已发")
                 : QStringLiteral("机体未发 %1 套 | 可整套发 %2 套")
                       .arg(order.unshippedSets)
                       .arg(order.availableSetShipments));
}

QString structuredShipmentOrderDisplayText(const StructuredOrderSummary &order)
{
    return QStringLiteral("#%1 | %2 | %3 | %4 | %5 | %6")
        .arg(order.id)
        .arg(order.orderDate)
        .arg(order.customerName)
        .arg(order.productSkuName)
        .arg(order.baseConfigurationName)
        .arg(order.isCompleted
                 ? QStringLiteral("已发")
                 : QStringLiteral("可整套发 %1 套").arg(order.availableSetShipments));
}

QString structuredOrderShipmentStatusText(const StructuredOrderSummary &order)
{
    if (order.isCompleted || order.status == QStringLiteral("completed")) {
        return QStringLiteral("已发");
    }
    if (order.hasShipmentRecord) {
        return QStringLiteral("部分发货");
    }
    return QStringLiteral("未发");
}

QString structuredOrderReadinessText(const StructuredOrderSummary &order)
{
    return order.shipmentReady ? QStringLiteral("可发货") : QStringLiteral("不可发货");
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    m_isInitializing = true;
    ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    m_statusMessageClearTimer = new QTimer(this);
    m_statusMessageClearTimer->setSingleShot(true);

    if (!m_databaseManager.ensureRequiredReferenceData()) {
        QMessageBox::critical(this,
                              QStringLiteral("基础数据初始化失败"),
                              m_databaseManager.lastError());
    }

    setupUiState();
    setupProductDataTab();
    setupInventoryTab();
    setupStructuredOrderUi();
    loadProductDataPage();
    loadInventoryPage();
    loadStructuredOrderSkus();
    loadStructuredQuerySkus();
    performStructuredOrderQuery();
    loadShipmentOrders();

    connect(m_structuredCategoryComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { loadStructuredOrderSkus(); });
    connect(ui->productModelComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { loadBaseConfigurationsForCurrentSku(); });
    connect(ui->templateComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { rebuildStructuredOrderComponents(); });
    connect(ui->quantitySetsSpinBox,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            [this](int) { updateStructuredComponentTotals(); });
    connect(ui->addComponentButton, &QPushButton::clicked, this, &MainWindow::addStructuredComponentRow);
    connect(ui->removeComponentButton,
            &QPushButton::clicked,
            this,
            [this]() {
                const int currentRow = ui->componentTableWidget->currentRow();
                if (currentRow >= 0) {
                    m_updatingComponentTable = true;
                    const QSignalBlocker blocker(ui->componentTableWidget);
                    ui->componentTableWidget->removeRow(currentRow);
                    m_updatingComponentTable = false;
                    updateStructuredComponentTotals();
                }
            });
    connect(ui->componentTableWidget,
            &QTableWidget::itemChanged,
            this,
            [this](QTableWidgetItem *item) {
                if (m_updatingComponentTable || m_isInitializing || m_isShuttingDown
                    || item == nullptr || ui == nullptr || ui->componentTableWidget == nullptr) {
                    return;
                }

                markStructuredRowAdjusted(item->row(), item->column());
                updateStructuredComponentTotals();
            });
    connect(ui->saveOrderButton,
            &QPushButton::clicked,
            this,
            [this]() {
                QString errorMessage;
                if (!validateStructuredOrderInput(&errorMessage)) {
                    QMessageBox::warning(this, QStringLiteral("订单保存失败"), errorMessage);
                    return;
                }

                const int skuId = ui->productModelComboBox->currentData().toInt();
                const int configurationId = ui->templateComboBox->currentData().toInt();
                ProductSkuOption currentSku;
                BaseConfigurationOption currentConfiguration;
                for (const ProductSkuOption &sku : m_databaseManager.productSkus()) {
                    if (sku.id == skuId) {
                        currentSku = sku;
                        break;
                    }
                }
                for (const BaseConfigurationOption &configuration :
                     m_databaseManager.baseConfigurationsForCategory(currentSku.productCategoryId)) {
                    if (configuration.id == configurationId) {
                        currentConfiguration = configuration;
                        break;
                    }
                }

                StructuredOrderSaveData orderData;
                orderData.orderDate = ui->orderDateEdit->date().toString(Qt::ISODate);
                orderData.customerName = ui->customerNameLineEdit->text().trimmed();
                orderData.productCategoryId = currentSku.productCategoryId;
                orderData.productCategoryName = currentSku.productCategoryName;
                orderData.productSkuId = currentSku.id;
                orderData.productSkuName = currentSku.skuName;
                orderData.baseConfigurationId = currentConfiguration.id;
                orderData.baseConfigurationName = currentConfiguration.configName;
                orderData.orderQuantity = ui->quantitySetsSpinBox->value();
                orderData.lampshadeName = m_lampshadeNameLineEdit != nullptr
                                              ? m_lampshadeNameLineEdit->text().trimmed()
                                              : QString();
                orderData.lampshadeUnitPrice = currentSku.lampshadeUnitPrice;
                orderData.configPrice = ui->unitPriceDoubleSpinBox->value();
                orderData.remark =
                    m_orderRemarkLineEdit != nullptr ? m_orderRemarkLineEdit->text().trimmed() : QString();

                if (!m_databaseManager.saveStructuredOrder(orderData,
                                                           collectStructuredComponentsFromTable())) {
                    QMessageBox::critical(this,
                                          QStringLiteral("订单保存失败"),
                                          m_databaseManager.lastError());
                    return;
                }

                showStatusMessage(QStringLiteral("订单已保存"), 3000);
                clearStructuredOrderForm();
                loadStructuredQuerySkus();
                loadInventoryPage();
                loadShipmentOrders();
                performStructuredOrderQuery();
            });
    connect(ui->querySearchButton, &QPushButton::clicked, this, &MainWindow::performStructuredOrderQuery);
    if (m_exportOrderSummaryButton != nullptr) {
        connect(m_exportOrderSummaryButton,
                &QPushButton::clicked,
                this,
                &MainWindow::exportOrderSummaryCsv);
    }
    if (m_exportShipmentListButton != nullptr) {
        connect(m_exportShipmentListButton,
                &QPushButton::clicked,
                this,
                &MainWindow::exportCurrentOrderShipmentCsv);
    }
    if (m_printCurrentOrderButton != nullptr) {
        connect(m_printCurrentOrderButton, &QPushButton::clicked, this, &MainWindow::printCurrentOrder);
    }
    connect(ui->queryResetButton,
            &QPushButton::clicked,
            this,
            [this]() {
                ui->queryCustomerLineEdit->clear();
                ui->queryProductModelComboBox->setCurrentIndex(0);
                ui->queryOnlyUnfinishedCheckBox->setChecked(false);
                if (m_queryStartDateEdit != nullptr) {
                    m_queryStartDateEdit->setDate(QDate::currentDate().addMonths(-1));
                }
                if (m_queryEndDateEdit != nullptr) {
                    m_queryEndDateEdit->setDate(QDate::currentDate());
                }
                performStructuredOrderQuery();
            });
    connect(ui->mainTabWidget,
            &QTabWidget::currentChanged,
            this,
            [this](int index) {
                if (index == 0) {
                    loadBaseConfigurationsForCurrentSku();
                } else if (m_inventoryTab != nullptr
                           && ui->mainTabWidget->widget(index) == m_inventoryTab) {
                    loadInventoryPage();
                } else if (index == 3) {
                    loadShipmentOrders();
                } else if (index == 4) {
                    performStructuredOrderQuery();
                }
            });
    connect(ui->orderListTableWidget,
            &QTableWidget::itemSelectionChanged,
            this,
            &MainWindow::refreshStructuredQueryOrderDetails);
    connect(ui->orderListTableWidget,
            &QTableWidget::itemClicked,
            this,
            [this](QTableWidgetItem *) {
                if (m_queryModeTabWidget != nullptr) {
                    m_queryModeTabWidget->setCurrentIndex(1);
                }
            });
    connect(ui->shipmentOrderComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { refreshShipmentDetails(); });
    connect(ui->shipmentComponentTableWidget,
            &QTableWidget::itemSelectionChanged,
            this,
            &MainWindow::updateSelectedShipmentComponent);
    connect(ui->saveOrderShipmentButton,
            &QPushButton::clicked,
            this,
            [this]() {
                const int orderId = currentShipmentOrderId();
                if (!m_databaseManager.saveStructuredOrderShipment(
                        orderId,
                        ui->shipmentDateEdit->date().toString(Qt::ISODate),
                        ui->shipmentSetsSpinBox->value(),
                        ui->shipmentOrderNoteLineEdit->text().trimmed())) {
                    QMessageBox::critical(this,
                                          QStringLiteral("保存发货失败"),
                                          m_databaseManager.lastError());
                    return;
                }

                ui->shipmentOrderNoteLineEdit->clear();
                loadShipmentOrders();
                loadInventoryPage();
                refreshShipmentReadyTables();
                performStructuredOrderQuery();
                showStatusMessage(QStringLiteral("订单发货已保存"), 3000);
            });
    connect(ui->saveComponentShipmentButton,
            &QPushButton::clicked,
            this,
            [this]() {
                const int orderId = currentShipmentOrderId();
                const int componentId = selectedShipmentComponentId();
                if (!m_databaseManager.saveStructuredComponentShipment(
                        orderId,
                        componentId,
                        ui->shipmentDateEdit->date().toString(Qt::ISODate),
                        ui->componentShipmentQuantitySpinBox->value(),
                        ui->componentShipmentNoteLineEdit->text().trimmed())) {
                    QMessageBox::critical(this,
                                          QStringLiteral("保存发货失败"),
                                          m_databaseManager.lastError());
                    return;
                }

                ui->componentShipmentNoteLineEdit->clear();
                loadShipmentOrders();
                loadInventoryPage();
                refreshShipmentReadyTables();
                performStructuredOrderQuery();
                showStatusMessage(QStringLiteral("组件发货已保存"), 3000);
            });
    connect(m_structuredShipmentReadyTableWidget,
            &QTableWidget::itemSelectionChanged,
            this,
            [this]() {
                const int orderId = currentStructuredShipmentOrderId();
                if (orderId <= 0 || ui == nullptr || ui->shipmentOrderComboBox == nullptr) {
                    refreshStructuredShipmentDetails();
                    return;
                }

                const int index = ui->shipmentOrderComboBox->findData(orderId);
                if (index >= 0) {
                    ui->shipmentOrderComboBox->setCurrentIndex(index);
                }
                refreshStructuredShipmentDetails();
            });

    m_isInitializing = false;
}

MainWindow::~MainWindow()
{
    m_isShuttingDown = true;
    if (ui != nullptr) {
        if (ui->mainTabWidget != nullptr) {
            disconnect(ui->mainTabWidget, nullptr, this, nullptr);
        }
        if (ui->productModelComboBox != nullptr) {
            disconnect(ui->productModelComboBox, nullptr, this, nullptr);
        }
        if (ui->templateComboBox != nullptr) {
            disconnect(ui->templateComboBox, nullptr, this, nullptr);
        }
        if (ui->customConfigurationRadioButton != nullptr) {
            disconnect(ui->customConfigurationRadioButton, nullptr, this, nullptr);
        }
        if (ui->quantitySetsSpinBox != nullptr) {
            disconnect(ui->quantitySetsSpinBox, nullptr, this, nullptr);
        }
        if (ui->componentTableWidget != nullptr) {
            disconnect(ui->componentTableWidget, nullptr, this, nullptr);
        }
        if (ui->orderListTableWidget != nullptr) {
            disconnect(ui->orderListTableWidget, nullptr, this, nullptr);
        }
        if (ui->shipmentComponentTableWidget != nullptr) {
            disconnect(ui->shipmentComponentTableWidget, nullptr, this, nullptr);
        }
        if (ui->queryProductModelComboBox != nullptr) {
            disconnect(ui->queryProductModelComboBox, nullptr, this, nullptr);
        }
        if (ui->shipmentOrderComboBox != nullptr) {
            disconnect(ui->shipmentOrderComboBox, nullptr, this, nullptr);
        }
    }
    delete ui;
    ui = nullptr;
}

void MainWindow::setupUiState()
{
    setMinimumSize(1180, 860);
    ui->menubar->hide();
    ui->mainTabWidget->setDocumentMode(true);
    ui->verticalLayout->setSpacing(0);
    ui->verticalLayout->setContentsMargins(2, 2, 2, 2);
    ui->windowSurfaceLayout->setSpacing(0);
    ui->windowSurfaceLayout->setContentsMargins(0, 0, 0, 0);
    ui->titleBarLayout->setSpacing(12);
    ui->titleBarLayout->setContentsMargins(16, 3, 10, 3);
    ui->titleIdentityLayout->setSpacing(2);
    ui->titleIdentityLayout->setContentsMargins(0, 0, 0, 0);
    ui->topMenuLayout->setSpacing(8);
    ui->topMenuLayout->setContentsMargins(0, 0, 0, 0);
    ui->windowControlsLayout->setSpacing(8);
    ui->windowControlsLayout->setContentsMargins(0, 0, 0, 0);
    ui->statusMessageLayout->setSpacing(0);
    ui->statusMessageLayout->setContentsMargins(16, 0, 16, 12);
    ui->titleBarLayout->setStretch(2, 1);
    ui->titleDragWidget->setCursor(Qt::SizeAllCursor);
    ui->orderEntryTabLayout->setSpacing(16);
    ui->orderEntryTabLayout->setContentsMargins(16, 16, 16, 16);
    ui->orderInputLayout->setSpacing(16);
    ui->formGridLayout->setHorizontalSpacing(16);
    ui->formGridLayout->setVerticalSpacing(12);
    ui->formGridLayout->setColumnStretch(1, 1);
    ui->formGridLayout->setColumnStretch(3, 1);
    ui->configurationModeLayout->setSpacing(12);
    ui->componentsLayout->setSpacing(16);
    ui->componentButtonLayout->setSpacing(8);
    ui->shipmentTabLayout->setSpacing(16);
    ui->shipmentTabLayout->setContentsMargins(16, 16, 16, 16);
    ui->shipmentLayout->setSpacing(16);
    ui->shipmentFormLayout->setHorizontalSpacing(16);
    ui->shipmentFormLayout->setVerticalSpacing(12);
    ui->shipmentFormLayout->setColumnStretch(1, 1);
    ui->shipmentFormLayout->setColumnStretch(3, 1);
    ui->shipmentOrderButtonLayout->setSpacing(8);
    ui->componentShipmentFormLayout->setHorizontalSpacing(16);
    ui->componentShipmentFormLayout->setVerticalSpacing(12);
    ui->componentShipmentFormLayout->setColumnStretch(1, 1);
    ui->componentShipmentFormLayout->setColumnStretch(3, 1);
    ui->queryTabLayout->setSpacing(16);
    ui->queryTabLayout->setContentsMargins(16, 16, 16, 16);
    ui->queryLayout->setSpacing(16);
    ui->queryFilterLayout->setHorizontalSpacing(8);
    ui->queryFilterLayout->setVerticalSpacing(8);

    ui->orderDateEdit->setDate(QDate::currentDate());
    ui->quantitySetsSpinBox->setMinimum(1);
    ui->quantitySetsSpinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    ui->bodyUnitPriceDoubleSpinBox->setMinimum(0.0);
    ui->bodyUnitPriceDoubleSpinBox->setDecimals(2);
    ui->bodyUnitPriceDoubleSpinBox->setMaximum(9999999.99);
    ui->bodyUnitPriceDoubleSpinBox->setReadOnly(true);
    ui->bodyUnitPriceDoubleSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->unitPriceDoubleSpinBox->setMinimum(0.0);
    ui->unitPriceDoubleSpinBox->setDecimals(2);
    ui->unitPriceDoubleSpinBox->setMaximum(9999999.99);
    ui->unitPriceDoubleSpinBox->setReadOnly(true);
    ui->unitPriceDoubleSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->shipmentDateEdit->setDate(QDate::currentDate());
    ui->shipmentSetsSpinBox->setMinimum(0);
    ui->shipmentSetsSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->componentShipmentQuantitySpinBox->setMinimum(0);
    ui->componentShipmentQuantitySpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->queryOrderCountValueLabel->setText(QStringLiteral("0"));
    ui->shipmentOrderStatusValueLabel->setWordWrap(true);
    ui->selectedComponentValueLabel->setWordWrap(true);
    ui->windowTitleLabel->setProperty("titleRole", "title");
    ui->statusMessageLabel->setProperty("labelRole", "statusMessage");
    ui->minimizeWindowButton->setText(QStringLiteral("−"));

    ui->saveOrderButton->setProperty("buttonRole", "primary");
    ui->saveOrderShipmentButton->setProperty("buttonRole", "primary");
    ui->saveComponentShipmentButton->setProperty("buttonRole", "primary");
    ui->addComponentButton->setProperty("buttonRole", "secondary");
    ui->removeComponentButton->setProperty("buttonRole", "secondary");
    ui->querySearchButton->setProperty("buttonRole", "secondary");
    ui->queryResetButton->setProperty("buttonRole", "secondary");
    ui->fileMenuButton->setProperty("buttonRole", "menu");
    ui->viewMenuButton->setProperty("buttonRole", "menu");
    ui->toolsMenuButton->setProperty("buttonRole", "menu");
    ui->helpMenuButton->setProperty("buttonRole", "menu");
    ui->minimizeWindowButton->setProperty("buttonRole", "window");
    ui->maximizeWindowButton->setProperty("buttonRole", "window");
    ui->closeWindowButton->setProperty("buttonRole", "dangerWindow");
    ui->shipmentOrderStatusValueLabel->setProperty("valueRole", "status");
    ui->selectedComponentValueLabel->setProperty("valueRole", "status");
    ui->queryOrderCountValueLabel->setProperty("valueRole", "metric");

    ui->productModelLabel->setText(QStringLiteral("具体型号"));
    ui->templateLabel->setText(QStringLiteral("基础配置"));
    ui->unitPriceLabel->setText(QStringLiteral("配置价格"));
    ui->orderInputGroupBox->setTitle(QStringLiteral("订单录入"));
    ui->componentsGroupBox->setTitle(QStringLiteral("订单组件"));
    ui->queryProductModelLabel->setText(QStringLiteral("具体型号"));
    ui->orderComponentDetailGroupBox->setTitle(QStringLiteral("订单组件明细"));
    ui->configurationModeLabel->hide();
    ui->templateConfigurationRadioButton->hide();
    ui->customConfigurationRadioButton->hide();
    setupQueryOutputControls();

    ui->componentTableWidget->setColumnCount(10);
    ui->componentTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("组件名称"),
         QStringLiteral("规格"),
         QStringLiteral("材质"),
         QStringLiteral("颜色"),
         QStringLiteral("单位"),
         QStringLiteral("每套数量"),
         QStringLiteral("单价"),
         QStringLiteral("来源"),
         QStringLiteral("需求数量"),
         QStringLiteral("总价")});
    ui->componentTableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kComponentNameColumn,
                                                                       QHeaderView::Stretch);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kComponentSpecColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kComponentMaterialColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kComponentColorColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kComponentUnitColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kQuantityPerSetColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kUnitPriceColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kSourceTypeColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kTotalRequiredColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(
        kComponentTotalPriceColumn, QHeaderView::ResizeToContents);
    configureTableWidget(ui->componentTableWidget);
    ui->componentTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->componentTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->shipmentComponentTableWidget->setColumnCount(8);
    ui->shipmentComponentTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("组件名称"),
         QStringLiteral("每套数量"),
         QStringLiteral("单价"),
         QStringLiteral("需求数量"),
         QStringLiteral("已发数量"),
         QStringLiteral("未发数量"),
         QStringLiteral("总价"),
         QStringLiteral("来源")});
    ui->shipmentComponentTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->shipmentComponentTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->shipmentComponentTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->shipmentComponentTableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentNameColumn, QHeaderView::Stretch);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentQuantityPerSetColumn, QHeaderView::ResizeToContents);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentUnitPriceColumn, QHeaderView::ResizeToContents);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentTotalRequiredColumn, QHeaderView::ResizeToContents);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentUnshippedColumn, QHeaderView::ResizeToContents);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentSourceColumn, QHeaderView::ResizeToContents);
    configureTableWidget(ui->shipmentComponentTableWidget);

    ui->orderListTableWidget->setColumnCount(10);
    ui->orderListTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("订单ID"),
         QStringLiteral("日期"),
         QStringLiteral("客户名称"),
         QStringLiteral("产品类型"),
         QStringLiteral("具体型号"),
         QStringLiteral("基础配置"),
         QStringLiteral("套数"),
         QStringLiteral("配置价格"),
         QStringLiteral("订单状态"),
         QStringLiteral("可发货")});
    ui->orderListTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->orderListTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->orderListTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->orderListTableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderIdColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderDateColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderCustomerColumn, QHeaderView::Stretch);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderCategoryColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderProductModelColumn, QHeaderView::Stretch);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderConfigurationColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderQuantitySetsColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderUnitPriceColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderStatusColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderShipmentReadyColumn, QHeaderView::ResizeToContents);
    configureTableWidget(ui->orderListTableWidget);

    ui->orderDetailComponentTableWidget->setColumnCount(8);
    ui->orderDetailComponentTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("组件名称"),
         QStringLiteral("规格"),
         QStringLiteral("材质"),
         QStringLiteral("颜色"),
         QStringLiteral("每套数量"),
         QStringLiteral("需求数量"),
         QStringLiteral("单价"),
         QStringLiteral("来源")});
    ui->orderDetailComponentTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->orderDetailComponentTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->orderDetailComponentTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailComponentNameColumn, QHeaderView::Stretch);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailComponentSpecColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailComponentMaterialColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailComponentColorColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailQuantityPerSetColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailTotalRequiredColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailUnitPriceColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailSourceColumn, QHeaderView::ResizeToContents);
    configureTableWidget(ui->orderDetailComponentTableWidget);

    ui->orderShipmentHistoryTableWidget->setColumnCount(4);
    ui->orderShipmentHistoryTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("时间"),
         QStringLiteral("类型"),
         QStringLiteral("数量"),
         QStringLiteral("备注")});
    ui->orderShipmentHistoryTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->orderShipmentHistoryTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->orderShipmentHistoryTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->orderShipmentHistoryTableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->orderShipmentHistoryTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryShipmentDateColumn, QHeaderView::ResizeToContents);
    ui->orderShipmentHistoryTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryShipmentTypeColumn, QHeaderView::ResizeToContents);
    ui->orderShipmentHistoryTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryShipmentQuantityColumn, QHeaderView::ResizeToContents);
    configureTableWidget(ui->orderShipmentHistoryTableWidget);

    ui->orderDetailSplitter->setOrientation(Qt::Vertical);
    ui->orderDetailSplitter->setChildrenCollapsible(false);
    ui->orderDetailSplitter->setHandleWidth(1);
    ui->orderDetailSplitter->setStretchFactor(0, 1);
    ui->orderDetailSplitter->setStretchFactor(1, 1);

    if (ui->queryLayout != nullptr && m_queryModeTabWidget == nullptr) {
        ui->queryLayout->removeWidget(ui->orderListTableWidget);
        ui->queryLayout->removeWidget(ui->orderDetailSplitter);

        m_queryModeTabWidget = new QTabWidget(ui->queryTab);
        m_queryModeTabWidget->setDocumentMode(true);

        auto *queryListPage = new QWidget(m_queryModeTabWidget);
        auto *queryListPageLayout = new QVBoxLayout(queryListPage);
        queryListPageLayout->setContentsMargins(0, 0, 0, 0);
        queryListPageLayout->setSpacing(16);
        queryListPageLayout->addWidget(ui->orderListTableWidget);

        auto *queryDetailPage = new QWidget(m_queryModeTabWidget);
        auto *queryDetailPageLayout = new QVBoxLayout(queryDetailPage);
        queryDetailPageLayout->setContentsMargins(0, 0, 0, 0);
        queryDetailPageLayout->setSpacing(16);
        queryDetailPageLayout->addWidget(ui->orderDetailSplitter);
        ui->orderDetailSplitter->setSizes({1, 1});

        m_queryModeTabWidget->addTab(queryListPage, QStringLiteral("订单查询"));
        m_queryModeTabWidget->addTab(queryDetailPage, QStringLiteral("订单详情"));
        ui->queryLayout->addWidget(m_queryModeTabWidget);
    }

    ui->addComponentButton->setEnabled(true);
    ui->removeComponentButton->setEnabled(true);
    statusBar()->hide();
    statusBar()->setSizeGripEnabled(false);
    setupTopMenus();
    setupCustomTitleBar();
    applyUiTheme();
    connect(m_statusMessageClearTimer, &QTimer::timeout, this, [this]() {
        if (ui != nullptr && ui->statusMessageLabel != nullptr) {
            ui->statusMessageLabel->clear();
        }
    });
    enableComboBoxFiltering(ui->productModelComboBox);
    enableComboBoxFiltering(ui->templateComboBox);
    enableComboBoxFiltering(ui->queryProductModelComboBox);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (ui != nullptr && ui->componentTableWidget != nullptr && watched != nullptr) {
        QWidget *widget = qobject_cast<QWidget *>(watched);
        const int row = structuredComponentRowForWidget(widget);
        if (row >= 0 && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn)) {
            ui->componentTableWidget->selectRow(row);
        }
    }

    if (watched == ui->titleDragWidget || watched == ui->windowTitleLabel
        || watched == ui->titleIdentityWidget) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                toggleMaximizeRestore();
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle() != nullptr
                && windowHandle()->startSystemMove()) {
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateWindowControlButtons();
    }
}

void MainWindow::setupCustomTitleBar()
{
    ui->titleDragWidget->installEventFilter(this);
    ui->titleIdentityWidget->installEventFilter(this);
    ui->windowTitleLabel->installEventFilter(this);

    connect(ui->minimizeWindowButton, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(ui->maximizeWindowButton, &QPushButton::clicked, this, &MainWindow::toggleMaximizeRestore);
    connect(ui->closeWindowButton, &QPushButton::clicked, this, &QWidget::close);

    updateWindowControlButtons();
}

void MainWindow::setupTopMenus()
{
    auto *fileMenu = new QMenu(this);
    fileMenu->addAction(QStringLiteral("刷新界面"), this, [this]() {
        loadProductDataPage();
        loadInventoryPage();
        loadStructuredOrderSkus();
        loadStructuredQuerySkus();
        loadShipmentOrders();
        performStructuredOrderQuery();
        showStatusMessage(QStringLiteral("界面数据已刷新"), 3000);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出"), this, &QWidget::close);
    ui->fileMenuButton->setMenu(fileMenu);

    auto *viewMenu = new QMenu(this);
    viewMenu->addAction(QStringLiteral("订单录入"), this, [this]() { ui->mainTabWidget->setCurrentIndex(0); });
    viewMenu->addAction(QStringLiteral("产品资料维护"),
                        this,
                        [this]() { ui->mainTabWidget->setCurrentWidget(m_productDataTab); });
    viewMenu->addAction(QStringLiteral("库存管理"),
                        this,
                        [this]() { ui->mainTabWidget->setCurrentWidget(m_inventoryTab); });
    viewMenu->addAction(QStringLiteral("发货登记"), this, [this]() { ui->mainTabWidget->setCurrentIndex(3); });
    viewMenu->addAction(QStringLiteral("订单查询"), this, [this]() { ui->mainTabWidget->setCurrentIndex(4); });
    ui->viewMenuButton->setMenu(viewMenu);

    auto *toolsMenu = new QMenu(this);
    toolsMenu->addAction(QStringLiteral("重新加载基础数据"), this, [this]() {
        loadProductDataPage();
        loadInventoryPage();
        loadStructuredOrderSkus();
        loadStructuredQuerySkus();
        loadShipmentOrders();
        performStructuredOrderQuery();
        showStatusMessage(QStringLiteral("基础数据已重新加载"), 3000);
    });
    toolsMenu->addAction(QStringLiteral("界面样式说明"), this, [this]() {
        QMessageBox::information(this,
                                 QStringLiteral("界面样式说明"),
                                 QStringLiteral("当前界面采用浅色桌面业务风格，顶栏、表单、表格和页签已按统一规范整理。"));
    });
    ui->toolsMenuButton->setMenu(toolsMenu);

    auto *helpMenu = new QMenu(this);
    helpMenu->addAction(QStringLiteral("使用说明"), this, [this]() {
        QMessageBox::information(this,
                                 QStringLiteral("使用说明"),
                                 QStringLiteral("通过页签完成产品资料维护、订单录入、库存管理、发货登记和订单查询。"));
    });
    helpMenu->addAction(QStringLiteral("关于"), this, [this]() {
        QMessageBox::information(this,
                                 QStringLiteral("关于"),
                                 QStringLiteral("OrderManagerSystem\nQt Widgets 桌面订单管理系统"));
    });
    ui->helpMenuButton->setMenu(helpMenu);
}

void MainWindow::toggleMaximizeRestore()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }

    updateWindowControlButtons();
}

void MainWindow::updateWindowControlButtons()
{
    const bool maximized = isMaximized() || isFullScreen();
    ui->maximizeWindowButton->setText(isMaximized() ? QStringLiteral("❐")
                                                    : QStringLiteral("□"));
    ui->verticalLayout->setContentsMargins(maximized ? 0 : 2,
                                           maximized ? 0 : 2,
                                           maximized ? 0 : 2,
                                           maximized ? 0 : 2);
    ui->windowSurfaceFrame->setProperty("windowState", maximized ? "maximized" : "normal");
    ui->titleBarFrame->setProperty("windowState", maximized ? "maximized" : "normal");
    ui->windowSurfaceFrame->style()->unpolish(ui->windowSurfaceFrame);
    ui->windowSurfaceFrame->style()->polish(ui->windowSurfaceFrame);
    ui->titleBarFrame->style()->unpolish(ui->titleBarFrame);
    ui->titleBarFrame->style()->polish(ui->titleBarFrame);
    ui->windowSurfaceFrame->update();
    ui->titleBarFrame->update();
}

void MainWindow::applyUiTheme()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow {"
        "  background: transparent;"
        "}"
        "QWidget#centralwidget {"
        "  background: transparent;"
        "  color: #202534;"
        "  font-size: 13px;"
        "}"
        "QFrame#windowSurfaceFrame {"
        "  background: #dfe5ee;"
        "  border: 1px solid #cfd7e3;"
        "  border-radius: 6px;"
        "}"
        "QFrame#windowSurfaceFrame[windowState=\"maximized\"] {"
        "  border-radius: 0px;"
        "}"
        "QFrame#titleBarFrame {"
        "  background: #f7f9fc;"
        "  border: none;"
        "  border-bottom: 1px solid #d7deea;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "}"
        "QFrame#titleBarFrame[windowState=\"maximized\"] {"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "}"
        "QFrame#statusMessageFrame {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel {"
        "  color: #4a5364;"
        "}"
        "QLabel[titleRole=\"title\"] {"
        "  color: #1f2937;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QLabel[labelRole=\"statusMessage\"] {"
        "  color: #667085;"
        "  background: transparent;"
        "  padding: 0;"
        "  min-height: 18px;"
        "}"
        "QLabel[valueRole=\"status\"] {"
        "  color: #243247;"
        "  background: #f8fafc;"
        "  border: 1px solid #d7dee8;"
        "  border-radius: 6px;"
        "  padding: 8px 10px;"
        "}"
        "QLabel[valueRole=\"metric\"] {"
        "  color: #1f2937;"
        "  font-size: 15px;"
        "  font-weight: 600;"
        "}"
        "QToolButton[buttonRole=\"menu\"] {"
        "  min-height: 34px;"
        "  padding: 0 12px;"
        "  color: #314155;"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "}"
        "QToolButton[buttonRole=\"menu\"]:hover {"
        "  background: #eef3f8;"
        "  border-color: #d7deea;"
        "}"
        "QToolButton[buttonRole=\"menu\"]::menu-indicator {"
        "  image: none;"
        "}"
        "QMenu {"
        "  background: #ffffff;"
        "  border: 1px solid #d7deea;"
        "  padding: 6px;"
        "}"
        "QMenu::item {"
        "  padding: 8px 18px;"
        "  border-radius: 6px;"
        "}"
        "QMenu::item:selected {"
        "  background: #edf3ff;"
        "  color: #1d4ed8;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid #d7deea;"
        "  background: transparent;"
        "  border-radius: 6px;"
        "  margin-top: 10px;"
        "}"
        "QTabBar::tab {"
        "  background: #eef2f7;"
        "  color: #5a6374;"
        "  border: 1px solid #d7deea;"
        "  padding: 9px 18px;"
        "  min-height: 18px;"
        "  margin-right: 8px;"
        "  border-top-left-radius: 4px;"
        "  border-top-right-radius: 4px;"
        "}"
        "QTabBar::tab:selected {"
        "  color: #1f2937;"
        "  background: #ffffff;"
        "  border-color: #cfd8e6;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "  background: #f4f7fb;"
        "  color: #344256;"
        "}"
        "QGroupBox {"
        "  background: #ffffff;"
        "  border: 1px solid #d7deea;"
        "  border-radius: 6px;"
        "  margin-top: 16px;"
        "  padding: 16px;"
        "  padding-top: 20px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 14px;"
        "  padding: 0 6px;"
        "  color: #526072;"
        "  background: transparent;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "}"
        "QLineEdit, QComboBox, QAbstractSpinBox, QDateEdit {"
        "  min-height: 36px;"
        "  background: #fcfdff;"
        "  color: #1f2937;"
        "  border: 1px solid #d8e0ea;"
        "  border-radius: 4px;"
        "  padding: 0 10px;"
        "  selection-background-color: #dbeafe;"
        "}"
        "QComboBox::drop-down {"
        "  width: 28px;"
        "  background: #eef3f8;"
        "  border-left: 1px solid #d8e0ea;"
        "  border-top-right-radius: 4px;"
        "  border-bottom-right-radius: 4px;"
        "}"
        "QComboBox::drop-down:hover {"
        "  background: #e7edf4;"
        "}"
        "QComboBox::down-arrow {"
        "  image: none;"
        "  width: 0px;"
        "  height: 0px;"
        "  border-left: 6px solid transparent;"
        "  border-right: 6px solid transparent;"
        "  border-top: 8px solid #000000;"
        "  margin-top: 2px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: #ffffff;"
        "  color: #1f2937;"
        "  border: 1px solid #d8e0ea;"
        "  selection-background-color: #e8f0ff;"
        "}"
        "QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus, QDateEdit:focus {"
        "  border: 1px solid #4f7cff;"
        "  background: #ffffff;"
        "}"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
        "  width: 18px;"
        "  border: none;"
        "  background: #f1f5f9;"
        "  border-left: 1px solid #d8e0ea;"
        "}"
        "QAbstractSpinBox::up-button {"
        "  border-top-right-radius: 4px;"
        "  border-bottom: 1px solid #d8e0ea;"
        "}"
        "QAbstractSpinBox::down-button {"
        "  border-bottom-right-radius: 4px;"
        "}"
        "QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow {"
        "  width: 8px;"
        "  height: 8px;"
        "}"
        "QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover {"
        "  background: #e7edf4;"
        "}"
        "QAbstractSpinBox[readOnly=\"true\"] {"
        "  color: #5c6472;"
        "  background: #f3f6fa;"
        "}"
        "QPushButton {"
        "  min-height: 36px;"
        "  padding: 0 14px;"
        "  border-radius: 4px;"
        "  border: 1px solid #d5dde8;"
        "  background: #f7f9fc;"
        "  color: #2c394b;"
        "}"
        "QPushButton:hover {"
        "  background: #eef3f8;"
        "}"
        "QPushButton:pressed {"
        "  background: #e4eaf2;"
        "}"
        "QPushButton[buttonRole=\"primary\"] {"
        "  background: #3b82f6;"
        "  color: #ffffff;"
        "  border: 1px solid #3b82f6;"
        "}"
        "QPushButton[buttonRole=\"primary\"]:hover {"
        "  background: #2563eb;"
        "  border-color: #2563eb;"
        "}"
        "QPushButton[buttonRole=\"window\"] {"
        "  min-width: 24px;"
        "  min-height: 24px;"
        "  padding: 0;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "}"
        "QPushButton[buttonRole=\"window\"]:hover {"
        "  background: #eef3f8;"
        "  border-color: #d7deea;"
        "}"
        "QPushButton[buttonRole=\"dangerWindow\"] {"
        "  min-width: 24px;"
        "  min-height: 24px;"
        "  padding: 0;"
        "  font-size: 13px;"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "}"
        "QPushButton[buttonRole=\"dangerWindow\"]:hover {"
        "  color: #ffffff;"
        "  background: #ef4444;"
        "  border-color: #ef4444;"
        "}"
        "QPushButton:disabled {"
        "  color: #98a2b3;"
        "  background: #f4f6f8;"
        "  border-color: #e1e7ef;"
        "}"
        "QRadioButton, QCheckBox {"
        "  spacing: 8px;"
        "  color: #425064;"
        "}"
        "QRadioButton::indicator, QCheckBox::indicator {"
        "  width: 16px;"
        "  height: 16px;"
        "}"
        "QHeaderView::section {"
        "  background: #f6f8fb;"
        "  color: #4a5568;"
        "  border: none;"
        "  border-bottom: 1px solid #d7deea;"
        "  padding: 8px 10px;"
        "}"
        "QTableWidget {"
        "  background: #ffffff;"
        "  alternate-background-color: #f8fafc;"
        "  border: 1px solid #d7deea;"
        "  border-radius: 4px;"
        "  gridline-color: #eef2f7;"
        "  color: #1f2937;"
        "  selection-background-color: #dbeafe;"
        "  selection-color: #1e3a8a;"
        "}"
        "QTableWidget::item {"
        "  padding: 6px 10px;"
        "  border-bottom: 1px solid #eef2f7;"
        "}"
        "QTableWidget::item:hover {"
        "  background: #f3f7fd;"
        "}"
        "QTableWidget::item:selected {"
        "  background: #dbeafe;"
        "}"
        "QSplitter::handle {"
        "  background: #dfe6ef;"
        "}"
        "QStatusBar {"
        "  background: transparent;"
        "  color: transparent;"
        "  border: none;"
        "}"
    ));
}

void MainWindow::showStatusMessage(const QString &message, int timeoutMs)
{
    if (ui == nullptr || ui->statusMessageLabel == nullptr || m_statusMessageClearTimer == nullptr) {
        return;
    }

    m_statusMessageClearTimer->stop();
    ui->statusMessageLabel->setText(message);

    if (timeoutMs > 0) {
        m_statusMessageClearTimer->start(timeoutMs);
    }
}

void MainWindow::configureTableWidget(QTableWidget *tableWidget) const
{
    if (tableWidget == nullptr) {
        return;
    }

    tableWidget->setAlternatingRowColors(true);
    tableWidget->setShowGrid(false);
    tableWidget->setWordWrap(false);
    tableWidget->setMouseTracking(true);
    tableWidget->setFocusPolicy(Qt::StrongFocus);
    tableWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tableWidget->verticalHeader()->setVisible(false);
    tableWidget->verticalHeader()->setDefaultSectionSize(34);
    tableWidget->horizontalHeader()->setMinimumSectionSize(48);
    tableWidget->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    tableWidget->horizontalHeader()->setFixedHeight(36);
    tableWidget->horizontalHeader()->setSectionsClickable(true);
    tableWidget->horizontalHeader()->setSortIndicatorShown(true);
    tableWidget->setSortingEnabled(true);
}

void MainWindow::setupProductDataTab()
{
    if (m_productDataTab != nullptr) {
        return;
    }

    m_productDataTab = new QWidget(ui->mainTabWidget);
    auto *rootLayout = new QHBoxLayout(m_productDataTab);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(16);

    m_productDataMenuListWidget = new QListWidget(m_productDataTab);
    m_productDataMenuListWidget->addItems({QStringLiteral("产品类型"),
                                           QStringLiteral("具体型号"),
                                           QStringLiteral("基础配置价格"),
                                           QStringLiteral("基础配置 BOM")});
    m_productDataMenuListWidget->setFixedWidth(180);
    rootLayout->addWidget(m_productDataMenuListWidget);

    m_productDataStackedWidget = new QStackedWidget(m_productDataTab);
    rootLayout->addWidget(m_productDataStackedWidget, 1);

    auto *categoryPage = new QWidget(m_productDataStackedWidget);
    auto *categoryPageLayout = new QVBoxLayout(categoryPage);
    auto *categoryGroup = new QGroupBox(QStringLiteral("产品类型维护"), categoryPage);
    auto *categoryLayout = new QVBoxLayout(categoryGroup);
    m_categoryTableWidget = new QTableWidget(categoryGroup);
    m_categoryTableWidget->setColumnCount(2);
    m_categoryTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("产品类型")});
    configureTableWidget(m_categoryTableWidget);
    m_categoryTableWidget->horizontalHeader()->setSectionResizeMode(kCategoryEditorIdColumn,
                                                                    QHeaderView::ResizeToContents);
    m_categoryTableWidget->horizontalHeader()->setSectionResizeMode(kCategoryEditorNameColumn,
                                                                    QHeaderView::Stretch);
    categoryLayout->addWidget(m_categoryTableWidget);
    auto *categoryButtonLayout = new QHBoxLayout();
    categoryButtonLayout->addStretch();
    m_importCategoriesButton = new QPushButton(QStringLiteral("导入 CSV"), categoryGroup);
    m_addCategoryButton = new QPushButton(QStringLiteral("新增类型"), categoryGroup);
    m_saveCategoryButton = new QPushButton(QStringLiteral("保存类型"), categoryGroup);
    m_importCategoriesButton->setProperty("buttonRole", "secondary");
    m_addCategoryButton->setProperty("buttonRole", "secondary");
    m_saveCategoryButton->setProperty("buttonRole", "primary");
    categoryButtonLayout->addWidget(m_importCategoriesButton);
    categoryButtonLayout->addWidget(m_addCategoryButton);
    categoryButtonLayout->addWidget(m_saveCategoryButton);
    categoryLayout->addLayout(categoryButtonLayout);
    categoryPageLayout->addWidget(categoryGroup);
    m_productDataStackedWidget->addWidget(categoryPage);

    auto *skuPage = new QWidget(m_productDataStackedWidget);
    auto *skuPageLayout = new QVBoxLayout(skuPage);
    auto *skuGroup = new QGroupBox(QStringLiteral("具体型号维护"), skuPage);
    auto *skuLayout = new QVBoxLayout(skuGroup);
    auto *skuFilterLayout = new QHBoxLayout();
    skuFilterLayout->addWidget(new QLabel(QStringLiteral("产品类型"), skuGroup));
    m_skuCategoryComboBox = new QComboBox(skuGroup);
    skuFilterLayout->addWidget(m_skuCategoryComboBox);
    skuFilterLayout->addStretch();
    skuLayout->addLayout(skuFilterLayout);
    m_skuTableWidget = new QTableWidget(skuGroup);
    m_skuTableWidget->setColumnCount(4);
    m_skuTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("具体型号"), QStringLiteral("默认灯罩"), QStringLiteral("灯罩单价")});
    configureTableWidget(m_skuTableWidget);
    m_skuTableWidget->horizontalHeader()->setSectionResizeMode(kSkuEditorIdColumn,
                                                               QHeaderView::ResizeToContents);
    m_skuTableWidget->horizontalHeader()->setSectionResizeMode(kSkuEditorNameColumn, QHeaderView::Stretch);
    m_skuTableWidget->horizontalHeader()->setSectionResizeMode(kSkuEditorLampshadeColumn,
                                                               QHeaderView::Stretch);
    m_skuTableWidget->horizontalHeader()->setSectionResizeMode(kSkuEditorLampshadePriceColumn,
                                                               QHeaderView::ResizeToContents);
    skuLayout->addWidget(m_skuTableWidget);
    auto *skuButtonLayout = new QHBoxLayout();
    skuButtonLayout->addStretch();
    m_importSkusButton = new QPushButton(QStringLiteral("导入 CSV"), skuGroup);
    m_addSkuButton = new QPushButton(QStringLiteral("新增型号"), skuGroup);
    m_saveSkuButton = new QPushButton(QStringLiteral("保存型号"), skuGroup);
    m_importSkusButton->setProperty("buttonRole", "secondary");
    m_addSkuButton->setProperty("buttonRole", "secondary");
    m_saveSkuButton->setProperty("buttonRole", "primary");
    skuButtonLayout->addWidget(m_importSkusButton);
    skuButtonLayout->addWidget(m_addSkuButton);
    skuButtonLayout->addWidget(m_saveSkuButton);
    skuLayout->addLayout(skuButtonLayout);
    skuPageLayout->addWidget(skuGroup);
    m_productDataStackedWidget->addWidget(skuPage);

    auto *configPage = new QWidget(m_productDataStackedWidget);
    auto *configPageLayout = new QVBoxLayout(configPage);
    auto *configGroup = new QGroupBox(QStringLiteral("基础配置价格维护"), configPage);
    auto *configLayout = new QVBoxLayout(configGroup);
    auto *configFilterLayout = new QHBoxLayout();
    configFilterLayout->addWidget(new QLabel(QStringLiteral("产品类型"), configGroup));
    m_configurationCategoryComboBox = new QComboBox(configGroup);
    configFilterLayout->addWidget(m_configurationCategoryComboBox);
    configFilterLayout->addStretch();
    configLayout->addLayout(configFilterLayout);
    m_configurationTableWidget = new QTableWidget(configGroup);
    m_configurationTableWidget->setColumnCount(5);
    m_configurationTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("ID"),
         QStringLiteral("配置代码"),
         QStringLiteral("配置名称"),
         QStringLiteral("配置价格"),
         QStringLiteral("排序")});
    configureTableWidget(m_configurationTableWidget);
    m_configurationTableWidget->horizontalHeader()->setSectionResizeMode(
        kConfigurationEditorIdColumn, QHeaderView::ResizeToContents);
    m_configurationTableWidget->horizontalHeader()->setSectionResizeMode(
        kConfigurationEditorCodeColumn, QHeaderView::ResizeToContents);
    m_configurationTableWidget->horizontalHeader()->setSectionResizeMode(
        kConfigurationEditorNameColumn, QHeaderView::Stretch);
    m_configurationTableWidget->horizontalHeader()->setSectionResizeMode(
        kConfigurationEditorPriceColumn, QHeaderView::ResizeToContents);
    m_configurationTableWidget->horizontalHeader()->setSectionResizeMode(
        kConfigurationEditorSortColumn, QHeaderView::ResizeToContents);
    configLayout->addWidget(m_configurationTableWidget);
    auto *configButtonLayout = new QHBoxLayout();
    configButtonLayout->addStretch();
    m_importConfigurationsButton = new QPushButton(QStringLiteral("导入 CSV"), configGroup);
    m_addConfigurationButton = new QPushButton(QStringLiteral("新增配置"), configGroup);
    m_saveConfigurationButton = new QPushButton(QStringLiteral("保存配置"), configGroup);
    m_importConfigurationsButton->setProperty("buttonRole", "secondary");
    m_addConfigurationButton->setProperty("buttonRole", "secondary");
    m_saveConfigurationButton->setProperty("buttonRole", "primary");
    configButtonLayout->addWidget(m_importConfigurationsButton);
    configButtonLayout->addWidget(m_addConfigurationButton);
    configButtonLayout->addWidget(m_saveConfigurationButton);
    configLayout->addLayout(configButtonLayout);
    configPageLayout->addWidget(configGroup);
    m_productDataStackedWidget->addWidget(configPage);

    auto *bomPage = new QWidget(m_productDataStackedWidget);
    auto *bomPageLayout = new QVBoxLayout(bomPage);
    auto *bomGroup = new QGroupBox(QStringLiteral("基础配置 BOM 维护"), bomPage);
    auto *bomLayout = new QVBoxLayout(bomGroup);
    auto *bomFilterLayout = new QHBoxLayout();
    bomFilterLayout->addWidget(new QLabel(QStringLiteral("基础配置"), bomGroup));
    m_bomConfigurationComboBox = new QComboBox(bomGroup);
    bomFilterLayout->addWidget(m_bomConfigurationComboBox);
    bomFilterLayout->addStretch();
    bomLayout->addLayout(bomFilterLayout);
    m_bomTableWidget = new QTableWidget(bomGroup);
    m_bomTableWidget->setColumnCount(9);
    m_bomTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("ID"),
         QStringLiteral("组件名称"),
         QStringLiteral("规格"),
         QStringLiteral("材质"),
         QStringLiteral("颜色"),
         QStringLiteral("单位"),
         QStringLiteral("每套数量"),
         QStringLiteral("单价"),
         QStringLiteral("排序")});
    configureTableWidget(m_bomTableWidget);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorIdColumn,
                                                               QHeaderView::ResizeToContents);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorNameColumn, QHeaderView::Stretch);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorSpecColumn,
                                                               QHeaderView::ResizeToContents);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorMaterialColumn,
                                                               QHeaderView::ResizeToContents);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorColorColumn,
                                                               QHeaderView::ResizeToContents);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorUnitColumn,
                                                               QHeaderView::ResizeToContents);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorQuantityColumn,
                                                               QHeaderView::ResizeToContents);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorUnitPriceColumn,
                                                               QHeaderView::ResizeToContents);
    m_bomTableWidget->horizontalHeader()->setSectionResizeMode(kBomEditorSortColumn,
                                                               QHeaderView::ResizeToContents);
    bomLayout->addWidget(m_bomTableWidget);
    auto *bomButtonLayout = new QHBoxLayout();
    bomButtonLayout->addStretch();
    m_importBomButton = new QPushButton(QStringLiteral("导入 CSV"), bomGroup);
    m_addBomButton = new QPushButton(QStringLiteral("新增 BOM 行"), bomGroup);
    m_saveBomButton = new QPushButton(QStringLiteral("保存 BOM"), bomGroup);
    m_importBomButton->setProperty("buttonRole", "secondary");
    m_addBomButton->setProperty("buttonRole", "secondary");
    m_saveBomButton->setProperty("buttonRole", "primary");
    bomButtonLayout->addWidget(m_importBomButton);
    bomButtonLayout->addWidget(m_addBomButton);
    bomButtonLayout->addWidget(m_saveBomButton);
    bomLayout->addLayout(bomButtonLayout);
    bomPageLayout->addWidget(bomGroup);
    m_productDataStackedWidget->addWidget(bomPage);

    ui->mainTabWidget->insertTab(1, m_productDataTab, QStringLiteral("产品资料维护"));

    connect(m_addCategoryButton, &QPushButton::clicked, this, &MainWindow::addCategoryEditorRow);
    connect(m_saveCategoryButton, &QPushButton::clicked, this, &MainWindow::saveCategoryEditor);
    connect(m_importCategoriesButton, &QPushButton::clicked, this, &MainWindow::importProductCategoriesCsv);
    connect(m_skuCategoryComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { loadSkuEditor(); });
    connect(m_addSkuButton, &QPushButton::clicked, this, &MainWindow::addSkuEditorRow);
    connect(m_saveSkuButton, &QPushButton::clicked, this, &MainWindow::saveSkuEditor);
    connect(m_importSkusButton, &QPushButton::clicked, this, &MainWindow::importProductSkusCsv);
    connect(m_configurationCategoryComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                loadConfigurationEditor();
                refreshManagementCategorySelectors();
            });
    connect(m_addConfigurationButton,
            &QPushButton::clicked,
            this,
            &MainWindow::addConfigurationEditorRow);
    connect(m_saveConfigurationButton,
            &QPushButton::clicked,
            this,
            &MainWindow::saveConfigurationEditor);
    connect(m_importConfigurationsButton,
            &QPushButton::clicked,
            this,
            &MainWindow::importBaseConfigurationsCsv);
    connect(m_bomConfigurationComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { loadBomEditor(); });
    connect(m_addBomButton, &QPushButton::clicked, this, &MainWindow::addBomEditorRow);
    connect(m_saveBomButton, &QPushButton::clicked, this, &MainWindow::saveBomEditor);
    connect(m_importBomButton, &QPushButton::clicked, this, &MainWindow::importBaseConfigurationBomCsv);
    connect(m_productDataMenuListWidget,
            &QListWidget::currentRowChanged,
            this,
            [this](int row) {
                if (m_productDataStackedWidget != nullptr && row >= 0) {
                    m_productDataStackedWidget->setCurrentIndex(row);
                }
            });

    m_productDataMenuListWidget->setCurrentRow(0);
}

void MainWindow::setupInventoryTab()
{
    if (m_inventoryTab != nullptr) {
        return;
    }

    m_inventoryTab = new QWidget(ui->mainTabWidget);
    auto *rootLayout = new QVBoxLayout(m_inventoryTab);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(16);
    m_inventoryModeTabWidget = new QTabWidget(m_inventoryTab);
    m_inventoryModeTabWidget->setDocumentMode(true);
    auto *inventoryEntryPage = new QWidget(m_inventoryModeTabWidget);
    auto *inventoryEntryPageLayout = new QVBoxLayout(inventoryEntryPage);
    inventoryEntryPageLayout->setContentsMargins(0, 0, 0, 0);
    inventoryEntryPageLayout->setSpacing(16);
    auto *inventoryListPage = new QWidget(m_inventoryModeTabWidget);
    auto *inventoryListPageLayout = new QVBoxLayout(inventoryListPage);
    inventoryListPageLayout->setContentsMargins(0, 0, 0, 0);
    inventoryListPageLayout->setSpacing(16);
    auto *inventoryDemandPage = new QWidget(m_inventoryModeTabWidget);
    auto *inventoryDemandPageLayout = new QVBoxLayout(inventoryDemandPage);
    inventoryDemandPageLayout->setContentsMargins(0, 0, 0, 0);
    inventoryDemandPageLayout->setSpacing(16);

    auto *inventoryGroup = new QGroupBox(QStringLiteral("库存录入"), m_inventoryTab);
    auto *inventoryLayout = new QVBoxLayout(inventoryGroup);
    auto *inventoryFormLayout = new QGridLayout();
    inventoryFormLayout->setHorizontalSpacing(16);
    inventoryFormLayout->setVerticalSpacing(12);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("适用产品类型"), inventoryGroup), 0, 0);
    m_inventoryCategoryComboBox = new QComboBox(inventoryGroup);
    inventoryFormLayout->addWidget(m_inventoryCategoryComboBox, 0, 1);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("物料名称"), inventoryGroup), 0, 2);
    m_inventoryComponentComboBox = new QComboBox(inventoryGroup);
    inventoryFormLayout->addWidget(m_inventoryComponentComboBox, 0, 3);
    enableComboBoxFiltering(m_inventoryCategoryComboBox);
    enableComboBoxFiltering(m_inventoryComponentComboBox);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("规格"), inventoryGroup), 1, 0);
    m_inventoryComponentSpecLineEdit = new QLineEdit(inventoryGroup);
    inventoryFormLayout->addWidget(m_inventoryComponentSpecLineEdit, 1, 1);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("材质"), inventoryGroup), 1, 2);
    m_inventoryMaterialLineEdit = new QLineEdit(inventoryGroup);
    inventoryFormLayout->addWidget(m_inventoryMaterialLineEdit, 1, 3);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("颜色"), inventoryGroup), 2, 0);
    m_inventoryColorLineEdit = new QLineEdit(inventoryGroup);
    inventoryFormLayout->addWidget(m_inventoryColorLineEdit, 2, 1);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("单位"), inventoryGroup), 2, 2);
    m_inventoryUnitLineEdit = new QLineEdit(inventoryGroup);
    inventoryFormLayout->addWidget(m_inventoryUnitLineEdit, 2, 3);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("默认单价"), inventoryGroup), 3, 0);
    m_inventoryUnitPriceSpinBox = new QDoubleSpinBox(inventoryGroup);
    m_inventoryUnitPriceSpinBox->setMinimum(0.0);
    m_inventoryUnitPriceSpinBox->setMaximum(9999999.99);
    m_inventoryUnitPriceSpinBox->setDecimals(2);
    inventoryFormLayout->addWidget(m_inventoryUnitPriceSpinBox, 3, 1);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("库存数量"), inventoryGroup), 3, 2);
    m_inventoryQuantitySpinBox = new QSpinBox(inventoryGroup);
    m_inventoryQuantitySpinBox->setMinimum(0);
    m_inventoryQuantitySpinBox->setMaximum(999999999);
    inventoryFormLayout->addWidget(m_inventoryQuantitySpinBox, 3, 3);
    inventoryFormLayout->addWidget(new QLabel(QStringLiteral("备注"), inventoryGroup), 4, 0);
    m_inventoryNoteLineEdit = new QLineEdit(inventoryGroup);
    inventoryFormLayout->addWidget(m_inventoryNoteLineEdit, 4, 1, 1, 3);
    inventoryLayout->addLayout(inventoryFormLayout);

    auto *inventoryButtonLayout = new QHBoxLayout();
    inventoryButtonLayout->addStretch();
    m_importInventoryButton = new QPushButton(QStringLiteral("导入库存 CSV"), inventoryGroup);
    m_clearInventoryButton = new QPushButton(QStringLiteral("清空"), inventoryGroup);
    m_saveInventoryButton = new QPushButton(QStringLiteral("保存库存"), inventoryGroup);
    m_importInventoryButton->setProperty("buttonRole", "secondary");
    m_clearInventoryButton->setProperty("buttonRole", "secondary");
    m_saveInventoryButton->setProperty("buttonRole", "primary");
    inventoryButtonLayout->addWidget(m_importInventoryButton);
    inventoryButtonLayout->addWidget(m_clearInventoryButton);
    inventoryButtonLayout->addWidget(m_saveInventoryButton);
    inventoryLayout->addLayout(inventoryButtonLayout);
    inventoryEntryPageLayout->addWidget(inventoryGroup);

    auto *inventoryListGroup = new QGroupBox(QStringLiteral("库存列表"), m_inventoryTab);
    auto *inventoryListLayout = new QVBoxLayout(inventoryListGroup);
    m_inventoryTableWidget = new QTableWidget(inventoryListGroup);
    m_inventoryTableWidget->setColumnCount(10);
    m_inventoryTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("ID"),
         QStringLiteral("适用类型"),
         QStringLiteral("物料名称"),
         QStringLiteral("规格"),
         QStringLiteral("材质"),
         QStringLiteral("颜色"),
         QStringLiteral("单位"),
         QStringLiteral("默认单价"),
         QStringLiteral("库存"),
         QStringLiteral("备注")});
    configureTableWidget(m_inventoryTableWidget);
    m_inventoryTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_inventoryTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_inventoryTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListIdColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListCategoryColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListNameColumn,
                                                                     QHeaderView::Stretch);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListSpecColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListMaterialColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListColorColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListUnitColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListUnitPriceColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListQuantityColumn,
                                                                     QHeaderView::ResizeToContents);
    m_inventoryTableWidget->horizontalHeader()->setSectionResizeMode(kInventoryListNoteColumn,
                                                                     QHeaderView::Stretch);
    inventoryListLayout->addWidget(m_inventoryTableWidget);
    inventoryListPageLayout->addWidget(inventoryListGroup);

    auto *blockedOrderGroup = new QGroupBox(QStringLiteral("不可发货订单"), inventoryDemandPage);
    auto *blockedOrderLayout = new QVBoxLayout(blockedOrderGroup);
    m_inventoryBlockedOrderTableWidget = new QTableWidget(blockedOrderGroup);
    m_inventoryBlockedOrderTableWidget->setColumnCount(7);
    m_inventoryBlockedOrderTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("订单ID"),
         QStringLiteral("客户"),
         QStringLiteral("产品类型"),
         QStringLiteral("具体型号"),
         QStringLiteral("基础配置"),
         QStringLiteral("套数"),
         QStringLiteral("状态")});
    configureTableWidget(m_inventoryBlockedOrderTableWidget);
    m_inventoryBlockedOrderTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_inventoryBlockedOrderTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_inventoryBlockedOrderTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inventoryBlockedOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderIdColumn,
                                                                                 QHeaderView::ResizeToContents);
    m_inventoryBlockedOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderCustomerColumn,
                                                                                 QHeaderView::Stretch);
    m_inventoryBlockedOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderCategoryColumn,
                                                                                 QHeaderView::ResizeToContents);
    m_inventoryBlockedOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderSkuColumn,
                                                                                 QHeaderView::Stretch);
    m_inventoryBlockedOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderConfigurationColumn,
                                                                                 QHeaderView::ResizeToContents);
    m_inventoryBlockedOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderQuantityColumn,
                                                                                 QHeaderView::ResizeToContents);
    m_inventoryBlockedOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderStatusColumn,
                                                                                 QHeaderView::ResizeToContents);
    blockedOrderLayout->addWidget(m_inventoryBlockedOrderTableWidget);

    auto *demandGroup = new QGroupBox(QStringLiteral("库存需求汇总"), inventoryDemandPage);
    auto *demandLayout = new QVBoxLayout(demandGroup);
    auto *demandScopeLayout = new QHBoxLayout();
    demandScopeLayout->addWidget(new QLabel(QStringLiteral("查看范围"), demandGroup));
    m_inventoryDemandScopeComboBox = new QComboBox(demandGroup);
    demandScopeLayout->addWidget(m_inventoryDemandScopeComboBox, 1);
    setupInventoryOutputControls();
    if (m_exportInventoryDemandButton != nullptr) {
        demandScopeLayout->addWidget(m_exportInventoryDemandButton);
    }
    demandLayout->addLayout(demandScopeLayout);
    m_demandSummaryTableWidget = new QTableWidget(demandGroup);
    m_demandSummaryTableWidget->setColumnCount(9);
    m_demandSummaryTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("产品类型"),
         QStringLiteral("物料名称"),
         QStringLiteral("规格"),
         QStringLiteral("材质"),
         QStringLiteral("颜色"),
         QStringLiteral("单位"),
         QStringLiteral("需求数量"),
         QStringLiteral("库存数量"),
         QStringLiteral("缺口数量")});
    configureTableWidget(m_demandSummaryTableWidget);
    m_demandSummaryTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_demandSummaryTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_demandSummaryTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryCategoryColumn,
                                                                         QHeaderView::ResizeToContents);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryNameColumn,
                                                                         QHeaderView::Stretch);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummarySpecColumn,
                                                                         QHeaderView::ResizeToContents);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryMaterialColumn,
                                                                         QHeaderView::ResizeToContents);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryColorColumn,
                                                                         QHeaderView::ResizeToContents);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryUnitColumn,
                                                                         QHeaderView::ResizeToContents);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryDemandColumn,
                                                                         QHeaderView::ResizeToContents);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryInventoryColumn,
                                                                         QHeaderView::ResizeToContents);
    m_demandSummaryTableWidget->horizontalHeader()->setSectionResizeMode(kDemandSummaryGapColumn,
                                                                         QHeaderView::ResizeToContents);
    demandLayout->addWidget(m_demandSummaryTableWidget);

    auto *readyOrderGroup = new QGroupBox(QStringLiteral("可发货订单"), inventoryListPage);
    auto *readyOrderLayout = new QVBoxLayout(readyOrderGroup);
    m_inventoryReadyOrderTableWidget = new QTableWidget(readyOrderGroup);
    m_inventoryReadyOrderTableWidget->setColumnCount(7);
    m_inventoryReadyOrderTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("订单ID"),
         QStringLiteral("客户"),
         QStringLiteral("产品类型"),
         QStringLiteral("具体型号"),
         QStringLiteral("基础配置"),
         QStringLiteral("套数"),
         QStringLiteral("状态")});
    configureTableWidget(m_inventoryReadyOrderTableWidget);
    m_inventoryReadyOrderTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_inventoryReadyOrderTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_inventoryReadyOrderTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inventoryReadyOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderIdColumn,
                                                                               QHeaderView::ResizeToContents);
    m_inventoryReadyOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderCustomerColumn,
                                                                               QHeaderView::Stretch);
    m_inventoryReadyOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderCategoryColumn,
                                                                               QHeaderView::ResizeToContents);
    m_inventoryReadyOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderSkuColumn,
                                                                               QHeaderView::Stretch);
    m_inventoryReadyOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderConfigurationColumn,
                                                                               QHeaderView::ResizeToContents);
    m_inventoryReadyOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderQuantityColumn,
                                                                               QHeaderView::ResizeToContents);
    m_inventoryReadyOrderTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderStatusColumn,
                                                                               QHeaderView::ResizeToContents);
    readyOrderLayout->addWidget(m_inventoryReadyOrderTableWidget);

    inventoryDemandPageLayout->addWidget(blockedOrderGroup);
    inventoryDemandPageLayout->addWidget(demandGroup);
    inventoryListPageLayout->addWidget(readyOrderGroup);

    m_inventoryModeTabWidget->addTab(inventoryEntryPage, QStringLiteral("库存录入"));
    m_inventoryModeTabWidget->addTab(inventoryListPage, QStringLiteral("库存列表"));
    m_inventoryModeTabWidget->addTab(inventoryDemandPage, QStringLiteral("库存需求"));
    rootLayout->addWidget(m_inventoryModeTabWidget);

    ui->mainTabWidget->insertTab(2, m_inventoryTab, QStringLiteral("库存管理"));

    m_shipmentModeTabWidget = new QTabWidget(ui->shipmentGroupBox);
    m_shipmentModeTabWidget->setDocumentMode(true);
    auto *shipmentOrderPage = new QWidget(m_shipmentModeTabWidget);
    auto *shipmentOrderPageLayout = new QVBoxLayout(shipmentOrderPage);
    shipmentOrderPageLayout->setContentsMargins(0, 0, 0, 0);
    shipmentOrderPageLayout->setSpacing(16);
    auto *shipmentComponentPage = new QWidget(m_shipmentModeTabWidget);
    auto *shipmentComponentPageLayout = new QVBoxLayout(shipmentComponentPage);
    shipmentComponentPageLayout->setContentsMargins(0, 0, 0, 0);
    shipmentComponentPageLayout->setSpacing(16);

    auto *structuredShipmentReadyGroup =
        new QGroupBox(QStringLiteral("可发货订单"), shipmentOrderPage);
    auto *structuredShipmentReadyLayout = new QVBoxLayout(structuredShipmentReadyGroup);
    m_structuredShipmentReadyLabel =
        new QLabel(QStringLiteral("显示当前待发货订单的可发货状态。"),
                   structuredShipmentReadyGroup);
    m_structuredShipmentReadyLabel->setWordWrap(true);
    structuredShipmentReadyLayout->addWidget(m_structuredShipmentReadyLabel);
    m_structuredShipmentReadyTableWidget = new QTableWidget(structuredShipmentReadyGroup);
    m_structuredShipmentReadyTableWidget->setColumnCount(7);
    m_structuredShipmentReadyTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("订单ID"),
         QStringLiteral("客户"),
         QStringLiteral("产品类型"),
         QStringLiteral("具体型号"),
         QStringLiteral("基础配置"),
         QStringLiteral("套数"),
         QStringLiteral("状态")});
    configureTableWidget(m_structuredShipmentReadyTableWidget);
    m_structuredShipmentReadyTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_structuredShipmentReadyTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_structuredShipmentReadyTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_structuredShipmentReadyTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderIdColumn,
                                                                                   QHeaderView::ResizeToContents);
    m_structuredShipmentReadyTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderCustomerColumn,
                                                                                   QHeaderView::Stretch);
    m_structuredShipmentReadyTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderCategoryColumn,
                                                                                   QHeaderView::ResizeToContents);
    m_structuredShipmentReadyTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderSkuColumn,
                                                                                   QHeaderView::Stretch);
    m_structuredShipmentReadyTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderConfigurationColumn,
                                                                                   QHeaderView::ResizeToContents);
    m_structuredShipmentReadyTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderQuantityColumn,
                                                                                   QHeaderView::ResizeToContents);
    m_structuredShipmentReadyTableWidget->horizontalHeader()->setSectionResizeMode(kReadyOrderStatusColumn,
                                                                                   QHeaderView::ResizeToContents);
    structuredShipmentReadyLayout->addWidget(m_structuredShipmentReadyTableWidget);

    if (ui->shipmentLayout != nullptr) {
        ui->shipmentLayout->removeItem(ui->shipmentOrderButtonLayout);
        ui->shipmentLayout->removeWidget(ui->shipmentComponentTableWidget);
        ui->shipmentLayout->removeItem(ui->componentShipmentFormLayout);
    }

    shipmentOrderPageLayout->addLayout(ui->shipmentOrderButtonLayout);
    shipmentOrderPageLayout->addWidget(structuredShipmentReadyGroup);
    shipmentComponentPageLayout->addWidget(ui->shipmentComponentTableWidget);
    shipmentComponentPageLayout->addLayout(ui->componentShipmentFormLayout);

    m_shipmentModeTabWidget->addTab(shipmentOrderPage, QStringLiteral("发货登记"));
    m_shipmentModeTabWidget->addTab(shipmentComponentPage, QStringLiteral("组件发货"));
    ui->shipmentLayout->addWidget(m_shipmentModeTabWidget);

    connect(m_saveInventoryButton, &QPushButton::clicked, this, &MainWindow::saveInventoryForm);
    connect(m_importInventoryButton, &QPushButton::clicked, this, &MainWindow::importInventoryItemsCsv);
    connect(m_clearInventoryButton, &QPushButton::clicked, this, &MainWindow::clearInventoryForm);
    connect(m_inventoryTableWidget,
            &QTableWidget::itemSelectionChanged,
            this,
            [this]() {
                const int row = m_inventoryTableWidget != nullptr ? m_inventoryTableWidget->currentRow() : -1;
                if (row < 0) {
                    return;
                }

                InventoryItemData item;
                item.id =
                    m_inventoryTableWidget->item(row, kInventoryListIdColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListIdColumn)->text().toInt()
                        : 0;
                item.productCategoryName =
                    m_inventoryTableWidget->item(row, kInventoryListCategoryColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListCategoryColumn)->text()
                        : QString();
                item.componentName =
                    m_inventoryTableWidget->item(row, kInventoryListNameColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListNameColumn)->text()
                        : QString();
                item.componentSpec =
                    m_inventoryTableWidget->item(row, kInventoryListSpecColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListSpecColumn)->text()
                        : QString();
                item.material =
                    m_inventoryTableWidget->item(row, kInventoryListMaterialColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListMaterialColumn)->text()
                        : QString();
                item.color =
                    m_inventoryTableWidget->item(row, kInventoryListColorColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListColorColumn)->text()
                        : QString();
                item.unitName =
                    m_inventoryTableWidget->item(row, kInventoryListUnitColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListUnitColumn)->text()
                        : QString();
                item.unitPrice =
                    m_inventoryTableWidget->item(row, kInventoryListUnitPriceColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListUnitPriceColumn)->text().toDouble()
                        : 0.0;
                item.currentQuantity =
                    m_inventoryTableWidget->item(row, kInventoryListQuantityColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListQuantityColumn)->text().toInt()
                        : 0;
                item.note =
                    m_inventoryTableWidget->item(row, kInventoryListNoteColumn) != nullptr
                        ? m_inventoryTableWidget->item(row, kInventoryListNoteColumn)->text()
                        : QString();
                for (const ProductCategoryOption &category : m_databaseManager.productCategories()) {
                    if (category.name == item.productCategoryName) {
                        item.productCategoryId = category.id;
                        break;
                    }
                }
                populateInventoryForm(item);
            });
    connect(m_inventoryBlockedOrderTableWidget,
            &QTableWidget::itemSelectionChanged,
            this,
            [this]() {
                if (m_inventoryBlockedOrderTableWidget == nullptr
                    || m_inventoryDemandScopeComboBox == nullptr) {
                    return;
                }

                const int row = m_inventoryBlockedOrderTableWidget->currentRow();
                if (row < 0) {
                    return;
                }

                const QTableWidgetItem *idItem =
                    m_inventoryBlockedOrderTableWidget->item(row, kReadyOrderIdColumn);
                const int orderId = idItem != nullptr ? idItem->data(Qt::UserRole).toInt() : 0;
                const int index = m_inventoryDemandScopeComboBox->findData(orderId);
                if (index >= 0) {
                    m_inventoryDemandScopeComboBox->setCurrentIndex(index);
                }
            });
    connect(m_inventoryDemandScopeComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { refreshInventoryDemandShortageTable(); });
    if (m_exportInventoryDemandButton != nullptr) {
        connect(m_exportInventoryDemandButton,
                &QPushButton::clicked,
                this,
                &MainWindow::exportInventoryDemandCsv);
    }

    connect(m_inventoryComponentComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                if (m_inventoryComponentComboBox == nullptr || index < 0) {
                    return;
                }
                const int optionId = m_inventoryComponentComboBox->itemData(index).toInt();
                if (optionId <= 0) {
                    return;
                }
                for (const InventoryItemData &item : m_databaseManager.inventoryItems()) {
                    if (item.id == optionId) {
                        m_inventoryComponentComboBox->setProperty("inventoryItemId", item.id);
                        if (m_inventoryCategoryComboBox != nullptr && item.productCategoryId > 0) {
                            const int categoryIndex =
                                m_inventoryCategoryComboBox->findData(item.productCategoryId);
                            if (categoryIndex >= 0) {
                                m_inventoryCategoryComboBox->setCurrentIndex(categoryIndex);
                            }
                        }
                        m_inventoryComponentSpecLineEdit->setText(item.componentSpec);
                        m_inventoryMaterialLineEdit->setText(item.material);
                        m_inventoryColorLineEdit->setText(item.color);
                        m_inventoryUnitLineEdit->setText(item.unitName);
                        if (m_inventoryUnitPriceSpinBox != nullptr) {
                            m_inventoryUnitPriceSpinBox->setValue(item.unitPrice);
                        }
                        m_inventoryQuantitySpinBox->setValue(item.currentQuantity);
                        if (m_inventoryNoteLineEdit != nullptr) {
                            m_inventoryNoteLineEdit->setText(item.note);
                        }
                        break;
                    }
                }
            });
    connect(m_inventoryComponentComboBox,
            &QComboBox::editTextChanged,
            this,
            [this](const QString &) {
                if (m_inventoryComponentComboBox != nullptr
                    && m_inventoryComponentComboBox->currentIndex() <= 0) {
                    m_inventoryComponentComboBox->setProperty("inventoryItemId", 0);
                }
            });
}

void MainWindow::setupStructuredOrderUi()
{
    if (ui->priceRowSpacer != nullptr) {
        ui->formGridLayout->removeItem(ui->priceRowSpacer);
        delete ui->priceRowSpacer;
        ui->priceRowSpacer = nullptr;
    }

    auto *productCategoryLabel = new QLabel(QStringLiteral("产品类型"), ui->orderInputGroupBox);
    m_structuredCategoryComboBox = new QComboBox(ui->orderInputGroupBox);
    auto *lampshadeNameLabel = new QLabel(QStringLiteral("默认灯罩"), ui->orderInputGroupBox);
    m_lampshadeNameLineEdit = new QLineEdit(ui->orderInputGroupBox);
    m_lampshadeNameLineEdit->setReadOnly(true);
    auto *remarkLabel = new QLabel(QStringLiteral("订单备注"), ui->orderInputGroupBox);
    m_orderRemarkLineEdit = new QLineEdit(ui->orderInputGroupBox);
    if (ui->bodyUnitPriceLabel != nullptr) {
        ui->bodyUnitPriceLabel->hide();
    }
    if (ui->bodyUnitPriceDoubleSpinBox != nullptr) {
        ui->bodyUnitPriceDoubleSpinBox->hide();
    }

    ui->formGridLayout->addWidget(productCategoryLabel, 1, 0);
    ui->formGridLayout->addWidget(m_structuredCategoryComboBox, 1, 1);
    ui->formGridLayout->addWidget(ui->productModelLabel, 1, 2);
    ui->formGridLayout->addWidget(ui->productModelComboBox, 1, 3);
    ui->formGridLayout->addWidget(ui->templateLabel, 2, 0);
    ui->formGridLayout->addWidget(ui->templateComboBox, 2, 1);
    ui->formGridLayout->addWidget(ui->quantitySetsLabel, 2, 2);
    ui->formGridLayout->addWidget(ui->quantitySetsSpinBox, 2, 3);
    ui->formGridLayout->addWidget(lampshadeNameLabel, 3, 0);
    ui->formGridLayout->addWidget(m_lampshadeNameLineEdit, 3, 1);
    ui->formGridLayout->addWidget(ui->unitPriceLabel, 3, 2);
    ui->formGridLayout->addWidget(ui->unitPriceDoubleSpinBox, 3, 3);
    ui->formGridLayout->addWidget(remarkLabel, 4, 0);
    ui->formGridLayout->addWidget(m_orderRemarkLineEdit, 4, 1, 1, 3);

    enableComboBoxFiltering(m_structuredCategoryComboBox);
}

void MainWindow::refreshComboBoxPopupWidth(QComboBox *comboBox) const
{
    if (comboBox == nullptr) {
        return;
    }

    int maxWidth = comboBox->width();
    const QFontMetrics metrics(comboBox->font());
    for (int index = 0; index < comboBox->count(); ++index) {
        maxWidth = qMax(maxWidth, metrics.horizontalAdvance(comboBox->itemText(index)) + 56);
    }

    comboBox->view()->setMinimumWidth(maxWidth);
    comboBox->view()->setTextElideMode(Qt::ElideNone);
}

void MainWindow::enableComboBoxFiltering(QComboBox *comboBox) const
{
    if (comboBox == nullptr) {
        return;
    }

    comboBox->setEditable(true);
    comboBox->setInsertPolicy(QComboBox::NoInsert);
    comboBox->setMinimumContentsLength(18);
    comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    comboBox->setMaxVisibleItems(20);
    refreshComboBoxPopupWidth(comboBox);

    auto *completer = new QCompleter(comboBox->model(), comboBox);
    completer->setCompletionColumn(comboBox->modelColumn());
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    comboBox->setCompleter(completer);
}

QComboBox *MainWindow::createInventoryComponentComboBox(int productCategoryId, QWidget *parent)
{
    auto *comboBox = new QComboBox(parent);
    comboBox->addItem(QString(), 0);

    const QList<ProductComponentOption> options =
        m_databaseManager.inventoryComponentOptions(productCategoryId);
    QHash<QString, QSet<QString>> priceVariantsByIdentity;
    for (const ProductComponentOption &option : options) {
        priceVariantsByIdentity[inventoryOptionIdentityKey(option)].insert(
            QString::number(option.unitPrice, 'f', 4));
    }

    for (const ProductComponentOption &option : options) {
        comboBox->addItem(inventoryOptionDisplayText(option, priceVariantsByIdentity), option.id);
        comboBox->setItemData(comboBox->count() - 1, option.unitPrice, Qt::UserRole + 1);
    }

    enableComboBoxFiltering(comboBox);
    return comboBox;
}

void MainWindow::populateStructuredRowFromOption(int row, const ProductComponentOption &option)
{
    if (ui == nullptr || ui->componentTableWidget == nullptr || row < 0
        || row >= ui->componentTableWidget->rowCount()) {
        return;
    }

    auto setTextIfPresent = [this, row](int column, const QString &text) {
        if (QTableWidgetItem *item = ui->componentTableWidget->item(row, column)) {
            item->setText(text);
        }
    };

    if (QTableWidgetItem *nameItem = ui->componentTableWidget->item(row, kComponentNameColumn)) {
        nameItem->setText(option.name);
        nameItem->setData(Qt::UserRole, option.id);
        nameItem->setData(Qt::UserRole + 1, QStringLiteral("extra"));
    }
    setTextIfPresent(kComponentSpecColumn, option.componentSpec);
    setTextIfPresent(kComponentMaterialColumn, option.material);
    setTextIfPresent(kComponentColorColumn, option.color);
    setTextIfPresent(kComponentUnitColumn, option.unitName.trimmed().isEmpty() ? QStringLiteral("件")
                                                                               : option.unitName);
    setTextIfPresent(kUnitPriceColumn, QString::number(option.unitPrice, 'f', 2));
    updateStructuredComponentTotals();
}

int MainWindow::structuredComponentRowForWidget(QWidget *widget) const
{
    if (ui == nullptr || ui->componentTableWidget == nullptr || widget == nullptr) {
        return -1;
    }

    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        QWidget *cellWidget = ui->componentTableWidget->cellWidget(row, kComponentNameColumn);
        if (cellWidget == widget || (widget->parentWidget() != nullptr && cellWidget == widget->parentWidget())
            || (cellWidget != nullptr && cellWidget->isAncestorOf(widget))) {
            return row;
        }
    }

    return -1;
}

int MainWindow::currentStructuredProductCategoryId()
{
    const int skuId = ui != nullptr && ui->productModelComboBox != nullptr
                          ? ui->productModelComboBox->currentData().toInt()
                          : 0;
    for (const ProductSkuOption &sku : m_databaseManager.productSkus()) {
        if (sku.id == skuId) {
            return sku.productCategoryId;
        }
    }
    return 0;
}

void MainWindow::loadStructuredOrderSkus()
{
    if (m_structuredCategoryComboBox == nullptr) {
        return;
    }

    const QList<ProductCategoryOption> categories = m_databaseManager.productCategories();
    const int previousCategoryId = m_structuredCategoryComboBox->currentData().toInt();
    {
        const QSignalBlocker blocker(m_structuredCategoryComboBox);
        m_structuredCategoryComboBox->clear();
        for (const ProductCategoryOption &category : categories) {
            m_structuredCategoryComboBox->addItem(category.name, category.id);
        }
        int targetIndex = m_structuredCategoryComboBox->findData(previousCategoryId);
        if (targetIndex < 0 && m_structuredCategoryComboBox->count() > 0) {
            targetIndex = 0;
        }
        if (targetIndex >= 0) {
            m_structuredCategoryComboBox->setCurrentIndex(targetIndex);
        }
    }
    refreshComboBoxPopupWidth(m_structuredCategoryComboBox);

    const QSignalBlocker blocker(ui->productModelComboBox);
    ui->productModelComboBox->clear();

    const int categoryId = m_structuredCategoryComboBox->currentData().toInt();
    const QList<ProductSkuOption> skus = m_databaseManager.productSkus(categoryId);
    for (const ProductSkuOption &sku : skus) {
        ui->productModelComboBox->addItem(sku.skuName, sku.id);
    }
    refreshComboBoxPopupWidth(ui->productModelComboBox);

    loadBaseConfigurationsForCurrentSku();
}

void MainWindow::loadBaseConfigurationsForCurrentSku()
{
    const int skuId = ui->productModelComboBox->currentData().toInt();
    ProductSkuOption currentSku;
    for (const ProductSkuOption &sku : m_databaseManager.productSkus()) {
        if (sku.id == skuId) {
            currentSku = sku;
            break;
        }
    }

    if (m_structuredCategoryComboBox != nullptr && currentSku.productCategoryId > 0) {
        const int categoryIndex = m_structuredCategoryComboBox->findData(currentSku.productCategoryId);
        if (categoryIndex >= 0) {
            const QSignalBlocker blocker(m_structuredCategoryComboBox);
            m_structuredCategoryComboBox->setCurrentIndex(categoryIndex);
        }
    }
    if (m_lampshadeNameLineEdit != nullptr) {
        m_lampshadeNameLineEdit->setText(currentSku.lampshadeName);
    }

    const QSignalBlocker blocker(ui->templateComboBox);
    ui->templateComboBox->clear();
    for (const BaseConfigurationOption &configuration :
         m_databaseManager.baseConfigurationsForCategory(currentSku.productCategoryId)) {
        ui->templateComboBox->addItem(configuration.configName, configuration.id);
        ui->templateComboBox->setItemData(ui->templateComboBox->count() - 1,
                                          configuration.configPrice,
                                          Qt::UserRole + 1);
    }
    refreshComboBoxPopupWidth(ui->templateComboBox);
    if (ui->unitPriceDoubleSpinBox != nullptr) {
        ui->unitPriceDoubleSpinBox->setValue(
            ui->templateComboBox->currentData(Qt::UserRole + 1).toDouble());
    }

    updateStructuredOrderDisplayFields();
    rebuildStructuredOrderComponents();
}

void MainWindow::rebuildStructuredOrderComponents()
{
    const int configurationId = ui->templateComboBox->currentData().toInt();
    ProductSkuOption currentSku;
    const int skuId = ui->productModelComboBox->currentData().toInt();
    for (const ProductSkuOption &sku : m_databaseManager.productSkus()) {
        if (sku.id == skuId) {
            currentSku = sku;
            break;
        }
    }
    QList<StructuredOrderComponentData> components;
    for (const BaseConfigurationComponentData &component :
         m_databaseManager.baseConfigurationComponents(configurationId)) {
        StructuredOrderComponentData row;
        row.sourceComponentId = component.id;
        row.componentName = component.componentName;
        row.componentSpec = component.componentSpec;
        row.material = component.material;
        row.color = component.color;
        row.unitName = component.unitName;
        row.quantityPerSet = component.quantity;
        row.unitAmount = component.unitAmount;
        row.sourceType = QStringLiteral("base_bom");
        row.adjustmentType = QStringLiteral("none");
        components.append(row);
    }

    if (m_lampshadeNameLineEdit != nullptr && !m_lampshadeNameLineEdit->text().trimmed().isEmpty()) {
        StructuredOrderComponentData lampshadeComponent;
        lampshadeComponent.componentName = m_lampshadeNameLineEdit->text().trimmed();
        lampshadeComponent.unitName = QStringLiteral("件");
        lampshadeComponent.quantityPerSet = 1;
        lampshadeComponent.unitAmount = currentSku.lampshadeUnitPrice;
        lampshadeComponent.sourceType = QStringLiteral("lampshade");
        lampshadeComponent.adjustmentType = QStringLiteral("none");
        components.append(lampshadeComponent);
    }

    setStructuredComponentTableRows(components);
    updateStructuredOrderDisplayFields();
}

void MainWindow::setStructuredComponentTableRows(const QList<StructuredOrderComponentData> &components)
{
    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->componentTableWidget);
    ui->componentTableWidget->setRowCount(0);

    for (const StructuredOrderComponentData &component : components) {
        const int row = ui->componentTableWidget->rowCount();
        ui->componentTableWidget->insertRow(row);

        auto *nameItem = new QTableWidgetItem(component.componentName);
        auto *specItem = new QTableWidgetItem(component.componentSpec);
        auto *materialItem = new QTableWidgetItem(component.material);
        auto *colorItem = new QTableWidgetItem(component.color);
        auto *unitItem = new QTableWidgetItem(component.unitName.isEmpty() ? QStringLiteral("件")
                                                                           : component.unitName);
        auto *quantityItem = new QTableWidgetItem(QString::number(component.quantityPerSet));
        auto *unitPriceItem = new QTableWidgetItem(QString::number(component.unitAmount, 'f', 2));
        auto *sourceItem = new QTableWidgetItem(structuredSourceDisplayText(component.sourceType));
        auto *requiredItem = new QTableWidgetItem;
        auto *lineAmountItem = new QTableWidgetItem;

        nameItem->setData(Qt::UserRole, component.sourceComponentId);
        nameItem->setData(Qt::UserRole + 1, component.sourceType);
        sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
        requiredItem->setFlags(requiredItem->flags() & ~Qt::ItemIsEditable);
        lineAmountItem->setFlags(lineAmountItem->flags() & ~Qt::ItemIsEditable);

        ui->componentTableWidget->setItem(row, kComponentNameColumn, nameItem);
        ui->componentTableWidget->setItem(row, kComponentSpecColumn, specItem);
        ui->componentTableWidget->setItem(row, kComponentMaterialColumn, materialItem);
        ui->componentTableWidget->setItem(row, kComponentColorColumn, colorItem);
        ui->componentTableWidget->setItem(row, kComponentUnitColumn, unitItem);
        ui->componentTableWidget->setItem(row, kQuantityPerSetColumn, quantityItem);
        ui->componentTableWidget->setItem(row, kUnitPriceColumn, unitPriceItem);
        ui->componentTableWidget->setItem(row, kSourceTypeColumn, sourceItem);
        ui->componentTableWidget->setItem(row, kTotalRequiredColumn, requiredItem);
        ui->componentTableWidget->setItem(row, kComponentTotalPriceColumn, lineAmountItem);
    }

    m_updatingComponentTable = false;
    updateStructuredComponentTotals();
}

QList<StructuredOrderComponentData> MainWindow::collectStructuredComponentsFromTable() const
{
    QList<StructuredOrderComponentData> components;
    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = ui->componentTableWidget->item(row, kComponentNameColumn);
        const QComboBox *nameComboBox =
            qobject_cast<QComboBox *>(ui->componentTableWidget->cellWidget(row, kComponentNameColumn));
        const QTableWidgetItem *specItem = ui->componentTableWidget->item(row, kComponentSpecColumn);
        const QTableWidgetItem *materialItem =
            ui->componentTableWidget->item(row, kComponentMaterialColumn);
        const QTableWidgetItem *colorItem = ui->componentTableWidget->item(row, kComponentColorColumn);
        const QTableWidgetItem *unitItem = ui->componentTableWidget->item(row, kComponentUnitColumn);
        const QTableWidgetItem *quantityItem =
            ui->componentTableWidget->item(row, kQuantityPerSetColumn);
        const QTableWidgetItem *unitPriceItem = ui->componentTableWidget->item(row, kUnitPriceColumn);
        const QTableWidgetItem *sourceItem = ui->componentTableWidget->item(row, kSourceTypeColumn);

        StructuredOrderComponentData component;
        component.sourceComponentId = nameItem != nullptr ? nameItem->data(Qt::UserRole).toInt() : 0;
        component.componentName = nameComboBox != nullptr
                                      ? nameComboBox->currentText().section('|', 0, 0).trimmed()
                                      : nameItem != nullptr ? nameItem->text().trimmed() : QString();
        if (nameComboBox != nullptr && nameComboBox->currentData().toInt() > 0
            && nameComboBox->currentText() == nameComboBox->itemText(nameComboBox->currentIndex())) {
            component.sourceComponentId = nameComboBox->currentData().toInt();
        } else if (nameComboBox != nullptr) {
            component.sourceComponentId = 0;
        }
        component.componentSpec = specItem != nullptr ? specItem->text().trimmed() : QString();
        component.material = materialItem != nullptr ? materialItem->text().trimmed() : QString();
        component.color = colorItem != nullptr ? colorItem->text().trimmed() : QString();
        component.unitName = unitItem != nullptr ? unitItem->text().trimmed() : QStringLiteral("件");
        component.quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
        component.unitAmount = unitPriceItem != nullptr ? unitPriceItem->text().toDouble() : 0.0;
        component.sourceType = sourceItem != nullptr
                                   ? structuredSourceStorageValue(sourceItem->text())
                                   : QStringLiteral("extra");
        component.adjustmentType = component.sourceType == QStringLiteral("adjusted_final")
                                       ? QStringLiteral("modified")
                                       : component.sourceType == QStringLiteral("extra")
                                       ? QStringLiteral("added")
                                       : QStringLiteral("none");
        components.append(component);
    }

    return components;
}

void MainWindow::addStructuredComponentRow()
{
    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->componentTableWidget);
    const int row = ui->componentTableWidget->rowCount();
    ui->componentTableWidget->insertRow(row);

    auto *nameItem = new QTableWidgetItem;
    auto *specItem = new QTableWidgetItem;
    auto *materialItem = new QTableWidgetItem;
    auto *colorItem = new QTableWidgetItem;
    auto *unitItem = new QTableWidgetItem(QStringLiteral("件"));
    auto *quantityItem = new QTableWidgetItem(QStringLiteral("1"));
    auto *unitPriceItem =
        new QTableWidgetItem(QString::number(kDefaultNonZeroUnitPrice, 'f', 2));
    auto *sourceItem = new QTableWidgetItem(QStringLiteral("附加新增"));
    auto *requiredItem = new QTableWidgetItem;
    auto *lineAmountItem = new QTableWidgetItem;

    nameItem->setData(Qt::UserRole, 0);
    nameItem->setData(Qt::UserRole + 1, QStringLiteral("extra"));
    sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
    requiredItem->setFlags(requiredItem->flags() & ~Qt::ItemIsEditable);
    lineAmountItem->setFlags(lineAmountItem->flags() & ~Qt::ItemIsEditable);

    ui->componentTableWidget->setItem(row, kComponentNameColumn, nameItem);
    ui->componentTableWidget->setItem(row, kComponentSpecColumn, specItem);
    ui->componentTableWidget->setItem(row, kComponentMaterialColumn, materialItem);
    ui->componentTableWidget->setItem(row, kComponentColorColumn, colorItem);
    ui->componentTableWidget->setItem(row, kComponentUnitColumn, unitItem);
    ui->componentTableWidget->setItem(row, kQuantityPerSetColumn, quantityItem);
    ui->componentTableWidget->setItem(row, kUnitPriceColumn, unitPriceItem);
    ui->componentTableWidget->setItem(row, kSourceTypeColumn, sourceItem);
    ui->componentTableWidget->setItem(row, kTotalRequiredColumn, requiredItem);
    ui->componentTableWidget->setItem(row, kComponentTotalPriceColumn, lineAmountItem);

    QComboBox *componentComboBox =
        createInventoryComponentComboBox(currentStructuredProductCategoryId(), ui->componentTableWidget);
    componentComboBox->installEventFilter(this);
    if (componentComboBox->lineEdit() != nullptr) {
        componentComboBox->lineEdit()->installEventFilter(this);
    }
    ui->componentTableWidget->setCellWidget(row, kComponentNameColumn, componentComboBox);
    connect(componentComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this, componentComboBox](int index) {
                if (m_updatingComponentTable || componentComboBox == nullptr || index < 0) {
                    return;
                }

                int row = -1;
                for (int currentRow = 0; currentRow < ui->componentTableWidget->rowCount(); ++currentRow) {
                    if (ui->componentTableWidget->cellWidget(currentRow, kComponentNameColumn)
                        == componentComboBox) {
                        row = currentRow;
                        break;
                    }
                }
                if (row < 0) {
                    return;
                }
                const int optionId = componentComboBox->itemData(index).toInt();
                if (optionId <= 0) {
                    if (QTableWidgetItem *nameItem =
                            ui->componentTableWidget->item(row, kComponentNameColumn)) {
                        nameItem->setText(componentComboBox->currentText().section('|', 0, 0).trimmed());
                        nameItem->setData(Qt::UserRole, 0);
                    }
                    updateStructuredComponentTotals();
                    return;
                }

                for (const ProductComponentOption &option :
                     m_databaseManager.inventoryComponentOptions(currentStructuredProductCategoryId())) {
                    if (option.id == optionId) {
                        populateStructuredRowFromOption(row, option);
                        break;
                    }
                }
            });

    m_updatingComponentTable = false;
    updateStructuredComponentTotals();
    componentComboBox->setFocus();
}

void MainWindow::markStructuredRowAdjusted(int row, int column)
{
    if (m_updatingComponentTable || row < 0 || row >= ui->componentTableWidget->rowCount()
        || column == kSourceTypeColumn || column == kTotalRequiredColumn
        || column == kComponentTotalPriceColumn) {
        return;
    }

    QTableWidgetItem *nameItem = ui->componentTableWidget->item(row, kComponentNameColumn);
    QTableWidgetItem *sourceItem = ui->componentTableWidget->item(row, kSourceTypeColumn);
    if (nameItem == nullptr || sourceItem == nullptr) {
        return;
    }

    const QString originalType = nameItem->data(Qt::UserRole + 1).toString();
    if (originalType == QStringLiteral("base_bom") || originalType == QStringLiteral("lampshade")) {
        sourceItem->setText(QStringLiteral("调整后最终"));
    }
}

void MainWindow::updateStructuredComponentTotals()
{
    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->componentTableWidget);
    const int orderQuantity = ui->quantitySetsSpinBox->value();
    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        QTableWidgetItem *quantityItem =
            ui->componentTableWidget->item(row, kQuantityPerSetColumn);
        QTableWidgetItem *unitPriceItem = ui->componentTableWidget->item(row, kUnitPriceColumn);
        QTableWidgetItem *requiredItem = ui->componentTableWidget->item(row, kTotalRequiredColumn);
        QTableWidgetItem *lineAmountItem =
            ui->componentTableWidget->item(row, kComponentTotalPriceColumn);
        const int quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
        const double unitAmount = unitPriceItem != nullptr ? unitPriceItem->text().toDouble() : 0.0;
        const int requiredQuantity = quantityPerSet * orderQuantity;
        const double totalAmount = static_cast<double>(requiredQuantity) * unitAmount;
        if (requiredItem != nullptr) {
            requiredItem->setText(quantityPerSet > 0 ? QString::number(requiredQuantity) : QString());
        }
        if (lineAmountItem != nullptr) {
            lineAmountItem->setText(quantityPerSet > 0 ? QString::number(totalAmount, 'f', 2)
                                                      : QString());
        }
    }
    m_updatingComponentTable = false;
    updateStructuredOrderDisplayFields();
}

void MainWindow::updateStructuredOrderDisplayFields()
{
    if (ui == nullptr || ui->unitPriceDoubleSpinBox == nullptr) {
        return;
    }

    double configPrice = 0.0;
    if (ui->componentTableWidget != nullptr) {
        for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
            const QTableWidgetItem *quantityItem =
                ui->componentTableWidget->item(row, kQuantityPerSetColumn);
            const QTableWidgetItem *unitPriceItem =
                ui->componentTableWidget->item(row, kUnitPriceColumn);
            const int quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
            const double unitAmount = unitPriceItem != nullptr ? unitPriceItem->text().toDouble() : 0.0;
            if (quantityPerSet > 0) {
                configPrice += static_cast<double>(quantityPerSet) * unitAmount;
            }
        }
    }

    ui->unitPriceDoubleSpinBox->setValue(configPrice);
}

void MainWindow::clearStructuredOrderForm()
{
    ui->orderDateEdit->setDate(QDate::currentDate());
    ui->customerNameLineEdit->clear();
    ui->quantitySetsSpinBox->setValue(1);
    if (m_orderRemarkLineEdit != nullptr) {
        m_orderRemarkLineEdit->clear();
    }
    loadStructuredOrderSkus();
}

bool MainWindow::validateStructuredOrderInput(QString *errorMessage) const
{
    if (ui->customerNameLineEdit->text().trimmed().isEmpty()) {
        *errorMessage = QStringLiteral("客户名称不能为空。");
        return false;
    }
    if (ui->productModelComboBox->currentIndex() < 0) {
        *errorMessage = QStringLiteral("请选择具体型号。");
        return false;
    }
    if (ui->templateComboBox->currentIndex() < 0) {
        *errorMessage = QStringLiteral("请选择基础配置。");
        return false;
    }
    const QList<StructuredOrderComponentData> components = collectStructuredComponentsFromTable();
    bool hasValidComponent = false;
    for (const StructuredOrderComponentData &component : components) {
        if (component.componentName.isEmpty()) {
            continue;
        }
        if (component.quantityPerSet <= 0) {
            *errorMessage = QStringLiteral("组件每套数量必须大于 0。");
            return false;
        }
        if (component.unitAmount < 0.0) {
            *errorMessage = QStringLiteral("组件单价不能小于 0。");
            return false;
        }
        hasValidComponent = true;
    }

    if (!hasValidComponent) {
        *errorMessage = QStringLiteral("请至少保留一条有效订单组件。");
        return false;
    }

    return true;
}

void MainWindow::loadProductDataPage()
{
    loadCategoryEditor();
    refreshManagementCategorySelectors();
    loadSkuEditor();
    loadConfigurationEditor();
    loadBomEditor();
}

void MainWindow::loadCategoryEditor()
{
    if (m_categoryTableWidget == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_categoryTableWidget);
    m_categoryTableWidget->setRowCount(0);
    for (const ProductCategoryOption &category : m_databaseManager.productCategories()) {
        const int row = m_categoryTableWidget->rowCount();
        m_categoryTableWidget->insertRow(row);
        auto *idItem = new QTableWidgetItem(QString::number(category.id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_categoryTableWidget->setItem(row, kCategoryEditorIdColumn, idItem);
        m_categoryTableWidget->setItem(row,
                                       kCategoryEditorNameColumn,
                                       new QTableWidgetItem(category.name));
    }
}

void MainWindow::saveCategoryEditor()
{
    for (int row = 0; row < m_categoryTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *nameItem =
            m_categoryTableWidget->item(row, kCategoryEditorNameColumn);
        if (nameItem == nullptr || nameItem->text().trimmed().isEmpty()) {
            continue;
        }

        ProductCategoryOption category;
        const QTableWidgetItem *idItem = m_categoryTableWidget->item(row, kCategoryEditorIdColumn);
        category.id = idItem != nullptr ? idItem->text().toInt() : 0;
        category.name = nameItem->text().trimmed();
        if (!m_databaseManager.saveProductCategory(category)) {
            QMessageBox::critical(this, QStringLiteral("保存失败"), m_databaseManager.lastError());
            return;
        }
    }

    loadProductDataPage();
    loadStructuredOrderSkus();
    loadStructuredQuerySkus();
    showStatusMessage(QStringLiteral("产品类型已保存"), 3000);
}

void MainWindow::addCategoryEditorRow()
{
    const int row = m_categoryTableWidget->rowCount();
    m_categoryTableWidget->insertRow(row);
    auto *idItem = new QTableWidgetItem;
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    m_categoryTableWidget->setItem(row, kCategoryEditorIdColumn, idItem);
    m_categoryTableWidget->setItem(row, kCategoryEditorNameColumn, new QTableWidgetItem);
    m_categoryTableWidget->setCurrentCell(row, kCategoryEditorNameColumn);
}

void MainWindow::refreshManagementCategorySelectors()
{
    const QList<ProductCategoryOption> categories = m_databaseManager.productCategories();

    if (m_skuCategoryComboBox != nullptr) {
        const QSignalBlocker blocker(m_skuCategoryComboBox);
        const int currentId = m_skuCategoryComboBox->currentData().toInt();
        m_skuCategoryComboBox->clear();
        for (const ProductCategoryOption &category : categories) {
            m_skuCategoryComboBox->addItem(category.name, category.id);
        }
        int targetIndex = m_skuCategoryComboBox->findData(currentId);
        if (targetIndex < 0 && m_skuCategoryComboBox->count() > 0) {
            targetIndex = 0;
        }
        if (targetIndex >= 0) {
            m_skuCategoryComboBox->setCurrentIndex(targetIndex);
        }
    }

    if (m_configurationCategoryComboBox != nullptr) {
        const QSignalBlocker blocker(m_configurationCategoryComboBox);
        const int currentId = m_configurationCategoryComboBox->currentData().toInt();
        m_configurationCategoryComboBox->clear();
        for (const ProductCategoryOption &category : categories) {
            m_configurationCategoryComboBox->addItem(category.name, category.id);
        }
        int targetIndex = m_configurationCategoryComboBox->findData(currentId);
        if (targetIndex < 0 && m_configurationCategoryComboBox->count() > 0) {
            targetIndex = 0;
        }
        if (targetIndex >= 0) {
            m_configurationCategoryComboBox->setCurrentIndex(targetIndex);
        }
    }

    if (m_bomConfigurationComboBox != nullptr) {
        const QSignalBlocker blocker(m_bomConfigurationComboBox);
        const int currentId = m_bomConfigurationComboBox->currentData().toInt();
        m_bomConfigurationComboBox->clear();
        for (const ProductCategoryOption &category : categories) {
            for (const BaseConfigurationOption &configuration :
                 m_databaseManager.baseConfigurationsForCategory(category.id)) {
                m_bomConfigurationComboBox->addItem(
                    QStringLiteral("%1 / %2").arg(category.name, configuration.configName),
                    configuration.id);
            }
        }
        int targetIndex = m_bomConfigurationComboBox->findData(currentId);
        if (targetIndex < 0 && m_bomConfigurationComboBox->count() > 0) {
            targetIndex = 0;
        }
        if (targetIndex >= 0) {
            m_bomConfigurationComboBox->setCurrentIndex(targetIndex);
        }
    }
}

void MainWindow::loadSkuEditor()
{
    if (m_skuTableWidget == nullptr || m_skuCategoryComboBox == nullptr) {
        return;
    }

    const int categoryId = m_skuCategoryComboBox->currentData().toInt();
    const QSignalBlocker blocker(m_skuTableWidget);
    m_skuTableWidget->setRowCount(0);
    for (const ProductSkuOption &sku : m_databaseManager.productSkus(categoryId)) {
        const int row = m_skuTableWidget->rowCount();
        m_skuTableWidget->insertRow(row);
        auto *idItem = new QTableWidgetItem(QString::number(sku.id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_skuTableWidget->setItem(row, kSkuEditorIdColumn, idItem);
        m_skuTableWidget->setItem(row, kSkuEditorNameColumn, new QTableWidgetItem(sku.skuName));
        m_skuTableWidget->setItem(row,
                                  kSkuEditorLampshadeColumn,
                                  new QTableWidgetItem(sku.lampshadeName));
        m_skuTableWidget->setItem(row,
                                  kSkuEditorLampshadePriceColumn,
                                  new QTableWidgetItem(QString::number(sku.lampshadeUnitPrice, 'f', 2)));
    }
}

void MainWindow::saveSkuEditor()
{
    const int categoryId = m_skuCategoryComboBox != nullptr ? m_skuCategoryComboBox->currentData().toInt() : 0;
    for (int row = 0; row < m_skuTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = m_skuTableWidget->item(row, kSkuEditorNameColumn);
        if (nameItem == nullptr || nameItem->text().trimmed().isEmpty()) {
            continue;
        }

        ProductSkuOption sku;
        const QTableWidgetItem *idItem = m_skuTableWidget->item(row, kSkuEditorIdColumn);
        sku.id = idItem != nullptr ? idItem->text().toInt() : 0;
        sku.productCategoryId = categoryId;
        sku.skuName = nameItem->text().trimmed();
        sku.lampshadeName =
            m_skuTableWidget->item(row, kSkuEditorLampshadeColumn) != nullptr
                ? m_skuTableWidget->item(row, kSkuEditorLampshadeColumn)->text().trimmed()
                : QString();
        sku.lampshadeUnitPrice =
            m_skuTableWidget->item(row, kSkuEditorLampshadePriceColumn) != nullptr
                ? m_skuTableWidget->item(row, kSkuEditorLampshadePriceColumn)->text().toDouble()
                : 0.0;
        if (!m_databaseManager.saveProductSku(sku)) {
            QMessageBox::critical(this, QStringLiteral("保存失败"), m_databaseManager.lastError());
            return;
        }
    }

    loadProductDataPage();
    loadStructuredOrderSkus();
    loadStructuredQuerySkus();
    showStatusMessage(QStringLiteral("具体型号已保存"), 3000);
}

void MainWindow::addSkuEditorRow()
{
    const int row = m_skuTableWidget->rowCount();
    m_skuTableWidget->insertRow(row);
    auto *idItem = new QTableWidgetItem;
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    m_skuTableWidget->setItem(row, kSkuEditorIdColumn, idItem);
    m_skuTableWidget->setItem(row, kSkuEditorNameColumn, new QTableWidgetItem);
    m_skuTableWidget->setItem(row, kSkuEditorLampshadeColumn, new QTableWidgetItem);
    m_skuTableWidget->setItem(row,
                              kSkuEditorLampshadePriceColumn,
                              new QTableWidgetItem(QString::number(kDefaultNonZeroUnitPrice, 'f', 2)));
    m_skuTableWidget->setCurrentCell(row, kSkuEditorNameColumn);
}

void MainWindow::loadConfigurationEditor()
{
    if (m_configurationTableWidget == nullptr || m_configurationCategoryComboBox == nullptr) {
        return;
    }

    const int categoryId = m_configurationCategoryComboBox->currentData().toInt();
    const QSignalBlocker blocker(m_configurationTableWidget);
    m_configurationTableWidget->setRowCount(0);
    for (const BaseConfigurationOption &configuration :
         m_databaseManager.baseConfigurationsForCategory(categoryId)) {
        const int row = m_configurationTableWidget->rowCount();
        m_configurationTableWidget->insertRow(row);
        auto *idItem = new QTableWidgetItem(QString::number(configuration.id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_configurationTableWidget->setItem(row, kConfigurationEditorIdColumn, idItem);
        m_configurationTableWidget->setItem(row,
                                            kConfigurationEditorCodeColumn,
                                            new QTableWidgetItem(configuration.configCode));
        m_configurationTableWidget->setItem(row,
                                            kConfigurationEditorNameColumn,
                                            new QTableWidgetItem(configuration.configName));
        m_configurationTableWidget->setItem(
            row,
            kConfigurationEditorPriceColumn,
            new QTableWidgetItem(QString::number(configuration.configPrice, 'f', 2)));
        m_configurationTableWidget->setItem(
            row,
            kConfigurationEditorSortColumn,
            new QTableWidgetItem(QString::number(configuration.sortOrder)));
    }
}

void MainWindow::saveConfigurationEditor()
{
    const int categoryId = m_configurationCategoryComboBox != nullptr
                               ? m_configurationCategoryComboBox->currentData().toInt()
                               : 0;
    for (int row = 0; row < m_configurationTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *codeItem =
            m_configurationTableWidget->item(row, kConfigurationEditorCodeColumn);
        if (codeItem == nullptr || codeItem->text().trimmed().isEmpty()) {
            continue;
        }

        BaseConfigurationOption configuration;
        const QTableWidgetItem *idItem =
            m_configurationTableWidget->item(row, kConfigurationEditorIdColumn);
        configuration.id = idItem != nullptr ? idItem->text().toInt() : 0;
        configuration.productCategoryId = categoryId;
        configuration.configCode = codeItem->text().trimmed();
        configuration.configName =
            m_configurationTableWidget->item(row, kConfigurationEditorNameColumn) != nullptr
                ? m_configurationTableWidget->item(row, kConfigurationEditorNameColumn)
                      ->text()
                      .trimmed()
                : QString();
        configuration.configPrice =
            m_configurationTableWidget->item(row, kConfigurationEditorPriceColumn) != nullptr
                ? m_configurationTableWidget->item(row, kConfigurationEditorPriceColumn)
                      ->text()
                      .toDouble()
                : 0.0;
        configuration.sortOrder =
            m_configurationTableWidget->item(row, kConfigurationEditorSortColumn) != nullptr
                ? m_configurationTableWidget->item(row, kConfigurationEditorSortColumn)
                      ->text()
                      .toInt()
                : row + 1;
        if (!m_databaseManager.saveBaseConfiguration(configuration)) {
            QMessageBox::critical(this, QStringLiteral("保存失败"), m_databaseManager.lastError());
            return;
        }
    }

    loadProductDataPage();
    loadStructuredOrderSkus();
    showStatusMessage(QStringLiteral("基础配置已保存"), 3000);
}

void MainWindow::addConfigurationEditorRow()
{
    const int row = m_configurationTableWidget->rowCount();
    m_configurationTableWidget->insertRow(row);
    auto *idItem = new QTableWidgetItem;
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    m_configurationTableWidget->setItem(row, kConfigurationEditorIdColumn, idItem);
    m_configurationTableWidget->setItem(row, kConfigurationEditorCodeColumn, new QTableWidgetItem);
    m_configurationTableWidget->setItem(row, kConfigurationEditorNameColumn, new QTableWidgetItem);
    m_configurationTableWidget->setItem(
        row, kConfigurationEditorPriceColumn, new QTableWidgetItem(QStringLiteral("0.00")));
    m_configurationTableWidget->setItem(
        row, kConfigurationEditorSortColumn, new QTableWidgetItem(QString::number(row + 1)));
    m_configurationTableWidget->setCurrentCell(row, kConfigurationEditorCodeColumn);
}

void MainWindow::loadBomEditor()
{
    if (m_bomTableWidget == nullptr || m_bomConfigurationComboBox == nullptr) {
        return;
    }

    const int configurationId = m_bomConfigurationComboBox->currentData().toInt();
    const QSignalBlocker blocker(m_bomTableWidget);
    m_bomTableWidget->setRowCount(0);
    for (const BaseConfigurationComponentData &component :
         m_databaseManager.baseConfigurationComponents(configurationId)) {
        const int row = m_bomTableWidget->rowCount();
        m_bomTableWidget->insertRow(row);
        auto *idItem = new QTableWidgetItem(QString::number(component.id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_bomTableWidget->setItem(row, kBomEditorIdColumn, idItem);
        m_bomTableWidget->setItem(row, kBomEditorNameColumn, new QTableWidgetItem(component.componentName));
        m_bomTableWidget->setItem(row, kBomEditorSpecColumn, new QTableWidgetItem(component.componentSpec));
        m_bomTableWidget->setItem(row, kBomEditorMaterialColumn, new QTableWidgetItem(component.material));
        m_bomTableWidget->setItem(row, kBomEditorColorColumn, new QTableWidgetItem(component.color));
        m_bomTableWidget->setItem(row, kBomEditorUnitColumn, new QTableWidgetItem(component.unitName));
        m_bomTableWidget->setItem(row,
                                  kBomEditorQuantityColumn,
                                  new QTableWidgetItem(QString::number(component.quantity)));
        m_bomTableWidget->setItem(row,
                                  kBomEditorUnitPriceColumn,
                                  new QTableWidgetItem(QString::number(component.unitAmount, 'f', 2)));
        m_bomTableWidget->setItem(row,
                                  kBomEditorSortColumn,
                                  new QTableWidgetItem(QString::number(component.sortOrder)));
    }
}

void MainWindow::saveBomEditor()
{
    const int configurationId = m_bomConfigurationComboBox != nullptr
                                    ? m_bomConfigurationComboBox->currentData().toInt()
                                    : 0;
    QList<BaseConfigurationComponentData> components;
    for (int row = 0; row < m_bomTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = m_bomTableWidget->item(row, kBomEditorNameColumn);
        if (nameItem == nullptr || nameItem->text().trimmed().isEmpty()) {
            continue;
        }

        BaseConfigurationComponentData component;
        component.componentName = nameItem->text().trimmed();
        component.componentSpec =
            m_bomTableWidget->item(row, kBomEditorSpecColumn) != nullptr
                ? m_bomTableWidget->item(row, kBomEditorSpecColumn)->text().trimmed()
                : QString();
        component.material =
            m_bomTableWidget->item(row, kBomEditorMaterialColumn) != nullptr
                ? m_bomTableWidget->item(row, kBomEditorMaterialColumn)->text().trimmed()
                : QString();
        component.color = m_bomTableWidget->item(row, kBomEditorColorColumn) != nullptr
                              ? m_bomTableWidget->item(row, kBomEditorColorColumn)->text().trimmed()
                              : QString();
        component.unitName = m_bomTableWidget->item(row, kBomEditorUnitColumn) != nullptr
                                 ? m_bomTableWidget->item(row, kBomEditorUnitColumn)->text().trimmed()
                                 : QStringLiteral("件");
        component.quantity =
            m_bomTableWidget->item(row, kBomEditorQuantityColumn) != nullptr
                ? m_bomTableWidget->item(row, kBomEditorQuantityColumn)->text().toInt()
                : 0;
        component.unitAmount =
            m_bomTableWidget->item(row, kBomEditorUnitPriceColumn) != nullptr
                ? m_bomTableWidget->item(row, kBomEditorUnitPriceColumn)->text().toDouble()
                : 0.0;
        component.sortOrder = m_bomTableWidget->item(row, kBomEditorSortColumn) != nullptr
                                  ? m_bomTableWidget->item(row, kBomEditorSortColumn)->text().toInt()
                                  : row + 1;
        components.append(component);
    }

    if (!m_databaseManager.replaceBaseConfigurationComponents(configurationId, components)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), m_databaseManager.lastError());
        return;
    }

    loadBomEditor();
    rebuildStructuredOrderComponents();
    showStatusMessage(QStringLiteral("基础配置 BOM 已保存"), 3000);
}

void MainWindow::addBomEditorRow()
{
    const int row = m_bomTableWidget->rowCount();
    m_bomTableWidget->insertRow(row);
    auto *idItem = new QTableWidgetItem;
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    m_bomTableWidget->setItem(row, kBomEditorIdColumn, idItem);
    m_bomTableWidget->setItem(row, kBomEditorNameColumn, new QTableWidgetItem);
    m_bomTableWidget->setItem(row, kBomEditorSpecColumn, new QTableWidgetItem);
    m_bomTableWidget->setItem(row, kBomEditorMaterialColumn, new QTableWidgetItem);
    m_bomTableWidget->setItem(row, kBomEditorColorColumn, new QTableWidgetItem);
    m_bomTableWidget->setItem(row, kBomEditorUnitColumn, new QTableWidgetItem(QStringLiteral("件")));
    m_bomTableWidget->setItem(row, kBomEditorQuantityColumn, new QTableWidgetItem(QStringLiteral("1")));
    m_bomTableWidget->setItem(
        row,
        kBomEditorUnitPriceColumn,
        new QTableWidgetItem(QString::number(kDefaultNonZeroUnitPrice, 'f', 2)));
    m_bomTableWidget->setItem(row, kBomEditorSortColumn, new QTableWidgetItem(QString::number(row + 1)));
    m_bomTableWidget->setCurrentCell(row, kBomEditorNameColumn);
}

void MainWindow::importProductCategoriesCsv()
{
    runCsvImport(DataImporter::ImportTarget::ProductCategories,
                 QStringLiteral("导入产品类型 CSV"),
                 QStringLiteral("产品类型导入完成"),
                 false);
}

void MainWindow::importProductSkusCsv()
{
    runCsvImport(DataImporter::ImportTarget::ProductSkus,
                 QStringLiteral("导入具体型号 CSV"),
                 QStringLiteral("具体型号导入完成"),
                 false);
}

void MainWindow::importBaseConfigurationsCsv()
{
    runCsvImport(DataImporter::ImportTarget::BaseConfigurations,
                 QStringLiteral("导入基础配置 CSV"),
                 QStringLiteral("基础配置导入完成"),
                 false);
}

void MainWindow::importBaseConfigurationBomCsv()
{
    runCsvImport(DataImporter::ImportTarget::BaseConfigurationBom,
                 QStringLiteral("导入基础配置 BOM CSV"),
                 QStringLiteral("基础配置 BOM 导入完成"),
                 false);
}

void MainWindow::importInventoryItemsCsv()
{
    runCsvImport(DataImporter::ImportTarget::InventoryItems,
                 QStringLiteral("导入库存 CSV"),
                 QStringLiteral("库存导入完成"),
                 true);
}

void MainWindow::runCsvImport(DataImporter::ImportTarget target,
                              const QString &dialogTitle,
                              const QString &successMessage,
                              bool inventoryImport)
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        dialogTitle,
        QString(),
        QStringLiteral("CSV Files (*.csv);;All Files (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }

    const DataImporter::ImportResult result = m_dataImporter.importCsv(target, filePath);
    QString message = result.summaryText();
    if (!result.failureReasons.isEmpty()) {
        message += QStringLiteral("\n\n失败摘要：\n- ");
        message += result.failureReasons.mid(0, 10).join(QStringLiteral("\n- "));
    }

    if (result.failedCount > 0) {
        QMessageBox errorBox(QMessageBox::Warning,
                             QStringLiteral("导入完成"),
                             message,
                             QMessageBox::Ok,
                             this);
        if (result.failureReasons.size() > 10) {
            QString detailText = result.failureReasons.join(QLatin1Char('\n'));
            errorBox.setDetailedText(detailText);
        }
        errorBox.exec();
    } else {
        QMessageBox::information(this, QStringLiteral("导入完成"), message);
    }

    loadProductDataPage();
    loadStructuredOrderSkus();
    loadStructuredQuerySkus();
    rebuildStructuredOrderComponents();
    if (inventoryImport) {
        clearInventoryForm();
    }
    loadInventoryPage();
    loadShipmentOrders();
    performStructuredOrderQuery();
    showStatusMessage(successMessage, 3000);
}

void MainWindow::setupQueryOutputControls()
{
    if (ui == nullptr || ui->queryFilterLayout == nullptr || m_queryStartDateEdit != nullptr) {
        return;
    }

    m_queryStartDateEdit = new QDateEdit(ui->queryGroupBox);
    m_queryStartDateEdit->setCalendarPopup(true);
    m_queryStartDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_queryStartDateEdit->setDate(QDate::currentDate().addMonths(-1));

    m_queryEndDateEdit = new QDateEdit(ui->queryGroupBox);
    m_queryEndDateEdit->setCalendarPopup(true);
    m_queryEndDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_queryEndDateEdit->setDate(QDate::currentDate());

    m_exportOrderSummaryButton = new QPushButton(QStringLiteral("导出订单汇总 CSV"), ui->queryGroupBox);
    m_exportShipmentListButton = new QPushButton(QStringLiteral("导出发货清单 CSV"), ui->queryGroupBox);
    m_printCurrentOrderButton = new QPushButton(QStringLiteral("打印当前订单"), ui->queryGroupBox);
    m_exportOrderSummaryButton->setProperty("buttonRole", "secondary");
    m_exportShipmentListButton->setProperty("buttonRole", "secondary");
    m_printCurrentOrderButton->setProperty("buttonRole", "secondary");

    ui->queryFilterLayout->addWidget(new QLabel(QStringLiteral("开始日期"), ui->queryGroupBox), 1, 0);
    ui->queryFilterLayout->addWidget(m_queryStartDateEdit, 1, 1);
    ui->queryFilterLayout->addWidget(new QLabel(QStringLiteral("结束日期"), ui->queryGroupBox), 1, 2);
    ui->queryFilterLayout->addWidget(m_queryEndDateEdit, 1, 3);
    ui->queryFilterLayout->addWidget(m_exportOrderSummaryButton, 1, 5);
    ui->queryFilterLayout->addWidget(m_exportShipmentListButton, 1, 6);
    ui->queryFilterLayout->addWidget(m_printCurrentOrderButton, 1, 7);
}

void MainWindow::setupInventoryOutputControls()
{
    if (m_inventoryModeTabWidget == nullptr || m_exportInventoryDemandButton != nullptr) {
        return;
    }

    m_exportInventoryDemandButton =
        new QPushButton(QStringLiteral("导出库存需求汇总 CSV"), m_inventoryModeTabWidget);
    m_exportInventoryDemandButton->setProperty("buttonRole", "secondary");
}

void MainWindow::loadStructuredQuerySkus()
{
    if (ui->queryProductModelComboBox == nullptr) {
        return;
    }

    m_structuredQuerySkus = m_databaseManager.productSkus();
    const QSignalBlocker blocker(ui->queryProductModelComboBox);
    ui->queryProductModelComboBox->clear();
    ui->queryProductModelComboBox->addItem(QStringLiteral("全部型号"), 0);
    for (const ProductSkuOption &sku : m_structuredQuerySkus) {
        ui->queryProductModelComboBox->addItem(sku.skuName, sku.id);
    }
}

void MainWindow::performStructuredOrderQuery()
{
    if (m_queryModeTabWidget != nullptr) {
        m_queryModeTabWidget->setCurrentIndex(0);
    }

    StructuredOrderQueryFilter filter;
    filter.startDate = m_queryStartDateEdit != nullptr
                           ? m_queryStartDateEdit->date().toString(Qt::ISODate)
                           : QString();
    filter.endDate = m_queryEndDateEdit != nullptr
                         ? m_queryEndDateEdit->date().toString(Qt::ISODate)
                         : QString();
    filter.customerKeyword = ui->queryCustomerLineEdit->text().trimmed();
    filter.productSkuId = ui->queryProductModelComboBox->currentData().toInt();
    filter.onlyOpen = ui->queryOnlyUnfinishedCheckBox->isChecked();

    if (m_queryStartDateEdit != nullptr && m_queryEndDateEdit != nullptr
        && m_queryStartDateEdit->date() > m_queryEndDateEdit->date()) {
        QMessageBox::warning(this, QStringLiteral("查询条件无效"), QStringLiteral("开始日期不能晚于结束日期。"));
        return;
    }

    m_structuredQueryOrders = m_databaseManager.structuredOrders(filter);
    m_filteredStructuredQueryOrders = m_structuredQueryOrders;
    setStructuredQueryOrderRows(m_filteredStructuredQueryOrders);
}

void MainWindow::setStructuredQueryOrderRows(const QList<StructuredOrderSummary> &orders)
{
    const int previousOrderId = currentQueryOrderId();
    ui->orderListTableWidget->setSortingEnabled(false);
    const QSignalBlocker blocker(ui->orderListTableWidget);
    ui->orderListTableWidget->setRowCount(0);

    for (const StructuredOrderSummary &order : orders) {
        const int row = ui->orderListTableWidget->rowCount();
        ui->orderListTableWidget->insertRow(row);
        auto *idItem = createIdTableWidgetItem(order.id);
        ui->orderListTableWidget->setItem(row, kQueryOrderIdColumn, idItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderDateColumn, new QTableWidgetItem(order.orderDate));
        ui->orderListTableWidget->setItem(row,
                                          kQueryOrderCustomerColumn,
                                          new QTableWidgetItem(order.customerName));
        ui->orderListTableWidget->setItem(row,
                                          kQueryOrderCategoryColumn,
                                          new QTableWidgetItem(order.productCategoryName));
        ui->orderListTableWidget->setItem(row,
                                          kQueryOrderProductModelColumn,
                                          new QTableWidgetItem(order.productSkuName));
        ui->orderListTableWidget->setItem(row,
                                          kQueryOrderConfigurationColumn,
                                          new QTableWidgetItem(order.baseConfigurationName));
        ui->orderListTableWidget->setItem(
            row, kQueryOrderQuantitySetsColumn, new QTableWidgetItem(QString::number(order.orderQuantity)));
        ui->orderListTableWidget->setItem(
            row,
            kQueryOrderUnitPriceColumn,
            new QTableWidgetItem(QString::number(order.configPrice, 'f', 2)));
        ui->orderListTableWidget->setItem(
            row,
            kQueryOrderStatusColumn,
            new QTableWidgetItem(structuredOrderShipmentStatusText(order)));
        ui->orderListTableWidget->setItem(
            row,
            kQueryOrderShipmentReadyColumn,
            new QTableWidgetItem(structuredOrderReadinessText(order)));
    }

    applyDefaultAscendingSort(ui->orderListTableWidget, kQueryOrderIdColumn);

    ui->queryOrderCountValueLabel->setText(QString::number(orders.size()));
    int targetRow = -1;
    for (int row = 0; row < ui->orderListTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *idItem = ui->orderListTableWidget->item(row, kQueryOrderIdColumn);
        if (idItem != nullptr && idItem->data(Qt::UserRole).toInt() == previousOrderId) {
            targetRow = row;
            break;
        }
    }
    if (targetRow < 0 && ui->orderListTableWidget->rowCount() > 0) {
        targetRow = 0;
    }
    if (targetRow >= 0) {
        ui->orderListTableWidget->selectRow(targetRow);
    } else {
        clearQueryDetails();
    }

    refreshStructuredQueryOrderDetails();
}

void MainWindow::refreshStructuredQueryOrderDetails()
{
    const int orderId = currentQueryOrderId();
    if (orderId <= 0) {
        clearQueryDetails();
        return;
    }

    setStructuredQueryComponentRows(m_databaseManager.structuredOrderComponents(orderId));
    setQueryShipmentRows(m_databaseManager.structuredOrderShipments(orderId));
}

void MainWindow::setStructuredQueryComponentRows(
    const QList<StructuredOrderComponentSnapshot> &components)
{
    const QSignalBlocker blocker(ui->orderDetailComponentTableWidget);
    ui->orderDetailComponentTableWidget->setRowCount(0);

    for (const StructuredOrderComponentSnapshot &component : components) {
        const int row = ui->orderDetailComponentTableWidget->rowCount();
        ui->orderDetailComponentTableWidget->insertRow(row);
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentNameColumn, new QTableWidgetItem(component.componentName));
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentSpecColumn, new QTableWidgetItem(component.componentSpec));
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentMaterialColumn, new QTableWidgetItem(component.material));
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentColorColumn, new QTableWidgetItem(component.color));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailQuantityPerSetColumn,
            new QTableWidgetItem(QString::number(component.quantityPerSet)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailTotalRequiredColumn,
            new QTableWidgetItem(QString::number(component.requiredQuantity)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailUnitPriceColumn,
            new QTableWidgetItem(QString::number(component.unitAmount, 'f', 2)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailSourceColumn,
            new QTableWidgetItem(structuredSourceDisplayText(component.sourceType)));
    }
}

StructuredOrderSummary MainWindow::structuredOrderSummaryById(int orderId) const
{
    const QList<QList<StructuredOrderSummary>> sources = {m_filteredStructuredQueryOrders,
                                                          m_structuredQueryOrders,
                                                          m_shipmentOrders,
                                                          m_structuredShipmentReadyOrders,
                                                          m_inventoryBlockedOrders};
    for (const QList<StructuredOrderSummary> &source : sources) {
        for (const StructuredOrderSummary &order : source) {
            if (order.id == orderId) {
                return order;
            }
        }
    }
    return {};
}

bool MainWindow::writeCsvFile(const QString &dialogTitle,
                              const QString &defaultFileName,
                              const QStringList &headers,
                              const QList<QStringList> &rows)
{
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        dialogTitle,
        defaultFileName,
        QStringLiteral("CSV Files (*.csv);;All Files (*.*)"));
    if (filePath.isEmpty()) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), file.errorString());
        return false;
    }

    auto escapeCsv = [](QString value) {
        value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"'))
            || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
            value = QStringLiteral("\"%1\"").arg(value);
        }
        return value;
    };

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QStringList escapedHeaders;
    for (const QString &header : headers) {
        escapedHeaders.append(escapeCsv(header));
    }
    stream << escapedHeaders.join(QStringLiteral(",")) << Qt::endl;
    for (const QStringList &row : rows) {
        QStringList escapedRow;
        for (const QString &cell : row) {
            escapedRow.append(escapeCsv(cell));
        }
        stream << escapedRow.join(QStringLiteral(",")) << Qt::endl;
    }
    file.close();
    return true;
}

void MainWindow::exportOrderSummaryCsv()
{
    QList<QStringList> rows;
    for (const StructuredOrderSummary &order : m_filteredStructuredQueryOrders) {
        rows.append({QString::number(order.id),
                     order.orderDate,
                     order.customerName,
                     order.productCategoryName,
                     order.productSkuName,
                     order.baseConfigurationName,
                     QString::number(order.orderQuantity),
                     structuredOrderShipmentStatusText(order),
                     structuredOrderReadinessText(order)});
    }

    if (writeCsvFile(QStringLiteral("导出订单汇总"),
                     QStringLiteral("订单汇总.csv"),
                     {QStringLiteral("订单ID"),
                      QStringLiteral("订单日期"),
                      QStringLiteral("客户"),
                      QStringLiteral("产品类型"),
                      QStringLiteral("具体型号"),
                      QStringLiteral("基础配置"),
                      QStringLiteral("数量"),
                      QStringLiteral("发货状态"),
                      QStringLiteral("可发货")},
                     rows)) {
        showStatusMessage(QStringLiteral("订单汇总已导出"), 3000);
    }
}

void MainWindow::exportInventoryDemandCsv()
{
    QList<QStringList> rows;
    for (const InventoryDemandSummaryRow &row : m_inventoryDemandSummaryRows) {
        rows.append({row.productCategoryName,
                     row.componentName,
                     row.componentSpec,
                     row.material,
                     row.color,
                     row.unitName,
                     QString::number(row.totalDemandQuantity),
                     QString::number(row.currentInventoryQuantity),
                     QString::number(row.shortageQuantity)});
    }

    if (writeCsvFile(QStringLiteral("导出库存需求汇总"),
                     QStringLiteral("库存需求汇总.csv"),
                     {QStringLiteral("产品类型"),
                      QStringLiteral("物料名称"),
                      QStringLiteral("规格"),
                      QStringLiteral("材质"),
                      QStringLiteral("颜色"),
                      QStringLiteral("单位"),
                      QStringLiteral("需求数量"),
                      QStringLiteral("库存数量"),
                      QStringLiteral("缺口数量")},
                     rows)) {
        showStatusMessage(QStringLiteral("库存需求汇总已导出"), 3000);
    }
}

void MainWindow::exportCurrentOrderShipmentCsv()
{
    const int orderId = currentQueryOrderId();
    if (orderId <= 0) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("请先在订单查询中选择一个订单。"));
        return;
    }

    const StructuredOrderSummary order = structuredOrderSummaryById(orderId);
    const QList<StructuredOrderComponentSnapshot> components =
        m_databaseManager.structuredOrderComponents(orderId);
    QList<QStringList> rows;
    for (const StructuredOrderComponentSnapshot &component : components) {
        rows.append({QString::number(order.id),
                     order.customerName,
                     order.productSkuName,
                     order.baseConfigurationName,
                     component.componentName,
                     component.componentSpec,
                     component.material,
                     component.color,
                     component.unitName,
                     QString::number(component.quantityPerSet),
                     QString::number(component.requiredQuantity),
                     QString::number(component.shippedQuantity),
                     QString::number(component.unshippedQuantity)});
    }

    if (writeCsvFile(QStringLiteral("导出发货清单"),
                     QStringLiteral("订单_%1_发货清单.csv").arg(orderId),
                     {QStringLiteral("订单ID"),
                      QStringLiteral("客户"),
                      QStringLiteral("具体型号"),
                      QStringLiteral("基础配置"),
                      QStringLiteral("组件名称"),
                      QStringLiteral("规格"),
                      QStringLiteral("材质"),
                      QStringLiteral("颜色"),
                      QStringLiteral("单位"),
                      QStringLiteral("每套数量"),
                      QStringLiteral("需求数量"),
                      QStringLiteral("已发数量"),
                      QStringLiteral("未发数量")},
                     rows)) {
        showStatusMessage(QStringLiteral("发货清单已导出"), 3000);
    }
}

QString MainWindow::buildPrintableOrderText(int orderId)
{
    const StructuredOrderSummary order = structuredOrderSummaryById(orderId);
    if (order.id <= 0) {
        return QString();
    }

    QString text;
    QTextStream stream(&text);
    stream << "订单打印单" << Qt::endl
           << "订单ID: " << order.id << Qt::endl
           << "订单日期: " << order.orderDate << Qt::endl
           << "客户: " << order.customerName << Qt::endl
           << "产品类型: " << order.productCategoryName << Qt::endl
           << "具体型号: " << order.productSkuName << Qt::endl
           << "基础配置: " << order.baseConfigurationName << Qt::endl
           << "数量: " << order.orderQuantity << Qt::endl
           << "订单状态: " << structuredOrderShipmentStatusText(order) << Qt::endl
           << Qt::endl
           << "订单组件" << Qt::endl;

    for (const StructuredOrderComponentSnapshot &component :
         m_databaseManager.structuredOrderComponents(orderId)) {
        stream << component.componentName;
        if (!component.componentSpec.trimmed().isEmpty()) {
            stream << " | " << component.componentSpec;
        }
        stream << " | 需求数量 " << component.requiredQuantity
               << " | 已发数量 " << component.shippedQuantity
               << " | 未发数量 " << component.unshippedQuantity << Qt::endl;
    }

    const QList<OrderShipmentRecord> shipments = m_databaseManager.structuredOrderShipments(orderId);
    if (!shipments.isEmpty()) {
        stream << Qt::endl << "发货记录" << Qt::endl;
        for (const OrderShipmentRecord &record : shipments) {
            stream << record.shipmentDate << " | " << record.shipmentType << " | "
                   << record.shipmentQuantity;
            if (!record.note.trimmed().isEmpty()) {
                stream << " | " << record.note;
            }
            stream << Qt::endl;
        }
    }

    return text;
}

void MainWindow::printCurrentOrder()
{
    const int orderId = currentQueryOrderId();
    if (orderId <= 0) {
        QMessageBox::warning(this, QStringLiteral("打印失败"), QStringLiteral("请先在订单查询中选择一个订单。"));
        return;
    }

    const QString printableText = buildPrintableOrderText(orderId);
    if (printableText.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("打印失败"), QStringLiteral("未找到当前订单内容。"));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(QStringLiteral("打印订单"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QTextDocument document;
    document.setPlainText(printableText);
    document.print(&printer);
    showStatusMessage(QStringLiteral("订单打印任务已发送"), 3000);
}

void MainWindow::loadInventoryPage()
{
    if (m_inventoryCategoryComboBox != nullptr) {
        const QList<ProductCategoryOption> categories = m_databaseManager.productCategories();
        const int currentCategoryId = m_inventoryCategoryComboBox->currentData().toInt();
        const QSignalBlocker blocker(m_inventoryCategoryComboBox);
        m_inventoryCategoryComboBox->clear();
        m_inventoryCategoryComboBox->addItem(QStringLiteral("选择产品类型"), 0);
        for (const ProductCategoryOption &category : categories) {
            m_inventoryCategoryComboBox->addItem(category.name, category.id);
        }
        const int index = m_inventoryCategoryComboBox->findData(currentCategoryId);
        m_inventoryCategoryComboBox->setCurrentIndex(index >= 0 ? index : 0);
        refreshComboBoxPopupWidth(m_inventoryCategoryComboBox);
    }

    if (m_inventoryComponentComboBox != nullptr) {
        const QString currentText = m_inventoryComponentComboBox->currentText();
        const int currentId = m_inventoryComponentComboBox->currentData().toInt();
        const QList<ProductComponentOption> options = m_databaseManager.inventoryComponentOptions();
        QHash<QString, QSet<QString>> priceVariantsByIdentity;
        for (const ProductComponentOption &option : options) {
            priceVariantsByIdentity[inventoryOptionIdentityKey(option)].insert(
                QString::number(option.unitPrice, 'f', 4));
        }
        const QSignalBlocker blocker(m_inventoryComponentComboBox);
        m_inventoryComponentComboBox->clear();
        m_inventoryComponentComboBox->addItem(QString(), 0);
        for (const ProductComponentOption &option : options) {
            m_inventoryComponentComboBox->addItem(
                inventoryOptionDisplayText(option, priceVariantsByIdentity), option.id);
            m_inventoryComponentComboBox->setItemData(m_inventoryComponentComboBox->count() - 1,
                                                      option.unitPrice,
                                                      Qt::UserRole + 1);
        }
        int index = m_inventoryComponentComboBox->findData(currentId);
        if (index < 0 && !currentText.trimmed().isEmpty()) {
            index = m_inventoryComponentComboBox->findText(currentText);
        }
        if (index >= 0) {
            m_inventoryComponentComboBox->setCurrentIndex(index);
        } else {
            m_inventoryComponentComboBox->setEditText(currentText);
        }
        refreshComboBoxPopupWidth(m_inventoryComponentComboBox);
    }

    refreshInventoryList();
    refreshShipmentReadyTables();
}

void MainWindow::populateInventoryForm(const InventoryItemData &item)
{
    if (m_inventoryComponentComboBox == nullptr || m_inventoryQuantitySpinBox == nullptr) {
        return;
    }

    m_inventoryComponentComboBox->setProperty("inventoryItemId", item.id);
    if (m_inventoryCategoryComboBox != nullptr) {
        const int categoryIndex = m_inventoryCategoryComboBox->findData(item.productCategoryId);
        m_inventoryCategoryComboBox->setCurrentIndex(categoryIndex >= 0 ? categoryIndex : 0);
    }
    const QString displayText = item.componentSpec.trimmed().isEmpty()
                                    ? item.componentName
                                    : QStringLiteral("%1 | %2").arg(item.componentName, item.componentSpec);
    const int componentIndex = m_inventoryComponentComboBox->findData(item.id);
    if (componentIndex >= 0) {
        m_inventoryComponentComboBox->setCurrentIndex(componentIndex);
    } else {
        m_inventoryComponentComboBox->setEditText(displayText);
    }
    if (m_inventoryComponentSpecLineEdit != nullptr) {
        m_inventoryComponentSpecLineEdit->setText(item.componentSpec);
    }
    if (m_inventoryMaterialLineEdit != nullptr) {
        m_inventoryMaterialLineEdit->setText(item.material);
    }
    if (m_inventoryColorLineEdit != nullptr) {
        m_inventoryColorLineEdit->setText(item.color);
    }
    if (m_inventoryUnitLineEdit != nullptr) {
        m_inventoryUnitLineEdit->setText(item.unitName);
    }
    if (m_inventoryUnitPriceSpinBox != nullptr) {
        m_inventoryUnitPriceSpinBox->setValue(item.unitPrice);
    }
    m_inventoryQuantitySpinBox->setValue(item.currentQuantity);
    if (m_inventoryNoteLineEdit != nullptr) {
        m_inventoryNoteLineEdit->setText(item.note);
    }
}

void MainWindow::clearInventoryForm()
{
    if (m_inventoryComponentComboBox == nullptr || m_inventoryQuantitySpinBox == nullptr) {
        return;
    }

    m_inventoryComponentComboBox->setProperty("inventoryItemId", 0);
    m_inventoryComponentComboBox->setCurrentIndex(0);
    m_inventoryComponentComboBox->setEditText(QString());
    if (m_inventoryCategoryComboBox != nullptr) {
        m_inventoryCategoryComboBox->setCurrentIndex(0);
    }
    if (m_inventoryComponentSpecLineEdit != nullptr) {
        m_inventoryComponentSpecLineEdit->clear();
    }
    if (m_inventoryMaterialLineEdit != nullptr) {
        m_inventoryMaterialLineEdit->clear();
    }
    if (m_inventoryColorLineEdit != nullptr) {
        m_inventoryColorLineEdit->clear();
    }
    if (m_inventoryUnitLineEdit != nullptr) {
        m_inventoryUnitLineEdit->setText(QStringLiteral("件"));
    }
    if (m_inventoryUnitPriceSpinBox != nullptr) {
        m_inventoryUnitPriceSpinBox->setValue(kDefaultNonZeroUnitPrice);
    }
    m_inventoryQuantitySpinBox->setValue(0);
    if (m_inventoryNoteLineEdit != nullptr) {
        m_inventoryNoteLineEdit->clear();
    }
}

void MainWindow::saveInventoryForm()
{
    if (m_inventoryComponentComboBox == nullptr || m_inventoryQuantitySpinBox == nullptr) {
        return;
    }

    InventoryItemData item;
    item.id = currentInventoryItemId();
    item.productCategoryId =
        m_inventoryCategoryComboBox != nullptr ? m_inventoryCategoryComboBox->currentData().toInt() : 0;
    item.componentName = m_inventoryComponentComboBox->currentText().section('|', 0, 0).trimmed();
    item.componentSpec = m_inventoryComponentSpecLineEdit != nullptr
                             ? m_inventoryComponentSpecLineEdit->text().trimmed()
                             : QString();
    item.material = m_inventoryMaterialLineEdit != nullptr
                        ? m_inventoryMaterialLineEdit->text().trimmed()
                        : QString();
    item.color =
        m_inventoryColorLineEdit != nullptr ? m_inventoryColorLineEdit->text().trimmed() : QString();
    item.unitName =
        m_inventoryUnitLineEdit != nullptr ? m_inventoryUnitLineEdit->text().trimmed() : QString();
    item.unitPrice =
        m_inventoryUnitPriceSpinBox != nullptr ? m_inventoryUnitPriceSpinBox->value() : 0.0;
    item.currentQuantity = m_inventoryQuantitySpinBox->value();
    item.note =
        m_inventoryNoteLineEdit != nullptr ? m_inventoryNoteLineEdit->text().trimmed() : QString();

    if (!m_databaseManager.saveInventoryItem(item)) {
        QMessageBox::critical(this, QStringLiteral("保存库存失败"), m_databaseManager.lastError());
        return;
    }

    clearInventoryForm();
    loadInventoryPage();
    loadStructuredQuerySkus();
    loadShipmentOrders();
    performStructuredOrderQuery();
    showStatusMessage(QStringLiteral("库存已保存"), 3000);
}

void MainWindow::refreshInventoryList()
{
    if (m_inventoryTableWidget == nullptr) {
        return;
    }

    m_inventoryTableWidget->setSortingEnabled(false);
    const QSignalBlocker blocker(m_inventoryTableWidget);
    m_inventoryTableWidget->setRowCount(0);
    for (const InventoryItemData &item : m_databaseManager.inventoryItems()) {
        const int row = m_inventoryTableWidget->rowCount();
        m_inventoryTableWidget->insertRow(row);
        auto *idItem = createIdTableWidgetItem(item.id);
        m_inventoryTableWidget->setItem(row, kInventoryListIdColumn, idItem);
        m_inventoryTableWidget->setItem(
            row, kInventoryListCategoryColumn, new QTableWidgetItem(item.productCategoryName));
        m_inventoryTableWidget->setItem(row, kInventoryListNameColumn, new QTableWidgetItem(item.componentName));
        m_inventoryTableWidget->setItem(row, kInventoryListSpecColumn, new QTableWidgetItem(item.componentSpec));
        m_inventoryTableWidget->setItem(row, kInventoryListMaterialColumn, new QTableWidgetItem(item.material));
        m_inventoryTableWidget->setItem(row, kInventoryListColorColumn, new QTableWidgetItem(item.color));
        m_inventoryTableWidget->setItem(row, kInventoryListUnitColumn, new QTableWidgetItem(item.unitName));
        m_inventoryTableWidget->setItem(
            row, kInventoryListUnitPriceColumn, new QTableWidgetItem(QString::number(item.unitPrice, 'f', 2)));
        m_inventoryTableWidget->setItem(
            row, kInventoryListQuantityColumn, new QTableWidgetItem(QString::number(item.currentQuantity)));
        m_inventoryTableWidget->setItem(row, kInventoryListNoteColumn, new QTableWidgetItem(item.note));
    }
    applyDefaultAscendingSort(m_inventoryTableWidget, kInventoryListIdColumn);
}

void MainWindow::refreshDemandSummaryTable()
{
    refreshInventoryDemandShortageTable();
}

void MainWindow::refreshInventoryDemandPage()
{
    if (m_inventoryBlockedOrderTableWidget == nullptr) {
        return;
    }

    const int previousScopeOrderId = currentInventoryDemandOrderId();
    m_inventoryBlockedOrders.clear();
    for (const StructuredOrderSummary &order : m_structuredShipmentReadyOrders) {
        if (!order.isCompleted && !order.shipmentReady) {
            m_inventoryBlockedOrders.append(order);
        }
    }

    m_inventoryBlockedOrderTableWidget->setSortingEnabled(false);
    {
        const QSignalBlocker blocker(m_inventoryBlockedOrderTableWidget);
        m_inventoryBlockedOrderTableWidget->setRowCount(0);
        for (const StructuredOrderSummary &order : m_inventoryBlockedOrders) {
            const int row = m_inventoryBlockedOrderTableWidget->rowCount();
            m_inventoryBlockedOrderTableWidget->insertRow(row);
            m_inventoryBlockedOrderTableWidget->setItem(row,
                                                        kReadyOrderIdColumn,
                                                        createIdTableWidgetItem(order.id));
            m_inventoryBlockedOrderTableWidget->setItem(row,
                                                        kReadyOrderCustomerColumn,
                                                        new QTableWidgetItem(order.customerName));
            m_inventoryBlockedOrderTableWidget->setItem(row,
                                                        kReadyOrderCategoryColumn,
                                                        new QTableWidgetItem(order.productCategoryName));
            m_inventoryBlockedOrderTableWidget->setItem(row,
                                                        kReadyOrderSkuColumn,
                                                        new QTableWidgetItem(order.productSkuName));
            m_inventoryBlockedOrderTableWidget->setItem(row,
                                                        kReadyOrderConfigurationColumn,
                                                        new QTableWidgetItem(order.baseConfigurationName));
            m_inventoryBlockedOrderTableWidget->setItem(row,
                                                        kReadyOrderQuantityColumn,
                                                        new QTableWidgetItem(QString::number(order.orderQuantity)));
            m_inventoryBlockedOrderTableWidget->setItem(row,
                                                        kReadyOrderStatusColumn,
                                                        new QTableWidgetItem(QStringLiteral("不可发货")));
        }
    }
    applyDefaultAscendingSort(m_inventoryBlockedOrderTableWidget, kReadyOrderIdColumn);

    refreshInventoryDemandScopeOptions();

    int selectedRow = -1;
    if (previousScopeOrderId > 0) {
        for (int row = 0; row < m_inventoryBlockedOrderTableWidget->rowCount(); ++row) {
            const QTableWidgetItem *idItem =
                m_inventoryBlockedOrderTableWidget->item(row, kReadyOrderIdColumn);
            if (idItem != nullptr && idItem->data(Qt::UserRole).toInt() == previousScopeOrderId) {
                selectedRow = row;
                break;
            }
        }
    }
    if (selectedRow >= 0) {
        m_inventoryBlockedOrderTableWidget->selectRow(selectedRow);
    } else {
        m_inventoryBlockedOrderTableWidget->clearSelection();
    }

    refreshInventoryDemandShortageTable();
}

void MainWindow::refreshInventoryDemandScopeOptions()
{
    if (m_inventoryDemandScopeComboBox == nullptr) {
        return;
    }

    const int previousOrderId = currentInventoryDemandOrderId();
    const QSignalBlocker blocker(m_inventoryDemandScopeComboBox);
    m_inventoryDemandScopeComboBox->clear();
    m_inventoryDemandScopeComboBox->addItem(QStringLiteral("全部未发订单需求汇总"), 0);
    for (const StructuredOrderSummary &order : m_inventoryBlockedOrders) {
        m_inventoryDemandScopeComboBox->addItem(structuredShipmentOrderDisplayText(order), order.id);
    }

    int index = m_inventoryDemandScopeComboBox->findData(previousOrderId);
    if (index < 0) {
        index = 0;
    }
    m_inventoryDemandScopeComboBox->setCurrentIndex(index);
    refreshComboBoxPopupWidth(m_inventoryDemandScopeComboBox);
}

void MainWindow::refreshInventoryDemandShortageTable()
{
    if (m_demandSummaryTableWidget == nullptr) {
        return;
    }

    QList<InventoryDemandSummaryRow> rows;
    const int scopeOrderId = currentInventoryDemandOrderId();
    if (scopeOrderId <= 0) {
        m_inventoryDemandSummaryRows = m_databaseManager.inventoryDemandSummary();
        rows = m_inventoryDemandSummaryRows;
    } else {
        StructuredOrderSummary order = structuredOrderSummaryById(scopeOrderId);
        if (order.id > 0) {
            QHash<QString, InventoryDemandSummaryRow> demandMap;
            QHash<QString, int> inventoryQuantities;
            for (const InventoryItemData &item : m_databaseManager.inventoryItems()) {
                const QString key = inventoryDemandIdentityKey(item.productCategoryId,
                                                               item.componentName,
                                                               item.componentSpec,
                                                               item.material,
                                                               item.color,
                                                               item.unitName);
                inventoryQuantities[key] += item.currentQuantity;
            }

            for (const StructuredOrderComponentSnapshot &component :
                 m_databaseManager.structuredOrderComponents(scopeOrderId)) {
                if (component.unshippedQuantity <= 0) {
                    continue;
                }

                const QString key = inventoryDemandIdentityKey(order.productCategoryId,
                                                               component.componentName,
                                                               component.componentSpec,
                                                               component.material,
                                                               component.color,
                                                               component.unitName);
                InventoryDemandSummaryRow &row = demandMap[key];
                if (row.componentName.isEmpty()) {
                    row.productCategoryId = order.productCategoryId;
                    row.productCategoryName = order.productCategoryName;
                    row.componentName = component.componentName;
                    row.componentSpec = component.componentSpec;
                    row.material = component.material;
                    row.color = component.color;
                    row.unitName = component.unitName;
                }
                row.totalDemandQuantity += component.unshippedQuantity;
                row.currentInventoryQuantity = inventoryQuantities.value(key, 0);
                row.shortageQuantity =
                    qMax(0, row.totalDemandQuantity - row.currentInventoryQuantity);
            }
            rows = demandMap.values();
        }
    }
    m_inventoryDemandSummaryRows = rows;

    m_demandSummaryTableWidget->setSortingEnabled(false);
    const QSignalBlocker blocker(m_demandSummaryTableWidget);
    m_demandSummaryTableWidget->setRowCount(0);
    for (const InventoryDemandSummaryRow &rowData : rows) {
        const int row = m_demandSummaryTableWidget->rowCount();
        m_demandSummaryTableWidget->insertRow(row);
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryCategoryColumn,
                                            new QTableWidgetItem(rowData.productCategoryName));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryNameColumn,
                                            new QTableWidgetItem(rowData.componentName));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummarySpecColumn,
                                            new QTableWidgetItem(rowData.componentSpec));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryMaterialColumn,
                                            new QTableWidgetItem(rowData.material));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryColorColumn,
                                            new QTableWidgetItem(rowData.color));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryUnitColumn,
                                            new QTableWidgetItem(rowData.unitName));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryDemandColumn,
                                            new QTableWidgetItem(QString::number(rowData.totalDemandQuantity)));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryInventoryColumn,
                                            new QTableWidgetItem(QString::number(rowData.currentInventoryQuantity)));
        m_demandSummaryTableWidget->setItem(row,
                                            kDemandSummaryGapColumn,
                                            new QTableWidgetItem(QString::number(rowData.shortageQuantity)));
    }
    m_demandSummaryTableWidget->setSortingEnabled(true);
}

void MainWindow::refreshShipmentReadyTables()
{
    const QList<StructuredOrderSummary> orders = m_databaseManager.structuredOrders(true);
    m_structuredShipmentReadyOrders = orders;

    auto populateReadyTable = [](QTableWidget *tableWidget, const QList<StructuredOrderSummary> &rows) {
        if (tableWidget == nullptr) {
            return;
        }

        tableWidget->setSortingEnabled(false);
        const QSignalBlocker blocker(tableWidget);
        tableWidget->setRowCount(0);
        for (const StructuredOrderSummary &order : rows) {
            const int row = tableWidget->rowCount();
            tableWidget->insertRow(row);
            auto *idItem = createIdTableWidgetItem(order.id);
            tableWidget->setItem(row, kReadyOrderIdColumn, idItem);
            tableWidget->setItem(row, kReadyOrderCustomerColumn, new QTableWidgetItem(order.customerName));
            tableWidget->setItem(row, kReadyOrderCategoryColumn, new QTableWidgetItem(order.productCategoryName));
            tableWidget->setItem(row, kReadyOrderSkuColumn, new QTableWidgetItem(order.productSkuName));
            tableWidget->setItem(row,
                                 kReadyOrderConfigurationColumn,
                                 new QTableWidgetItem(order.baseConfigurationName));
            tableWidget->setItem(row,
                                 kReadyOrderQuantityColumn,
                                 new QTableWidgetItem(QString::number(order.orderQuantity)));
            tableWidget->setItem(row,
                                 kReadyOrderStatusColumn,
                                 new QTableWidgetItem(structuredOrderReadinessText(order)));
        }
        applyDefaultAscendingSort(tableWidget, kReadyOrderIdColumn);
    };

    populateReadyTable(m_inventoryReadyOrderTableWidget, orders);
    populateReadyTable(m_structuredShipmentReadyTableWidget, orders);
    refreshInventoryDemandPage();
    refreshStructuredShipmentReadySummary();
    if (m_structuredShipmentReadyTableWidget != nullptr) {
        if (m_structuredShipmentReadyTableWidget->rowCount() > 0) {
            m_structuredShipmentReadyTableWidget->selectRow(0);
        } else {
            refreshStructuredShipmentDetails();
        }
    }
}

void MainWindow::refreshStructuredShipmentReadySummary()
{
    if (m_structuredShipmentReadyLabel == nullptr) {
        return;
    }

    int readyCount = 0;
    int blockedCount = 0;
    for (const StructuredOrderSummary &order : m_structuredShipmentReadyOrders) {
        if (order.shipmentReady) {
            ++readyCount;
        } else {
            ++blockedCount;
        }
    }

    m_structuredShipmentReadyLabel->setText(
        QStringLiteral("当前订单中，可发货 %1 单，不可发货 %2 单。")
            .arg(readyCount)
            .arg(blockedCount));
}

int MainWindow::currentStructuredShipmentOrderId() const
{
    if (m_structuredShipmentReadyTableWidget == nullptr) {
        return 0;
    }
    const int row = m_structuredShipmentReadyTableWidget->currentRow();
    if (row < 0) {
        return 0;
    }
    const QTableWidgetItem *idItem =
        m_structuredShipmentReadyTableWidget->item(row, kReadyOrderIdColumn);
    return idItem != nullptr ? idItem->data(Qt::UserRole).toInt() : 0;
}

void MainWindow::setStructuredShipmentComponentRows(
    const QList<StructuredOrderComponentSnapshot> &components)
{
    if (ui == nullptr || ui->shipmentComponentTableWidget == nullptr) {
        return;
    }

    const QSignalBlocker blocker(ui->shipmentComponentTableWidget);
    ui->shipmentComponentTableWidget->setRowCount(0);
    for (const StructuredOrderComponentSnapshot &component : components) {
        const int row = ui->shipmentComponentTableWidget->rowCount();
        ui->shipmentComponentTableWidget->insertRow(row);
        auto *nameItem = new QTableWidgetItem(component.componentName);
        nameItem->setData(Qt::UserRole, component.id);
        ui->shipmentComponentTableWidget->setItem(row, kShipmentComponentNameColumn, nameItem);
        ui->shipmentComponentTableWidget->setItem(
            row,
            kShipmentComponentQuantityPerSetColumn,
            new QTableWidgetItem(QString::number(component.quantityPerSet)));
        ui->shipmentComponentTableWidget->setItem(
            row,
            kShipmentComponentUnitPriceColumn,
            new QTableWidgetItem(QString::number(component.unitAmount, 'f', 2)));
        ui->shipmentComponentTableWidget->setItem(
            row,
            kShipmentComponentTotalRequiredColumn,
            new QTableWidgetItem(QString::number(component.requiredQuantity)));
        ui->shipmentComponentTableWidget->setItem(
            row,
            kShipmentComponentShippedColumn,
            new QTableWidgetItem(QString::number(component.shippedQuantity)));
        ui->shipmentComponentTableWidget->setItem(
            row,
            kShipmentComponentUnshippedColumn,
            new QTableWidgetItem(QString::number(component.unshippedQuantity)));
        ui->shipmentComponentTableWidget->setItem(
            row,
            kShipmentComponentTotalPriceColumn,
            new QTableWidgetItem(QString::number(component.lineAmount, 'f', 2)));
        ui->shipmentComponentTableWidget->setItem(
            row,
            kShipmentComponentSourceColumn,
            new QTableWidgetItem(structuredSourceDisplayText(component.sourceType)));
    }

    if (!components.isEmpty()) {
        ui->shipmentComponentTableWidget->selectRow(0);
    }
}

void MainWindow::refreshStructuredShipmentDetails()
{
    if (ui == nullptr || ui->shipmentOrderStatusValueLabel == nullptr) {
        return;
    }

    const int orderId = currentStructuredShipmentOrderId();
    if (orderId <= 0) {
        ui->shipmentOrderStatusValueLabel->setText(QStringLiteral("暂无订单数据"));
        setStructuredShipmentComponentRows({});
        return;
    }

    StructuredOrderSummary currentOrder;
    bool found = false;
    for (const StructuredOrderSummary &order : m_structuredShipmentReadyOrders) {
        if (order.id == orderId) {
            currentOrder = order;
            found = true;
            break;
        }
    }

    if (!found) {
        ui->shipmentOrderStatusValueLabel->setText(QStringLiteral("暂无订单数据"));
        setStructuredShipmentComponentRows({});
        return;
    }

    ui->shipmentOrderStatusValueLabel->setText(
        QStringLiteral("订单 #%1 | %2 | %3 | %4 | %5 套 | 可整套再发 %6 | 配置价格 %7 | %8")
            .arg(currentOrder.id)
            .arg(currentOrder.customerName)
            .arg(currentOrder.productSkuName)
            .arg(currentOrder.baseConfigurationName)
            .arg(currentOrder.orderQuantity)
            .arg(currentOrder.availableSetShipments)
            .arg(QString::number(currentOrder.configPrice, 'f', 2))
            .arg(structuredOrderShipmentStatusText(currentOrder)));
    setStructuredShipmentComponentRows(m_databaseManager.structuredOrderComponents(orderId));
}

int MainWindow::currentInventoryItemId() const
{
    if (m_inventoryComponentComboBox == nullptr) {
        return 0;
    }
    return m_inventoryComponentComboBox->property("inventoryItemId").toInt();
}

int MainWindow::currentInventoryDemandOrderId() const
{
    if (m_inventoryDemandScopeComboBox == nullptr) {
        return 0;
    }
    return m_inventoryDemandScopeComboBox->currentData().toInt();
}

void MainWindow::loadProductModels()
{
    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->productModelComboBox);
    ui->productModelComboBox->clear();

    const QList<ProductModelOption> models = m_databaseManager.productModels();
    for (const ProductModelOption &model : models) {
        ui->productModelComboBox->addItem(model.name, model.id);
        ui->productModelComboBox->setItemData(ui->productModelComboBox->count() - 1,
                                              model.defaultPrice,
                                              Qt::UserRole + 1);
    }

    if (ui->productModelComboBox->count() == 0) {
        showStatusMessage(QStringLiteral("没有可用的具体型号数据"), 5000);
    }

    m_updatingComponentTable = false;
    loadCustomComponentOptions();
    loadTemplatesForCurrentProduct();
}

void MainWindow::loadCustomComponentOptions()
{
    m_customComponentOptions.clear();
    if (m_isShuttingDown || ui == nullptr || ui->productModelComboBox == nullptr) {
        return;
    }

    const int productModelId = ui->productModelComboBox->currentData().toInt();
    if (productModelId <= 0) {
        return;
    }

    m_customComponentOptions = m_databaseManager.productModelComponents(productModelId);
}

void MainWindow::loadTemplatesForCurrentProduct()
{
    if (m_isShuttingDown) {
        return;
    }

    const int productModelId = ui->productModelComboBox->currentData().toInt();
    ui->bodyUnitPriceDoubleSpinBox->setValue(
        ui->productModelComboBox->currentData(Qt::UserRole + 1).toDouble());
    loadCustomComponentOptions();

    {
        const QSignalBlocker blocker(ui->templateComboBox);
        ui->templateComboBox->clear();

        if (productModelId > 0) {
            const QList<TemplateOption> templates =
                m_databaseManager.optionTemplatesForProduct(productModelId);
            for (const TemplateOption &option : templates) {
                ui->templateComboBox->addItem(option.name, option.id);
            }
        }
    }

    if (ui->customConfigurationRadioButton->isChecked()) {
        setComponentTableRows({}, true);
        if (!m_customComponentOptions.isEmpty()) {
            addEmptyComponentRow();
        } else {
            showStatusMessage(QStringLiteral("当前产品没有可选组件数据"), 5000);
        }
        updatePriceDisplays();
        return;
    }

    loadSelectedTemplateComponents();
}

void MainWindow::loadSelectedTemplateComponents()
{
    if (m_isShuttingDown) {
        return;
    }

    const int templateId = ui->templateComboBox->currentData().toInt();
    if (templateId <= 0) {
        setComponentTableRows({}, false);
        if (ui->templateConfigurationRadioButton->isChecked()) {
            showStatusMessage(QStringLiteral("当前产品没有可用模板"), 5000);
        }
        return;
    }

    setComponentTableRows(m_databaseManager.templateComponents(templateId), false);
}

void MainWindow::setCustomConfigurationMode(bool enabled)
{
    if (m_isShuttingDown) {
        return;
    }

    ui->templateComboBox->setEnabled(!enabled);
    ui->addComponentButton->setEnabled(enabled);
    ui->removeComponentButton->setEnabled(enabled);

    if (enabled) {
        setComponentTableRows({}, true);
        if (!m_customComponentOptions.isEmpty()) {
            addEmptyComponentRow();
        } else {
            showStatusMessage(QStringLiteral("当前产品没有可选组件数据"), 5000);
        }
    } else {
        loadSelectedTemplateComponents();
    }

    updatePriceDisplays();
}

void MainWindow::setComponentTableRows(const QList<OrderComponentData> &components,
                                       bool editableNamesAndQty)
{
    if (m_isShuttingDown) {
        return;
    }

    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->componentTableWidget);
    ui->componentTableWidget->setRowCount(0);

    for (const OrderComponentData &component : components) {
        const int row = ui->componentTableWidget->rowCount();
        ui->componentTableWidget->insertRow(row);

        auto *nameItem = new QTableWidgetItem(component.componentName);
        auto *quantityItem =
            new QTableWidgetItem(component.quantityPerSet > 0
                                     ? QString::number(component.quantityPerSet)
                                     : QString());
        auto *unitPriceItem = new QTableWidgetItem(
            component.unitPrice > 0.0 ? QString::number(component.unitPrice, 'f', 2) : QString());
        auto *sourceItem =
            new QTableWidgetItem(component.sourceType == QStringLiteral("template")
                                     ? QStringLiteral("模板")
                                     : QStringLiteral("手动"));
        auto *totalItem = new QTableWidgetItem(QString());
        auto *totalPriceItem = new QTableWidgetItem(QString());

        if (!editableNamesAndQty) {
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            quantityItem->setFlags(quantityItem->flags() & ~Qt::ItemIsEditable);
            if (component.sourceType != QStringLiteral("template")) {
                unitPriceItem->setFlags(unitPriceItem->flags() & ~Qt::ItemIsEditable);
            }
        }
        sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
        totalItem->setFlags(totalItem->flags() & ~Qt::ItemIsEditable);
        totalPriceItem->setFlags(totalPriceItem->flags() & ~Qt::ItemIsEditable);

        ui->componentTableWidget->setItem(row, kComponentNameColumn, nameItem);
        ui->componentTableWidget->setItem(row, kQuantityPerSetColumn, quantityItem);
        ui->componentTableWidget->setItem(row, kUnitPriceColumn, unitPriceItem);
        ui->componentTableWidget->setItem(row, kSourceTypeColumn, sourceItem);
        ui->componentTableWidget->setItem(row, kTotalRequiredColumn, totalItem);
        ui->componentTableWidget->setItem(row, kComponentTotalPriceColumn, totalPriceItem);
    }

    m_updatingComponentTable = false;
    updateComponentTotals();
}

void MainWindow::addEmptyComponentRow()
{
    if (m_isShuttingDown || !ui->customConfigurationRadioButton->isChecked()) {
        return;
    }

    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->componentTableWidget);
    const int row = ui->componentTableWidget->rowCount();
    ui->componentTableWidget->insertRow(row);

    auto *componentComboBox = new QComboBox(ui->componentTableWidget);
    for (const ProductComponentOption &option : m_customComponentOptions) {
        componentComboBox->addItem(option.name, option.id);
        componentComboBox->setItemData(componentComboBox->count() - 1,
                                       option.unitPrice,
                                       Qt::UserRole + 1);
    }
    auto *quantityItem = new QTableWidgetItem(QStringLiteral("1"));
    auto *unitPriceItem = new QTableWidgetItem(QString());
    auto *sourceItem = new QTableWidgetItem(QStringLiteral("手动"));
    auto *totalItem = new QTableWidgetItem(QString());
    auto *totalPriceItem = new QTableWidgetItem(QString());

    sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
    totalItem->setFlags(totalItem->flags() & ~Qt::ItemIsEditable);
    totalPriceItem->setFlags(totalPriceItem->flags() & ~Qt::ItemIsEditable);

    ui->componentTableWidget->setCellWidget(row, kComponentNameColumn, componentComboBox);
    ui->componentTableWidget->setItem(row, kQuantityPerSetColumn, quantityItem);
    ui->componentTableWidget->setItem(row, kUnitPriceColumn, unitPriceItem);
    ui->componentTableWidget->setItem(row, kSourceTypeColumn, sourceItem);
    ui->componentTableWidget->setItem(row, kTotalRequiredColumn, totalItem);
    ui->componentTableWidget->setItem(row, kComponentTotalPriceColumn, totalPriceItem);

    connect(componentComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this, componentComboBox](int) {
                if (m_isShuttingDown || ui == nullptr || ui->componentTableWidget == nullptr) {
                    return;
                }
                const int row = customComponentRowForWidget(componentComboBox);
                if (row >= 0) {
                    updateCustomComponentRow(row);
                }
            });

    m_updatingComponentTable = false;
    updateCustomComponentRow(row);
    updateComponentTotals();
    ui->componentTableWidget->setCurrentCell(row, kQuantityPerSetColumn);
}

void MainWindow::updateCustomComponentRow(int row)
{
    if (m_isShuttingDown || ui == nullptr || ui->componentTableWidget == nullptr || row < 0
        || row >= ui->componentTableWidget->rowCount()) {
        return;
    }

    auto *componentComboBox =
        qobject_cast<QComboBox *>(ui->componentTableWidget->cellWidget(row, kComponentNameColumn));
    QTableWidgetItem *unitPriceItem = ui->componentTableWidget->item(row, kUnitPriceColumn);
    QTableWidgetItem *sourceItem = ui->componentTableWidget->item(row, kSourceTypeColumn);
    if (componentComboBox == nullptr || unitPriceItem == nullptr || sourceItem == nullptr) {
        return;
    }

    const double unitPrice = componentComboBox->currentData(Qt::UserRole + 1).toDouble();
    unitPriceItem->setText(QString::number(unitPrice, 'f', 2));
    sourceItem->setText(QStringLiteral("手动"));
    updateComponentTotals();
}

int MainWindow::customComponentRowForWidget(QWidget *widget) const
{
    if (ui == nullptr || ui->componentTableWidget == nullptr || widget == nullptr) {
        return -1;
    }

    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        if (ui->componentTableWidget->cellWidget(row, kComponentNameColumn) == widget) {
            return row;
        }
    }

    return -1;
}

QList<OrderComponentData> MainWindow::collectComponentsFromTable() const
{
    QList<OrderComponentData> components;

    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        const auto *componentComboBox =
            qobject_cast<QComboBox *>(ui->componentTableWidget->cellWidget(row, kComponentNameColumn));
        const QTableWidgetItem *nameItem =
            ui->componentTableWidget->item(row, kComponentNameColumn);
        const QTableWidgetItem *quantityItem =
            ui->componentTableWidget->item(row, kQuantityPerSetColumn);
        const QTableWidgetItem *unitPriceItem =
            ui->componentTableWidget->item(row, kUnitPriceColumn);
        const QTableWidgetItem *sourceItem =
            ui->componentTableWidget->item(row, kSourceTypeColumn);

        OrderComponentData component;
        component.componentName = componentComboBox != nullptr
                                      ? componentComboBox->currentText().trimmed()
                                      : nameItem != nullptr ? nameItem->text().trimmed()
                                                            : QString();
        component.quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
        component.unitPrice = unitPriceItem != nullptr ? unitPriceItem->text().toDouble() : 0.0;
        component.sourceType =
            sourceItem != nullptr && sourceItem->text() == QStringLiteral("模板")
                ? QStringLiteral("template")
                : QStringLiteral("manual");
        components.append(component);
    }

    return components;
}

void MainWindow::updateComponentTotals()
{
    if (m_isShuttingDown) {
        return;
    }

    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->componentTableWidget);

    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        QTableWidgetItem *quantityItem =
            ui->componentTableWidget->item(row, kQuantityPerSetColumn);
        QTableWidgetItem *unitPriceItem =
            ui->componentTableWidget->item(row, kUnitPriceColumn);
        QTableWidgetItem *totalItem = ui->componentTableWidget->item(row, kTotalRequiredColumn);
        QTableWidgetItem *totalPriceItem =
            ui->componentTableWidget->item(row, kComponentTotalPriceColumn);
        if (totalItem == nullptr) {
            continue;
        }

        const int quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
        const double unitPrice = unitPriceItem != nullptr ? unitPriceItem->text().toDouble() : 0.0;
        const int totalRequired = quantityPerSet * ui->quantitySetsSpinBox->value();
        totalItem->setText(quantityPerSet > 0 ? QString::number(totalRequired) : QString());
        if (totalPriceItem != nullptr) {
            const double totalPrice = static_cast<double>(totalRequired) * unitPrice;
            totalPriceItem->setText(quantityPerSet > 0
                                        ? QString::number(totalPrice, 'f', 2)
                                        : QString());
        }
    }

    m_updatingComponentTable = false;
    updatePriceDisplays();
}

void MainWindow::updatePriceDisplays()
{
    if (m_isShuttingDown || ui == nullptr || ui->productModelComboBox == nullptr
        || ui->bodyUnitPriceDoubleSpinBox == nullptr || ui->unitPriceDoubleSpinBox == nullptr) {
        return;
    }

    const double bodyUnitPrice = ui->bodyUnitPriceDoubleSpinBox->value();
    double orderUnitPrice = bodyUnitPrice;
    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *quantityItem =
            ui->componentTableWidget->item(row, kQuantityPerSetColumn);
        const QTableWidgetItem *unitPriceItem =
            ui->componentTableWidget->item(row, kUnitPriceColumn);
        const QTableWidgetItem *sourceItem =
            ui->componentTableWidget->item(row, kSourceTypeColumn);
        const int quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
        const double unitPrice = unitPriceItem != nullptr ? unitPriceItem->text().toDouble() : 0.0;
        const QString sourceText = sourceItem != nullptr ? sourceItem->text() : QString();
        if (sourceText == QStringLiteral("机体")) {
            continue;
        }
        orderUnitPrice += static_cast<double>(quantityPerSet) * unitPrice;
    }
    ui->unitPriceDoubleSpinBox->setValue(orderUnitPrice);
}

void MainWindow::clearOrderForm()
{
    if (m_isShuttingDown) {
        return;
    }

    m_updatingComponentTable = true;
    ui->orderDateEdit->setDate(QDate::currentDate());
    ui->customerNameLineEdit->clear();
    ui->quantitySetsSpinBox->setValue(1);
    ui->bodyUnitPriceDoubleSpinBox->setValue(0.0);
    ui->unitPriceDoubleSpinBox->setValue(0.0);
    ui->templateConfigurationRadioButton->setChecked(true);
    m_updatingComponentTable = false;
    loadTemplatesForCurrentProduct();
}

bool MainWindow::validateOrderInput(QString *errorMessage) const
{
    if (ui->customerNameLineEdit->text().trimmed().isEmpty()) {
        *errorMessage = QStringLiteral("客户名称不能为空。");
        return false;
    }

    if (ui->productModelComboBox->currentIndex() < 0) {
        *errorMessage = QStringLiteral("请选择具体型号。");
        return false;
    }

    if (ui->bodyUnitPriceDoubleSpinBox->value() < 0.0) {
        *errorMessage = QStringLiteral("主体单价不能小于 0。");
        return false;
    }

    if (ui->templateConfigurationRadioButton->isChecked()
        && ui->templateComboBox->currentIndex() < 0) {
        *errorMessage = QStringLiteral("请选择配置模板。");
        return false;
    }

    const QList<OrderComponentData> components = collectComponentsFromTable();
    bool hasValidComponent = false;
    for (const OrderComponentData &component : components) {
        if (component.componentName.isEmpty()) {
            continue;
        }

        if (component.quantityPerSet <= 0) {
            *errorMessage = QStringLiteral("组件每套数量必须大于 0。");
            return false;
        }

        if (component.unitPrice < 0.0) {
            *errorMessage = QStringLiteral("组件单价不能小于 0。");
            return false;
        }

        hasValidComponent = true;
    }

    if (!hasValidComponent) {
        *errorMessage = QStringLiteral("请至少录入一个有效组件。");
        return false;
    }

    return true;
}

void MainWindow::loadQueryProductModels()
{
    if (m_isShuttingDown || ui == nullptr || ui->queryProductModelComboBox == nullptr) {
        return;
    }

    const QSignalBlocker blocker(ui->queryProductModelComboBox);
    ui->queryProductModelComboBox->clear();
    ui->queryProductModelComboBox->addItem(QStringLiteral("全部型号"), QString());

    const QList<ProductModelOption> models = m_databaseManager.productModels();
    for (const ProductModelOption &model : models) {
        ui->queryProductModelComboBox->addItem(model.name, model.name);
    }
}

void MainWindow::performOrderQuery()
{
    if (m_isShuttingDown || ui == nullptr) {
        return;
    }

    m_queryOrders = m_databaseManager.queryOrders(ui->queryCustomerLineEdit->text().trimmed(),
                                                  ui->queryProductModelComboBox->currentData().toString(),
                                                  ui->queryOnlyUnfinishedCheckBox->isChecked());
    setQueryOrderRows(m_queryOrders);
}

void MainWindow::setQueryOrderRows(const QList<ShipmentOrderSummary> &orders)
{
    if (m_isShuttingDown || ui == nullptr || ui->orderListTableWidget == nullptr) {
        return;
    }

    const int previousOrderId = currentQueryOrderId();
    ui->orderListTableWidget->setSortingEnabled(false);
    const QSignalBlocker blocker(ui->orderListTableWidget);
    ui->orderListTableWidget->setRowCount(0);

    for (const ShipmentOrderSummary &order : orders) {
        const int row = ui->orderListTableWidget->rowCount();
        ui->orderListTableWidget->insertRow(row);

        auto *idItem = createIdTableWidgetItem(order.id);
        auto *dateItem = new QTableWidgetItem(order.orderDate);
        auto *customerItem = new QTableWidgetItem(order.customerName);
        auto *categoryItem = new QTableWidgetItem;
        auto *modelItem = new QTableWidgetItem(order.productModelName);
        auto *configurationItem = new QTableWidgetItem(order.configurationName);
        auto *quantityItem = new QTableWidgetItem(QString::number(order.quantitySets));
        auto *unitPriceItem = new QTableWidgetItem(QString::number(order.unitPrice, 'f', 2));
        auto *statusItem =
            new QTableWidgetItem(order.isCompleted ? QStringLiteral("已完成")
                                                   : QStringLiteral("未完成"));
        auto *readyItem = new QTableWidgetItem(order.availableSetShipments > 0
                                                   ? QStringLiteral("可发货")
                                                   : QStringLiteral("不可发货"));

        ui->orderListTableWidget->setItem(row, kQueryOrderIdColumn, idItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderDateColumn, dateItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderCustomerColumn, customerItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderCategoryColumn, categoryItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderProductModelColumn, modelItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderConfigurationColumn, configurationItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderQuantitySetsColumn, quantityItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderUnitPriceColumn, unitPriceItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderStatusColumn, statusItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderShipmentReadyColumn, readyItem);
    }

    applyDefaultAscendingSort(ui->orderListTableWidget, kQueryOrderIdColumn);

    ui->queryOrderCountValueLabel->setText(QString::number(orders.size()));

    int targetRow = -1;
    for (int row = 0; row < ui->orderListTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *idItem = ui->orderListTableWidget->item(row, kQueryOrderIdColumn);
        if (idItem != nullptr && idItem->data(Qt::UserRole).toInt() == previousOrderId) {
            targetRow = row;
            break;
        }
    }
    if (targetRow < 0 && ui->orderListTableWidget->rowCount() > 0) {
        targetRow = 0;
    }

    if (targetRow >= 0) {
        ui->orderListTableWidget->selectRow(targetRow);
    } else {
        clearQueryDetails();
    }

    refreshQueryOrderDetails();
}

void MainWindow::refreshQueryOrderDetails()
{
    if (m_isShuttingDown) {
        return;
    }

    const int orderItemId = currentQueryOrderId();
    if (orderItemId <= 0) {
        clearQueryDetails();
        return;
    }

    setQueryComponentRows(m_databaseManager.orderComponents(orderItemId));
    setQueryShipmentRows(m_databaseManager.orderShipments(orderItemId));
}

void MainWindow::setQueryComponentRows(const QList<ShipmentComponentStatus> &components)
{
    if (m_isShuttingDown || ui == nullptr || ui->orderDetailComponentTableWidget == nullptr) {
        return;
    }

    const QSignalBlocker blocker(ui->orderDetailComponentTableWidget);
    ui->orderDetailComponentTableWidget->setRowCount(0);

    for (const ShipmentComponentStatus &component : components) {
        const int row = ui->orderDetailComponentTableWidget->rowCount();
        ui->orderDetailComponentTableWidget->insertRow(row);
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentNameColumn, new QTableWidgetItem(component.componentName));
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentSpecColumn, new QTableWidgetItem);
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentMaterialColumn, new QTableWidgetItem);
        ui->orderDetailComponentTableWidget->setItem(
            row, kQueryDetailComponentColorColumn, new QTableWidgetItem);
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailQuantityPerSetColumn,
            new QTableWidgetItem(QString::number(component.quantityPerSet)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailTotalRequiredColumn,
            new QTableWidgetItem(QString::number(component.totalRequiredQuantity)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailUnitPriceColumn,
            new QTableWidgetItem);
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailSourceColumn,
            new QTableWidgetItem(
                QStringLiteral("已发 %1 / 未发 %2")
                    .arg(component.shippedQuantity)
                    .arg(component.unshippedQuantity)));
    }
}

void MainWindow::setQueryShipmentRows(const QList<OrderShipmentRecord> &records)
{
    if (m_isShuttingDown || ui == nullptr || ui->orderShipmentHistoryTableWidget == nullptr) {
        return;
    }

    const QSignalBlocker blocker(ui->orderShipmentHistoryTableWidget);
    ui->orderShipmentHistoryTableWidget->setRowCount(0);

    for (const OrderShipmentRecord &record : records) {
        const int row = ui->orderShipmentHistoryTableWidget->rowCount();
        ui->orderShipmentHistoryTableWidget->insertRow(row);
        ui->orderShipmentHistoryTableWidget->setItem(
            row, kQueryShipmentDateColumn, new QTableWidgetItem(record.shipmentDate));
        ui->orderShipmentHistoryTableWidget->setItem(
            row, kQueryShipmentTypeColumn, new QTableWidgetItem(record.shipmentType));
        ui->orderShipmentHistoryTableWidget->setItem(
            row,
            kQueryShipmentQuantityColumn,
            new QTableWidgetItem(QString::number(record.shipmentQuantity)));
        ui->orderShipmentHistoryTableWidget->setItem(
            row, kQueryShipmentNoteColumn, new QTableWidgetItem(record.note));
    }
}

void MainWindow::clearQueryDetails()
{
    if (ui == nullptr) {
        return;
    }

    if (ui->orderDetailComponentTableWidget != nullptr) {
        const QSignalBlocker blocker(ui->orderDetailComponentTableWidget);
        ui->orderDetailComponentTableWidget->setRowCount(0);
    }
    if (ui->orderShipmentHistoryTableWidget != nullptr) {
        const QSignalBlocker blocker(ui->orderShipmentHistoryTableWidget);
        ui->orderShipmentHistoryTableWidget->setRowCount(0);
    }
}

int MainWindow::currentQueryOrderId() const
{
    if (ui == nullptr || ui->orderListTableWidget == nullptr) {
        return 0;
    }

    const int row = ui->orderListTableWidget->currentRow();
    if (row < 0) {
        return 0;
    }

    const QTableWidgetItem *idItem = ui->orderListTableWidget->item(row, kQueryOrderIdColumn);
    return idItem != nullptr ? idItem->data(Qt::UserRole).toInt() : 0;
}

void MainWindow::loadShipmentOrders()
{
    if (m_isShuttingDown) {
        return;
    }

    const int previousOrderId = currentShipmentOrderId();
    m_shipmentOrders = m_databaseManager.structuredOrders(true);

    {
        const QSignalBlocker blocker(ui->shipmentOrderComboBox);
        ui->shipmentOrderComboBox->clear();

        for (const StructuredOrderSummary &order : m_shipmentOrders) {
            ui->shipmentOrderComboBox->addItem(structuredShipmentOrderDisplayText(order), order.id);
        }

        int targetIndex = -1;
        for (int index = 0; index < ui->shipmentOrderComboBox->count(); ++index) {
            if (ui->shipmentOrderComboBox->itemData(index).toInt() == previousOrderId) {
                targetIndex = index;
                break;
            }
        }

        if (targetIndex < 0 && ui->shipmentOrderComboBox->count() > 0) {
            targetIndex = 0;
        }

        if (targetIndex >= 0) {
            ui->shipmentOrderComboBox->setCurrentIndex(targetIndex);
        }
    }

    refreshShipmentDetails();
}

void MainWindow::refreshShipmentDetails()
{
    if (m_isShuttingDown) {
        return;
    }

    const int orderId = currentShipmentOrderId();
    StructuredOrderSummary currentOrder;
    bool hasOrder = false;
    for (const StructuredOrderSummary &order : m_shipmentOrders) {
        if (order.id == orderId) {
            currentOrder = order;
            hasOrder = true;
            break;
        }
    }

    if (!hasOrder) {
        ui->shipmentOrderStatusValueLabel->setText(QStringLiteral("暂无待发货订单"));
        ui->shipmentSetsSpinBox->setMaximum(1);
        ui->shipmentSetsSpinBox->setValue(0);
        ui->saveOrderShipmentButton->setEnabled(false);
        ui->componentShipmentNoteLineEdit->clear();
        ui->shipmentOrderNoteLineEdit->clear();
        m_shipmentComponents.clear();
        setShipmentComponentRows({});
        return;
    }

    ui->shipmentOrderStatusValueLabel->setText(
        QStringLiteral("订单 #%1 | %2 | %3 | %4 | 总套数 %5 | 可整套再发 %6 | 配置价格 %7 | 状态：%8")
            .arg(currentOrder.id)
            .arg(currentOrder.customerName)
            .arg(currentOrder.productSkuName)
            .arg(currentOrder.baseConfigurationName)
            .arg(currentOrder.orderQuantity)
            .arg(currentOrder.availableSetShipments)
            .arg(QString::number(currentOrder.configPrice, 'f', 2))
            .arg(structuredOrderShipmentStatusText(currentOrder)));
    ui->shipmentOrderNoteLineEdit->clear();
    ui->componentShipmentNoteLineEdit->clear();
    ui->shipmentSetsSpinBox->setMaximum(qMax(1, currentOrder.availableSetShipments));
    ui->shipmentSetsSpinBox->setValue(currentOrder.availableSetShipments > 0 ? 1 : 0);
    ui->saveOrderShipmentButton->setEnabled(currentOrder.availableSetShipments > 0);

    m_shipmentComponents = m_databaseManager.structuredOrderComponents(orderId);
    setShipmentComponentRows(m_shipmentComponents);
}

void MainWindow::setShipmentComponentRows(const QList<StructuredOrderComponentSnapshot> &components)
{
    if (m_isShuttingDown || ui == nullptr || ui->shipmentComponentTableWidget == nullptr) {
        return;
    }

    const QSignalBlocker blocker(ui->shipmentComponentTableWidget);
    ui->shipmentComponentTableWidget->setRowCount(0);

    for (const StructuredOrderComponentSnapshot &component : components) {
        const int row = ui->shipmentComponentTableWidget->rowCount();
        ui->shipmentComponentTableWidget->insertRow(row);

        auto *nameItem = new QTableWidgetItem(component.componentName);
        nameItem->setData(Qt::UserRole, component.id);
        auto *quantityPerSetItem =
            new QTableWidgetItem(QString::number(component.quantityPerSet));
        auto *unitPriceItem =
            new QTableWidgetItem(QString::number(component.unitAmount, 'f', 2));
        auto *totalRequiredItem =
            new QTableWidgetItem(QString::number(component.requiredQuantity));
        auto *shippedItem = new QTableWidgetItem(QString::number(component.shippedQuantity));
        auto *unshippedItem = new QTableWidgetItem(QString::number(component.unshippedQuantity));
        auto *totalPriceItem =
            new QTableWidgetItem(QString::number(component.lineAmount, 'f', 2));
        auto *sourceItem =
            new QTableWidgetItem(structuredSourceDisplayText(component.sourceType));

        ui->shipmentComponentTableWidget->setItem(row, kShipmentComponentNameColumn, nameItem);
        ui->shipmentComponentTableWidget->setItem(row,
                                                  kShipmentComponentQuantityPerSetColumn,
                                                  quantityPerSetItem);
        ui->shipmentComponentTableWidget->setItem(row,
                                                  kShipmentComponentUnitPriceColumn,
                                                  unitPriceItem);
        ui->shipmentComponentTableWidget->setItem(row,
                                                  kShipmentComponentTotalRequiredColumn,
                                                  totalRequiredItem);
        ui->shipmentComponentTableWidget->setItem(row, kShipmentComponentShippedColumn, shippedItem);
        ui->shipmentComponentTableWidget->setItem(row,
                                                  kShipmentComponentUnshippedColumn,
                                                  unshippedItem);
        ui->shipmentComponentTableWidget->setItem(row,
                                                  kShipmentComponentTotalPriceColumn,
                                                  totalPriceItem);
        ui->shipmentComponentTableWidget->setItem(row, kShipmentComponentSourceColumn, sourceItem);
    }

    if (!components.isEmpty()) {
        ui->shipmentComponentTableWidget->selectRow(0);
    }

    updateSelectedShipmentComponent();
}

void MainWindow::updateSelectedShipmentComponent()
{
    if (m_isShuttingDown || ui == nullptr || ui->shipmentComponentTableWidget == nullptr
        || ui->selectedComponentValueLabel == nullptr
        || ui->componentShipmentQuantitySpinBox == nullptr
        || ui->saveComponentShipmentButton == nullptr) {
        return;
    }

    const int componentId = selectedShipmentComponentId();
    if (componentId <= 0) {
        ui->selectedComponentValueLabel->setText(QStringLiteral("未选择"));
        ui->componentShipmentQuantitySpinBox->setEnabled(false);
        ui->componentShipmentQuantitySpinBox->setMaximum(1);
        ui->componentShipmentQuantitySpinBox->setValue(1);
        ui->componentShipmentNoteLineEdit->clear();
        ui->saveComponentShipmentButton->setEnabled(false);
        return;
    }

    for (const StructuredOrderComponentSnapshot &component : m_shipmentComponents) {
        if (component.id != componentId) {
            continue;
        }

        ui->selectedComponentValueLabel->setText(
            QStringLiteral("%1（未发 %2）")
                .arg(component.componentName)
                .arg(component.unshippedQuantity));
        ui->componentShipmentQuantitySpinBox->setEnabled(component.unshippedQuantity > 0);
        ui->componentShipmentQuantitySpinBox->setMaximum(qMax(1, component.unshippedQuantity));
        ui->componentShipmentQuantitySpinBox->setValue(component.unshippedQuantity > 0 ? 1 : 0);
        ui->componentShipmentNoteLineEdit->clear();
        ui->saveComponentShipmentButton->setEnabled(component.unshippedQuantity > 0);
        return;
    }

    ui->selectedComponentValueLabel->setText(QStringLiteral("未选择"));
    ui->componentShipmentQuantitySpinBox->setEnabled(false);
    ui->componentShipmentNoteLineEdit->clear();
    ui->saveComponentShipmentButton->setEnabled(false);
}

int MainWindow::currentShipmentOrderId() const
{
    if (ui == nullptr || ui->shipmentOrderComboBox == nullptr) {
        return 0;
    }
    return ui->shipmentOrderComboBox->currentData().toInt();
}

int MainWindow::selectedShipmentComponentId() const
{
    if (ui == nullptr || ui->shipmentComponentTableWidget == nullptr) {
        return 0;
    }

    const int row = ui->shipmentComponentTableWidget->currentRow();
    if (row < 0) {
        return 0;
    }

    const QTableWidgetItem *nameItem =
        ui->shipmentComponentTableWidget->item(row, kShipmentComponentNameColumn);
    return nameItem != nullptr ? nameItem->data(Qt::UserRole).toInt() : 0;
}
