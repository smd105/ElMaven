#include <algorithm>

#include <QHistogramSlider.h>
#include <qtconcurrentrun.h>

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
#include "groupClassifier.h"
#include "grouprtwidget.h"
#include "groupsettingslog.h"
#include "heatmap.h"
#include "isotopeswidget.h"
#include "jsonReports.h";
#include "ligandwidget.h"
#include "mainwindow.h"
#include "masscalcgui.h"
#include "mavenparameters.h"
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
#include "svmPredictor.h"
#include "tabledockwidget.h";
#include "PeakDetector.h"
#include "background_peaks_update.h"

QMap<int, QString> TableDockWidget::_idTitleMap;

TableDockWidget::TableDockWidget(MainWindow *mw) {
  QDateTime current_time;
  const QString format = "dd-MM-yyyy_hh_mm_ss";
  QString datetimestamp= current_time.currentDateTime().toString(format);
  datetimestamp.replace(" ","_");
  datetimestamp.replace(":","-");
  
  uploadId = datetimestamp+"_Peak_table_"+QString::number(lastTableId());

  writableTempS3Dir = QStandardPaths::writableLocation(
                                                QStandardPaths::QStandardPaths::GenericConfigLocation)
                                                + QDir::separator()
                                                + "tmp_Elmaven_s3_files_"
                                                + QString::number(lastTableId());
  if (!QDir(writableTempS3Dir).exists())
    QDir().mkdir(writableTempS3Dir);

  setAllowedAreas(Qt::AllDockWidgetAreas);
  setFloating(false);
  _mainwindow = mw;
  _labeledGroups = 0;
  _targetedGroups = 0;
  pal = palette();
  setAutoFillBackground(true);
  pal.setColor(QPalette::Background, QColor(170, 170, 170, 100));
  setPalette(pal);
  setDefaultStyle();

  viewType = groupView;
  maxPeaks = 0; //Maximum Number of Peaks in a Group

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

  delete treeWidget;
  QDir qDirS3(writableTempS3Dir);
  if(qDirS3.exists()){
    qDirS3.removeRecursively();
  }

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
  header->setText(9, temp);
}

