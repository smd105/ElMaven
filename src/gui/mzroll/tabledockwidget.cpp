#include <algorithm>

#include <QHistogramSlider.h>
#include <qtconcurrentrun.h>
#include <QtCharts>

#include "alignmentdialog.h"
#include "common/analytics.h"
#include "clusterdialog.h"
#include "Compound.h"
#include "controller.h"
#include "classifierNeuralNet.h"
#include "csvreports.h"
#include "EIC.h"
#include "eicwidget.h"
#include "globals.h"
#include "grouprtwidget.h"
#include "groupsettingslog.h"
#include "heatmap.h"
#include "isotopeswidget.h"
#include "jsonReports.h";
#include "ligandwidget.h"
#include "mainwindow.h"
#include "masscalcgui.h"
#include "mavenparameters.h"
#include "multiselectcombobox.h"
#include "mzAligner.h"
#include "mzSample.h"
#include "mzUtils.h"
#include "notificator.h"
#include "numeric_treewidgetitem.h"
#include "peakeditor.h"
#include "PeakGroup.h"
#include "peaktabledeletiondialog.h"
#include "saveJson.h"
#include "Scan.h"
#include "scatterplot.h"
#include "spectrallibexport.h"
#include "spectrawidget.h"
#include "tabledockwidget.h";
#include "PeakDetector.h"
#include "background_peaks_update.h"

using namespace QtCharts;

QMap<int, QString> TableDockWidget::_idTitleMap;

const QMap<PeakGroup::ClassifiedLabel, QString>
TableDockWidget::labelsForLegend()
{
  static QMap<PeakGroup::ClassifiedLabel, QString> labels = {
    {PeakGroup::ClassifiedLabel::CorrelationAndPattern,
     "Signal with correlation and cohort-variance"},
    {PeakGroup::ClassifiedLabel::Correlation,
     "Signal with only correlation"},
    {PeakGroup::ClassifiedLabel::Pattern,
     "Signal with only cohort-variance"},
    {PeakGroup::ClassifiedLabel::Signal,
     "Signal"},
    {PeakGroup::ClassifiedLabel::Noise,
     "Noise"}
  };
  return labels;
}

const QMap<PeakGroup::ClassifiedLabel, QIcon>
TableDockWidget::iconsForLegend()
{
  static QMap<PeakGroup::ClassifiedLabel, QIcon> icons {
    {PeakGroup::ClassifiedLabel::CorrelationAndPattern,
     QIcon(":/images/moi_pattern_correlated.png")},
    {PeakGroup::ClassifiedLabel::Correlation,
     QIcon(":/images/moi_correlated.png")},
    {PeakGroup::ClassifiedLabel::Pattern,
     QIcon(":/images/moi_pattern.png")},
    {PeakGroup::ClassifiedLabel::Signal,
     QIcon(":/images/good.png")},
    {PeakGroup::ClassifiedLabel::Noise,
     QIcon(":/images/bad.png")},
  };
  return icons;
}

TableDockWidget::TableDockWidget(MainWindow *mw) {
  setAllowedAreas(Qt::AllDockWidgetAreas);
  setFloating(false);
  _mainwindow = mw;
  _labeledGroups = 0;
  _targetedGroups = 0;
  _legend = nullptr;
  pal = palette();
  setAutoFillBackground(true);
  pal.setColor(QPalette::Background, QColor(170, 170, 170, 100));
  setPalette(pal);
  setDefaultStyle();

  viewType = groupView;

  treeWidget = new QTreeWidget(this);
  treeWidget->setSortingEnabled(false);
  treeWidget->setDragDropMode(QAbstractItemView::DragOnly);
  treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
  treeWidget->setAcceptDrops(false);
  treeWidget->setObjectName("PeakGroupTable");
  treeWidget->setFocusPolicy(Qt::NoFocus);
  treeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  this->setFocusPolicy(Qt::ClickFocus);
  tableSelectionFlagUp = false;
  tableSelectionFlagDown = false;
  this->setAcceptDrops(true);

  setWidget(treeWidget);
  setupPeakTable();

  connect(treeWidget,
          &QTreeWidget::itemClicked,
          this,
          &TableDockWidget::showSelectedGroup);
  connect(treeWidget,
          &QTreeWidget::itemSelectionChanged,
          this,
          &TableDockWidget::showSelectedGroup);
  connect(treeWidget,
          SIGNAL(itemExpanded(QTreeWidgetItem *)), this,
          SLOT(sortChildrenAscending(QTreeWidgetItem *)));

  clusterDialog = new ClusterDialog(this);
  connect(clusterDialog->clusterButton,
          SIGNAL(clicked(bool)),
          SLOT(clusterGroups()));
  connect(clusterDialog->clearButton,
          SIGNAL(clicked(bool)),
          SLOT(clearClusters()));

  connect(this,
          SIGNAL(updateProgressBar(QString, int, int, bool)),
          _mainwindow,
          SLOT(setProgressBar(QString, int, int, bool)));
  connect(this, SIGNAL(UploadPeakBatch()), this, SLOT(UploadPeakBatchToCloud()));
  connect(this, SIGNAL(renderedPdf()), this, SLOT(pdfReadyNotification()));

  setupFiltersDialog();

  setAcceptDrops(true);
}

TableDockWidget::~TableDockWidget() {
  if (clusterDialog != NULL)
    delete clusterDialog;
}

void TableDockWidget::sortChildrenAscending(QTreeWidgetItem *item) {
  item->sortChildren(1, Qt::AscendingOrder);
}

void TableDockWidget::showClusterDialog() { clusterDialog->show(); }

void TableDockWidget::sortBy(int col) {
  treeWidget->sortByColumn(col, Qt::AscendingOrder);
}

void TableDockWidget::updateTableAfterAlignment()
{
    BackgroundPeakUpdate::updateGroups(_topLevelGroups,
                                       _mainwindow->getVisibleSamples(),
                                       _mainwindow->mavenParameters);
    showAllGroups();
}

void TableDockWidget::setIntensityColName() {
  QTreeWidgetItem *header = treeWidget->headerItem();
  QString temp;
  PeakGroup::QType qtype = _mainwindow->getUserQuantType();
  switch (qtype) {
  case PeakGroup::AreaTop:
    temp = "Max AreaTop";
    break;
  case PeakGroup::Area:
    temp = "Max Area";
    break;
  case PeakGroup::Height:
    temp = "Max Height";
    break;
  case PeakGroup::AreaNotCorrected:
    temp = "Max AreaNotCorrected";
    break;
  case PeakGroup::AreaTopNotCorrected:
    temp = "Max AreaTopNotCorrected";
    break;
  default:
    temp = _mainwindow->currentIntensityName;
    break;
  }
  _mainwindow->currentIntensityName = temp;
  header->setText(10, temp);
}

void TableDockWidget::setupPeakTable() {

  QStringList colNames;

  // Add common coulmns to the Table
  colNames << "Label"; // TODO: add this column conditionally
  colNames << "#";
  colNames << "ID";
  colNames << "Observed m/z";
  colNames << "Expected m/z";
  colNames << "rt";

  if (viewType == groupView) {

    // Add group view columns to the peak table
    colNames << "rt delta";
    colNames << "#peaks";
    colNames << "#good";
    colNames << "Max Width";
    colNames << "Max AreaTop";
    colNames << "Max S/N";
    colNames << "Max Quality";
    colNames << "MS2 Score";
    colNames << "#MS2 Events";
    colNames << "Probability"; // TODO: add this column conditionally
    colNames << "Rank";
  } else if (viewType == peakView) {
    vector<mzSample *> vsamples = _mainwindow->getVisibleSamples();
    sort(vsamples.begin(), vsamples.end(), mzSample::compSampleOrder);
    for (unsigned int i = 0; i < vsamples.size(); i++) {
      // Add peak view columns to the table
      colNames << QString(vsamples[i]->sampleName.c_str());
    }
  }

  treeWidget->setColumnCount(colNames.size());
  treeWidget->setHeaderLabels(colNames);
  treeWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  treeWidget->header()->adjustSize();
  treeWidget->setSortingEnabled(true);
}

void TableDockWidget::updateTable() {
  QTreeWidgetItemIterator it(treeWidget);
  while (*it) {
    updateItem(*it);
    ++it;
  }
  updateStatus();
}

void TableDockWidget::_paintClassificationDisagreement(QTreeWidgetItem *item)
{
  QVariant v = item->data(1, Qt::UserRole);
  PeakGroup *group = v.value<PeakGroup *>();
  int numGood = 0;
  int numBad = 0;
  int total = group->peakCount();
  for (int i = 0; i < group->peakCount(); i++) {
    group->peaks[i].quality > _mainwindow->mavenParameters->minQuality
          ? numGood++
          : numBad++;
  }

  float incorrectFraction = 0.0f;
  if (numGood > 0 && group->userLabel() == 'b') {
    incorrectFraction = static_cast<float>(numGood) / total;
  } else if (numBad > 0 && group->userLabel() == 'g') {
    incorrectFraction = static_cast<float>(numBad) / total;
  }
  QLinearGradient gradient(0, 6, 42, 6);
  gradient.setColorAt(0, QColor::fromRgbF(1, 1, 1, 0));
  gradient.setColorAt(1, QColor::fromRgbF(0.8, 0.2, 0.2, incorrectFraction));
  QBrush brush(gradient);
  item->setBackground(0, brush);
}

void TableDockWidget::updateItem(QTreeWidgetItem *item, bool updateChildren) {
  QVariant v = item->data(0, Qt::UserRole);
  shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
  if (group == nullptr)
    return;

  heatmapBackground(item);

  //Find maximum number of peaks
  if (maxPeaks < group->peakCount()) maxPeaks = group->peakCount();

  //score group quality
  groupClassifier* groupClsf = _mainwindow->getGroupClassifier();
  if (group->peakCount() > 0 && groupClsf != NULL) {
      groupClsf->classify(group.get());
  }

  //get probability good/bad from svm
  svmPredictor* groupPred = _mainwindow->getSVMPredictor();
  if (group->peakCount() > 0 && groupPred != NULL) {
      groupPred->predict(group.get());
  }

  // Updating the peakid
  item->setText(0, QString::number(group->groupId));
  item->setText(1, QString(group->getName().c_str()));
  item->setText(2, QString::number(group->meanMz, 'f', 4));

  int charge = group->parameters()->getCharge(group->getCompound());
  if (group->getExpectedMz(charge) != -1) {
    float mz = group->getExpectedMz(charge);
    item->setText(3, QString::number(mz, 'f', 4));
  } else {
    item->setText(3, "NA");
  }

  item->setText(4, QString::number(group->meanRt, 'f', 2));

  if (viewType == groupView) {
    auto expectedRtDiff = group->expectedRtDiff();
    if (expectedRtDiff == -1.0f) {
      item->setText(5, "NA");
    } else {
      item->setText(5, QString::number(expectedRtDiff, 'f', 2));
    }
    item->setText(6, QString::number(group->sampleCount
                                     + group->blankSampleCount));
    item->setText(7, QString::number(group->goodPeakCount));
    item->setText(8, QString::number(group->maxNoNoiseObs));
    item->setText(9, QString::number(extractMaxIntensity(group.get()), 'g', 3));
    item->setText(10, QString::number(group->maxSignalBaselineRatio, 'f', 0));
    item->setText(11, QString::number(group->maxQuality, 'f', 2));
    item->setText(12, QString::number(group->fragMatchScore.mergedScore, 'f', 2));
    item->setText(13, QString::number(group->ms2EventCount));
    item->setText(14, QString::number(group->groupRank, 'e', 6));

    if (fabs(group->changeFoldRatio) >= 0) {
      item->setText(15, QString::number(group->changeFoldRatio, 'f', 3));
      item->setText(16, QString::number(group->changePValue, 'f', 6));
    }
  }
  if (viewType == groupView)
    item->setText(12, QString::number(group->maxQuality, 'f', 2));

  item->setText(2, QString(group->getName().c_str()));

  item->setIcon(0, iconsForLegend()[group->predictedLabel()]);
  QString castLabel =
      QString::fromStdString(PeakGroup::labelToString(group->predictedLabel()));
  item->setData(0, Qt::UserRole, QVariant::fromValue(castLabel));
  _paintClassificationDisagreement(item);

  if (filtersDialog->isVisible()) {
    float minG = sliders["GoodPeakCount"]->minBoundValue();
    float maxG = sliders["GoodPeakCount"]->maxBoundValue();

    if (group->goodPeakCount < minG || group->goodPeakCount > maxG) {
      item->setHidden(true);
    } else {
      item->setHidden(false);
    }
  }

  if (updateChildren) {
    for (int i = 0; i < item->childCount(); ++i)
      updateItem(item->child(i));
  }
}

void TableDockWidget::updateCompoundWidget() {
  _mainwindow->ligandWidget->resetColor();
  QTreeWidgetItemIterator itr(treeWidget);
  while (*itr) {
    QTreeWidgetItem *item = (*itr);
    if (item) {
      QVariant v = item->data(0, Qt::UserRole);
      shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
      if (group == nullptr)
        continue;
      _mainwindow->ligandWidget->markAsDone(group->getCompound());
    }
    ++itr;
  }
}

void TableDockWidget::heatmapBackground(QTreeWidgetItem *item) {
  if (viewType != peakView)
    return;

  int firstColumn = 4;
  StatisticsVector<float> values;
  float sum = 0;
  for (unsigned int i = firstColumn; i < item->columnCount(); i++) {
    values.push_back(item->text(i).toFloat());
  }

  if (values.size()) {
    // normalize
    float mean = values.mean();
    float sd = values.stddev();

    float max = values.maximum();
    float min = values.minimum();
    float range = max - min;

    for (int i = 0; i < values.size(); i++) {
      if (max != 0)
        values[i] = abs((max - values[i]) / max); // Z-score
    }

    QColor color = Qt::white;

    float colorramp = 0.5;

    for (int i = 0; i < values.size(); i++) {
      float value = values[i];
      float prob = value;
      if (prob < 0)
        prob = 0;
      color.setHsvF(0.0, prob, 1, 1);

      item->setBackgroundColor(firstColumn + i, color);
    }
  }
}

