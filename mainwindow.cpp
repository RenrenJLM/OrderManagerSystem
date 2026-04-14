#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDate>
#include <QDebug>
#include <QHeaderView>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTableWidgetItem>

namespace {
constexpr int kComponentNameColumn = 0;
constexpr int kQuantityPerSetColumn = 1;
constexpr int kSourceTypeColumn = 2;
constexpr int kTotalRequiredColumn = 3;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    m_isInitializing = true;
    ui->setupUi(this);

    if (!m_databaseManager.ensureMinimumDemoData()) {
        QMessageBox::critical(this,
                              QStringLiteral("基础数据初始化失败"),
                              m_databaseManager.lastError());
    }

    setupUiState();
    loadProductModels();

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

                if (item->column() == kQuantityPerSetColumn) {
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

                statusBar()->showMessage(QStringLiteral("订单已保存"), 3000);
                clearOrderForm();
            });

    m_isInitializing = false;
    qDebug() << "MainWindow initialized";
}

MainWindow::~MainWindow()
{
    m_isShuttingDown = true;
    qDebug() << "MainWindow shutting down";
    delete ui;
    ui = nullptr;
}

void MainWindow::setupUiState()
{
    ui->orderDateEdit->setDate(QDate::currentDate());
    ui->quantitySetsSpinBox->setMinimum(1);
    ui->unitPriceDoubleSpinBox->setMinimum(0.0);
    ui->unitPriceDoubleSpinBox->setDecimals(2);
    ui->unitPriceDoubleSpinBox->setMaximum(9999999.99);

    ui->componentTableWidget->setColumnCount(4);
    ui->componentTableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("组件名称"),
         QStringLiteral("每套数量"),
         QStringLiteral("来源"),
         QStringLiteral("总需求数量")});
    ui->componentTableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kComponentNameColumn,
                                                                       QHeaderView::Stretch);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kQuantityPerSetColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kSourceTypeColumn,
                                                                       QHeaderView::ResizeToContents);
    ui->componentTableWidget->horizontalHeader()->setSectionResizeMode(kTotalRequiredColumn,
                                                                       QHeaderView::ResizeToContents);

    ui->templateConfigurationRadioButton->setChecked(true);
    setCustomConfigurationMode(false);
}

void MainWindow::loadProductModels()
{
    m_updatingComponentTable = true;
    const QSignalBlocker blocker(ui->productModelComboBox);
    ui->productModelComboBox->clear();

    const QList<ProductModelOption> models = m_databaseManager.productModels();
    for (const ProductModelOption &model : models) {
        ui->productModelComboBox->addItem(model.name, model.id);
    }

    if (ui->productModelComboBox->count() == 0) {
        statusBar()->showMessage(QStringLiteral("没有可用的产品型号数据"), 5000);
    }

    m_updatingComponentTable = false;
    loadTemplatesForCurrentProduct();
}

void MainWindow::loadTemplatesForCurrentProduct()
{
    if (m_isShuttingDown) {
        return;
    }

    const int productModelId = ui->productModelComboBox->currentData().toInt();

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
            statusBar()->showMessage(QStringLiteral("当前产品没有可用模板"), 5000);
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
        addEmptyComponentRow();
    } else {
        loadSelectedTemplateComponents();
    }
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
        auto *sourceItem =
            new QTableWidgetItem(component.sourceType == QStringLiteral("template")
                                     ? QStringLiteral("模板")
                                     : QStringLiteral("手动"));
        auto *totalItem = new QTableWidgetItem(QString());

        if (!editableNamesAndQty) {
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            quantityItem->setFlags(quantityItem->flags() & ~Qt::ItemIsEditable);
        }
        sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
        totalItem->setFlags(totalItem->flags() & ~Qt::ItemIsEditable);

        ui->componentTableWidget->setItem(row, kComponentNameColumn, nameItem);
        ui->componentTableWidget->setItem(row, kQuantityPerSetColumn, quantityItem);
        ui->componentTableWidget->setItem(row, kSourceTypeColumn, sourceItem);
        ui->componentTableWidget->setItem(row, kTotalRequiredColumn, totalItem);
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

    auto *nameItem = new QTableWidgetItem();
    auto *quantityItem = new QTableWidgetItem(QStringLiteral("1"));
    auto *sourceItem = new QTableWidgetItem(QStringLiteral("手动"));
    auto *totalItem = new QTableWidgetItem(QString());

    sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
    totalItem->setFlags(totalItem->flags() & ~Qt::ItemIsEditable);

    ui->componentTableWidget->setItem(row, kComponentNameColumn, nameItem);
    ui->componentTableWidget->setItem(row, kQuantityPerSetColumn, quantityItem);
    ui->componentTableWidget->setItem(row, kSourceTypeColumn, sourceItem);
    ui->componentTableWidget->setItem(row, kTotalRequiredColumn, totalItem);

    m_updatingComponentTable = false;
    updateComponentTotals();
    ui->componentTableWidget->setCurrentCell(row, kComponentNameColumn);
}

QList<OrderComponentData> MainWindow::collectComponentsFromTable() const
{
    QList<OrderComponentData> components;

    for (int row = 0; row < ui->componentTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *nameItem =
            ui->componentTableWidget->item(row, kComponentNameColumn);
        const QTableWidgetItem *quantityItem =
            ui->componentTableWidget->item(row, kQuantityPerSetColumn);
        const QTableWidgetItem *sourceItem =
            ui->componentTableWidget->item(row, kSourceTypeColumn);

        OrderComponentData component;
        component.componentName = nameItem != nullptr ? nameItem->text().trimmed() : QString();
        component.quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
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
        QTableWidgetItem *totalItem = ui->componentTableWidget->item(row, kTotalRequiredColumn);
        if (totalItem == nullptr) {
            continue;
        }

        const int quantityPerSet = quantityItem != nullptr ? quantityItem->text().toInt() : 0;
        const int totalRequired = quantityPerSet * ui->quantitySetsSpinBox->value();
        totalItem->setText(quantityPerSet > 0 ? QString::number(totalRequired) : QString());
    }

    m_updatingComponentTable = false;
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

        hasValidComponent = true;
    }

    if (!hasValidComponent) {
        *errorMessage = QStringLiteral("请至少录入一个有效组件。");
        return false;
    }

    return true;
}
