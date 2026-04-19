#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "dataimporter.h"
#include "databasemanager.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QEvent;
class QDateEdit;
class QComboBox;
class QCompleter;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTimer;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUiState();
    void setupCustomTitleBar();
    void setupTopMenus();
    void restoreLastDatabase();
    QString currentDatabaseStatusText() const;
    void refreshStatusMessageLabel();
    bool ensureDatabaseOpenForAction(const QString &actionName);
    void createDatabase();
    void openDatabase();
    void closeCurrentDatabase();
    void refreshAllDatabaseViews();
    void clearAllDatabaseViews();
    void applyDatabaseOpenState(bool isOpen);
    void updateDatabaseStatusDisplay();
    void applyUiTheme();
    void configureTableWidget(QTableWidget *tableWidget) const;
    void showStatusMessage(const QString &message, int timeoutMs = 0);
    void toggleMaximizeRestore();
    void updateWindowControlButtons();
    void setupProductDataTab();
    void setupInventoryTab();
    void setupStructuredOrderUi();
    void configureDateEditCalendar(QDateEdit *dateEdit) const;
    void refreshComboBoxPopupWidth(QComboBox *comboBox) const;
    void enableComboBoxFiltering(QComboBox *comboBox) const;
    QComboBox *createInventoryComponentComboBox(int productCategoryId, QWidget *parent);
    void populateStructuredRowFromOption(int row, const ProductComponentOption &option);
    int structuredComponentRowForWidget(QWidget *widget) const;
    void refreshStructuredShipmentDetails();
    void setStructuredShipmentComponentRows(const QList<StructuredOrderComponentSnapshot> &components);
    int currentStructuredShipmentOrderId() const;
    int currentStructuredProductCategoryId();
    void loadStructuredOrderSkus();
    void loadBaseConfigurationsForCurrentSku();
    void rebuildStructuredOrderComponents();
    void setStructuredComponentTableRows(const QList<StructuredOrderComponentData> &components);
    QList<StructuredOrderComponentData> collectStructuredComponentsFromTable() const;
    void addStructuredComponentRow();
    void markStructuredRowAdjusted(int row, int column);
    void updateStructuredComponentTotals();
    void updateStructuredOrderDisplayFields();
    void clearStructuredOrderForm();
    bool validateStructuredOrderInput(QString *errorMessage) const;
    void loadProductDataPage();
    void loadCategoryEditor();
    void saveCategoryEditor();
    void addCategoryEditorRow();
    void refreshManagementCategorySelectors();
    void loadSkuEditor();
    void saveSkuEditor();
    void addSkuEditorRow();
    void loadConfigurationEditor();
    void saveConfigurationEditor();
    void addConfigurationEditorRow();
    void loadBomEditor();
    void saveBomEditor();
    void addBomEditorRow();
    void importProductCategoriesCsv();
    void importProductSkusCsv();
    void importBaseConfigurationsCsv();
    void importBaseConfigurationBomCsv();
    void importInventoryItemsCsv();
    void runCsvImport(DataImporter::ImportTarget target,
                      const QString &dialogTitle,
                      const QString &successMessage,
                      bool inventoryImport);
    void setupQueryOutputControls();
    void setupInventoryOutputControls();
    void loadStructuredQuerySkus();
    void performStructuredOrderQuery();
    void setStructuredQueryOrderRows(const QList<StructuredOrderSummary> &orders);
    void refreshStructuredQueryOrderDetails();
    void setStructuredQueryComponentRows(const QList<StructuredOrderComponentSnapshot> &components);
    void exportOrderSummaryCsv();
    void exportInventoryDemandCsv();
    void exportCurrentOrderShipmentCsv();
    StructuredOrderSummary structuredOrderSummaryById(int orderId) const;
    bool writeCsvFile(const QString &dialogTitle,
                      const QString &defaultFileName,
                      const QStringList &headers,
                      const QList<QStringList> &rows);
    void loadInventoryPage();
    void populateInventoryForm(const InventoryItemData &item);
    void clearInventoryForm();
    void saveInventoryForm();
    void refreshInventoryList();
    void refreshDemandSummaryTable();
    void refreshInventoryDemandPage();
    void refreshInventoryDemandScopeOptions();
    void refreshInventoryDemandShortageTable();
    void refreshShipmentReadyTables();
    void refreshStructuredShipmentReadySummary();
    void refreshStructuredOperationalViews(bool reloadInventoryPage);
    void updateResponsiveTableColumnWidths();
    void updateInventoryTableColumnWidths();
    int currentInventoryItemId() const;
    int currentInventoryDemandOrderId() const;
    void setQueryShipmentRows(const QList<OrderShipmentRecord> &records);
    void clearQueryDetails();
    int currentQueryOrderId() const;
    void loadShipmentOrders();
    void refreshShipmentDetails();
    void setShipmentComponentRows(const QList<StructuredOrderComponentSnapshot> &components);
    void updateSelectedShipmentComponent();
    int currentShipmentOrderId() const;
    int selectedShipmentComponentId() const;

    Ui::MainWindow *ui;
    DatabaseManager m_databaseManager;
    QWidget *m_productDataTab = nullptr;
    QWidget *m_inventoryTab = nullptr;
    QListWidget *m_productDataMenuListWidget = nullptr;
    QStackedWidget *m_productDataStackedWidget = nullptr;
    QTableWidget *m_categoryTableWidget = nullptr;
    QPushButton *m_addCategoryButton = nullptr;
    QPushButton *m_saveCategoryButton = nullptr;
    QPushButton *m_importCategoriesButton = nullptr;
    QComboBox *m_skuCategoryComboBox = nullptr;
    QTableWidget *m_skuTableWidget = nullptr;
    QPushButton *m_addSkuButton = nullptr;
    QPushButton *m_saveSkuButton = nullptr;
    QPushButton *m_importSkusButton = nullptr;
    QComboBox *m_configurationCategoryComboBox = nullptr;
    QTableWidget *m_configurationTableWidget = nullptr;
    QPushButton *m_addConfigurationButton = nullptr;
    QPushButton *m_saveConfigurationButton = nullptr;
    QPushButton *m_importConfigurationsButton = nullptr;
    QComboBox *m_bomConfigurationComboBox = nullptr;
    QTableWidget *m_bomTableWidget = nullptr;
    QPushButton *m_addBomButton = nullptr;
    QPushButton *m_saveBomButton = nullptr;
    QPushButton *m_importBomButton = nullptr;
    QComboBox *m_structuredCategoryComboBox = nullptr;
    QLineEdit *m_lampshadeNameLineEdit = nullptr;
    QLineEdit *m_orderRemarkLineEdit = nullptr;
    QComboBox *m_inventoryComponentComboBox = nullptr;
    QComboBox *m_inventoryCategoryComboBox = nullptr;
    QLineEdit *m_inventoryComponentSpecLineEdit = nullptr;
    QLineEdit *m_inventoryMaterialLineEdit = nullptr;
    QLineEdit *m_inventoryColorLineEdit = nullptr;
    QLineEdit *m_inventoryUnitLineEdit = nullptr;
    QDoubleSpinBox *m_inventoryUnitPriceSpinBox = nullptr;
    QLineEdit *m_inventoryCurrentQuantityLineEdit = nullptr;
    QSpinBox *m_inventoryInboundQuantitySpinBox = nullptr;
    QSpinBox *m_inventoryOutboundQuantitySpinBox = nullptr;
    QLineEdit *m_inventoryNoteLineEdit = nullptr;
    QPushButton *m_saveInventoryButton = nullptr;
    QPushButton *m_clearInventoryButton = nullptr;
    QPushButton *m_importInventoryButton = nullptr;
    QTableWidget *m_inventoryTableWidget = nullptr;
    QTableWidget *m_inventoryBlockedOrderTableWidget = nullptr;
    QTableWidget *m_demandSummaryTableWidget = nullptr;
    QTableWidget *m_inventoryReadyOrderTableWidget = nullptr;
    QComboBox *m_inventoryDemandScopeComboBox = nullptr;
    QTabWidget *m_inventoryModeTabWidget = nullptr;
    QTabWidget *m_queryModeTabWidget = nullptr;
    QLabel *m_structuredShipmentReadyLabel = nullptr;
    QTableWidget *m_structuredShipmentReadyTableWidget = nullptr;
    QTabWidget *m_shipmentModeTabWidget = nullptr;
    QDateEdit *m_queryStartDateEdit = nullptr;
    QDateEdit *m_queryEndDateEdit = nullptr;
    QPushButton *m_exportOrderSummaryButton = nullptr;
    QPushButton *m_exportInventoryDemandButton = nullptr;
    QPushButton *m_exportShipmentListButton = nullptr;
    QList<ProductSkuOption> m_structuredQuerySkus;
    QList<StructuredOrderSummary> m_structuredQueryOrders;
    QList<StructuredOrderSummary> m_filteredStructuredQueryOrders;
    QList<StructuredOrderSummary> m_structuredShipmentReadyOrders;
    QList<StructuredOrderSummary> m_inventoryBlockedOrders;
    QList<StructuredOrderSummary> m_shipmentOrders;
    QList<StructuredOrderComponentSnapshot> m_shipmentComponents;
    QList<InventoryDemandSummaryRow> m_inventoryDemandSummaryRows;
    QTimer *m_statusMessageClearTimer = nullptr;
    DataImporter m_dataImporter{&m_databaseManager};
    bool m_updatingComponentTable = false;
    bool m_isInitializing = false;
    bool m_isShuttingDown = false;
    bool m_isDatabaseUiUpdating = false;
    bool m_hasTransientStatusMessage = false;
};
#endif // MAINWINDOW_H
