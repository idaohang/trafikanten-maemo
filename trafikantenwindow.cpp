/*
 Conversion from longlat to easting/northing is (c) 2006 Jonathan Stott and GPLv3
 Rewritten using code from http://code.google.com/p/trafikanten/source/browse/src/uk/me/jstott/jcoord/LatLng.java
*/


#include "trafikantenwindow.h"
#include "ui_trafikantenwindow.h"

#include "searchdialog.h"
#include "travelsearchwindow.h"
#include "aboutdialog.h"
#include "departureswindow.h"
#include "common.h"
#include "recentwindow.h"
#ifdef Q_WS_MAEMO_5
#include <QMaemo5InformationBox>
#endif

TrafikantenWindow::TrafikantenWindow(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::TrafikantenWindow)
{
#ifdef Q_WS_MAEMO_5
    setAttribute(Qt::WA_Maemo5StackedWindow);
    setAttribute(Qt::WA_Maemo5AutoOrientation, true);
#endif
    ui->setupUi(this);


    search = new SearchDialog(this); // there is only one search window :)
    travelSearch = new TravelSearchWindow(this);

    // Set up GPS stuff
    positionSource = QGeoPositionInfoSource::createDefaultSource(this);
    connect(positionSource, SIGNAL(positionUpdated(QGeoPositionInfo)),
            this, SLOT(positionUpdated(QGeoPositionInfo)));
    connect(positionSource, SIGNAL(updateTimeout()),
            this, SLOT(updateTimeout()));
    connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(orientationChanged()));
    connect(&updateRequestTimer, SIGNAL(timeout()), this, SLOT(updateTimeout()));
    orientationChanged(); // call this just in case we're in portrait mode from before
}

TrafikantenWindow::~TrafikantenWindow()
{
    delete ui;
}

void TrafikantenWindow::orientationChanged() {
    // Change the layout of the search controls
    QRect screenGeometry = QApplication::desktop()->screenGeometry();
    if (screenGeometry.width() > screenGeometry.height()) {
        portraitMode = false;
        ui->layoutButtonsAdditional->removeWidget(ui->btnNearby);
        ui->layoutButtonsAdditional->removeWidget(ui->btnRouting);
        ui->layoutButtons->addWidget(ui->btnNearby);
        ui->layoutButtons->addWidget(ui->btnRouting);
    } else {
        portraitMode = true;
        ui->layoutButtons->removeWidget(ui->btnNearby);
        ui->layoutButtons->removeWidget(ui->btnRouting);
        ui->layoutButtonsAdditional->addWidget(ui->btnNearby);
        ui->layoutButtonsAdditional->addWidget(ui->btnRouting);
    }
}

void TrafikantenWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void TrafikantenWindow::on_btnSearch_clicked()
{
    positionSource->stopUpdates();
    updateRequestTimer.stop();
    search->setNormalSearch();
    if(portraitMode) {
#ifdef Q_WS_MAEMO_5
        search->setAttribute(Qt::WA_Maemo5PortraitOrientation, true);
#endif
    } else {
#ifdef Q_WS_MAEMO_5
        search->setAttribute(Qt::WA_Maemo5LandscapeOrientation, true);
#endif
    }
    int result = search->exec();

    if(result == QDialog::Accepted) {
        DeparturesWindow *win = new DeparturesWindow(search->place(), this);
        if(portraitMode) {
#ifdef Q_WS_MAEMO_5
            win->setAttribute(Qt::WA_Maemo5PortraitOrientation, true);
#endif
        } else {
#ifdef Q_WS_MAEMO_5
            win->setAttribute(Qt::WA_Maemo5LandscapeOrientation, true);
#endif
        }
#if defined(Q_WS_S60)
    win->showMaximized();
#else
    win->show();
#endif
    }
}

