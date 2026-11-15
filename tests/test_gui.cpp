// GUI smoke test: launches the main window, switches to a Bedrock version,
// opens the route dialog and captures screenshots for visual verification.
#include "mainwindow.h"
#include "mapview.h"
#include "routedialog.h"
#include "route.h"
#include "config.h"
#include "world.h"

#include "cubiomes/finders.h"
#include "cubiomes/util.h"

#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QPixmap>
#include <QScreen>
#include <QComboBox>
#include <QCheckBox>
#include <QDebug>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextStream>

extern ExtGenConfig g_extgen;

int getStructureConfig_override(int stype, int mc, StructureConfig *sconf)
{
    int ok = getStructureConfig(stype, mc, sconf);
    if (ok && g_extgen.saltOverride)
    {
        uint64_t salt = g_extgen.salts[stype];
        if (salt <= MASK48)
            sconf->salt = salt;
    }
    return ok;
}

static int g_stage = 0;
static MainWindow *g_mw = nullptr;
static RouteDialog *g_dlg = nullptr;

static void snap(QString name)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    QPixmap pm = screen->grabWindow(0);
    QDir().mkpath("shots");
    pm.save(QString("shots/") + name + ".png");
    qInfo() << "saved" << name << pm.size();
}

static void stage()
{
    switch (g_stage++)
    {
    case 0:
    {
        WorldInfo wi;
        wi.mc = MC_1_21_3;
        wi.bedrock = BE_UNDEF;
        wi.seed = 123456789ULL;
        wi.y = 63;
        wi.large = false;
        g_mw->setSeed(wi, DIM_OVERWORLD);
        // Show one clearly identifiable structure type.
        g_mw->getMapView()->setShow(D_VILLAGE, true);
        g_mw->getMapView()->setShow(D_MONUMENT, false);
        g_mw->mapGoto(0, 0, 16);
        break;
    }

    case 1:
        snap("01_java_villages");
        // Switch to Bedrock 1.21.
        if (QComboBox *cb = g_mw->findChild<QComboBox*>("comboBoxMC"))
            cb->setCurrentIndex(cb->findText("Bedrock 1.21"));
        break;

    case 2:
        qInfo() << "== bedrock village layer";
        g_mw->mapGoto(0, 0, 16);
        break;

    case 3:
        snap("02_bedrock_villages");
        qInfo() << "== open route dialog";
        g_dlg = new RouteDialog(g_mw, &g_mw->routeconfig);
        g_dlg->show();
        break;

    case 4:
        snap("03_route_dialog");
        qInfo() << "== compute mixed route";
        g_mw->routeconfig.start = Pos{0, 0};
        g_mw->routeconfig.end = Pos{4000, 2500};
        g_mw->routeconfig.modes = ROUTE_LAND | ROUTE_SEA;
        g_mw->routeconfig.optimize = ROUTE_OPT_FASTEST;
        g_dlg->refresh();
        break;

    case 5:
        snap("04_route_result");
        // Also dump the dialog text so the output can be checked without a
        // screenshot.
        if (g_dlg)
        {
            if (QPlainTextEdit *log = g_dlg->findChild<QPlainTextEdit*>("textLog"))
            {
                QFile f("shots/route_dialog_log.txt");
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                {
                    QTextStream ts(&f);
                    ts << log->toPlainText() << "\n";
                }
            }
            QFile f("shots/route_dialog_labels.txt");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream ts(&f);
                for (const char *name : {"labelDistance", "labelTime", "labelMode"})
                    if (QLabel *l = g_dlg->findChild<QLabel*>(name))
                        ts << name << ": " << l->text() << "\n";
            }
        }
        g_mw->mapGoto(2000, 1250, 6);
        break;

    case 6:
        snap("05_route_on_map");
        qInfo() << "== land-only";
        g_mw->routeconfig.modes = ROUTE_LAND;
        g_dlg->refresh();
        break;

    case 7:
        snap("06_land_only");
        qInfo() << "== shortest distance option";
        g_mw->routeconfig.modes = ROUTE_LAND | ROUTE_SEA;
        g_mw->routeconfig.optimize = ROUTE_OPT_SHORTEST;
        g_dlg->refresh();
        break;

    case 8:
        snap("07_shortest");
        qInfo() << "== done";
        qApp->quit();
        break;

    default:
        qApp->quit();
        break;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName(APP_STRING);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(APP_STRING);
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QString sessions = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(sessions);

    g_mw = new MainWindow(sessions + "/session.save", "");
    g_mw->resize(1280, 820);
    g_mw->show();

    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &stage);
    t.start(6000);

    return app.exec();
}
