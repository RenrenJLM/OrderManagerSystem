#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QDate>
#include <QDebug>
#include <QEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QWindow>
#include <QtGlobal>

namespace {
constexpr int kComponentNameColumn = 0;
constexpr int kQuantityPerSetColumn = 1;
constexpr int kUnitPriceColumn = 2;
constexpr int kSourceTypeColumn = 3;
constexpr int kTotalRequiredColumn = 4;
constexpr int kComponentTotalPriceColumn = 5;

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
constexpr int kQueryOrderProductModelColumn = 3;
constexpr int kQueryOrderQuantitySetsColumn = 4;
constexpr int kQueryOrderShippedColumn = 5;
constexpr int kQueryOrderUnshippedColumn = 6;
constexpr int kQueryOrderUnitPriceColumn = 7;
constexpr int kQueryOrderTotalPriceColumn = 8;
constexpr int kQueryOrderStatusColumn = 9;

constexpr int kQueryDetailComponentNameColumn = 0;
constexpr int kQueryDetailQuantityPerSetColumn = 1;
constexpr int kQueryDetailTotalRequiredColumn = 2;
constexpr int kQueryDetailShippedColumn = 3;
constexpr int kQueryDetailUnshippedColumn = 4;

constexpr int kQueryShipmentDateColumn = 0;
constexpr int kQueryShipmentTypeColumn = 1;
constexpr int kQueryShipmentQuantityColumn = 2;
constexpr int kQueryShipmentNoteColumn = 3;

QString shipmentOrderDisplayText(const ShipmentOrderSummary &order)
{
    return QStringLiteral("#%1 | %2 | %3 | %4 | %5 | %6")
        .arg(order.id)
        .arg(order.orderDate)
        .arg(order.customerName)
        .arg(order.productModelName)
        .arg(order.configurationName)
        .arg(order.isCompleted
                 ? QStringLiteral("订单已完成")
                 : QStringLiteral("机体未发 %1 套 | 可整套发 %2 套")
                       .arg(order.unshippedSets)
                       .arg(order.availableSetShipments));
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

    if (!m_databaseManager.ensureMinimumDemoData()) {
        QMessageBox::critical(this,
                              QStringLiteral("基础数据初始化失败"),
                              m_databaseManager.lastError());
    }

    setupUiState();
    loadProductModels();
    loadQueryProductModels();
    loadShipmentOrders();
    performOrderQuery();

    connect(ui->productModelComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { loadTemplatesForCurrentProduct(); });
    connect(ui->templateComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (!ui->customConfigurationRadioButton->isChecked()) {
                    loadSelectedTemplateComponents();
                }
            });
    connect(ui->customConfigurationRadioButton,
            &QRadioButton::toggled,
            this,
            [this](bool checked) { setCustomConfigurationMode(checked); });
    connect(ui->quantitySetsSpinBox,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            [this](int) { updateComponentTotals(); });
    connect(ui->addComponentButton, &QPushButton::clicked, this, &MainWindow::addEmptyComponentRow);
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
                    updateComponentTotals();
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