void TableDockWidget::addRow(shared_ptr<PeakGroup> group,
                             QTreeWidgetItem *root) {

  if (group == nullptr)
    return;
  if (group->meanMz <= 0) {
    return;
  }

  NumericTreeWidgetItem *item = new NumericTreeWidgetItem(root, 0);
  item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                 Qt::ItemIsDragEnabled);
  item->setData(1, Qt::UserRole, QVariant::fromValue(group));

  item->setText(0, QString::number(group->groupId));
  item->setText(1, QString(group->getName().c_str()));
  item->setText(2, QString::number(group->meanMz, 'f', 4));

  int charge = group->parameters()->getCharge(group->getCompound());
  if (group->getExpectedMz(charge) != -1) {
    float mz = group->getExpectedMz(charge);

    item->setText(4, QString::number(mz, 'f', 4));
  } else {
    item->setText(4, "NA");
  }

  item->setText(5, QString::number(group->meanRt, 'f', 2));

  if (viewType == groupView) {
    auto expectedRtDiff = group->expectedRtDiff();
    if (expectedRtDiff == -1.0f) {
      item->setText(6, "NA");
    } else {
      item->setText(6, QString::number(expectedRtDiff, 'f', 2));
    }
    item->setText(7, QString::number(group->sampleCount
                                     + group->blankSampleCount));
    item->setText(8, QString::number(group->goodPeakCount));
    item->setText(9, QString::number(group->maxNoNoiseObs));
    item->setText(10, QString::number(extractMaxIntensity(group), 'g', 3));
    item->setText(11, QString::number(group->maxSignalBaselineRatio, 'f', 0));
    item->setText(12, QString::number(group->maxQuality, 'f', 2));
    item->setText(13, QString::number(group->fragMatchScore.mergedScore, 'f', 2));
    item->setText(14, QString::number(group->ms2EventCount));
    item->setText(15, QString::number(group->predictionProbability(), 'f', 3));
    item->setText(16, QString::number(group->groupRank, 'e', 6));

    if (group->changeFoldRatio != 0) {

      item->setText(17, QString::number(group->changeFoldRatio, 'f', 2));
      item->setText(18, QString::number(group->changePValue, 'e', 4));
    }

    //Find maximum number of peaks
    if (maxPeaks < group->peakCount())
        maxPeaks = group->peakCount();

    //TODO: Move this to peak detector or peakgroup
    groupClassifier* groupClsf = _mainwindow->getGroupClassifier();
    if (group->peakCount() > 0 && groupClsf != NULL) {
        groupClsf->classify(group.get());
    }

    //Get prediction labels from svm
    svmPredictor* groupPred = _mainwindow->getSVMPredictor();
    if (group->peakCount() > 0 && groupPred != NULL) {
        groupPred->predict(group.get());
    }
  } else if (viewType == peakView) {
    vector<mzSample *> vsamples = _mainwindow->getVisibleSamples();
    sort(vsamples.begin(), vsamples.end(), mzSample::compSampleOrder);
    vector<float> yvalues = group->getOrderedIntensityVector(
        vsamples, _mainwindow->getUserQuantType());
    for (unsigned int i = 0; i < yvalues.size(); i++) {

      item->setText(6 + i, QString::number(yvalues[i]));
    }
    heatmapBackground(item);
  }
  if (root == NULL)
    treeWidget->addTopLevelItem(item);
  updateItem(item);

  if (group->childCount() > 0) {
    for (auto child : group->children)
      addRow(child, item);
  }
}

void ListView::keyPressEvent(QKeyEvent *event) {
  if (event->matches(QKeySequence::Copy)) {
    // set all selected compound name to clipboard
    QApplication::clipboard()->setText(strings.join("\n"));
  }
}

shared_ptr<PeakGroup> TableDockWidget::addPeakGroup(PeakGroup *group)
{
  if (group != NULL) {
    shared_ptr<PeakGroup> sharedGroup = make_shared<PeakGroup>(*group);
    _topLevelGroups.push_back(sharedGroup);
    if (sharedGroup->childCount() > 0)
      _labeledGroups++;
    if (sharedGroup->getCompound())
      _targetedGroups++;
    if (_topLevelGroups.size() > 0) {
      shared_ptr<PeakGroup> g = _topLevelGroups.back();
      g->setTableName(this->titlePeakTable->text().toStdString());
      for (int i = 0; i < _topLevelGroups.size(); ++i) {
        auto group = _topLevelGroups[i];
        group->groupId = i + 1;
        group->setGroupIdForChildren();
      }
      return g;
    }
  }

  return NULL;
}

QList<shared_ptr<PeakGroup>> TableDockWidget::getGroups()
{
  return _topLevelGroups;
}

void TableDockWidget::deleteAll()
{
  if (treeWidget->currentItem()) {
      _mainwindow->getEicWidget()->unSetPeakTableGroup(
          treeWidget->currentItem()->data(0, Qt::UserRole)
              .value<shared_ptr<PeakGroup>>());
  }

  disconnect(treeWidget,
             &QTreeWidget::itemSelectionChanged,
             this,
             &TableDockWidget::showSelectedGroup);
  treeWidget->clear();
  _topLevelGroups.clear();
  connect(treeWidget,
          &QTreeWidget::itemSelectionChanged,
          this,
          &TableDockWidget::showSelectedGroup);

  _mainwindow->getEicWidget()->replotForced();

  this->hide();

  if (_mainwindow->heatmap) {
    HeatMap *_heatmap = _mainwindow->heatmap;
    _heatmap->setTable(this);
    _heatmap->replot();
  }
}

void TableDockWidget::showAllGroups() {
  treeWidget->clear();

  setFocus();
  if (topLevelGroupCount() == 0) {
    if (viewType == groupView)
      setIntensityColName();
    setVisible(false);
    return;
  }

  treeWidget->setSortingEnabled(false);

  setupPeakTable();
  if (viewType == groupView)
    setIntensityColName();

  QMap<int, QTreeWidgetItem *> parents;
  for (auto group : _topLevelGroups) {
    int clusterId = group->clusterId;
    if (clusterId && group->meanMz > 0 && group->peakCount() > 0) {
      if (!parents.contains(clusterId)) {
        parents[clusterId] = new QTreeWidgetItem(treeWidget);
        parents[clusterId]->setText(1, QString("Cluster ") +
                                           QString::number(clusterId));
        parents[clusterId]->setText(
            5, QString::number(group->meanRt, 'f', 2));
        parents[clusterId]->setExpanded(true);
      }
      QTreeWidgetItem *parent = parents[clusterId];
      addRow(group, parent);
    } else {
      addRow(group, NULL);
    }
  }

  QScrollBar *vScroll = treeWidget->verticalScrollBar();
  if (vScroll) {
    vScroll->setSliderPosition(vScroll->maximum());
  }
  sortBy(1);
  treeWidget->setSortingEnabled(true);
  updateStatus();
  updateCompoundWidget();
  //@Kailash: Check and validate all groups automatically
  QTreeWidgetItemIterator itr(treeWidget);
  while(*itr) {
      QTreeWidgetItem* item = (*itr);
      QVariant v = item->data(0,Qt::UserRole);
      shared_ptr<PeakGroup> grp = v.value<shared_ptr<PeakGroup>>();
      validateGroup(grp.get(), item);
      itr++;
  }

  treeWidget->header()->setSectionResizeMode(1,QHeaderView::Interactive);
  treeWidget->setColumnWidth(1, 250);
}

QMap<TableDockWidget::PeakTableSubsetType, int>
TableDockWidget::countBySubsets()
{
  auto itemsBySubset = _peakTableGroupedBySubsets();
  QMap<PeakTableSubsetType, int> numItemsPerSubset;

  auto insertCountForSubset = [&](PeakTableSubsetType subset) {
    numItemsPerSubset.insert(subset,
                             itemsBySubset.value(subset).size());
  };

  insertCountForSubset(PeakTableSubsetType::Selected);
  insertCountForSubset(PeakTableSubsetType::All);
  insertCountForSubset(PeakTableSubsetType::Good);
  insertCountForSubset(PeakTableSubsetType::Bad);
  insertCountForSubset(PeakTableSubsetType::ExcludeBad);
  insertCountForSubset(PeakTableSubsetType::Unmarked);
  insertCountForSubset(PeakTableSubsetType::Correlated);
  insertCountForSubset(PeakTableSubsetType::Variance);
  insertCountForSubset(PeakTableSubsetType::CorrelatedVariance);

  return numItemsPerSubset;
}

void TableDockWidget::showOnlySubsets(QList<PeakTableSubsetType> visibleSubsets)
{
  auto itemsBySubset = _peakTableGroupedBySubsets();

  // first, we hide all items from subsets that are not in the inclusion list
  QMapIterator<PeakTableSubsetType, QList<QTreeWidgetItem*>>
      hideItr(itemsBySubset);
  while (hideItr.hasNext()) {
    hideItr.next();
    auto subset = hideItr.key();
    auto& itemsForSubset = hideItr.value();
    if (!visibleSubsets.contains(subset)) {
      for (auto item : itemsForSubset)
        item->setHidden(true);
    }
  }

  // next, we separately show visible subset because some of the items that
  // were hidden in the last iteration might have to be shown again
  QMapIterator<PeakTableSubsetType, QList<QTreeWidgetItem*>>
      showItr(itemsBySubset);
  while (showItr.hasNext()) {
    showItr.next();
    auto subset = showItr.key();
    auto& itemsForSubset = showItr.value();
    if (visibleSubsets.contains(subset)) {
      for (auto item : itemsForSubset)
        item->setHidden(false);
    }
  }
}

void TableDockWidget::filterForSelectedLabels()
{
  QList<PeakTableSubsetType> selectedSubsets;
  auto selectedLabels = _legend->selectedTexts();
  for (auto& label : selectedLabels) {
    auto predictionLabel = labelsForLegend().key(label);
    if (predictionLabel == PeakGroup::ClassifiedLabel::Noise)
      selectedSubsets.append(PeakTableSubsetType::Bad);
    if (predictionLabel == PeakGroup::ClassifiedLabel::Signal)
      selectedSubsets.append(PeakTableSubsetType::Good);
    if (predictionLabel == PeakGroup::ClassifiedLabel::Correlation)
      selectedSubsets.append(PeakTableSubsetType::Correlated);
    if (predictionLabel == PeakGroup::ClassifiedLabel::Pattern)
      selectedSubsets.append(PeakTableSubsetType::Variance);
    if (predictionLabel == PeakGroup::ClassifiedLabel::CorrelationAndPattern)
      selectedSubsets.append(PeakTableSubsetType::CorrelatedVariance);
  }
  selectedSubsets.append(PeakTableSubsetType::Unmarked);
  showOnlySubsets(selectedSubsets);
}

float TableDockWidget::extractMaxIntensity(PeakGroup *group) {
  float temp;
  PeakGroup::QType qtype = _mainwindow->getUserQuantType();
  switch (qtype) {
  case PeakGroup::AreaTop:
    temp = group->maxAreaTopIntensity;
    break;
  case PeakGroup::Area:
    temp = group->maxAreaIntensity;
    break;
  case PeakGroup::Height:
    temp = group->maxHeightIntensity;
    break;
  case PeakGroup::AreaNotCorrected:
    temp = group->maxAreaNotCorrectedIntensity;
    break;
  case PeakGroup::AreaTopNotCorrected:
    temp = group->maxAreaTopNotCorrectedIntensity;
    break;
  default:
    temp = group->currentIntensity;
    break;
  }
  group->currentIntensity = temp;
  return temp;
}

void TableDockWidget::exportGroupsToSpreadsheet() {

  vector<mzSample *> samples = _mainwindow->getSamples();

  if (topLevelGroupCount() == 0) {
    QString msg = "Peaks Table is Empty";
    QMessageBox::warning(this, tr("Error"), msg);
    return;
  }

  QString dir = ".";
  QSettings *settings = _mainwindow->getSettings();

  if (settings->contains("lastDir"))
    dir = settings->value("lastDir").value<QString>();

  QString groupsSTAB = "Groups Summary Matrix Format With Set Names (*.tab)";
  QString groupsTAB = "Groups Summary Matrix Format (*.tab)";
  QString peaksTAB = "Peaks Detailed Format (*.tab)";
  QString groupsSCSV =
      "Groups Summary Matrix Format Comma Delimited With Set Names (*.csv)";
  QString groupsCSV = "Groups Summary Matrix Format Comma Delimited (*.csv)";
  QString peaksCSV = "Peaks Detailed Format Comma Delimited (*.csv)";

  QString peaksListQE = "Inclusion List QE (*.csv)";
  QString mascotMGF = "Mascot Format MS2 Scans (*.mgf)";

  QString sFilterSel;
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Export Groups"), dir,
      groupsCSV + ";;" + groupsSCSV + ";;" + groupsTAB + ";;" + groupsSTAB +
          ";;" + peaksCSV + ";;" + peaksTAB + ";;" + peaksListQE + ";;" +
          mascotMGF,
      &sFilterSel);

  if (fileName.isEmpty())
    return;

  if (sFilterSel == groupsSCSV || sFilterSel == peaksCSV ||
      sFilterSel == groupsCSV) {
    if (!fileName.endsWith(".csv", Qt::CaseInsensitive))
      fileName = fileName + ".csv";
  }

  if (sFilterSel == groupsSTAB || sFilterSel == peaksTAB ||
      sFilterSel == groupsTAB) {
    if (!fileName.endsWith(".tab", Qt::CaseInsensitive))
      fileName = fileName + ".tab";
  }

  if (samples.size() == 0)
    return;

  if (sFilterSel == groupsSCSV || sFilterSel == groupsSTAB ||
      sFilterSel == groupsCSV || sFilterSel == groupsTAB)
    _mainwindow->getAnalytics()->hitEvent("Exports", "CSV", "Groups");
  if (sFilterSel == peaksCSV || sFilterSel == peaksTAB)
    _mainwindow->getAnalytics()->hitEvent("Exports", "CSV", "Peaks");

  if (sFilterSel == peaksListQE) {
    _mainwindow->getAnalytics()->hitEvent("Exports", "CSV", "Peaks List");
    writeQEInclusionList(fileName);
    return;
  } else if (sFilterSel == mascotMGF) {
    _mainwindow->getAnalytics()->hitEvent("Exports", "CSV", "Mascot");
    writeMascotGeneric(fileName);
    return;
  }

 
  auto ms2GroupAt = find_if(begin(_topLevelGroups),
                            end(_topLevelGroups),
                            [] (shared_ptr<PeakGroup> group) {
                              if (!group->hasCompoundLink())
                                  return false;
                              return (group->getCompound()->type()
                                      == Compound::Type::MS2);
                            });
  bool ms2GroupExists = ms2GroupAt != end(_topLevelGroups);
  bool includeSetNamesLines = false;

  auto reportType = CSVReports::ReportType::GroupReport;
  if (sFilterSel == groupsSCSV) {
    reportType = CSVReports::ReportType::GroupReport;
    includeSetNamesLines = true;
  } else if (sFilterSel == groupsSTAB) {
    reportType = CSVReports::ReportType::GroupReport;
    includeSetNamesLines = true;
  } else if (sFilterSel == peaksCSV) {
    reportType = CSVReports::ReportType::PeakReport;
  } else if (sFilterSel == peaksTAB) {
    reportType = CSVReports::ReportType::PeakReport;
  } else {
    reportType = CSVReports::ReportType::GroupReport;
  }

  CSVReports csvreports(fileName.toStdString(),
                        reportType,
                        samples,
                        _mainwindow->getUserQuantType(),
                        ms2GroupExists,
                        includeSetNamesLines,
                        _mainwindow->mavenParameters);
  QList<shared_ptr<PeakGroup>> selectedGroups = getSelectedGroups();
  csvreports.setSelectionFlag(static_cast<int>(peakTableSelection));

  for (auto group : _topLevelGroups) {
    if (selectedGroups.contains(group))
      csvreports.addGroup(group.get());
  }
 
  if (csvreports.getErrorReport() != "") {
    QMessageBox msgBox(_mainwindow);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText(csvreports.getErrorReport());
    msgBox.exec();
  }
}