void TrafikantenWindow::on_btnNearby_clicked()
{
    qDebug() << "Requesting update...";
    bool usePositionSource = true;
    if (positionSearchPerformed && lastPositionSearch.elapsed() < 5 * 60 * 1000) { // position requested within the last 5 minutes
        QMessageBox msg(this);
        msg.setWindowTitle(tr("Reuse last result?"));
        msg.setText(tr("Your position was found recently.\nWould you like to reuse you previous position?"));
        msg.setStandardButtons(QMessageBox::Cancel);
        QPushButton *yesButton = msg.addButton(tr("Yes"), QMessageBox::YesRole);
        msg.addButton(tr("No"), QMessageBox::NoRole);
        int ret = msg.exec();
        if(msg.clickedButton() == yesButton) {
            qDebug() << "Reusing last result";
            positionUpdated(positionSource->lastKnownPosition());
            usePositionSource = false;
        } else if(ret == QMessageBox::Cancel) {
            usePositionSource = false;
        }
    }
    if (positionSource && usePositionSource) {
#ifdef Q_WS_MAEMO_5
        QMaemo5InformationBox::information(this, tr("Requesting your position using GPS/GSM"), QMaemo5InformationBox::DefaultTimeout);
#endif
        positionSource->setPreferredPositioningMethods(QGeoPositionInfoSource::SatellitePositioningMethods); // set preferred methods
#ifdef Q_WS_MAEMO_5
        setAttribute(Qt::WA_Maemo5ShowProgressIndicator, true);
#endif
        positionSource->startUpdates();
        updateRequestTimer.start(45000); // time out after 45 seconds
    }
}

void TrafikantenWindow::positionUpdated(const QGeoPositionInfo &info) {
    qDebug() << "Position updated:" << info;
    qreal accuracy = 0; // if no accuarcy info is available, assume perfect accuracy (works here, would be silly for map applications)
    if(info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)) {
        accuracy = info.attribute(QGeoPositionInfo::HorizontalAccuracy);
        qDebug() << "Accuracy" << accuracy;
    }
    if(accuracy > 1000) { // require an accuracy of at least x metres to avoid results returning nothing
        qDebug() << "Not good enough accuracy, waiting for new updates";
    } else {
        positionSource->stopUpdates();
        updateRequestTimer.stop();
        positionSearchPerformed = true;
        lastPositionSearch.start();
#ifdef Q_WS_MAEMO_5
        setAttribute(Qt::WA_Maemo5ShowProgressIndicator, false);
#endif
        // TODO: Convert from latlong to easting and northing. See http://code.google.com/p/trafikanten/source/browse/src/uk/me/jstott/jcoord/UTMRef.java
        double UTM_F0 = 0.9996;
        double maj = 6378137.000;
        double min = 6356752.3141;
        double a = maj;
        double eSquared = ((maj * maj) - (min * min)) / (maj * maj);
        double longitude = info.coordinate().longitude();
        double latitude = info.coordinate().latitude();

        double latitudeRad = latitude * (M_PI / 180.0);
        double longitudeRad = longitude * (M_PI / 180.0);
        int longitudeZone = (int) floor((longitude + 180.0) / 6.0) + 1;

        // Special zone for Norway
        if (latitude >= 56.0 && latitude < 64.0 && longitude >= 3.0
            && longitude < 12.0) {
            longitudeZone = 32;
        }

        // Special zones for Svalbard
        if (latitude >= 72.0 && latitude < 84.0) {
            if (longitude >= 0.0 && longitude < 9.0) {
                longitudeZone = 31;
            } else if (longitude >= 9.0 && longitude < 21.0) {
                longitudeZone = 33;
            } else if (longitude >= 21.0 && longitude < 33.0) {
                longitudeZone = 35;
            } else if (longitude >= 33.0 && longitude < 42.0) {
                longitudeZone = 37;
            }
        }

        double longitudeOrigin = (longitudeZone - 1) * 6 - 180 + 3;
        double longitudeOriginRad = longitudeOrigin * (M_PI / 180.0);

        //    char UTMZone = UTMRef.getUTMLatitudeZoneLetter(latitude);

        double ePrimeSquared = (eSquared) / (1 - eSquared);

        double n =
                a
                / sqrt(1 - eSquared * sin(latitudeRad)
                       * sin(latitudeRad));
        double t = tan(latitudeRad) * tan(latitudeRad);
        double c = ePrimeSquared * cos(latitudeRad) * cos(latitudeRad);
        double A = cos(latitudeRad) * (longitudeRad - longitudeOriginRad);

        double M =
                a
                * ((1 - eSquared / 4 - 3 * eSquared * eSquared / 64 - 5 * eSquared
                    * eSquared * eSquared / 256)
                   * latitudeRad
                   - (3 * eSquared / 8 + 3 * eSquared * eSquared / 32 + 45
                      * eSquared * eSquared * eSquared / 1024)
                   * sin(2 * latitudeRad)
                   + (15 * eSquared * eSquared / 256 + 45 * eSquared * eSquared
                      * eSquared / 1024) * sin(4 * latitudeRad) - (35
                                                                   * eSquared * eSquared * eSquared / 3072)
                   * sin(6 * latitudeRad));

        double UTMEasting =
                (UTM_F0
                 * n
                 * (A + (1 - t + c) * pow(A, 3.0) / 6 + (5 - 18 * t + t * t
                                                         + 72 * c - 58 * ePrimeSquared)
                    * pow(A, 5.0) / 120) + 500000.0);

        double UTMNorthing =
                (UTM_F0 * (M + n
                           * tan(latitudeRad)
                           * (A * A / 2 + (5 - t + (9 * c) + (4 * c * c)) * pow(A, 4.0)
                              / 24 + (61 - (58 * t) + (t * t) + (600 * c) - (330 * ePrimeSquared))
                              * pow(A, 6.0) / 720)));


        // Adjust for the southern hemisphere
        if (latitude < 0) {
            UTMNorthing += 10000000.0;
        }
        QString northing = QString::number((int)UTMNorthing);
        QString easting = QString::number((int)UTMEasting);
        qDebug() << "northing" << northing << "easting" << easting;
        search->searchPosition(easting, northing);
#ifdef Q_WS_MAEMO_5
        if(portraitMode) {
            search->setAttribute(Qt::WA_Maemo5PortraitOrientation, true);
        } else {
            search->setAttribute(Qt::WA_Maemo5LandscapeOrientation, true);
        }
#endif
        int result = search->exec();

        if(result == QDialog::Accepted) {
            DeparturesWindow *win = new DeparturesWindow(search->place(), this);
#ifdef Q_WS_MAEMO_5
            if(portraitMode) {
                win->setAttribute(Qt::WA_Maemo5PortraitOrientation, true);
            } else {
                win->setAttribute(Qt::WA_Maemo5LandscapeOrientation, true);
            }
#endif
#if defined(Q_WS_S60)
    win->showMaximized();
#else
    win->show();
#endif
        }
    }
}