                if (item->column() == kQuantityPerSetColumn || item->column() == kUnitPriceColumn) {
                    updateComponentTotals();
                }
            });
    connect(ui->saveOrderButton,
            &QPushButton::clicked,
            this,
            [this]() {
                QString errorMessage;
                if (!validateOrderInput(&errorMessage)) {
                    QMessageBox::warning(this, QStringLiteral("订单保存失败"), errorMessage);
                    return;
                }

                OrderSaveData orderData;
                orderData.orderDate = ui->orderDateEdit->date().toString(Qt::ISODate);
                orderData.customerName = ui->customerNameLineEdit->text().trimmed();
                orderData.productModelName = ui->productModelComboBox->currentText();
                orderData.quantitySets = ui->quantitySetsSpinBox->value();
                orderData.bodyUnitPrice = ui->bodyUnitPriceDoubleSpinBox->value();
                orderData.unitPrice = ui->unitPriceDoubleSpinBox->value();
                orderData.configurationName = ui->customConfigurationRadioButton->isChecked()
                                                  ? QStringLiteral("自定义配置")
                                                  : ui->templateComboBox->currentText();

                if (!m_databaseManager.saveOrder(orderData, collectComponentsFromTable())) {
                    QMessageBox::critical(this,
                                          QStringLiteral("订单保存失败"),
                                          m_databaseManager.lastError());
                    return;
                }

                showStatusMessage(QStringLiteral("订单已保存"), 3000);
                clearOrderForm();
                loadShipmentOrders();
                performOrderQuery();
            });
    connect(ui->querySearchButton, &QPushButton::clicked, this, &MainWindow::performOrderQuery);
    connect(ui->queryResetButton,
            &QPushButton::clicked,
            this,
            [this]() {
                ui->queryCustomerLineEdit->clear();
                ui->queryProductModelComboBox->setCurrentIndex(0);
                ui->queryOnlyUnfinishedCheckBox->setChecked(false);
                performOrderQuery();
            });
    connect(ui->mainTabWidget,
            &QTabWidget::currentChanged,
            this,
            [this](int index) {
                if (index == 0) {
                    loadTemplatesForCurrentProduct();
                } else if (index == 2) {
                    performOrderQuery();
                }
            });
    connect(ui->orderListTableWidget,
            &QTableWidget::itemSelectionChanged,
            this,
            &MainWindow::refreshQueryOrderDetails);
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
                const int orderItemId = currentShipmentOrderId();
                if (orderItemId <= 0) {
                    QMessageBox::warning(this,
                                         QStringLiteral("订单发货失败"),
                                         QStringLiteral("请先选择订单。"));
                    return;
                }

                if (!m_databaseManager.saveOrderShipment(orderItemId,
                                                         ui->shipmentDateEdit->date().toString(Qt::ISODate),
                                                         ui->shipmentSetsSpinBox->value(),
                                                         ui->shipmentOrderNoteLineEdit->text())) {
                    QMessageBox::critical(this,
                                          QStringLiteral("订单发货失败"),
                                          m_databaseManager.lastError());
                    return;
                }

                ui->shipmentOrderNoteLineEdit->clear();
                showStatusMessage(QStringLiteral("订单发货已保存"), 3000);
                loadShipmentOrders();
            });
    connect(ui->saveComponentShipmentButton,
            &QPushButton::clicked,
            this,
            [this]() {
                const int orderItemId = currentShipmentOrderId();
                const int componentId = selectedShipmentComponentId();
                if (orderItemId <= 0 || componentId <= 0) {
                    QMessageBox::warning(this,
                                         QStringLiteral("组件发货失败"),
                                         QStringLiteral("请先选择订单组件。"));
                    return;
                }

                if (!m_databaseManager.saveComponentShipment(
                        orderItemId,
                        componentId,
                        ui->shipmentDateEdit->date().toString(Qt::ISODate),
                        ui->componentShipmentQuantitySpinBox->value(),
                        ui->componentShipmentNoteLineEdit->text())) {
                    QMessageBox::critical(this,
                                          QStringLiteral("组件发货失败"),
                                          m_databaseManager.lastError());
                    return;
                }

                const int previouslySelectedComponentId = componentId;
                ui->componentShipmentNoteLineEdit->clear();
                showStatusMessage(QStringLiteral("组件发货已保存"), 3000);
                loadShipmentOrders();
                performOrderQuery();

                for (int row = 0; row < ui->shipmentComponentTableWidget->rowCount(); ++row) {
                    QTableWidgetItem *item =
                        ui->shipmentComponentTableWidget->item(row, kShipmentComponentNameColumn);
                    if (item != nullptr
                        && item->data(Qt::UserRole).toInt() == previouslySelectedComponentId) {
                        ui->shipmentComponentTableWidget->selectRow(row);
                        break;
                    }
                }
            });

    m_isInitializing = false;
    qDebug() << "MainWindow initialized";
}