void TableDockWidget::prepareDataForPolly(QString writableTempDir,
                                          QString exportFormat,
                                          QString userFilename) {
    vector<mzSample*> samples = _mainwindow->getSamples();

    if (topLevelGroupCount() == 0) {
        QString msg = "Peaks Table is Empty";
        QMessageBox::warning(this, tr("Error"), msg);
        return;
    }

    QString groupsSTAB = "Groups Summary Matrix Format With Set Names (*.tab)";
    QString groupsTAB = "Groups Summary Matrix Format (*.tab)";
    QString peaksTAB = "Peaks Detailed Format (*.tab)";
    QString groupsSCSV =
        "Groups Summary Matrix Format Comma Delimited With Set Names (*.csv)";
    QString groupsCSV = "Groups Summary Matrix Format Comma Delimited (*.csv)";
    QString peaksCSV = "Peaks Detailed Format Comma Delimited (*.csv)";

    QString peaksListQE = "Inclusion List QE (*.csv)";
    QString mascotMGF = "Mascot Format MS2 Scans (*.mgf)";

    QString sFilterSel = exportFormat;
    QString fileName = writableTempDir + QDir::separator() + userFilename;
    if (fileName.isEmpty())
        return;

    if (sFilterSel == groupsSCSV || sFilterSel == peaksCSV
        || sFilterSel == groupsCSV) {
        if (!fileName.endsWith(".csv", Qt::CaseInsensitive))
            fileName = fileName + ".csv";
    }

    if (sFilterSel == groupsSTAB || sFilterSel == peaksTAB
        || sFilterSel == groupsTAB) {
        if (!fileName.endsWith(".tab", Qt::CaseInsensitive))
            fileName = fileName + ".tab";
    }

    if (samples.size() == 0)
        return;

    if (sFilterSel == peaksListQE) {
        writeQEInclusionList(fileName);
        return;
    } else if (sFilterSel == mascotMGF) {
        writeMascotGeneric(fileName);
        return;
    }

    auto ms2GroupAt = find_if(begin(_topLevelGroups),
                              end(_topLevelGroups),
                              [](shared_ptr<PeakGroup> group) {
                                if (!group->hasCompoundLink())
                                  return false;
                                return (group->getCompound()->type()
                                        == Compound::Type::MS2);
                              });
    bool ms2GroupExists = ms2GroupAt != end(_topLevelGroups);
    bool includeSetNamesLines = false;

    auto reportType = CSVReports::ReportType::GroupReport;
    if (sFilterSel == groupsSCSV) {
        reportType = CSVReports::ReportType::GroupReport;
    } else if (sFilterSel == groupsSTAB) {
        reportType = CSVReports::ReportType::GroupReport;
    } else if (sFilterSel == peaksCSV) {
        reportType = CSVReports::ReportType::PeakReport;
    } else if (sFilterSel == peaksTAB) {
        reportType = CSVReports::ReportType::PeakReport;
    }
    CSVReports csvreports(fileName.toStdString(),
                          reportType,
                          samples,
                          _mainwindow->getUserQuantType(),
                          ms2GroupExists,
                          includeSetNamesLines,
                          _mainwindow->mavenParameters,
                          true);

    QList<shared_ptr<PeakGroup>> selectedGroups = getSelectedGroups();
    csvreports.setSelectionFlag(static_cast<int>(peakTableSelection));

    for (auto group : _topLevelGroups) {
        // we do not set untargeted groups to Polly yet, remove this when we
        // can.
        if (selectedGroups.contains(group) && group->hasCompoundLink()) {
            csvreports.addGroup(group.get());
        }
    }

    if (csvreports.getErrorReport() != "") {
        QMessageBox msgBox(_mainwindow);
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText(csvreports.getErrorReport());
        msgBox.exec();
    }
}

void TableDockWidget::exportJsonToPolly(QString writableTempDir,
                                        QString jsonfileName, 
                                        bool addMLInfo)
{

  if (topLevelGroupCount() == 0) {
    QString msg = "Peaks Table is Empty";
    QMessageBox::warning(this, tr("Error"), msg);
    return;
  }

  // copy all groups from <_topLevelGroups> to <vallgroups> which is used by
  // <libmaven/jsonReports.cpp>
  vallgroups.clear();
  for (auto group : _topLevelGroups)
    vallgroups.push_back(*(group.get()));

  jsonReports = new JSONReports(_mainwindow->mavenParameters, addMLInfo);
  jsonReports->save(jsonfileName.toStdString(),
                    vallgroups,
                    _mainwindow->getVisibleSamples());
}

void TableDockWidget::exportJson() {

  if (topLevelGroupCount() == 0) {
    QString msg = "Peaks Table is Empty";
    QMessageBox::warning(this, tr("Error"), msg);
    return;
  }

  _mainwindow->getAnalytics()->hitEvent("Exports", "JSON");

  // copy all groups from <_topLevelGroups> to <vallgroups> which is used by
  // <libmaven/jsonReports.cpp>
  vallgroups.clear();
  for (auto group : _topLevelGroups)
    vallgroups.push_back(*(group.get()));

  QString dir = ".";
  QSettings *settings = _mainwindow->getSettings();
  if (settings->contains("lastDir"))
    dir = settings->value("lastDir").value<QString>();

  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save EICs to Json File"), dir, tr("*.json"));
  if (fileName.isEmpty())
    return;
  if (!fileName.endsWith(".json", Qt::CaseInsensitive))
    fileName = fileName + ".json";

  saveJson *jsonSaveThread = new saveJson();
  jsonSaveThread->setMainwindow(_mainwindow);
  jsonSaveThread->setPeakTable(this);
  jsonSaveThread->setfileName(fileName.toStdString());
  jsonSaveThread->start();
}

void TableDockWidget::exportSpectralLib()
{
    bool ok;
    int limitNumPeaks = QInputDialog::getInt(this,
                                             "",
                                             "Limit number of fragments per "
                                             "compound",
                                             20, 1, 100, 1,
                                             &ok);
    if (!ok)
        return;

    QString dir = ".";
    QSettings* settings = _mainwindow->getSettings();
    if (settings->contains("lastDir"))
        dir = settings->value("lastDir").value<QString>();

    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export table as spectral library"), dir, tr("*.msp"));
    if (fileName.isEmpty())
        return;
    if (!fileName.endsWith(".msp", Qt::CaseInsensitive))
        fileName = fileName + ".msp";
    if (QFile::exists(fileName))
        QFile::remove(fileName);

    SpectralLibExport library(fileName.toStdString(),
                              SpectralLibExport::Format::Nist,
                              limitNumPeaks);
    for (auto group : _topLevelGroups)
        library.writePeakGroupData(group.get());
}

vector<EIC *> TableDockWidget::getEICs(float rtmin,
                                       float rtmax,
                                       PeakGroup &grp) {
  vector<EIC *> eics;
  for (int i = 0; i < grp.peaks.size(); i++) {
    float mzmin = grp.meanMz - 0.2;
    float mzmax = grp.meanMz + 0.2;
    vector<mzSample *> samples = _mainwindow->getSamples();
    for (unsigned int j = 0; j < samples.size(); j++) {
      if (!grp.srmId.empty()) {
        EIC *eic = samples[j]->getEIC(grp.srmId,
                                      _mainwindow->mavenParameters->eicType);
        eics.push_back(eic);
      } else {
        EIC *eic = samples[j]->getEIC(mzmin, mzmax,
                                      rtmin, rtmax, 1,
                                      _mainwindow->mavenParameters->eicType,
                                      _mainwindow->mavenParameters->filterline);
        eics.push_back(eic);
      }
    }
  }
  return (eics);
}

bool TableDockWidget::selectPeakGroup(shared_ptr<PeakGroup> group)
{
  if (group == nullptr)
    return false;

  QTreeWidgetItemIterator it(treeWidget);
  while (*it) {
    QTreeWidgetItem *item = (*it);
    QVariant v = item->data(0, Qt::UserRole);
    shared_ptr<PeakGroup> currentGroup = v.value<shared_ptr<PeakGroup>>();
    if (currentGroup == group) {
        treeWidget->setCurrentItem(item);
      return true;
    }
  }
  return false;
}

void TableDockWidget::showSelectedGroup() {

  QTreeWidgetItem *item = treeWidget->currentItem();
  if (!item)
    return;

  QVariant v = item->data(0, Qt::UserRole);
  shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
  _mainwindow->groupRtWidget->plotGraph(group.get());

  if (group != nullptr && _mainwindow != nullptr) {
    _mainwindow->setPeakGroup(group);
  }

  if (item->childCount() > 0) {
    vector<PeakGroup *> children;
    for (int i = 0; i < item->childCount(); i++) {
      QTreeWidgetItem *child = item->child(i);
      QVariant data = child->data(1, Qt::UserRole);
      PeakGroup *group = data.value<PeakGroup *>();
      if (group)
        children.push_back(group);
    }
  }
}

QList<shared_ptr<PeakGroup>> TableDockWidget::getSelectedGroups()
{
  QList<shared_ptr<PeakGroup>> selectedGroups;
  Q_FOREACH (QTreeWidgetItem *item, treeWidget->selectedItems()) {
    if (item) {
      QVariant v = item->data(0, Qt::UserRole);
      shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
      if (group != nullptr) {
        selectedGroups.append(group);
      }
    }
  }
  return selectedGroups;
}

void TableDockWidget::showNotification()
{
  _mainwindow->showNotification(this);
}

QList<PeakGroup *>
TableDockWidget::getCustomGroups(PeakTableSubsetType peakSelection) {
  QList<PeakGroup *> selectedGroups;
  PeakTableSubsetType temppeakSelection = peakSelection;
  Q_FOREACH (QTreeWidgetItem *item, treeWidget->selectedItems()) {
    if (item) {
      QVariant v = item->data(1, Qt::UserRole);
      PeakGroup *group = v.value<PeakGroup *>();
      if (group != NULL) {
        if (temppeakSelection == PeakTableSubsetType::Good) {
          if (group->userLabel() == 'g') {
            selectedGroups.append(group);
          }
        } else if (temppeakSelection == PeakTableSubsetType::Bad) {
          if (group->userLabel() == 'b') {
            selectedGroups.append(group);
          }
        } else {
          selectedGroups.append(group);
        }
      }
    }
  }
  return selectedGroups;
}

QMap<TableDockWidget::PeakTableSubsetType, QList<QTreeWidgetItem*>>
TableDockWidget::_peakTableGroupedBySubsets() {
  QList<QTreeWidgetItem*> allItems;
  QList<QTreeWidgetItem*> selectedItems;
  QList<QTreeWidgetItem*> goodItems;
  QList<QTreeWidgetItem*> badItems;
  QList<QTreeWidgetItem*> nonBadItems;
  QList<QTreeWidgetItem*> unmarkedItems;
  QList<QTreeWidgetItem*> correlatedItems;
  QList<QTreeWidgetItem*> varianceItems;
  QList<QTreeWidgetItem*> correlatedVarianceItems;

  QTreeWidgetItemIterator itr(treeWidget);
  while (*itr) {
    QTreeWidgetItem *item = (*itr);
    if (item) {
      QVariant v = item->data(1, Qt::UserRole);
      PeakGroup *group = v.value<PeakGroup *>();
      if (group == nullptr)
        continue;

      // all groups are inserted into this list
      allItems.append(item);

      // all selected groups are inserted into this list
      if (item->isSelected())
        selectedItems.append(item);

      auto label = group->predictedLabel();
      if (label == PeakGroup::ClassifiedLabel::Noise) {
        badItems.append(item);
      } else {
        nonBadItems.append(item);
      }
      if (label == PeakGroup::ClassifiedLabel::Signal)
        goodItems.append(item);
      if (label == PeakGroup::ClassifiedLabel::None)
        unmarkedItems.append(item);
      if (label == PeakGroup::ClassifiedLabel::Correlation)
        correlatedItems.append(item);
      if (label == PeakGroup::ClassifiedLabel::Pattern)
        varianceItems.append(item);
      if (label == PeakGroup::ClassifiedLabel::CorrelationAndPattern)
        correlatedVarianceItems.append(item);
    }
    ++itr;
  }

  QMap<PeakTableSubsetType, QList<QTreeWidgetItem*>> itemsBySubset;
  itemsBySubset.insert(PeakTableSubsetType::Selected, selectedItems);
  itemsBySubset.insert(PeakTableSubsetType::All, allItems);
  itemsBySubset.insert(PeakTableSubsetType::Good, goodItems);
  itemsBySubset.insert(PeakTableSubsetType::Bad, badItems);
  itemsBySubset.insert(PeakTableSubsetType::ExcludeBad, nonBadItems);
  itemsBySubset.insert(PeakTableSubsetType::Unmarked, unmarkedItems);
  itemsBySubset.insert(PeakTableSubsetType::Correlated, correlatedItems);
  itemsBySubset.insert(PeakTableSubsetType::Variance, varianceItems);
  itemsBySubset.insert(PeakTableSubsetType::CorrelatedVariance,
                       correlatedVarianceItems);

  return itemsBySubset;
}