void TrafikantenWindow::updateTimeout() {
    qDebug() << "Timed out";
    positionSource->stopUpdates();
    updateRequestTimer.stop();
#ifdef Q_WS_MAEMO_5
    setAttribute(Qt::WA_Maemo5ShowProgressIndicator, false);
#endif
    QMessageBox messageBox;
    messageBox.setWindowTitle(tr("Timed out"));
    messageBox.setText(tr("We are terribly sorry, but we're unable to locate your position at the current time. Please try again."));
    messageBox.exec();
}

void TrafikantenWindow::on_btnRouting_clicked()
{
    positionSource->stopUpdates();
    updateRequestTimer.stop();
#ifdef Q_WS_MAEMO_5
    if(portraitMode) {
        travelSearch->setAttribute(Qt::WA_Maemo5PortraitOrientation, true);
    } else {
        travelSearch->setAttribute(Qt::WA_Maemo5LandscapeOrientation, true);
    }
#endif
#if defined(Q_WS_S60)
    travelSearch->showMaximized();
#else
    travelSearch->show();
#endif
}

void TrafikantenWindow::on_actionAbout_triggered()
{
    AboutDialog dialog;
    dialog.exec();
}

void TrafikantenWindow::on_btnRecent_clicked()
{
    positionSource->stopUpdates();
    updateRequestTimer.stop();
    RecentWindow* win = new RecentWindow(RecentWindow::Recent, this);
#ifdef Q_WS_MAEMO_5
    if(portraitMode) {
        win->setAttribute(Qt::WA_Maemo5PortraitOrientation, true);
    } else {
        win->setAttribute(Qt::WA_Maemo5LandscapeOrientation, true);
    }
#endif
#if defined(Q_WS_S60)
    win->showMaximized();
#else
    win->show();
#endif
}

void TrafikantenWindow::on_btnFavorites_clicked()
{
    positionSource->stopUpdates();
    updateRequestTimer.stop();
    RecentWindow* win = new RecentWindow(RecentWindow::Favorites, this);
#ifdef Q_WS_MAEMO_5
    if(portraitMode) {
        win->setAttribute(Qt::WA_Maemo5PortraitOrientation, true);
    } else {
        win->setAttribute(Qt::WA_Maemo5LandscapeOrientation, true);
    }
#endif
#if defined(Q_WS_S60)
    win->showMaximized();
#else
    win->show();
#endif
}