MainWindow::~MainWindow()
{
    m_isShuttingDown = true;
    qDebug() << "MainWindow shutting down";
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
    ui->bodyUnitPriceDoubleSpinBox->setReadOnly(false);
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

    ui->componentTableWidget->setColumnCount(6);
    ui->componentTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("组件名称"),
         QStringLiteral("每套数量"),
         QStringLiteral("单价"),
         QStringLiteral("来源"),
         QStringLiteral("总需求数量"),
         QStringLiteral("总价")});
    ui->componentTableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kComponentNameColumn,
                                                                       QHeaderView::Stretch);
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

    ui->shipmentComponentTableWidget->setColumnCount(8);
    ui->shipmentComponentTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("组件名称"),
         QStringLiteral("每套数量"),
         QStringLiteral("单价"),
         QStringLiteral("总需求数量"),
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
        kShipmentComponentShippedColumn, QHeaderView::ResizeToContents);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentUnshippedColumn, QHeaderView::ResizeToContents);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentTotalPriceColumn, QHeaderView::ResizeToContents);
    ui->shipmentComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kShipmentComponentSourceColumn, QHeaderView::ResizeToContents);
    configureTableWidget(ui->shipmentComponentTableWidget);

    ui->orderListTableWidget->setColumnCount(10);
    ui->orderListTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("订单ID"),
         QStringLiteral("日期"),
         QStringLiteral("客户名称"),
         QStringLiteral("产品型号"),
         QStringLiteral("套数"),
         QStringLiteral("已发"),
         QStringLiteral("未发"),
         QStringLiteral("单套总价"),
         QStringLiteral("订单总价"),
         QStringLiteral("状态")});
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
        kQueryOrderProductModelColumn, QHeaderView::Stretch);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderQuantitySetsColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderShippedColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderUnshippedColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderUnitPriceColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderTotalPriceColumn, QHeaderView::ResizeToContents);
    ui->orderListTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryOrderStatusColumn, QHeaderView::ResizeToContents);
    configureTableWidget(ui->orderListTableWidget);

    ui->orderDetailComponentTableWidget->setColumnCount(5);
    ui->orderDetailComponentTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("组件名称"),
         QStringLiteral("每套数量"),
         QStringLiteral("总需求"),
         QStringLiteral("已发"),
         QStringLiteral("未发")});
    ui->orderDetailComponentTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->orderDetailComponentTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->orderDetailComponentTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailComponentNameColumn, QHeaderView::Stretch);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailQuantityPerSetColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailTotalRequiredColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailShippedColumn, QHeaderView::ResizeToContents);
    ui->orderDetailComponentTableWidget->horizontalHeader()->setSectionResizeMode(
        kQueryDetailUnshippedColumn, QHeaderView::ResizeToContents);
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

    ui->orderDetailSplitter->setChildrenCollapsible(false);
    ui->orderDetailSplitter->setHandleWidth(1);
    ui->orderDetailSplitter->setStretchFactor(0, 1);
    ui->orderDetailSplitter->setStretchFactor(1, 1);

    ui->templateConfigurationRadioButton->setChecked(true);
    setCustomConfigurationMode(false);
    ui->saveOrderShipmentButton->setEnabled(false);
    ui->saveComponentShipmentButton->setEnabled(false);
    ui->componentShipmentQuantitySpinBox->setEnabled(false);
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
    connect(ui->bodyUnitPriceDoubleSpinBox,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this](double) {
                if (!m_updatingComponentTable && !m_isInitializing && !m_isShuttingDown) {
                    updatePriceDisplays();
                }
            });
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
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
        loadProductModels();
        loadQueryProductModels();
        loadShipmentOrders();
        performOrderQuery();
        showStatusMessage(QStringLiteral("界面数据已刷新"), 3000);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出"), this, &QWidget::close);
    ui->fileMenuButton->setMenu(fileMenu);

    auto *viewMenu = new QMenu(this);
    viewMenu->addAction(QStringLiteral("订单录入"), this, [this]() { ui->mainTabWidget->setCurrentIndex(0); });
    viewMenu->addAction(QStringLiteral("发货登记"), this, [this]() { ui->mainTabWidget->setCurrentIndex(1); });
    viewMenu->addAction(QStringLiteral("订单查询"), this, [this]() { ui->mainTabWidget->setCurrentIndex(2); });
    ui->viewMenuButton->setMenu(viewMenu);

    auto *toolsMenu = new QMenu(this);
    toolsMenu->addAction(QStringLiteral("重新加载基础数据"), this, [this]() {
        loadProductModels();
        loadQueryProductModels();
        loadShipmentOrders();
        performOrderQuery();
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
                                 QStringLiteral("通过页签完成订单录入、发货登记和订单查询。顶栏菜单用于常用入口和预留扩展。"));
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
        "  border: none;"
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
        showStatusMessage(QStringLiteral("没有可用的产品型号数据"), 5000);
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
        *errorMessage = QStringLiteral("请选择产品型号。");
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
    const QSignalBlocker blocker(ui->orderListTableWidget);
    ui->orderListTableWidget->setRowCount(0);

    for (const ShipmentOrderSummary &order : orders) {
        const int row = ui->orderListTableWidget->rowCount();
        ui->orderListTableWidget->insertRow(row);

        auto *idItem = new QTableWidgetItem(QString::number(order.id));
        idItem->setData(Qt::UserRole, order.id);
        auto *dateItem = new QTableWidgetItem(order.orderDate);
        auto *customerItem = new QTableWidgetItem(order.customerName);
        auto *modelItem = new QTableWidgetItem(order.productModelName);
        auto *quantityItem = new QTableWidgetItem(QString::number(order.quantitySets));
        auto *shippedItem = new QTableWidgetItem(QString::number(order.shippedSets));
        auto *unshippedItem = new QTableWidgetItem(QString::number(order.unshippedSets));
        auto *unitPriceItem = new QTableWidgetItem(QString::number(order.unitPrice, 'f', 2));
        auto *totalPriceItem = new QTableWidgetItem(QString::number(order.totalPrice, 'f', 2));
        auto *statusItem =
            new QTableWidgetItem(order.isCompleted ? QStringLiteral("已完成")
                                                   : QStringLiteral("未完成"));

        ui->orderListTableWidget->setItem(row, kQueryOrderIdColumn, idItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderDateColumn, dateItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderCustomerColumn, customerItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderProductModelColumn, modelItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderQuantitySetsColumn, quantityItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderShippedColumn, shippedItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderUnshippedColumn, unshippedItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderUnitPriceColumn, unitPriceItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderTotalPriceColumn, totalPriceItem);
        ui->orderListTableWidget->setItem(row, kQueryOrderStatusColumn, statusItem);
    }

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
            row,
            kQueryDetailQuantityPerSetColumn,
            new QTableWidgetItem(QString::number(component.quantityPerSet)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailTotalRequiredColumn,
            new QTableWidgetItem(QString::number(component.totalRequiredQuantity)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailShippedColumn,
            new QTableWidgetItem(QString::number(component.shippedQuantity)));
        ui->orderDetailComponentTableWidget->setItem(
            row,
            kQueryDetailUnshippedColumn,
            new QTableWidgetItem(QString::number(component.unshippedQuantity)));
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
    m_shipmentOrders = m_databaseManager.shipmentOrders();

    {
        const QSignalBlocker blocker(ui->shipmentOrderComboBox);
        ui->shipmentOrderComboBox->clear();

        for (const ShipmentOrderSummary &order : m_shipmentOrders) {
            ui->shipmentOrderComboBox->addItem(shipmentOrderDisplayText(order), order.id);
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

    const int orderItemId = currentShipmentOrderId();
    ShipmentOrderSummary currentOrder;
    bool hasOrder = false;
    for (const ShipmentOrderSummary &order : m_shipmentOrders) {
        if (order.id == orderItemId) {
            currentOrder = order;
            hasOrder = true;
            break;
        }
    }

    if (!hasOrder) {
        ui->shipmentOrderStatusValueLabel->setText(QStringLiteral("暂无可发货订单"));
        ui->shipmentSetsSpinBox->setMaximum(1);
        ui->shipmentSetsSpinBox->setValue(0);
        ui->saveOrderShipmentButton->setEnabled(false);
        m_shipmentComponents.clear();
        setShipmentComponentRows({});
        return;
    }

    QString completionText;
    if (currentOrder.isCompleted) {
        completionText = QStringLiteral("订单已完成");
    } else if (currentOrder.unshippedSets == 0) {
        completionText = QStringLiteral("机体已发完，仍有组件未发完");
    } else if (currentOrder.availableSetShipments == 0) {
        completionText = QStringLiteral("组件不足以继续整套发货");
    } else {
        completionText = QStringLiteral("订单未完成");
    }

    ui->shipmentOrderStatusValueLabel->setText(
        QStringLiteral("总套数 %1，机体已发 %2，机体未发 %3，可按套再发 %4，单套总价 %5，订单总价 %6，状态：%7")
            .arg(currentOrder.quantitySets)
            .arg(currentOrder.shippedSets)
            .arg(currentOrder.unshippedSets)
            .arg(currentOrder.availableSetShipments)
            .arg(QString::number(currentOrder.unitPrice, 'f', 2))
            .arg(QString::number(currentOrder.totalPrice, 'f', 2))
            .arg(completionText));
    ui->shipmentSetsSpinBox->setMaximum(qMax(1, currentOrder.availableSetShipments));
    ui->shipmentSetsSpinBox->setValue(currentOrder.availableSetShipments > 0 ? 1 : 0);
    ui->saveOrderShipmentButton->setEnabled(currentOrder.availableSetShipments > 0);

    m_shipmentComponents = m_databaseManager.shipmentComponents(orderItemId);
    setShipmentComponentRows(m_shipmentComponents);
}

void MainWindow::setShipmentComponentRows(const QList<ShipmentComponentStatus> &components)
{
    if (m_isShuttingDown || ui == nullptr || ui->shipmentComponentTableWidget == nullptr) {
        return;
    }

    const QSignalBlocker blocker(ui->shipmentComponentTableWidget);
    ui->shipmentComponentTableWidget->setRowCount(0);

    for (const ShipmentComponentStatus &component : components) {
        const int row = ui->shipmentComponentTableWidget->rowCount();
        ui->shipmentComponentTableWidget->insertRow(row);

        auto *nameItem = new QTableWidgetItem(component.componentName);
        nameItem->setData(Qt::UserRole, component.id);
        auto *quantityPerSetItem =
            new QTableWidgetItem(QString::number(component.quantityPerSet));
        auto *unitPriceItem =
            new QTableWidgetItem(QString::number(component.unitPrice, 'f', 2));
        auto *totalRequiredItem =
            new QTableWidgetItem(QString::number(component.totalRequiredQuantity));
        auto *shippedItem = new QTableWidgetItem(QString::number(component.shippedQuantity));
        auto *unshippedItem = new QTableWidgetItem(QString::number(component.unshippedQuantity));
        auto *totalPriceItem =
            new QTableWidgetItem(QString::number(component.totalPrice, 'f', 2));
        auto *sourceItem =
            new QTableWidgetItem(component.isBodyComponent
                                     ? QStringLiteral("机体")
                                     : component.sourceType == QStringLiteral("template")
                                     ? QStringLiteral("模板")
                                     : QStringLiteral("手动"));

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
        ui->saveComponentShipmentButton->setEnabled(false);
        return;
    }

    for (const ShipmentComponentStatus &component : m_shipmentComponents) {
        if (component.id != componentId) {
            continue;
        }

        ui->selectedComponentValueLabel->setText(
            QStringLiteral("%1（未发 %2）")
                .arg(component.isBodyComponent ? QStringLiteral("机体") : component.componentName)
                .arg(component.unshippedQuantity));
        ui->componentShipmentQuantitySpinBox->setEnabled(component.unshippedQuantity > 0);
        ui->componentShipmentQuantitySpinBox->setMaximum(qMax(1, component.unshippedQuantity));
        ui->componentShipmentQuantitySpinBox->setValue(component.unshippedQuantity > 0 ? 1 : 0);
        ui->saveComponentShipmentButton->setEnabled(component.unshippedQuantity > 0);
        return;
    }

    ui->selectedComponentValueLabel->setText(QStringLiteral("未选择"));
    ui->componentShipmentQuantitySpinBox->setEnabled(false);
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
