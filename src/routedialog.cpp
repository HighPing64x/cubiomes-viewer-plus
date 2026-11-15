#include "routedialog.h"
#include "ui_routedialog.h"

#include "mainwindow.h"
#include "mapview.h"
#include "util.h"
#include "world.h"

#include <QIntValidator>
#include <QMessageBox>
#include <QPushButton>

RouteDialog::RouteDialog(MainWindow *parent_, RouteConfig *config_)
    : QDialog(parent_)
    , ui(new Ui::RouteDialog)
    , parent(parent_)
    , config(config_)
{
    ui->setupUi(this);

    ui->lineStartX->setValidator(new QIntValidator(-30000000, 30000000, this));
    ui->lineStartZ->setValidator(new QIntValidator(-30000000, 30000000, this));
    ui->lineEndX->setValidator(new QIntValidator(-30000000, 30000000, this));
    ui->lineEndZ->setValidator(new QIntValidator(-30000000, 30000000, this));

    ui->comboOptimize->addItem(tr("Fastest route"));
    ui->comboOptimize->addItem(tr("Shortest distance"));

    // A dedicated button to trigger the computation, in addition to the
    // standard dialog buttons.
    QPushButton *apply = ui->buttonBox->button(QDialogButtonBox::Apply);
    if (apply)
        apply->setText(tr("Calculate"));

    ui->textLog->setFont(parent->config.fontMono);

    // Default the endpoints to the current map view: the start is the centre
    // of the map and the destination is left at the same spot, so that the
    // user only has to enter the target coordinates.
    if (config->start.x == 0 && config->start.z == 0
        && config->end.x == 0 && config->end.z == 0)
    {
        config->start.x = (int)parent->getMapView()->getX();
        config->start.z = (int)parent->getMapView()->getZ();
        config->end = config->start;
    }

    writeConfig();
}

RouteDialog::~RouteDialog()
{
    delete ui;
}

void RouteDialog::writeConfig()
{
    ui->lineStartX->setText(QString::number(config->start.x));
    ui->lineStartZ->setText(QString::number(config->start.z));
    ui->lineEndX->setText(QString::number(config->end.x));
    ui->lineEndZ->setText(QString::number(config->end.z));

    ui->checkLand->setChecked(config->modes & ROUTE_LAND);
    ui->checkSea->setChecked(config->modes & ROUTE_SEA);
    ui->comboOptimize->setCurrentIndex(config->optimize == ROUTE_OPT_SHORTEST ? 1 : 0);
    ui->checkAvoidHazards->setChecked(config->avoidHazards);
}

void RouteDialog::readConfig()
{
    config->start.x = ui->lineStartX->text().toInt();
    config->start.z = ui->lineStartZ->text().toInt();
    config->end.x = ui->lineEndX->text().toInt();
    config->end.z = ui->lineEndZ->text().toInt();

    config->modes = 0;
    if (ui->checkLand->isChecked())
        config->modes |= ROUTE_LAND;
    if (ui->checkSea->isChecked())
        config->modes |= ROUTE_SEA;

    config->optimize = ui->comboOptimize->currentIndex() == 1
        ? ROUTE_OPT_SHORTEST : ROUTE_OPT_FASTEST;
    config->avoidHazards = ui->checkAvoidHazards->isChecked();
}

void RouteDialog::refresh()
{
    writeConfig();
    on_computeClicked();
}

void RouteDialog::on_buttonBox_clicked(QAbstractButton *button)
{
    QDialogButtonBox::StandardButton b = ui->buttonBox->standardButton(button);

    if (b == QDialogButtonBox::Apply)
    {
        on_computeClicked();
    }
    else if (b == QDialogButtonBox::Reset)
    {
        config->reset();
        writeConfig();
        on_computeClicked();
    }
}

void RouteDialog::on_computeClicked()
{
    readConfig();
    config->save();

    ui->textLog->clear();

    if (!config->modes)
    {
        ui->textLog->appendPlainText(
            tr("Select at least one travel mode (land or sea)."));
        return;
    }

    WorldInfo wi;
    if (!parent->getSeed(&wi, false))
    {
        ui->textLog->appendPlainText(tr("The current seed or version is invalid."));
        return;
    }

    // The route planner queries biomes, so a generator for the current world
    // is set up here rather than reusing the one of the map (which lives in a
    // different thread).
    Generator g;
    setupGenerator(&g, wi.mc, wi.large);
    applySeed(&g, DIM_OVERWORLD, wi.seed);

    computeRoute(&result, &g, *config, nullptr);
    updateResult(result);
    applyToMap(result);
}

void RouteDialog::updateResult(const RouteResult& res)
{
    if (!res.valid)
    {
        ui->labelDistance->setText("-");
        ui->labelTime->setText("-");
        ui->labelMode->setText("-");
        if (!res.error.isEmpty())
            ui->textLog->appendPlainText(res.error);
        return;
    }

    ui->labelDistance->setText(QString::asprintf("%.1f blocks", res.distance));

    int secs = (int)(res.traveltime + 0.5);
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    QString timestr;
    if (h > 0)
        timestr = QString::asprintf("%dh %02dm", h, m);
    else if (m > 0)
        timestr = QString::asprintf("%dm %02ds", m, s);
    else
        timestr = QString::asprintf("%ds", s);
    ui->labelTime->setText(timestr);

    ui->labelMode->setText(tr("%1 land / %2 water").arg(res.nland).arg(res.nsea));

    // Summarise the route as a list of legs: consecutive runs of the same
    // terrain type are collapsed into a single entry with their extent.
    ui->textLog->appendPlainText(tr("Route from (%1, %2) to (%3, %4)")
        .arg(res.start.x).arg(res.start.z)
        .arg(res.end.x).arg(res.end.z));
    ui->textLog->appendPlainText(QString());

    size_t i = 0;
    const size_t n = res.path.size();
    while (i < n)
    {
        bool water = res.path[i].water;

        // Extend the leg while the terrain type stays the same.
        size_t j = i;
        double leglen = 0;
        while (j + 1 < n && res.path[j+1].water == water)
        {
            double dx = res.path[j+1].p.x - res.path[j].p.x;
            double dz = res.path[j+1].p.z - res.path[j].p.z;
            leglen += sqrt(dx*dx + dz*dz);
            j++;
        }

        const Pos& a = res.path[i].p;
        const Pos& b = res.path[j].p;
        // The first and last waypoints already include their segment length.
        if (j > i)
        {
            double dx = res.path[i+1].p.x - a.x;
            double dz = res.path[i+1].p.z - a.z;
            leglen += sqrt(dx*dx + dz*dz);
        }

        QString terr = water ? tr("by water") : tr("on land");
        ui->textLog->appendPlainText(
            tr("%1  %2 blocks  (%3, %4) -> (%5, %6)")
            .arg(terr, -8)
            .arg(QString::asprintf("%.0f", leglen), 8)
            .arg(a.x).arg(a.z).arg(b.x).arg(b.z));

        i = j + 1;
    }
}

void RouteDialog::applyToMap(const RouteResult& res)
{
    std::vector<Shape> shapes;
    if (res.valid && res.path.size() >= 2)
    {
        // The map draws line shapes, so consecutive waypoints are emitted as
        // individual segments.
        for (size_t i = 1; i < res.path.size(); i++)
        {
            Shape s;
            s.type = Shape::LINE;
            s.dim = DIM_OVERWORLD;
            s.p1 = res.path[i-1].p;
            s.p2 = res.path[i].p;
            s.r = 0;
            shapes.push_back(s);
        }
    }
    parent->getMapView()->setShapes(shapes);
}
