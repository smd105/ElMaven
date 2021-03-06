#include "adductwidget.h"
#include "Compound.h"
#include "database.h"
#include "datastructures/adduct.h"
#include "globals.h"
#include "mainwindow.h"
#include "mavenparameters.h"
#include "mzfileio.h"
#include "mzUtils.h"
#include "mzSample.h"
#include "numeric_treewidgetitem.h"
#include "Scan.h"

AdductWidget::AdductWidget(MainWindow* parent) :
    QDialog(parent)
{
    _mw = parent;
    setupUi(this);
    setModal(false);
    setWindowTitle("Adducts");

    qRegisterMetaType<Adduct*>("Adduct*");

    connect(adductList,
            &QTreeWidget::itemChanged,
            this,
            [this] (QTreeWidgetItem* item, int column) {
                QCoreApplication::processEvents();
                if (item == nullptr || column != 0)
                    return;
                auto checked = item->checkState(column);
                auto items = adductList->selectedItems();
                for (auto item : items) {
                    if (item != nullptr)
                        item->setCheckState(column, checked);
                }
            });
}

void AdductWidget::loadAdducts()
{
    adductList->clear();
    for (auto adduct : DB.adductsDB) {
        auto * item = new NumericTreeWidgetItem(adductList, AdductType);
        item->setCheckState(0, Qt::Unchecked);
        item->setData(0, Qt::UserRole, QVariant::fromValue(adduct));

        item->setText(0, QString(adduct->getName().c_str()));
        item->setText(1, QString::number(adduct->getNmol()));
        item->setText(2, QString::number(adduct->getCharge()));
        item->setText(3, QString::number(adduct->getMass()));
    }
    adductList->resizeColumnToContents(0);
}

vector<Adduct*> AdductWidget::getSelectedAdducts()
{
    vector<Adduct*> selectedAdducts;
    QTreeWidgetItemIterator it(adductList);
    while (*it) {
        if ((*it)->checkState(0) == Qt::Checked) {
            auto variant = (*it)->data(0, Qt::UserRole);
            selectedAdducts.push_back(variant.value<Adduct*>());
        }
        ++it;
    }
    return selectedAdducts;
}

void AdductWidget::selectAdductsForCurrentPolarity()
{
    QTreeWidgetItemIterator it(adductList);
    while (*it) {
        auto variant = (*it)->data(0, Qt::UserRole);
        auto adduct = variant.value<Adduct*>();
        if (SIGN(adduct->getCharge()) == SIGN(_mw->getIonizationMode())
            && adduct->isParent()
            && !_mw->getVisibleSamples().empty()) {
            // the primary adduct for current polarity should not be allowed
            // to be unselected
            (*it)->setCheckState(0, Qt::Checked);
            (*it)->setDisabled(true);
        } else if (SIGN(adduct->getCharge()) != SIGN(_mw->getIonizationMode())
                   && adduct->isParent()
                   && !_mw->getVisibleSamples().empty()) {
            (*it)->setCheckState(0, Qt::Unchecked);
            (*it)->setDisabled(false);
        }

        ++it;
    }
    _mw->mavenParameters->setChosenAdductList(getSelectedAdducts());
}

void AdductWidget::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    selectAdductsForCurrentPolarity();
}

void AdductWidget::hideEvent(QHideEvent* event)
{
    _mw->mavenParameters->setChosenAdductList(getSelectedAdducts());
    QDialog::hideEvent(event);
}
