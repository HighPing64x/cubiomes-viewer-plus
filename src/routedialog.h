#ifndef ROUTEDIALOG_H
#define ROUTEDIALOG_H

#include <QDialog>
#include <QAbstractButton>

#include "route.h"

namespace Ui {
class RouteDialog;
}

class MainWindow;

class RouteDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RouteDialog(MainWindow *parent, RouteConfig *config);
    ~RouteDialog();

    // Recomputes the route and updates the map overlay.
    void refresh();

private slots:
    void on_buttonBox_clicked(QAbstractButton *button);
    void on_computeClicked();

private:
    void readConfig();
    void writeConfig();
    void updateResult(const RouteResult& res);
    void applyToMap(const RouteResult& res);

private:
    Ui::RouteDialog *ui;
    MainWindow *parent;
    RouteConfig *config;
    RouteResult result;
};

#endif // ROUTEDIALOG_H
