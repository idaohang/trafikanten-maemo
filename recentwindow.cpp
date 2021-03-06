#include "recentwindow.h"
#include "ui_recentwindow.h"

#include "departureswindow.h"
#include "travelsearchwindow.h"
#include "common.h"

RecentWindow::RecentWindow(Mode mode, QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::RecentWindow)
{
#ifdef Q_WS_MAEMO_5
    setAttribute(Qt::WA_Maemo5AutoOrientation, true);
    setAttribute(Qt::WA_Maemo5StackedWindow);
#endif
#ifdef Q_OS_SYMBIAN
    // We need to add a back button
    QAction *backSoftKeyAction = new QAction(tr("Back"), this);
    backSoftKeyAction->setSoftKeyRole(QAction::NegativeSoftKey);
    connect(backSoftKeyAction,SIGNAL(triggered()),SLOT(close())); // when the back button is pressed, just close this window
    addAction(backSoftKeyAction);
#endif
    ui->setupUi(this);

    this->mode = mode;
    QList<Search*> searches;
    if(mode == Recent) {
        setWindowTitle(tr("Recent searches"));
        searches = Search::recent();
    } else if(mode == Favorites){
        setWindowTitle(tr("Favorite searches"));
        searches = Search::favorites();
    } else if(mode == FavoritesRealtime) {
        setWindowTitle(tr("Favorite places"));
        searches = Search::favoritesRealtime();
        ui->menuMenu->removeAction(ui->actionDelete_all);
    }

    model = new SearchListModel(this, searches);
    ui->tblResults->setModel(model);
    delegate = new SearchListDelegate(this);
    ui->tblResults->setItemDelegate(delegate);
    ui->tblResults->horizontalHeader()->setResizeMode(QHeaderView::Stretch);
    ui->tblResults->resizeRowsToContents();
    connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(orientationChanged()));
    orientationChanged(); // call this just in case we're in portrait mode from before
}

void RecentWindow::orientationChanged() {
    // Change the layout of the search controls
    QRect screenGeometry = QApplication::desktop()->screenGeometry();
    if (screenGeometry.width() > screenGeometry.height()) {
        _portraitMode = false;
    } else {
        _portraitMode = true;
    }

}

RecentWindow::~RecentWindow()
{
    delete ui;
}

void RecentWindow::changeEvent(QEvent *e)
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
int SearchListModel::rowCount(const QModelIndex &) const {
    return searches_.size();;
}

QVariant SearchListModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid())
        return QVariant();
    if(index.row() >= rowCount() || index.row() < 0) {
        return QVariant();
    }

    if(role == Qt::DisplayRole) {
        return qVariantFromValue(searches_.at(index.row()));
    }

    return QVariant();
}

void SearchListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const {
    QStyledItemDelegate::paint(painter, option, index);

    Search *e = qVariantValue<Search *>(index.data());

    QFont font = option.font;
    QRect rect = option.rect;
    rect.adjust(10, 6, -20, -4);

    painter->save();
    font.setPointSizeF(font.pointSizeF() * 0.8);

    int maxLabelLength = 40;
    if (recentWindow->portraitMode()) {
        maxLabelLength = 24;
    }
    QString placeFromText;
    QString placeToText;
    if(e->placeFrom.placeName.length() > maxLabelLength) {
        placeFromText = e->placeFrom.placeName.left(maxLabelLength - 2) + "...";
    } else {
        placeFromText = e->placeFrom.placeName;
    }
    if(e->placeTo.placeName.length() > maxLabelLength) {
        placeToText = e->placeTo.placeName.left(maxLabelLength - 2) + "...";
    } else {
        placeToText = e->placeTo.placeName;
    }

    if(e->type == Search::Realtime) {
        painter->drawText(rect, Qt::AlignTop | Qt::AlignLeft, placeFromText);
        painter->drawText(rect, Qt::AlignTop | Qt::AlignRight, tr("Realtime"));
    } else {
        painter->drawText(rect, Qt::AlignTop | Qt::AlignLeft, placeFromText);
        painter->drawText(rect, Qt::AlignBottom | Qt::AlignLeft, placeToText);
        painter->drawText(rect, Qt::AlignTop | Qt::AlignRight, tr("Travel"));
    }

    painter->restore();
}

void RecentWindow::on_tblResults_clicked(QModelIndex index)
{
    Search *search = qVariantValue<Search *>(index.data());
    if(mode == FavoritesRealtime) {
        emit placeSelected(search->placeFrom);
        close();
    } else if(mode == Recent || mode == Favorites)
    if(search->type == Search::Realtime) {
        DeparturesWindow *win = new DeparturesWindow(search->placeFrom, this);
#ifdef Q_WS_MAEMO_5
        if(_portraitMode) {
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
    } else if(search->type == Search::Travel) {
        TravelSearchWindow *win = new TravelSearchWindow(this);
        win->setPlaceFrom(search->placeFrom);
        win->setPlaceTo(search->placeTo);
#ifdef Q_WS_MAEMO_5
        if(_portraitMode) {
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

void RecentWindow::on_tblResults_customContextMenuRequested(QPoint pos)
{
    QMenu *menu = new QMenu;
    menu->addAction(tr("Delete"), this, SLOT(removeFavorite()));
    menu->exec(mapToGlobal(pos));
}

void RecentWindow::removeFavorite() {
    Search *search = qVariantValue<Search *>(ui->tblResults->selectionModel()->selectedIndexes().first().data());
    QList<Search*> emptySearches = model->searches();
    emptySearches.removeAll(search);
    model->setSearches(emptySearches);
    if(mode == Favorites || mode == FavoritesRealtime) {
        Search::saveFavorites(emptySearches);
    } else {
        Search::saveRecent(emptySearches);
    }
}

void RecentWindow::on_actionDelete_all_triggered()
{
    QMessageBox msg;
    msg.setWindowTitle(tr("Are you sure?"));
    msg.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msg.setDefaultButton(QMessageBox::Ok);
    if(mode == Favorites) {
        msg.setText(tr("This will remove all your favorite searches and is an irriversible action. Are you sure that you want to continue?"));
    } else {
        msg.setText(tr("This will remove all your recent searches and is an irriversible action. Are you sure that you want to continue?"));
    }
    int ret = msg.exec();
    qDebug() << ret;
    if(ret == QMessageBox::Ok) {
        qDebug() << "Accepted";
        model->setSearches(QList<Search*>());
        if(mode == Favorites) {
            Search::saveFavorites(QList<Search*>());
        } else {
            Search::saveRecent(QList<Search*>());
        }
    }
}
