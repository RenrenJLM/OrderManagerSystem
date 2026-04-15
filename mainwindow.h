#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "databasemanager.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QEvent;
class QTableWidget;
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

private:
    void setupUiState();
    void setupCustomTitleBar();
    void setupTopMenus();
    void applyUiTheme();
    void configureTableWidget(QTableWidget *tableWidget) const;
    void showStatusMessage(const QString &message, int timeoutMs = 0);
    void toggleMaximizeRestore();
    void updateWindowControlButtons();
    void loadProductModels();
    void loadCustomComponentOptions();
    void loadTemplatesForCurrentProduct();
    void loadSelectedTemplateComponents();
    void setCustomConfigurationMode(bool enabled);
    void setComponentTableRows(const QList<OrderComponentData> &components, bool editableNamesAndQty);
    void addEmptyComponentRow();
    void updateCustomComponentRow(int row);
    int customComponentRowForWidget(QWidget *widget) const;
    QList<OrderComponentData> collectComponentsFromTable() const;
    void updateComponentTotals();
    void updatePriceDisplays();
    void clearOrderForm();
    bool validateOrderInput(QString *errorMessage) const;
    void loadQueryProductModels();
    void performOrderQuery();
    void setQueryOrderRows(const QList<ShipmentOrderSummary> &orders);
    void refreshQueryOrderDetails();
    void setQueryComponentRows(const QList<ShipmentComponentStatus> &components);
    void setQueryShipmentRows(const QList<OrderShipmentRecord> &records);
    void clearQueryDetails();
    int currentQueryOrderId() const;
    void loadShipmentOrders();
    void refreshShipmentDetails();
    void setShipmentComponentRows(const QList<ShipmentComponentStatus> &components);
    void updateSelectedShipmentComponent();
    int currentShipmentOrderId() const;
    int selectedShipmentComponentId() const;

    Ui::MainWindow *ui;
    DatabaseManager m_databaseManager;
    QList<ProductComponentOption> m_customComponentOptions;
    QList<ShipmentOrderSummary> m_queryOrders;
    QList<ShipmentOrderSummary> m_shipmentOrders;
    QList<ShipmentComponentStatus> m_shipmentComponents;
    QTimer *m_statusMessageClearTimer = nullptr;
    bool m_updatingComponentTable = false;
    bool m_isInitializing = false;
    bool m_isShuttingDown = false;
};
#endif // MAINWINDOW_H