void TableDockWidget::setupPeakTable() {

  QStringList colNames;

  // Add common coulmns to the Table
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

  int good = 0;
  int bad = 0;
  int total = group->peakCount();
  for (int i = 0; i < group->peakCount(); i++) {
    group->peaks[i].quality > _mainwindow->mavenParameters->minQuality ? good++
                                                                       : bad++;
  }

  QBrush brush = Qt::NoBrush;
  if (good > 0 && group->label == 'b') {
    float incorrectFraction = ((float)good) / total;
    brush = QBrush(QColor::fromRgbF(0.8, 0, 0, incorrectFraction));
  } else if (bad > 0 && group->label == 'g') {
    float incorrectFraction = ((float)bad) / total;
    brush = QBrush(QColor::fromRgbF(0.8, 0, 0, incorrectFraction));
  } else {
    brush = QBrush(QColor::fromRgbF(1.0, 1.0, 1.0, 1.0));
  }
  item->setBackground(0, brush);

  if (group->label == 'g') {
    item->setIcon(0, QIcon(":/images/good.png"));
  } else if (group->label == 'b') {
    item->setIcon(0, QIcon(":/images/bad.png"));
  } else {
    item->setIcon(0, QIcon());
  }

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
  item->setData(0, Qt::UserRole, QVariant::fromValue(group));

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

  if (group->label == 'g')
    item->setIcon(0, QIcon(":/images/good.png"));
  if (group->label == 'b')
    item->setIcon(0, QIcon(":/images/bad.png"));

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

    if (group->changeFoldRatio != 0) {

      item->setText(15, QString::number(group->changeFoldRatio, 'f', 2));
      item->setText(16, QString::number(group->changePValue, 'e', 4));
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

      item->setText(5 + i, QString::number(yvalues[i]));
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
        parents[clusterId]->setText(0, QString("Cluster ") +
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

UploadPeaksToCloudThread::UploadPeaksToCloudThread(PollyIntegration* iPolly)
{
    _pollyintegration = iPolly;
    
};

void UploadPeaksToCloudThread::run()
{
    qDebug() << "Checking for active internet connection..";
    QString status;
    ErrorStatus response = _pollyintegration->activeInternet();
    if (response == ErrorStatus::Failure ||
        response == ErrorStatus::Error) {
        qDebug() << "No internet connection..aborting upload";
        return;
    }
    ErrorStatus uploadStatus = _pollyintegration->UploadPeaksToCloud(sessionId,fileName, filePath);
    if (uploadStatus == ErrorStatus::Failure ||
        uploadStatus == ErrorStatus::Error) {
        qDebug() << "Peaks upload failed...";
        return;
    }
    return;
}

UploadPeaksToCloudThread::~UploadPeaksToCloudThread()
{
}


void TableDockWidget::UploadPeakBatchToCloud(){
    jsonReports=new JSONReports(_mainwindow->mavenParameters);
    QString filePath = writableTempS3Dir + QDir::separator() + uploadId + "_" + QString::number(uploadCount) +  ".json";
    jsonReports->save(filePath.toStdString(),subsetPeakGroups,_mainwindow->getVisibleSamples());

    PollyIntegration* iPolly = _mainwindow->getController()->iPolly;
    UploadPeaksToCloudThread *uploadPeaksToCloudThread = new UploadPeaksToCloudThread(iPolly);
    connect(uploadPeaksToCloudThread, &UploadPeaksToCloudThread::finished, uploadPeaksToCloudThread, &QObject::deleteLater);
    uploadPeaksToCloudThread->sessionId = uploadId;
    uploadPeaksToCloudThread->fileName = uploadId + "_" + QString::number(uploadCount) ;
    uploadPeaksToCloudThread->filePath = filePath;
    uploadPeaksToCloudThread->start();
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

shared_ptr<PeakGroup> TableDockWidget::getSelectedGroup()
{
  QTreeWidgetItem *item = treeWidget->currentItem();
  if (!item)
    return NULL;
  QVariant v = item->data(0, Qt::UserRole);
  shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
  if (group != nullptr) {
    return group;
  } else
    return shared_ptr<PeakGroup>(nullptr);
}

void TableDockWidget::setGroupLabel(char label)
{
  Q_FOREACH (QTreeWidgetItem *item, treeWidget->selectedItems()) {
    if (item) {
      QVariant v = item->data(0, Qt::UserRole);
      shared_ptr<PeakGroup> group = v.value<shared_ptr<PeakGroup>>();
      if (group != nullptr) {
        if (group->label != 'g' && group->label != 'b') {
          numberOfGroupsMarked+=1;
          subsetPeakGroups.push_back(*(group.get()));
        }
        group->setLabel(label);
        if (numberOfGroupsMarked ==10){
          numberOfGroupsMarked = 0;
          Q_EMIT(UploadPeakBatch());
          subsetPeakGroups.clear();
          uploadCount+=1;
        }
      }
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

  menu.exec(event->globalPos());
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

    connect(exportSelected, SIGNAL(triggered()), td, SLOT(selectedPeakSet()));
    connect(exportSelected,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(exportSelected, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(exportAll, SIGNAL(triggered()), td, SLOT(wholePeakSet()));
    connect(exportAll, SIGNAL(triggered()), td->treeWidget, SLOT(selectAll()));
    connect(exportAll,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(exportAll, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(exportGood, SIGNAL(triggered()), td, SLOT(goodPeakSet()));
    connect(exportGood, SIGNAL(triggered()), td->treeWidget, SLOT(selectAll()));
    connect(exportGood,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(exportGood, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(excludeBad, SIGNAL(triggered()), td, SLOT(excludeBadPeakSet()));
    connect(excludeBad, SIGNAL(triggered()), td->treeWidget, SLOT(selectAll()));
    connect(excludeBad,
            SIGNAL(triggered()),
            td,
            SLOT(exportGroupsToSpreadsheet()));
    connect(excludeBad, SIGNAL(triggered()), td, SLOT(showNotification()));

    connect(exportBad, SIGNAL(triggered()), td, SLOT(badPeakSet()));
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

//@Kailash: Put decision sequence/tree for automatic validation here
void TableDockWidget::validateGroup(PeakGroup* grp, QTreeWidgetItem* item)
{
    int mark=0;
    bool decisionConflict=false;
    if (grp != NULL)
    {
        //Disjoint Decision Tree
        //Require improvements

        //Decisions to mark group good
        if (grp->avgPeakQuality > 0.74 && grp->groupQuality > 0.69 && grp->weightedAvgPeakQuality > 0.73) {
            if (mark!=-1) mark=1;
            else decisionConflict=true;
        }
        else if (grp->groupQuality > 0.73) {
            if (mark!=-1) mark=1;
            else decisionConflict=true;
        }
        else if (grp->predictedLabel==1) {
            if (grp->avgPeakQuality > 0.76) {
                if (mark!=-1) mark=1;
                else decisionConflict=true;   
            }

            if (grp->weightedAvgPeakQuality > 0.69) {
                if (mark!=-1) mark=1;
                else decisionConflict=true;
            }
        }
        else if (grp->predictedLabel==0 && grp->avgPeakQuality > 0.72 && grp->weightedAvgPeakQuality > 0.76) {
            if (mark!=-1) mark=1;
            else decisionConflict=true;
        }

        //Decisions to mark group bad
        if (grp->avgPeakQuality < 0.25 && grp->weightedAvgPeakQuality < 0.35) {
            if (mark!=1) mark=-1;
            else decisionConflict=true;
        }

        if (grp->predictedLabel==-1) {
            if (grp->avgPeakQuality < 0.29) {
                if (mark!=1) mark=-1;
                else decisionConflict=true;
            }

            if (grp->weightedAvgPeakQuality < 0.31) {
                if (mark!=1) mark=-1;
                else decisionConflict=true;
            }
        }

        //Decisions to not mark group
        if (grp->peakCount() < int(maxPeaks/4) || grp->peakCount() == 1) decisionConflict=true; //Do not mark if number of peaks in the group is less

        if (abs(grp->avgPeakQuality - grp->weightedAvgPeakQuality) > 0.2) {
            decisionConflict=true;
        }

        if (grp->avgPeakQuality > 0.25 && abs(grp->avgPeakQuality - grp->maxQuality) > 0.3) {
            decisionConflict=true;
        }
        grp->markedGoodByCloudModel = 0;
        grp->markedBadByCloudModel = 0;
        //Call respected functions to mark the groups
        if (mark==1 && !decisionConflict) {
            // markGroupGood(grp, item);
            grp->markedGoodByCloudModel = 1;
        }
        else if (mark==-1 && !decisionConflict) {
            // markGroupBad(grp, item);
            grp->markedBadByCloudModel = 1;
        }
    }
}