PeakGroup *TableDockWidget::getSelectedGroup() {
  QTreeWidgetItem *item = treeWidget->currentItem();
  if (!item)
    return NULL;
  QVariant v = item->data(1, Qt::UserRole);
  PeakGroup *group = v.value<PeakGroup *>();
  if (group != NULL) {
    return group;
  } else
    return shared_ptr<PeakGroup>(nullptr);
}

void TableDockWidget::setGroupLabel(char label)
{
  Q_FOREACH (QTreeWidgetItem *item, treeWidget->selectedItems()) {
    if (item) {
      QVariant v = item->data(1, Qt::UserRole);
      shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
      PeakGroup *group = v.value<PeakGroup *>();
      if (group != NULL)
        group->setUserLabel(label);
      updateItem(item);
      if (item->parent() != nullptr)
        updateItem(item->parent(), false);
    }
  }
  updateStatus();
}

void TableDockWidget::deleteGroup(PeakGroup *groupX) {
  if (!groupX)
    return;

  int pos = -1;
  for (int i = 0; i < _topLevelGroups.size(); i++) {
    if (_topLevelGroups[i].get() == groupX) {
      pos = i;
      break;
    }
  }
  if (pos == -1)
    return;

  QTreeWidgetItemIterator it(treeWidget);
  while (*it) {
    QTreeWidgetItem *item = (*it);
    if (item->isHidden()) {
      ++it;
      continue;
    }
    QVariant v = item->data(0, Qt::UserRole);
    shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
    if (group != nullptr and group.get() == groupX) {
      item->setHidden(true);

      if (!group->children.empty())
        _labeledGroups--;
      if (group->hasCompoundLink())
        _targetedGroups--;

      // Deleting
      int posTree = treeWidget->indexOfTopLevelItem(item);
      if (posTree != -1)
        treeWidget->takeTopLevelItem(posTree);

      _topLevelGroups.erase(_topLevelGroups.begin() + pos);
      break;
    }
    ++it;
  }

  for (int i = 0; i < _topLevelGroups.size(); ++i) {
    auto group = _topLevelGroups[i];
    group->groupId = i + 1;
    group->setGroupIdForChildren();
  }
  updateTable();
  updateCompoundWidget();
}

void TableDockWidget::deleteSelectedItems()
{
    // temporarily disconnect selection trigger
    disconnect(treeWidget,
               SIGNAL(itemSelectionChanged()),
               this,
               SLOT(showSelectedGroup()));

    // extract selected items such that all parent items occur first
    QList<QTreeWidgetItem*> selectedItems;
    QTreeWidgetItem* nextItem = nullptr;
    for (auto item : treeWidget->selectedItems()) {
        if (item->parent() == nullptr) {
            selectedItems.prepend(item);
            nextItem = treeWidget->itemBelow(item);
            while(nextItem && nextItem->parent() != nullptr) {
                nextItem = treeWidget->itemBelow(nextItem);
            }
        } else {
            selectedItems.append(item);
            nextItem = treeWidget->itemBelow(item);
        }
    }
    if (selectedItems.isEmpty())
        return;

    set<QTreeWidgetItem*> itemsToDelete;
    set<shared_ptr<PeakGroup>> groupsToDelete;

    for (auto item : selectedItems)
    {
        if (item == nullptr)
            continue;

        auto v = item->data(0, Qt::UserRole);
        auto group = v.value<shared_ptr<PeakGroup>>();
        if (group == nullptr)
            continue;

        auto parentGroup = group->parent;
        if (parentGroup == nullptr){
            if (!group->children.empty())
                _labeledGroups--;
            if (group->hasCompoundLink())
                _targetedGroups--;
            itemsToDelete.insert(item);
            groupsToDelete.insert(group);
        } else if (parentGroup && parentGroup->childCount() > 0) {
            auto parentItem = item->parent();
            if (parentItem == nullptr)
                continue;
            if (itemsToDelete.count(parentItem) > 0
                || itemsToDelete.count(item) > 0) {
                continue;
            }

            if (!parentGroup->deleteChild(group.get()))
                continue;

            itemsToDelete.insert(item);

            // once a child is deleted, the pointers storing the
            // location of memory blocks of child `PeakGroup`
            // objects, may no longer be valid, therefore we
            // update them.
            for (int i = 0; i < parentItem->childCount(); ++i) {
                QTreeWidgetItem* child = parentItem->child(i);
                if (!child)
                    continue;

                auto name = child->text(1).toStdString();
                auto childGroupIter =
                    find_if(begin(parentGroup->children),
                            end(parentGroup->children),
                            [&](shared_ptr<PeakGroup> g) {
                                return g->getName() == name;
                            });
                if (childGroupIter != end(parentGroup->children)) {
                    auto& childGroup = *childGroupIter;
                    child->setData(0,
                                   Qt::UserRole,
                                   QVariant::fromValue(childGroup));
                }
            }
        }
    }

    if (!groupsToDelete.empty()) {
        _topLevelGroups.erase(remove_if(begin(_topLevelGroups),
                                        end(_topLevelGroups),
                                        [groupsToDelete](shared_ptr<PeakGroup> g) {
                                            return groupsToDelete.count(g) > 0;
                                        }),
                              end(_topLevelGroups));
    }

    // reconnect selection trigger
    connect(treeWidget,
            SIGNAL(itemSelectionChanged()),
            this,
            SLOT(showSelectedGroup()));

    if(nextItem){
        treeWidget->setCurrentItem(nextItem);
    }

    /* Disconnecting selection trigger as while deleting item from
     * tree widget selection changes and might give some null pointer
     * for setting group which causes segment fault */
    disconnect(treeWidget,
               SIGNAL(itemSelectionChanged()),
               this,
               SLOT(showSelectedGroup()));
    Q_FOREACH (QTreeWidgetItem* item, itemsToDelete) {
        if(item)
            delete(item);
    }
    connect(treeWidget,
            SIGNAL(itemSelectionChanged()),
            this,
            SLOT(showSelectedGroup()));

    auto groupSelected = getSelectedGroup();
    showAllGroups();

    if (_topLevelGroups.empty()) {
        _mainwindow->getEicWidget()->replot(nullptr);
        _mainwindow->ligandWidget->resetColor();
        _mainwindow->removePeaksTable(this);
        return;
    }

    QTreeWidgetItemIterator it(treeWidget);
    while (*it) {
        QTreeWidgetItem *item = (*it);
        QVariant v = item->data(0, Qt::UserRole);
        shared_ptr<PeakGroup> currentGroup = v.value<shared_ptr<PeakGroup>>();
        if (currentGroup == groupSelected) {
            treeWidget->setCurrentItem(item);
            break;
        }
        it++;
    }

    return;
}

void TableDockWidget::setClipboard() {
  _mainwindow->getAnalytics()->hitEvent("Exports",
                                        "Clipboard",
                                        "From Peak Table Menu");
  QList<shared_ptr<PeakGroup>> groups = getSelectedGroups();
  if (groups.size() > 0) {
    _mainwindow->isotopeWidget->setClipboard(groups);
  }
}

void TableDockWidget::showConsensusSpectra() {
  shared_ptr<PeakGroup> group = getSelectedGroup();
  if (group != nullptr) {
    _mainwindow->fragSpectraDockWidget->setVisible(true);
    _mainwindow->fragSpectraWidget->overlayPeakGroup(group);
  }
}

void TableDockWidget::markGroupGood() {
  setGroupLabel('g');
  auto currentGroups = getSelectedGroups();
  _mainwindow->getAnalytics()->hitEvent("Peak Group Curation", "Mark Good");
  showNextGroup();
  _mainwindow->autoSaveSignal(currentGroups);
}

void TableDockWidget::markGroupBad() {
  setGroupLabel('b');
  auto currentGroups = getSelectedGroups();
  _mainwindow->getAnalytics()->hitEvent("Peak Group Curation", "Mark Bad");
  showNextGroup();
  _mainwindow->autoSaveSignal(currentGroups);
}

void TableDockWidget::unmarkGroup() {
  // TODO: Add a button for unmarking peak-groups?
  setGroupLabel('\0');
  auto currentGroups = getSelectedGroups();
  _mainwindow->getAnalytics()->hitEvent("Peak Group Curation", "Unmark");
  _mainwindow->autoSaveSignal(currentGroups);
}

bool TableDockWidget::checkLabeledGroups() {

  int totalCount = 0;
  int goodCount = 0;
  int badCount = 0;

  if (_mainwindow->peaksMarked >= allgroups.size()) {
    for (int i = 0; i < allgroups.size(); i++) {
      char groupLabel = allgroups[i].userLabel();
      if (groupLabel == 'g') {
        goodCount++;
      } else if (groupLabel == 'b') {
        badCount++;
      }
      totalCount++;
    }

    if (totalCount == goodCount + badCount)
      return true;
  }

  return false;
}

void TableDockWidget::markGroupIgnored() {
  setGroupLabel('i');
  showNextGroup();
}

void TableDockWidget::showLastGroup() {
  QTreeWidgetItem *item = treeWidget->currentItem();
  if (item != NULL) {
    treeWidget->setCurrentItem(treeWidget->itemAbove(item));
  }
}

void TableDockWidget::showNextGroup() {

  QTreeWidgetItem *item = treeWidget->currentItem();
  if (item == NULL)
    return;

  // get next item
  QTreeWidgetItem *nextitem = treeWidget->itemBelow(item);
  if (nextitem != NULL)
    treeWidget->setCurrentItem(nextitem);
}

void TableDockWidget::Train() {

  Classifier *clsf = _mainwindow->getClassifier();

  if (allgroups.size() == 0)
    return;
  if (clsf == NULL)
    return;

  vector<PeakGroup *> train_groups;
  vector<PeakGroup *> test_groups;
  vector<PeakGroup *> good_groups;
  vector<PeakGroup *> bad_groups;

  for (int i = 0; i < allgroups.size(); i++) {
    PeakGroup *grp = &allgroups[i];
    if (grp->userLabel() == 'g')
      good_groups.push_back(grp);
    if (grp->userLabel() == 'b')
      bad_groups.push_back(grp);
  }

  mzUtils::shuffle(good_groups);
  for (int i = 0; i < good_groups.size(); i++) {
    PeakGroup *grp = good_groups[i];
    i % 2 == 0 ? train_groups.push_back(grp) : test_groups.push_back(grp);
  }

  mzUtils::shuffle(bad_groups);
  for (int i = 0; i < bad_groups.size(); i++) {
    PeakGroup *grp = bad_groups[i];
    i % 2 == 0 ? train_groups.push_back(grp) : test_groups.push_back(grp);
  }

  clsf->train(train_groups);
  clsf->classify(test_groups);
  showAccuracy(test_groups);
  updateTable();
}

