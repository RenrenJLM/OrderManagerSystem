#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "databasemanager.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void setupUiState();
    void loadProductModels();
    void loadTemplatesForCurrentProduct();
    void loadSelectedTemplateComponents();
    void setCustomConfigurationMode(bool enabled);
    void setComponentTableRows(const QList<OrderComponentData> &components, bool editableNamesAndQty);
    void addEmptyComponentRow();
    QList<OrderComponentData> collectComponentsFromTable() const;
    void updateComponentTotals();
    void updatePriceDisplays();
    void clearOrderForm();
    bool validateOrderInput(QString *errorMessage) const;
    void loadShipmentOrders();
    void refreshShipmentDetails();
    void setShipmentComponentRows(const QList<ShipmentComponentStatus> &components);
    void updateSelectedShipmentComponent();
    int currentShipmentOrderId() const;
    int selectedShipmentComponentId() const;

    Ui::MainWindow *ui;
    DatabaseManager m_databaseManager;
    QList<ShipmentOrderSummary> m_shipmentOrders;
    QList<ShipmentComponentStatus> m_shipmentComponents;
    bool m_updatingComponentTable = false;
    bool m_isInitializing = false;
    bool m_isShuttingDown = false;
};
#endif // MAINWINDOW_H