void TableDockWidget::keyPressEvent(QKeyEvent *e) {

  QTreeWidgetItem *item = treeWidget->currentItem();
  if (e->key() == Qt::Key_Delete) {
    QList<QTreeWidgetItem *> items = treeWidget->selectedItems();
    if (items.size() > 0) {
      cerr << items.size() << endl;
      deleteSelectedItems();
    }
  } else if (e->key() == Qt::Key_G) {

    if (item) {
      markGroupGood();
    }
  } else if (e->key() == Qt::Key_B) {

    if (item) {
      markGroupBad();
    }
  } else if (e->key() == Qt::Key_U) {
    if (item) {
      unmarkGroup();
    }
  } else if (e->key() == Qt::Key_Left) {

    if (treeWidget->currentItem()) {
      if (treeWidget->currentItem()->parent()) {
        treeWidget->collapseItem(treeWidget->currentItem()->parent());
        treeWidget->setCurrentItem(treeWidget->currentItem()->parent());
      } else {
        treeWidget->collapseItem(treeWidget->currentItem());
      }
    }
  } else if (e->key() == Qt::Key_Right) {

    if (treeWidget->currentItem()) {
      if (!treeWidget->currentItem()->isExpanded()) {
        treeWidget->expandItem(treeWidget->currentItem());
      }
    }
  } else if (e->key() == Qt::Key_O) {
    if (treeWidget->currentItem()) {
      if (treeWidget->currentItem()->isExpanded()) {
        if (treeWidget->currentItem()->parent()) {
          treeWidget->collapseItem(treeWidget->currentItem()->parent());
          treeWidget->setCurrentItem(treeWidget->currentItem()->parent());
        } else {
          treeWidget->collapseItem(treeWidget->currentItem());
        }
      } else {
        treeWidget->expandItem(treeWidget->currentItem());
      }
    }
  } else if (e->key() == Qt::Key_Down && e->modifiers() == Qt::ShiftModifier) {
    if (treeWidget->itemBelow(item)) {
      if (tableSelectionFlagDown) {
        treeWidget->selectionModel()->setCurrentIndex(
            treeWidget->currentIndex(),
            QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
        tableSelectionFlagDown = false;
      } else {
        treeWidget->selectionModel()->setCurrentIndex(
            treeWidget->indexBelow(treeWidget->currentIndex()),
            QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
      }
      tableSelectionFlagUp = true;
    }
  } else if (e->key() == Qt::Key_Up && e->modifiers() == Qt::ShiftModifier) {
    if (treeWidget->itemAbove(item)) {
      if (tableSelectionFlagUp) {
        treeWidget->selectionModel()->setCurrentIndex(
            treeWidget->currentIndex(),
            QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
        tableSelectionFlagUp = false;
      } else {
        treeWidget->selectionModel()->setCurrentIndex(
            treeWidget->indexAbove(treeWidget->currentIndex()),
            QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
      }
      tableSelectionFlagDown = true;
    }
  } else if (e->key() == Qt::Key_Down) {

    if (treeWidget->itemBelow(item)) {
      treeWidget->setCurrentItem(treeWidget->itemBelow(item));
    }
  } else if (e->key() == Qt::Key_Up) {

    if (treeWidget->itemAbove(item)) {
      treeWidget->setCurrentItem(treeWidget->itemAbove(item));
    }
  } else if (e->key() == Qt::Key_E) {
      editSelectedPeakGroup();
  }
  QDockWidget::keyPressEvent(e);
  updateStatus();
}

void TableDockWidget::updateStatus() {

  int totalCount = 0;
  int goodCount = 0;
  int badCount = 0;
  for (auto group : _topLevelGroups) {
    char groupLabel = group->label;
    if (groupLabel == 'g') {
      goodCount++;
    } else if (groupLabel == 'b') {
      badCount++;
    }
    totalCount++;
  }
  QString title =
      tr("Group Validation Status: Good=%2 Bad=%3 Total=%1")
          .arg(QString::number(totalCount),
               QString::number(goodCount),
               QString::number(badCount));
  _mainwindow->setStatusText(title);
}

float TableDockWidget::showAccuracy(vector<PeakGroup *> &groups) {
  // check accuracy
  if (groups.size() == 0)
    return 0;

  int fp = 0;
  int fn = 0;
  int tp = 0;
  int tn = 0;
  int total = 0;
  float accuracy = 0;
  int gc = 0;
  int bc = 0;
  for (int i = 0; i < groups.size(); i++) {
    if (groups[i]->userLabel() == 'g' || groups[i]->userLabel() == 'b') {
      for (int j = 0; j < groups[i]->peaks.size(); j++) {
        float q = groups[i]->peaks[j].quality;
        char l = groups[i]->peaks[j].label;
        if (l == 'g')
          gc++;
        if (l == 'g' && q > _mainwindow->mavenParameters->minQuality)
          tp++;
        if (l == 'g' && q < _mainwindow->mavenParameters->minQuality)
          fn++;

        if (l == 'b')
          bc++;
        if (l == 'b' && q < _mainwindow->mavenParameters->minQuality)
          tn++;
        if (l == 'b' && q > _mainwindow->mavenParameters->minQuality)
          fp++;
        total++;
      }
    }
  }
  if (total > 0)
    accuracy = 1.00 - ((float)(fp + fn) / total);
  cerr << "TOTAL=" << total << endl;
  if (total == 0)
    return 0;

  cerr << "GC=" << gc << " BC=" << bc << endl;
  cerr << "TP=" << tp << " FN=" << fn << endl;
  cerr << "TN=" << tn << " FP=" << fp << endl;
  cerr << "Accuracy=" << accuracy << endl;

  traindialog->FN->setText(QString::number(fn));
  traindialog->FP->setText(QString::number(fp));
  traindialog->TN->setText(QString::number(tn));
  traindialog->TP->setText(QString::number(tp));
  traindialog->accuracy->setText(QString::number(accuracy * 100, 'f', 2));
  traindialog->show();
  _mainwindow->setStatusText(tr("Good Groups=%1 Bad Groups=%2 Accuracy=%3")
                                 .arg(QString::number(gc), QString::number(bc),
                                      QString::number(accuracy * 100)));

  return accuracy;
}

void TableDockWidget::showScatterPlot() {

  if (topLevelGroupCount() == 0)
    return;
  _mainwindow->scatterDockWidget->setVisible(true);
  ((ScatterPlot *)_mainwindow->scatterDockWidget)->setTable(this);
  ((ScatterPlot *)_mainwindow->scatterDockWidget)->replot();
  ((ScatterPlot *)_mainwindow->scatterDockWidget)->contrastGroups();
}

void TableDockWidget::printPdfReport() {

  _mainwindow->getAnalytics()->hitEvent("Exports", "PDF", "From Table");
  QString dir = ".";
  QSettings *settings = _mainwindow->getSettings();
  if (settings->contains("lastDir"))
    dir = settings->value("lastDir").value<QString>();

  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Group Report a PDF File"), dir, tr("*.pdf"));
  if (fileName.isEmpty())
    return;
  if (!fileName.endsWith(".pdf", Qt::CaseInsensitive))
    fileName = fileName + ".pdf";

  auto res = QtConcurrent::run(this, &TableDockWidget::renderPdf, fileName);
}


void TableDockWidget::renderPdf(QString fileName)
{
    //Setting printer.
    QPrinter printer;
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOrientation(QPrinter::Landscape);
    printer.setCreator("El-MAVEN");
    printer.setOutputFileName(fileName);
    printer.setResolution(50);

    QPainter painter;

    if (!painter.begin(&printer)) {
        // failed to open file
        qWarning("failed to open file, is it writable?");
        return;
    }

    if (printer.printerState() != QPrinter::Active) {
        qDebug() << "PrinterState:" << printer.printerState();
    }

    QList<shared_ptr<PeakGroup>> selected;
    // PDF report only for selected groups
    if(peakTableSelection == peakTableSelectionType::Selected) {
        selected = getSelectedGroups();
    } else {
        selected = getGroups();
    }

    for (int i = 0; i < selected.size(); i++) {
        shared_ptr<PeakGroup> grp = selected[i];
        emit updateProgressBar("Saving PDF export for table…",
                               i + 1,
                               selected.size(),
                               true);
        _mainwindow->getEicWidget()->renderPdf(grp, &painter);

        if(!printer.newPage())
        {
            qWarning("failed in flushing page to disk, disk full?");
            return;
        }

   }
   painter.end();
   emit updateProgressBar("", selected.size(), selected.size());
   emit renderedPdf();
}

void TableDockWidget::pdfReadyNotification()
{
    QIcon icon = QIcon("");
    QString title("");
    QString message("Your PDF has been saved successfully.");

    Notificator* fluxomicsPrompt = Notificator::showMessage(icon,
                                                            title,
                                                            message,
                                                           this);
    title = "Your PDF has been saved successfully.";
    _mainwindow->setStatusText(title);
}

void TableDockWidget::showHeatMap() {

  _mainwindow->heatMapDockWidget->setVisible(true);
  HeatMap *_heatmap = _mainwindow->heatmap;
  if (_heatmap) {
    _heatmap->setTable(this);
    _heatmap->replot();
  }
}

void TableDockWidget::editSelectedPeakGroup()
{
  if (treeWidget->selectedItems().size() != 1)
      return;

  shared_ptr<PeakGroup> group = getSelectedGroup();
  PeakEditor* editor = _mainwindow->peakEditor();
  if (editor == nullptr || group == nullptr)
    return;

  editor->setPeakGroup(group);
  editor->exec();

  auto currentItem = treeWidget->currentItem();
  if (currentItem->parent() != nullptr) {
    updateItem(currentItem->parent(), true);
  } else {
    updateItem(currentItem, true);
  }

  auto groupToSave = group;
  if (group->isIsotope() && group->parent != nullptr) {
      QTreeWidgetItemIterator it(treeWidget);
      while (*it) {
          QTreeWidgetItem* item = (*it);
          QVariant v = item->data(0, Qt::UserRole);
          shared_ptr<PeakGroup> currentGroup = v.value<shared_ptr<PeakGroup>>();
          if (currentGroup.get() == group->parent) {
              groupToSave = currentGroup;
              break;
          }
          it++;
      }
  }
  _mainwindow->autoSaveSignal({groupToSave});
}

void TableDockWidget::showIntegrationSettings()
{
  if (treeWidget->selectedItems().size() != 1)
      return;

  shared_ptr<PeakGroup> group = getSelectedGroup();
  GroupSettingsLog log(this, group);
  log.exec();
}

void TableDockWidget::contextMenuEvent(QContextMenuEvent *event) {
  if (treeWidget->topLevelItemCount() < 1)
      return;

  QMenu menu;
  QAction *z0 = menu.addAction("Copy to Clipboard");
  connect(z0, SIGNAL(triggered()), this, SLOT(setClipboard()));

  QAction *z1 = menu.addAction("Edit peak-group");
  connect(z1,
          &QAction::triggered,
          this,
          &TableDockWidget::editSelectedPeakGroup);

  QAction *z2 = menu.addAction("Show integration settings");
  connect(z2,
          &QAction::triggered,
          this,
          &TableDockWidget::showIntegrationSettings);

  QAction *z3 = menu.addAction("Show Consensus Spectra");
  connect(z3, SIGNAL(triggered()), SLOT(showConsensusSpectra()));

  QAction *z4 = menu.addAction("Delete All Groups");
  connect(z4, SIGNAL(triggered()), SLOT(deleteAll()));

  if (treeWidget->selectedItems().empty()) {
    // disable actions not relevant when nothing is selected
    z0->setEnabled(false);
  }
  if (treeWidget->selectedItems().size() != 1) {
    // disable actions not relevant to individual peak-groups
    z1->setEnabled(false);
    z2->setEnabled(false);
    z3->setEnabled(false);
  }

  QAction *z8 = menu.addAction("Explain classification");
  connect(z8,
          &QAction::triggered,
          [this] {
              PeakGroup* group = getSelectedGroup();
              if (group == nullptr)
                  return;

              // TODO: maybe the widget defined here should be a class instead?

              int top_n = 5;
              int counter = 0;
              double sumNegativeWeights = 0.0;
              double sumPositiveWeights = 0.0;
              QHorizontalStackedBarSeries *series = new QHorizontalStackedBarSeries();
              auto predictionInference = group->predictionInference();
              for (auto it = predictionInference.begin();
                   it != predictionInference.end();
                   ++it) {
                if (it->first < 0 && counter < top_n) {
                  QBarSet* set = new QBarSet(it->second.c_str());
                  *set << it->first;
                  series->append(set);
                  sumNegativeWeights += it->first;
                  ++counter;
                } else {
                  break;
                }
              }
              counter = 0;
              for (auto it = predictionInference.rbegin();
                   it != predictionInference.rend();
                   ++it) {
                if (it->first > 0 && counter < top_n) {
                  QBarSet* set = new QBarSet(it->second.c_str());
                  *set << it->first;
                  series->append(set);
                  sumPositiveWeights += it->first;
                  ++counter;
                } else {
                  break;
                }
              }

              QChart *chart = new QChart();
              chart->addSeries(series);
              chart->setAnimationOptions(QChart::NoAnimation);
              chart->legend()->setVisible(true);
              chart->legend()->setAlignment(Qt::AlignRight);

              auto font = chart->titleFont();
              font.setBold(true);
              chart->setTitleFont(font);
              chart->setTitle(group->getName().c_str());

              auto setActiveTitle = [chart, group](QBarSet* bar, bool active) {
                if (active) {
                  chart->setTitle(bar->label()
                                  + " = "
                                  + QString::number(bar->sum(), 'f', 2));
                } else {
                  chart->setTitle(group->getName().c_str());
                }
              };

              // lambda that activates/inactivates bars
              auto setBarActive = [](QBarSet* bar, bool active) {
                auto color = bar->color();
                if (active) {
                  color.setAlphaF(1.0);
                } else {
                  color.setAlphaF(0.25);
                }
                bar->setColor(color);
              };

              // lambda that activates/inactivates legend markers
              auto setMarkerActive = [](QLegendMarker* marker, bool active) {
                auto labelColor = marker->labelBrush().color();
                if (active) {
                  labelColor.setAlphaF(1.0);
                } else {
                  labelColor.setAlphaF(0.25);
                }
                marker->setLabelBrush(labelColor);
              };

              // lambda that fades out bars
              auto fadeBars = [setBarActive, setMarkerActive, setActiveTitle]
                  (QLegendMarker* marker,
                   const QList<QLegendMarker*>& markers,
                   const QHorizontalStackedBarSeries* series,
                   bool hovering) {
                      if (!series)
                        return;
                      const auto barSets = series->barSets();
                      QBarSet* activeBar = nullptr;
                      for (auto bar : barSets) {
                        bool match = bar->label() == marker->label();
                        setBarActive(bar, !hovering || match);
                        if (match) {
                          activeBar = bar;
                          setActiveTitle(bar, hovering);
                        }
                      }
                      for (auto marker : markers) {
                        bool match = false;
                        if (activeBar != nullptr)
                          match = activeBar->label() == marker->label();
                        setMarkerActive(marker, !hovering || match);
                      }
                  };

              // lambda that fades out legend markers
              auto fadeMarkers = [setBarActive, setMarkerActive, setActiveTitle]
                  (QBarSet* bar,
                   const QList<QBarSet*>& barSets,
                   const QLegend* legend,
                   bool hovering) {
                      if (!legend)
                        return;
                      const auto markers = legend->markers();
                      QLegendMarker* activeMarker = nullptr;
                      for (auto marker : markers) {
                        bool match = bar->label() == marker->label();
                        setMarkerActive(marker, !hovering || match);
                        if (match)
                          activeMarker = marker;
                      }
                      for (auto bar : barSets) {
                        bool match = false;
                        if (activeMarker != nullptr)
                          match = bar->label() == activeMarker->label();
                        setBarActive(bar, !hovering || match);
                        if (match)
                          setActiveTitle(bar, hovering);
                      }
                  };

              const auto markers = chart->legend()->markers();
              for (auto marker : markers) {
                  connect(marker,
                          &QLegendMarker::hovered,
                          this,
                          [fadeBars, marker, markers, series](bool status) {
                              fadeBars(marker, markers, series, status);
                          });
              }
              const auto barSets = series->barSets();
              for (auto bar : barSets) {
                  auto legend = chart->legend();
                  connect(bar,
                          &QBarSet::hovered,
                          this,
                          [fadeMarkers, bar, barSets, legend](bool status) {
                              fadeMarkers(bar, barSets, legend, status);
                          });

                  // the logic below assigns a nice hue to the bars
                  QColor color;
                  if (bar->sum() > 0) {
                      double f = bar->sum() / sumPositiveWeights;
                      double f_inv = 1 - f;
                      color = QColor(int(200 * f_inv) - int(200 * f),
                                     int(200 * f_inv),
                                     255);
                  } else {
                      double f = bar->sum() / sumNegativeWeights;
                      double f_inv = 1 - f;
                      color = QColor(255,
                                     int(200 * f_inv) - int(200 * f),
                                     int(200 * f_inv));
                  }
                  bar->setColor(color);
                  bar->setBorderColor(QColor("white"));
              }

              QChartView *chartView = new QChartView(chart);
              chartView->setRenderHint(QPainter::Antialiasing);
              QDialog inferenceVisual(this);
              auto layout = new QVBoxLayout;
              layout->addWidget(chartView);
              inferenceVisual.setLayout(layout);
              inferenceVisual.resize(QSize(900, 400));
              inferenceVisual.setWindowTitle("Classification inference");
              inferenceVisual.exec();
          });

  QAction *selectedAction = menu.exec(event->globalPos());
}

void TableDockWidget::focusInEvent(QFocusEvent *event) {
  if (event->gotFocus()) {
    pal.setColor(QPalette::Background, QColor(255, 255, 255, 100));
    setPalette(pal);
    _mainwindow->setActiveTable(this);
  }
}

void TableDockWidget::focusOutEvent(QFocusEvent *event) {
  if (event->lostFocus()) {
    pal.setColor(QPalette::Background, QColor(170, 170, 170, 100));
    setPalette(pal);
  }
}

void TableDockWidget::saveModel() {

  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Classification Model to a File"));
  if (fileName.isEmpty())
    return;

  if (!fileName.endsWith(".model", Qt::CaseInsensitive))
    fileName = fileName + ".model";

  Classifier *clsf = _mainwindow->getClassifier();
  if (clsf != NULL) {
    clsf->saveModel(fileName.toStdString());
  }

  if (clsf) {
    vector<PeakGroup *> groups;
    for (int i = 0; i < allgroups.size(); i++)
      if (allgroups[i].userLabel() == 'g' || allgroups[i].userLabel() == 'b')
        groups.push_back(&allgroups[i]);
    clsf->saveFeatures(groups, fileName.toStdString() + ".csv");
  }
}

void TableDockWidget::findMatchingCompounds() {
  // matching compounds
  MassCutoff *massCutoff = _mainwindow->getUserMassCutoff();
  float ionizationMode = _mainwindow->mavenParameters->ionizationMode;
  for (int i = 0; i < allgroups.size(); i++) {
    PeakGroup &g = allgroups[i];
    int charge = _mainwindow->mavenParameters->getCharge(g.getCompound());
    QSet<Compound *> compounds =
        _mainwindow->massCalcWidget->findMathchingCompounds(g.meanMz,
                                                            massCutoff,
                                                            charge);
    if (compounds.size() > 0)
      Q_FOREACH (Compound *c, compounds) {
        g.tagString += " |" + c->name();
        break;
      }
  }
  updateTable();
}

void TableDockWidget::writeQEInclusionList(QString filename) {
  QFile file(filename);
  if (!file.open(QFile::WriteOnly)) {
    QErrorMessage errDialog(this);
    errDialog.showMessage("File open " + filename + " failed");
    return; // error
  }

  QList<shared_ptr<PeakGroup>> selected = getSelectedGroups();

  float window = 1.5;
  int polarity = _mainwindow->mavenParameters->ionizationMode;
  QTextStream out(&file);
  for (auto g : selected) {
    out << g->meanMz << ",";
    polarity > 0 ? out << "Positive," : out << "Negative,";
    out << g->meanRt - window << ",";
    out << g->meanRt + window << ",";
    out << 25 << ","; // default CE set to 25
    out << 2 << ",";
    out << QString(g->getName().c_str());
    out << endl;
  }
  file.close();
}

void TableDockWidget::writeMascotGeneric(QString filename) {
  QFile file(filename);
  if (!file.open(QFile::WriteOnly)) {
    QErrorMessage errDialog(this);
    errDialog.showMessage("File open " + filename + " failed");
    return; // error
  }

  QList<shared_ptr<PeakGroup>> selected = getSelectedGroups();
  QTextStream out(&file);
  for (auto g : selected) {
    Scan *cons = g->getAverageFragmentationScan(
          _mainwindow->mavenParameters->fragmentTolerance);

    if (cons) {
      string scandata = cons->toMGF();
      out << scandata.c_str();
    }
  }
  file.close();
}

void TableDockWidget::clearClusters()
{
  for (auto group : _topLevelGroups)
    group->clusterId = 0;
  showAllGroups();
}

void TableDockWidget::clusterGroups()
{
  sort(_topLevelGroups.begin(),
       _topLevelGroups.end(),
       [](shared_ptr<PeakGroup> a, shared_ptr<PeakGroup> b) {
           return a->meanRt < b->meanRt;
       });
  qDebug() << "Clustering…";
  int clusterId = 0;

  double maxRtDiff = clusterDialog->maxRtDiff_2->value();
  double minSampleCorrelation = clusterDialog->minSampleCorr->value();
  double minRtCorrelation = clusterDialog->minRt->value();
  MassCutoff *massCutoff = _mainwindow->getUserMassCutoff();

  vector<mzSample *> samples = _mainwindow->getSamples();

  // clear cluster information
  for (auto group : _topLevelGroups)
    group->clusterId = 0;

  map<int, shared_ptr<PeakGroup>> parentGroups;
  for (int i = 0; i < _topLevelGroups.size(); ++i) {
    auto group1 = _topLevelGroups[i];
    if (group1->clusterId == 0) {
      // create new cluster
      group1->clusterId = ++clusterId;
      parentGroups[clusterId] = group1;
    }

    // cluster parent
    shared_ptr<PeakGroup> parent = parentGroups[clusterId];

    mzSample *largestSample = NULL;
    double maxIntensity = 0;

    for (int i = 0; i < group1->peakCount(); i++) {
      mzSample *sample = group1->peaks[i].getSample();
      if (group1->peaks[i].peakIntensity > maxIntensity)
        largestSample = sample;
    }

    if (largestSample == NULL)
      continue;
    vector<float> peakIntensityA =
        group1->getOrderedIntensityVector(samples, PeakGroup::AreaTop);

    for (auto group2 : _topLevelGroups) {
      if (group2->clusterId > 0)
        continue;

      // retention time distance
        float rtdist = abs(parent->meanRt - group2->meanRt);
      if (rtdist > maxRtDiff * 2)
        continue;

      // retention time overlap
      float rtoverlap = mzUtils::checkOverlap(group1->minRt, group1->maxRt,
                                              group2->minRt, group2->maxRt);
      if (rtoverlap < 0.1)
        continue;

      // peak intensity correlation
      vector<float> peakIntensityB =
          group2->getOrderedIntensityVector(samples, PeakGroup::AreaTop);
      float cor = correlation(peakIntensityA, peakIntensityB);
      if (cor < minSampleCorrelation)
        continue;

      // peak shape correlation
      float cor2 = largestSample->correlation(group1->meanMz,
                                              group2->meanMz,
                                              massCutoff,
                                              group1->minRt,
                                              group1->maxRt,
                                              _mainwindow->mavenParameters->eicType,
                                              _mainwindow->mavenParameters->filterline);
      if (cor2 < minRtCorrelation)
        continue;

      // passed all the filters, group1 and group2 into a single metagroup
      group2->clusterId = group1->clusterId;
    }
    if (i % 10 == 0)
      _mainwindow->setProgressBar("Clustering…",
                                  i + 1,
                                  _topLevelGroups.size());
  }

  _mainwindow->setProgressBar("Clustering done!",
                              _topLevelGroups.size(),
                              _topLevelGroups.size());
  showAllGroups();
}

void TableDockWidget::setupFiltersDialog() {

  filtersDialog = new QDialog(this);
  QVBoxLayout *layout = new QVBoxLayout(filtersDialog);

  sliders["PeakQuality"] = new QHistogramSlider(this);
  sliders["PeakIntensity"] = new QHistogramSlider(this);
  sliders["PeakWidth"] = new QHistogramSlider(this);
  sliders["GaussianFit"] = new QHistogramSlider(this);
  sliders["PeakAreaFractional"] = new QHistogramSlider(this);
  sliders["PeakAreaTop"] = new QHistogramSlider(this);
  sliders["S/N Ratio"] = new QHistogramSlider(this);
  sliders["GoodPeakCount"] = new QHistogramSlider(this);

  Q_FOREACH (QHistogramSlider *slider, sliders) {
    connect(slider, SIGNAL(minBoundChanged(double)), SLOT(filterPeakTable()));
    connect(slider, SIGNAL(maxBoundChanged(double)), SLOT(filterPeakTable()));
    layout->addWidget(slider);
  }

  filtersDialog->setLayout(layout);
}

void TableDockWidget::showFiltersDialog() {
  filtersDialog->setVisible(!filtersDialog->isVisible());
  if (filtersDialog->isVisible() == false)
    return;

  Q_FOREACH (QHistogramSlider *slider, sliders) { slider->clearData(); }

  for (int i = 0; i < 100; i++)
    sliders["PeakQuality"]->addDataPoint(QPointF((float)i / 100.00, i));
  for (int i = 0; i < 50; i++)
    sliders["GoodPeakCount"]->addDataPoint(QPointF(i, 5));
  for (int i = 0; i < 100; i++)
    sliders["PeakIntensity"]->addDataPoint(QPointF(i, i));
  sliders["PeakQuality"]->setPrecision(2);
  Q_FOREACH (QHistogramSlider *slider, sliders)
    slider->recalculatePlotBounds();
}

void TableDockWidget::filterPeakTable() { updateTable(); }

void TableDockWidget::showFocusedGroups() {
  int N = treeWidget->topLevelItemCount();
  for (int i = 0; i < N; i++) {
    QTreeWidgetItem *item = treeWidget->topLevelItem(i);
    QVariant v = item->data(0, Qt::UserRole);
    shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
    if (group != nullptr && group->isFocused)
      item->setHidden(false);
    else
      item->setHidden(true);

    if (item->text(0).startsWith("Cluster")) {
      bool showParentFlag = false;
      for (int j = 0; j < item->childCount(); j++) {
        QVariant v = (item->child(j))->data(0, Qt::UserRole);
        shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
        if (group != nullptr && group->isFocused) {
          item->setHidden(false);
          showParentFlag = true;
        } else
          item->setHidden(true);
      }
      if (showParentFlag)
        item->setHidden(false);
    }
  }
}

void TableDockWidget::clearFocusedGroups() {
  for (auto group : _topLevelGroups)
    group->isFocused = false;
}

void TableDockWidget::dragEnterEvent(QDragEnterEvent *event) {
  Q_FOREACH (QUrl url, event->mimeData()->urls()) {
    std::cerr << "dragEnterEvent:" << url.toString().toStdString() << endl;
    if (url.toString() == "ok") {
      event->acceptProposedAction();
      return;
    } else {
      return;
    }
  }
}

void TableDockWidget::dropEvent(QDropEvent *event) {
  Q_FOREACH (QUrl url, event->mimeData()->urls()) {
    std::cerr << "dropEvent:" << url.toString().toStdString() << endl;
  }
}

void TableDockWidget::switchTableView() {
  viewType == groupView ? viewType = peakView : viewType = groupView;
  setupPeakTable();
  showAllGroups();
  updateTable();
}

int TableDockWidget::getTargetedGroupCount()
{
  return _targetedGroups;
}

int TableDockWidget::getLabeledGroupCount()
{
  return _labeledGroups;
}

QString TableDockWidget::getTitleForId(int tableId)
{
  return _idTitleMap.value(tableId, QString(""));
}

void TableDockWidget::setTitleForId(int tableId, const QString& tableTitle)
{
  if (_idTitleMap.contains(tableId))
      return;

  QString title = tableTitle;
  if (tableId == -1) {
      title = "Scatterplot Peak Table";
  } else if (tableId == 0) {
      title = "Bookmark Table";
  } else if (title.isEmpty()) {
      title = QString("Peak Table ") + QString::number(tableId);
  } else {
    QString expression("^(%1) (?:\\((\\d+)\\)$)");
    QRegularExpression re(expression.arg(tableTitle));
    bool titleExists = false;
    int highestCounter = 0;
    for (auto& existingTitle : _idTitleMap.values()) {
      QRegularExpressionMatch match = re.match(existingTitle);
      if (match.hasMatch()) {
        titleExists = true;
        int currentCounter = match.captured(2).toInt();
        highestCounter = currentCounter > highestCounter ? currentCounter
                                                         : highestCounter;
      } else if (existingTitle == tableTitle) {
        titleExists = true;
      }
    }
    if (titleExists)
      title = QString("%1 (%2)").arg(tableTitle).arg(highestCounter + 1);
  }
  _idTitleMap.insert(tableId, title);
}

int TableDockWidget::lastTableId()
{
  if (!_idTitleMap.isEmpty())
      return _idTitleMap.lastKey();
  return -1;
}

void TableDockWidget::setDefaultStyle(bool isActive)
{
    QString style = "";
    style += "QLabel       { margin:        0px 6px;             }";
    style += "QToolBar     { background:    white;               }";
    style += "QToolBar     { border:        none;                }";
    style += "QToolBar     { border-bottom: 1px solid lightgray; }";
    style += "QTreeView    { border:        none;                }";
    if (isActive) {
        style += "QToolBar { background:    #ebeafa;             }";
    }
    setStyleSheet(style);
}

QWidget *TableToolBarWidgetAction::createWidget(QWidget *parent) {
  if (btnName == "titlePeakTable") {

    td->titlePeakTable = new QLabel(parent);
    QFont font;
    font.setPointSize(14);
    td->titlePeakTable->setFont(font);

    td->titlePeakTable->setText(TableDockWidget::getTitleForId(td->tableId));

    td->titlePeakTable->setStyleSheet("font-weight: bold; color: black");
    td->setWindowTitle(td->titlePeakTable->text());

    return td->titlePeakTable;
  } else if (btnName == "btnSwitchView") {

    QToolButton *btnSwitchView = new QToolButton(parent);
    btnSwitchView->setIcon(QIcon(rsrcPath + "/flip.png"));
    btnSwitchView->setToolTip("Switch between Group and Peak Views");
    connect(btnSwitchView, SIGNAL(clicked()), td, SLOT(switchTableView()));
    return btnSwitchView;
  } else if (btnName == "btnGroupCSV") {

    QToolButton *btnGroupCSV = new QToolButton(parent);

    btnGroupCSV->setIcon(QIcon(rsrcPath + "/exportcsv.png"));
    btnGroupCSV->setToolTip(tr("Export Groups To SpreadSheet (.csv) "));
    btnGroupCSV->setMenu(new QMenu("Export Groups"));
    btnGroupCSV->setPopupMode(QToolButton::InstantPopup);
    QAction *exportSelected =
        btnGroupCSV->menu()->addAction(tr("Export selected groups"));
    QAction *exportAll =
        btnGroupCSV->menu()->addAction(tr("Export all groups"));
    QAction *exportGood =
        btnGroupCSV->menu()->addAction(tr("Export good groups"));
    QAction *excludeBad =
        btnGroupCSV->menu()->addAction(tr("Export excluding bad groups"));
    QAction *exportBad =
        btnGroupCSV->menu()->addAction(tr("Export bad groups"));

    connect(exportSelected, SIGNAL(triggered()), td, SLOT(selectedPeaks()));
    connect(exportSelected,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(exportSelected, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(exportAll, SIGNAL(triggered()), td, SLOT(allPeaks()));
    connect(exportAll, SIGNAL(triggered()), td->treeWidget, SLOT(selectAll()));
    connect(exportAll,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(exportAll, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(exportGood, SIGNAL(triggered()), td, SLOT(goodPeaks()));
    connect(exportGood, SIGNAL(triggered()), td->treeWidget, SLOT(selectAll()));
    connect(exportGood,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(exportGood, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(excludeBad, SIGNAL(triggered()), td, SLOT(excludeBadPeaks()));
    connect(excludeBad, SIGNAL(triggered()), td->treeWidget, SLOT(selectAll()));
    connect(excludeBad,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(excludeBad, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(exportBad, SIGNAL(triggered()), td, SLOT(badPeaks()));
    connect(exportBad, SIGNAL(triggered()), td->treeWidget, SLOT(selectAll()));
    connect(exportBad,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(exportBad, SIGNAL(triggered()), td, SLOT(showNotification()));
    return btnGroupCSV;
  } else if (btnName == "btnSaveJson") {

    QToolButton *btnSaveJson = new QToolButton(parent);
    btnSaveJson->setIcon(QIcon(rsrcPath + "/JSON.png"));
    btnSaveJson->setToolTip(tr("Export EICs to Json (.json)"));
    connect(btnSaveJson, SIGNAL(clicked()), td, SLOT(exportJson()));
    connect(btnSaveJson, SIGNAL(clicked()), td, SLOT(showNotification()));
    return btnSaveJson;
  } else if (btnName == "btnScatter") {

    QToolButton *btnScatter = new QToolButton(parent);
    btnScatter->setIcon(QIcon(rsrcPath + "/scatterplot.png"));
    btnScatter->setToolTip("Show ScatterPlot");
    connect(btnScatter, SIGNAL(clicked()), td, SLOT(showScatterPlot()));
    return btnScatter;
  } else if (btnName == "btnCluster") {

    QToolButton *btnCluster = new QToolButton(parent);
    btnCluster->setIcon(QIcon(rsrcPath + "/cluster.png"));
    btnCluster->setToolTip("Cluster Groups");
    connect(btnCluster, SIGNAL(clicked()), td, SLOT(showClusterDialog()));
    return btnCluster;
  } else if (btnName == "btnGood") {

    QToolButton *btnGood = new QToolButton(parent);
    btnGood->setIcon(QIcon(rsrcPath + "/markgood.png"));
    btnGood->setToolTip("Mark selected group as good");
    connect(btnGood, SIGNAL(clicked()), td, SLOT(markGroupGood()));
    return btnGood;
  } else if (btnName == "btnBad") {

    QToolButton *btnBad = new QToolButton(parent);
    btnBad->setIcon(QIcon(rsrcPath + "/markbad.png"));
    btnBad->setToolTip("Mark selected group as bad");
    connect(btnBad, SIGNAL(clicked()), td, SLOT(markGroupBad()));
    return btnBad;
  } else if (btnName == "btnUnmark") {
    QToolButton *btnUnmark = new QToolButton(parent);
    btnUnmark->setIcon(QIcon(rsrcPath + "/unmark.png"));
    btnUnmark->setToolTip("Unmark selected group from good/bad");
    connect(btnUnmark, SIGNAL(clicked()), td, SLOT(unmarkGroup()));
    return btnUnmark;
  } else if (btnName == "btnHeatmapelete") {

    QToolButton *btnHeatmapelete = new QToolButton(parent);
    btnHeatmapelete->setIcon(QIcon(rsrcPath + "/Delete Group.png"));
    btnHeatmapelete->setToolTip("Delete Group");
    connect(btnHeatmapelete, SIGNAL(clicked()), td, SLOT(deleteSelectedItems()));
    return btnHeatmapelete;
  } else if (btnName == "btnPDF") {

    QToolButton *btnPDF = new QToolButton(parent);
    btnPDF->setIcon(QIcon(rsrcPath + "/PDF.png"));
    btnPDF->setToolTip("Generate PDF Report");
    btnPDF->setMenu(new QMenu("Export Groups"));
    btnPDF->setPopupMode(QToolButton::InstantPopup);
    QAction *exportSelected =
        btnPDF->menu()->addAction(tr("Export selected groups"));
    QAction *exportAll =
        btnPDF->menu()->addAction(tr("Export all groups"));

    connect(exportSelected, SIGNAL(triggered()), td, SLOT(selectedPeakSet()));
    connect(exportSelected, SIGNAL(triggered()), td, SLOT(printPdfReport()));
    connect(exportSelected, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(exportAll, SIGNAL(triggered()), td, SLOT(wholePeakSet()));
    connect(exportAll, SIGNAL(triggered()), td, SLOT(printPdfReport()));
    connect(exportAll, SIGNAL(triggered()), td, SLOT(showNotification()));



    return btnPDF;
  } else if (btnName == "btnX") {

    QToolButton *btnX = new QToolButton(parent);
    btnX->setIcon(td->style()->standardIcon(QStyle::SP_DockWidgetCloseButton));
    connect(btnX, SIGNAL(clicked()), td, SLOT(showDeletionDialog()));
    return btnX;
  } else if (btnName == "btnMin") {

    QToolButton *btnMin = new QToolButton(parent);
    btnMin->setIcon(td->style()->standardIcon(QStyle::SP_TitleBarMinButton));
    connect(btnMin, SIGNAL(clicked()), td, SLOT(hide()));
    return btnMin;
  } else if (btnName == "btnSaveSpectral") {
    QToolButton *btnSaveSpectral = new QToolButton(parent);
    btnSaveSpectral->setIcon(QIcon(rsrcPath + "/exportmsp.png"));
    btnSaveSpectral->setToolTip(tr("Export table as spectral library (.msp)"));
    connect(btnSaveSpectral,
            &QToolButton::clicked,
            td,
            &TableDockWidget::exportSpectralLib);
    return btnSaveSpectral;
  } else if (btnName == "btnEditPeakGroup") {
    QToolButton *btnEditPeakGroup = new QToolButton(parent);
    btnEditPeakGroup->setIcon(QIcon(rsrcPath + "/editPeakGroup.png"));
    btnEditPeakGroup->setToolTip("Edit the peak area or baseline for individual peaks of the selected peak-group.");
    connect(btnEditPeakGroup,
            &QToolButton::clicked,
            td,
            &TableDockWidget::editSelectedPeakGroup);
    return btnEditPeakGroup;
  } else if (btnName == "btnSettingsLog") {
    QToolButton *btnSettingsLog = new QToolButton(parent);
    btnSettingsLog->setIcon(QIcon(rsrcPath + "/settingsLog.png"));
    btnSettingsLog->setToolTip("Show a log of settings that were used while integrating the selected peak-group.");
    connect(btnSettingsLog,
            &QToolButton::clicked,
            td,
            &TableDockWidget::showIntegrationSettings);
    return btnSettingsLog;
  } else {
    return NULL;
  }
}

PeakTableDockWidget::PeakTableDockWidget(MainWindow *mw,
                                         const QString& tableTitle)
  : TableDockWidget(mw) {

  _mainwindow = mw;
  auto lastId = lastTableId();
  tableId = ++lastId;
  setTitleForId(tableId, tableTitle);

  toolBar = new QToolBar(this);
  toolBar->setFloatable(false);
  toolBar->setMovable(false);
  toolBar->setIconSize(QSize(24, 24));

  QWidgetAction *titlePeakTable =
      new TableToolBarWidgetAction(toolBar, this, "titlePeakTable");
  QWidgetAction *btnSwitchView =
      new TableToolBarWidgetAction(toolBar, this, "btnSwitchView");
  QWidgetAction *btnGroupCSV =
      new TableToolBarWidgetAction(toolBar, this, "btnGroupCSV");
  QWidgetAction *btnSaveJson =
      new TableToolBarWidgetAction(toolBar, this, "btnSaveJson");
  QWidgetAction *btnSaveSpectral =
      new TableToolBarWidgetAction(toolBar, this, "btnSaveSpectral");
  QWidgetAction *btnScatter =
      new TableToolBarWidgetAction(toolBar, this, "btnScatter");
  QWidgetAction *btnCluster =
      new TableToolBarWidgetAction(toolBar, this, "btnCluster");
  QWidgetAction *btnGood =
      new TableToolBarWidgetAction(toolBar, this, "btnGood");
  QWidgetAction *btnBad =
      new TableToolBarWidgetAction(toolBar, this, "btnBad");
  QWidgetAction *btnUnmark =
      new TableToolBarWidgetAction(toolBar, this, "btnUnmark");
  QWidgetAction *btnHeatmapelete =
      new TableToolBarWidgetAction(toolBar, this, "btnHeatmapelete");
  QWidgetAction *btnEditPeakGroup =
      new TableToolBarWidgetAction(toolBar, this, "btnEditPeakGroup");
  QWidgetAction *btnSettingsLog =
      new TableToolBarWidgetAction(toolBar, this, "btnSettingsLog");
  QWidgetAction *btnPDF = new TableToolBarWidgetAction(toolBar, this, "btnPDF");
  QWidgetAction *btnX = new TableToolBarWidgetAction(toolBar, this, "btnX");
  QWidgetAction *btnMin = new TableToolBarWidgetAction(toolBar, this, "btnMin");

  QWidget *spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

  MultiSelectComboBox *legend = new MultiSelectComboBox(this);
  legend->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  auto labelsForLegend = TableDockWidget::labelsForLegend();
  auto iconsForLegend = TableDockWidget::iconsForLegend();
  for (const auto& label : labelsForLegend) {
    auto type = labelsForLegend.key(label);
    auto icon = iconsForLegend.value(type);
    legend->addItem(icon, label);
  }
  legend->selectAll();
  setLegend(legend);
  connect(legend,
          &MultiSelectComboBox::selectionChanged,
          this,
          &TableDockWidget::filterForSelectedLabels);

  toolBar->addAction(titlePeakTable);
  toolBar->addSeparator();
  toolBar->addWidget(new QLabel("Labels"));
  toolBar->addWidget(legend);
  toolBar->addSeparator();
  toolBar->addAction(btnSwitchView);
  toolBar->addAction(btnGood);
  toolBar->addAction(btnBad);
  toolBar->addAction(btnUnmark);
  toolBar->addAction(btnHeatmapelete);

  toolBar->addSeparator();
  toolBar->addAction(btnEditPeakGroup);
  toolBar->addAction(btnSettingsLog);

  toolBar->addSeparator();
  toolBar->addAction(btnScatter);
  toolBar->addAction(btnCluster);

  toolBar->addSeparator();
  toolBar->addAction(btnPDF);
  toolBar->addAction(btnGroupCSV);
  toolBar->addAction(btnSaveJson);
  toolBar->addAction(btnSaveSpectral);
  toolBar->addWidget(spacer);
  toolBar->addAction(btnMin);
  toolBar->addAction(btnX);

  setTitleBarWidget(toolBar); 

  connect(this,
          &PeakTableDockWidget::unSetFromEicWidget,
          _mainwindow->getEicWidget(),
          &EicWidget::unSetPeakTableGroup);

  deletionDialog = new PeakTableDeletionDialog(this);
}

PeakTableDockWidget::~PeakTableDockWidget() {
  toolBar->clear();
  delete toolBar;
}

void PeakTableDockWidget::destroy() {

  cleanUp();
  deleteLater();
  _mainwindow->removePeaksTable(this);
}

void PeakTableDockWidget::deleteAll()
{
  TableDockWidget::deleteAll();
  destroy();
}

void PeakTableDockWidget::cleanUp()
{
  if (treeWidget->currentItem()) {
    emit unSetFromEicWidget(
          treeWidget->currentItem()->data(0, Qt::UserRole)
              .value<shared_ptr<PeakGroup>>());
  }
  _mainwindow->ligandWidget->resetColor();
}

void PeakTableDockWidget::showDeletionDialog() {
  deletionDialog->show();
}

BookmarkTableDockWidget::BookmarkTableDockWidget(MainWindow *mw) : TableDockWidget(mw) {
  _mainwindow = mw;
  tableId = 0;
  setTitleForId(0);

  toolBar = new QToolBar(this);
  toolBar->setFloatable(false);
  toolBar->setMovable(false);
  toolBar->setIconSize(QSize(24, 24));
  btnMerge = new QToolButton(toolBar);
  btnMerge->setIcon(QIcon(rsrcPath + "/merge.png"));
  btnMerge->setToolTip("Merge Groups With");
  btnMergeMenu = new QMenu("Merge Groups");
  btnMerge->setMenu(btnMergeMenu);
  btnMerge->setPopupMode(QToolButton::InstantPopup);
  connect(btnMergeMenu,
          SIGNAL(aboutToShow()),
          SLOT(showMergeTableOptions()));
  connect(btnMergeMenu,
          SIGNAL(triggered(QAction *)),
          SLOT(mergeGroupsIntoPeakTable(QAction *)));

  QWidgetAction *titlePeakTable =
      new TableToolBarWidgetAction(toolBar, this, "titlePeakTable");
  QWidgetAction *btnSwitchView =
      new TableToolBarWidgetAction(toolBar, this, "btnSwitchView");
  QWidgetAction *btnGroupCSV =
      new TableToolBarWidgetAction(toolBar, this, "btnGroupCSV");
  QWidgetAction *btnSaveJson =
      new TableToolBarWidgetAction(toolBar, this, "btnSaveJson");
  QWidgetAction *btnSaveSpectral =
      new TableToolBarWidgetAction(toolBar, this, "btnSaveSpectral");
  QWidgetAction *btnScatter =
      new TableToolBarWidgetAction(toolBar, this, "btnScatter");
  QWidgetAction *btnCluster =
      new TableToolBarWidgetAction(toolBar, this, "btnCluster");
  QWidgetAction *btnGood =
      new TableToolBarWidgetAction(toolBar, this, "btnGood");
  QWidgetAction *btnBad =
      new TableToolBarWidgetAction(toolBar, this, "btnBad");
  QWidgetAction *btnUnmark =
      new TableToolBarWidgetAction(toolBar, this, "btnUnmark");
  QWidgetAction *btnHeatmapelete =
      new TableToolBarWidgetAction(toolBar, this, "btnHeatmapelete");
  QWidgetAction *btnEditPeakGroup =
      new TableToolBarWidgetAction(toolBar, this, "btnEditPeakGroup");
  QWidgetAction *btnSettingsLog =
      new TableToolBarWidgetAction(toolBar, this, "btnSettingsLog");
  QWidgetAction *btnPDF = new TableToolBarWidgetAction(toolBar, this, "btnPDF");
  QWidgetAction *btnMin = new TableToolBarWidgetAction(toolBar, this, "btnMin");

  QWidget *spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

  toolBar->addAction(titlePeakTable);
  toolBar->addAction(btnSwitchView);
  toolBar->addAction(btnGood);
  toolBar->addAction(btnBad);
  toolBar->addAction(btnUnmark);
  toolBar->addAction(btnHeatmapelete);
  toolBar->addWidget(btnMerge);

  toolBar->addSeparator();
  toolBar->addAction(btnEditPeakGroup);
  toolBar->addAction(btnSettingsLog);

  toolBar->addSeparator();
  toolBar->addAction(btnScatter);
  toolBar->addAction(btnCluster);

  toolBar->addSeparator();
  toolBar->addAction(btnPDF);
  toolBar->addAction(btnGroupCSV);
  toolBar->addAction(btnSaveJson);
  toolBar->addAction(btnSaveSpectral);
  toolBar->addWidget(spacer);
  toolBar->addAction(btnMin);

  setTitleBarWidget(toolBar);
}

BookmarkTableDockWidget::~BookmarkTableDockWidget() {
  toolBar->clear();
  delete toolBar;
}

void BookmarkTableDockWidget::showMergeTableOptions() {
  QList<QPointer<TableDockWidget>> peaksTableList =
      _mainwindow->getPeakTableList();
  int n = peaksTableList.size();
  btnMergeMenu->clear();
  mergeAction.clear();
  for (int i = 0; i < n; i++) {
    mergeAction.insert(
        btnMergeMenu->addAction(peaksTableList[i]->titlePeakTable->text()),
        peaksTableList[i]->tableId);
  }
}

void BookmarkTableDockWidget::showMsgBox(bool check, int tableNo) {

  QMessageBox *msgBox = new QMessageBox(this);
  msgBox->setAttribute(Qt::WA_DeleteOnClose);
  msgBox->setStandardButtons(QMessageBox::Ok);

  if (check) {
    msgBox->setIconPixmap(QPixmap(rsrcPath + "/success.png"));
    msgBox->setText("Successfully Merged from Bookmark Table to Table " +
                    QString::number(tableNo) + "   ");
  } else {
    msgBox->setIcon(QMessageBox::Warning);
    if (tableNo == -1)
      msgBox->setText("Error while merging");
    else
      msgBox->setText("Error while merging from Bookmark Table to Table " +
                      QString::number(tableNo) + "   ");
  }

  msgBox->open();
}

void BookmarkTableDockWidget::mergeGroupsIntoPeakTable(QAction *action)
{
    QList<QPointer<TableDockWidget>> peaksTableList = _mainwindow->getPeakTableList();
    int j = mergeAction.value(action, -1);

    //check if action exists
    if (j == -1) {
        showMsgBox(false, j);
        return;
    }

    //return if no bookmarked groups or peak tables
    if (_topLevelGroups.isEmpty() || peaksTableList.isEmpty()) {
        showMsgBox(true, j);
        return;
    }

    //find table to merge with
    TableDockWidget* peakTable = nullptr;
    for (auto table : peaksTableList) {
        if (table->tableId == j) {
            peakTable = table;
            break;
        }
    }

    //return if peak table not found
    if (peakTable == nullptr) {
        showMsgBox(false, j);
        return;
    }

    int initialSize = peakTable->topLevelGroupCount();
    int finalSize = _topLevelGroups.size() + initialSize;
    for (auto group : _topLevelGroups)
        peakTable->addPeakGroup(group.get());

    deleteAll();
    peakTable->showAllGroups();
    showAllGroups();

    bool merged = true;

    if (finalSize == peakTable->topLevelGroupCount())
        merged = true;
    else
        merged = false;
    
    showMsgBox(merged, j);
    QString status = merged? "Success" : "Failure";
    _mainwindow->getAnalytics()->hitEvent("Bookmark Table",
                                          "Merge Table",
                                          status);
}

void BookmarkTableDockWidget::showSameGroup(QPair<int, int> sameMzRtGroupIndexHash) {

  QMessageBox prompt(_mainwindow);
  prompt.setWindowTitle("Bookmarking possible duplicate");

  QString compoundList = "";
  for (QString groupName : sameMzRtGroups[sameMzRtGroupIndexHash])
    compoundList += "<li>" + groupName;
  auto htmlText = QString("<p>The table already contains one or more "
                          "peak-groups having m/z and RT values same as the "
                          "group being currently bookmarked:</p>"
                          "<ul>%1</ul>").arg(compoundList);
  htmlText += "<p>Do you want to add it anyway?</p>";
  prompt.setText(htmlText);

  auto discardButton = prompt.addButton("Discard", QMessageBox::RejectRole);
  auto addButton = prompt.addButton("Add", QMessageBox::AcceptRole);
  prompt.setDefaultButton(discardButton);

  prompt.exec();
  if (prompt.clickedButton() == addButton) {
    addSameMzRtGroup = true;
  } else {
    addSameMzRtGroup = false;
  }
}

bool BookmarkTableDockWidget::hasPeakGroup(PeakGroup *group) {

  int intMz = group->meanMz * 1e5;
  int intRt = group->meanRt * 1e5;
  QPair<int, int> sameMzRtGroupIndexHash(intMz, intRt);
  QString compoundName = QString::fromStdString(group->getName());

  if (topLevelGroupCount() == 0 ||
      sameMzRtGroups[sameMzRtGroupIndexHash].size() == 0) {
    /**
     * add this group corresponding compound name to list of string which all
     * share same mz and rt value. Both mz and rt are hashed in
     * sameMzRtGroupIndexHash.
     */
    sameMzRtGroups[sameMzRtGroupIndexHash].append(compoundName);
  }

  for (auto g : _topLevelGroups) {
    if (mzUtils::almostEqual(group->meanMz, g->meanMz)
        && mzUtils::almostEqual(group->meanRt, g->meanRt)) {
      addSameMzRtGroup = false;
      showSameGroup(sameMzRtGroupIndexHash);

      if (addSameMzRtGroup) {
        /**
         * if user pressed <save> button <addSameMzRtGroup> will be set to true
         * otherwise false. if it is true, this groups corresponding compound
         * name will we saved by an index of sameMzRtGroupIndexHash to show all
         * these string next time if a group with same rt and mz is encountered.
         */
        sameMzRtGroups[sameMzRtGroupIndexHash].append(compoundName);

        /**
         * return false such that calling method will add this group to
         * bookmarked group.
         */
        return false;
      }

      return true;
    }
  }

  return false;
}

void BookmarkTableDockWidget::deleteGroup(PeakGroup *groupX) {
  if (!groupX)
    return;

  int pos = -1;
  for (int i = 0; i < _topLevelGroups.size(); ++i) {
    auto group = _topLevelGroups[i];
    if (group.get() == groupX) {
      pos = i;
      break;
    }
  }
  if (pos == -1)
    return;

  QTreeWidgetItemIterator it(treeWidget);
  while (*it) {
    QTreeWidgetItem *item = (*it);
    if (item->isHidden()) {
      ++it;
      continue;
    }
    QVariant v = item->data(0, Qt::UserRole);
    shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
    if (group != nullptr and group.get() == groupX) {
        item->setHidden(true);

    if (!group->children.empty())
      _labeledGroups--;
    if (group->getCompound())
      _targetedGroups--;

      	// Deleting
        int posTree = treeWidget->indexOfTopLevelItem(item);
        if (posTree != -1)
    		treeWidget->takeTopLevelItem(posTree);

        /**
         * delete name of compound associated with this group stored in
         * <sameMzRtGroups> with given mz and rt
        */
        int intMz = group->meanMz * 1e5;
        int intRt = group->meanRt * 1e5;
        QPair<int, int> sameMzRtGroupIndexHash(intMz, intRt);
        QString compoundName = QString::fromStdString(groupX->getName());
        if (sameMzRtGroups[sameMzRtGroupIndexHash].contains(compoundName)) {
        	for (int i = 0; i < sameMzRtGroups[sameMzRtGroupIndexHash].size();
            	 ++i)
			{
            	if (sameMzRtGroups[sameMzRtGroupIndexHash][i] == compoundName) {
            	sameMzRtGroups[sameMzRtGroupIndexHash].removeAt(i);
            	break;
          	}
        }
      }

      _topLevelGroups.erase(_topLevelGroups.begin() + pos);
      break;
    }
    ++it;
  }

  for (int i = 0; i < _topLevelGroups.size(); ++i) {
    auto group = _topLevelGroups[i];
    group->groupId = i + 1;
    group->setGroupIdForChildren();
  }
  updateTable();
  updateCompoundWidget();
}

void BookmarkTableDockWidget::markGroupGood() {
  setGroupLabel('g');
  auto currentGroups = getSelectedGroups();
  showNextGroup();
  _mainwindow->autoSaveSignal(currentGroups);
}

void BookmarkTableDockWidget::markGroupBad() {

  setGroupLabel('b');
  auto currentGroups = getSelectedGroups();
  showNextGroup();
  _mainwindow->autoSaveSignal(currentGroups);
}

ScatterplotTableDockWidget::ScatterplotTableDockWidget(MainWindow *mw) :
    TableDockWidget(mw) {
  _mainwindow = mw;
  tableId = -1;
  setTitleForId(tableId);

  toolBar = new QToolBar(this);
  toolBar->setFloatable(false);
  toolBar->setMovable(false);
  toolBar->setIconSize(QSize(24, 24));

  QWidgetAction *titlePeakTable =
      new TableToolBarWidgetAction(toolBar, this, "titlePeakTable");
  QWidgetAction *btnSwitchView =
      new TableToolBarWidgetAction(toolBar, this, "btnSwitchView");
  QWidgetAction *btnGroupCSV =
      new TableToolBarWidgetAction(toolBar, this, "btnGroupCSV");
  QWidgetAction *btnSaveJson =
      new TableToolBarWidgetAction(toolBar, this, "btnSaveJson");
  QWidgetAction *btnSaveSpectral =
      new TableToolBarWidgetAction(toolBar, this, "btnSaveSpectral");
  QWidgetAction *btnCluster =
      new TableToolBarWidgetAction(toolBar, this, "btnCluster");
  QWidgetAction *btnGood =
      new TableToolBarWidgetAction(toolBar, this, "btnGood");
  QWidgetAction *btnBad =
      new TableToolBarWidgetAction(toolBar, this, "btnBad");
  QWidgetAction *btnUnmark =
      new TableToolBarWidgetAction(toolBar, this, "btnUnmark");
  QWidgetAction *btnHeatmapelete =
      new TableToolBarWidgetAction(toolBar, this, "btnHeatmapelete");
  QWidgetAction *btnEditPeakGroup =
      new TableToolBarWidgetAction(toolBar, this, "btnEditPeakGroup");
  QWidgetAction *btnSettingsLog =
      new TableToolBarWidgetAction(toolBar, this, "btnSettingsLog");
  QWidgetAction *btnPDF = new TableToolBarWidgetAction(toolBar, this, "btnPDF");
  QWidgetAction *btnMin = new TableToolBarWidgetAction(toolBar, this, "btnMin");

  QWidget *spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

  toolBar->addAction(titlePeakTable);
  toolBar->addAction(btnSwitchView);
  toolBar->addAction(btnGood);
  toolBar->addAction(btnBad);
  toolBar->addAction(btnUnmark);
  toolBar->addAction(btnHeatmapelete);

  toolBar->addSeparator();
  toolBar->addAction(btnEditPeakGroup);
  toolBar->addAction(btnSettingsLog);

  toolBar->addSeparator();
  toolBar->addAction(btnCluster);

  toolBar->addSeparator();
  toolBar->addAction(btnPDF);
  toolBar->addAction(btnGroupCSV);
  toolBar->addAction(btnSaveJson);
  toolBar->addAction(btnSaveSpectral);
  toolBar->addWidget(spacer);
  toolBar->addAction(btnMin);

  setTitleBarWidget(toolBar);
}

ScatterplotTableDockWidget::~ScatterplotTableDockWidget() {
  toolBar->clear();
  delete toolBar;
}

void ScatterplotTableDockWidget::setupPeakTable() {

  QStringList colNames;

  // Add common columns to the table
  colNames << "Label";
  colNames << "#";
  colNames << "ID";
  colNames << "Observed m/z";
  colNames << "Expected m/z";
  colNames << "rt";

  if (viewType == groupView) {

    // Add group view columns to the table
    colNames << "rt delta";
    colNames << "#peaks";
    colNames << "#good";
    colNames << "Max Width";
    colNames << "Max AreaTop";
    colNames << "Max S/N";
    colNames << "Max Quality";
    colNames << "MS2 Score";
    colNames << "#MS2 Events";
    colNames << "Probability"; // TODO: add this column conditionally
    colNames << "Rank";

    // add scatterplot table columns
    colNames << "Ratio Change";
    colNames << "P-value";
  } else if (viewType == peakView) {
    vector<mzSample *> vsamples = _mainwindow->getVisibleSamples();
    sort(vsamples.begin(), vsamples.end(), mzSample::compSampleOrder);
    for (unsigned int i = 0; i < vsamples.size(); i++) {
      // Add peak view columns to the table
      colNames << QString(vsamples[i]->sampleName.c_str());
    }
  }

  treeWidget->setColumnCount(colNames.size());
  treeWidget->setHeaderLabels(colNames);
  treeWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  treeWidget->header()->adjustSize();
  treeWidget->setSortingEnabled(true);
}
