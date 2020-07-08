#include <cmath>
#include "standardincludes.h"
#include "classificationWidget.h"
#include "tabledockwidget.h"
#include "PeakGroup.h"
#include "Compound.h"
#include "plot_axes.h"
#include "mzUtils.h"

ClassificationWidget::ClassificationWidget(TableDockWidget *tabledock)
{
    _inferenceVisual = new QDialog(tabledock);
    _layout = new QVBoxLayout;
    _tableDock = tabledock;
    _scene.setSceneRect(0, 0, 676, 267);
    _sceneView = new QGraphicsView(&_scene);
    _sceneView->setRenderHints(QPainter::Antialiasing);
    _layout->addWidget(_sceneView);
    _inferenceVisual->setLayout(_layout);
    _inferenceVisual->resize(QSize(900, 400));
    _inferenceVisual->setWindowTitle("Classification inference");

    _group = _tableDock->getSelectedGroup();
    _sumNegativeWeights = 0;
    _sumPositiveWeights = 0;
    _absoluteTotalWeight = 0;
    _totalWeight = 0;
    
    initialiseLabelName();
}

void ClassificationWidget::showClassification()
{
    setTitle();

    auto predictionInference = _group->predictionInference();

    int counter = 0;
    int startPosition = _scene.width() - 500;

    // Only top 3 attributes taken in account.
    // getting total weights.
    for (auto it = predictionInference.begin();
         it != predictionInference.end() && counter <  5;
         ++it)
    {
        _sumNegativeWeights += it->first;
        counter++;
    }

    counter = 0;
    for (auto it = predictionInference.rbegin();
         it != predictionInference.rend() && counter < 5;
         ++it)
    {
        _sumPositiveWeights += it->first;
        counter++;
    }

    _absoluteTotalWeight = abs(_sumNegativeWeights) + _sumPositiveWeights;
    _totalWeight = _sumNegativeWeights + _sumPositiveWeights;
    setOutputValue();

    counter = 0;
    //making left side arrows with negatives
    for (auto it = predictionInference.begin();
         it != predictionInference.end() && counter <  5;
         ++it)
    {
        if (counter == 0){
            startPosition = makeArrowForNegatives(it->first, 
                                                  it->second, 
                                                  counter, 
                                                  startPosition);
        }
        else
            startPosition = makeArrowForNegatives(it->first, 
                                                  it->second, 
                                                  counter, 
                                                  startPosition + 4);

        counter++;
    }

    counter = 0;
    //making right side arrows with positives
    for (auto it = predictionInference.rbegin();
         it != predictionInference.rend() && counter < 5;
         ++it)
    {   
        if(counter == 0){
            startPosition = makeArrowForPositives(it->first, 
                                                  it->second, 
                                                  counter, 
                                                  startPosition);
        }
        else
            startPosition = makeArrowForPositives(it->first, 
                                                  it->second, 
                                                  counter, 
                                                  startPosition + 4);
        counter++;
     }
    _inferenceVisual->exec();
}

int ClassificationWidget::makeArrowForNegatives(float shapValue, 
                                                string label, 
                                                int counter, 
                                                int startPosition)
{
    // Setting pens and brush
    QPen pen;
    pen.setWidth(1);
    pen.setColor(Qt::red);
    auto color = pen.color();
    double alpha = 0.8 - (shapValue / _sumNegativeWeights);
    color.setAlphaF(alpha);
    QBrush brush;
    brush.setColor(color);
    brush.setStyle(Qt::SolidPattern);
    pen.setColor(color);

    // Setting font for labelling.
    QFont font = QApplication::font();
    int pxSize = _scene.height() * 0.01;
    if (pxSize < 10)
        pxSize = 10;
    font.setPixelSize(pxSize);
    
    // Calculating percentage for shap value. 
    // and width of the arrow.
    shapValue  = 0.5 + shapValue; // to normalize negative value. as (-0.1 > -0.4).
    float percentage = (abs(shapValue)/_absoluteTotalWeight) * 100;
    int x_displacement = percentage * 4; // for width of arrow.

    if (counter != 4) {
        // for proper arrow shape.
        QPolygonF polygon;
        polygon << QPoint(startPosition, 150);
        polygon << QPoint(startPosition + x_displacement, 150);
        polygon << QPoint(startPosition + x_displacement + 20, 170);
        polygon << QPoint(startPosition + x_displacement, 190);
        polygon << QPoint(startPosition , 190);
        polygon << QPoint(startPosition + 20, 170);
        _scene.addPolygon(polygon, pen, brush);

    } else { 
        // for the 5th arrow, straight line in front.
        x_displacement += 20;
        QPolygonF polygon;
        polygon << QPoint(startPosition, 150);
        polygon << QPoint(startPosition + x_displacement, 150);
        polygon << QPoint(startPosition + x_displacement, 190);
        polygon << QPoint(startPosition , 190);
        polygon << QPoint(startPosition + 20, 170);
        _scene.addPolygon(polygon, pen, brush);
    }

    //Labelings
    QPen Linepen;
    Linepen.setWidth(2);
    Linepen.setColor(color);
    auto line = _scene.addLine(QLineF(startPosition + x_displacement/2 ,190,
                                      startPosition + x_displacement/2 - 150,
                                      220),
                               Linepen);
    line = _scene.addLine(QLineF(startPosition + x_displacement/2 - 150,
                                 220, startPosition + x_displacement/2 - 150,
                                 250 + (4 - counter) * 10),
                          Linepen);


    string shapValueString = mzUtils::float2string(shapValue, 3);
    string changedLabel = _changedLabelName[label];                      
    changedLabel += " = ";
    changedLabel += shapValueString;

    QString peakFeature = QString(changedLabel.c_str());
    QString featureText = tr("%1").arg(peakFeature);

    QGraphicsTextItem* title = _scene.addText(featureText, font);
    title->setPos(startPosition - 150, 
                  250 + (4 - counter) * 10);
    
    return (startPosition + x_displacement);
}

int ClassificationWidget::makeArrowForPositives(float shapValue, 
                                                string label, 
                                                int counter, 
                                                int startPosition)
{
    // Setting pens and brush
    QPen pen;
    pen.setWidth(1);
    pen.setColor(Qt::blue);
    auto color = pen.color();
    double alpha = shapValue / _sumPositiveWeights;
    color.setAlphaF(alpha);
    QBrush brush;
    brush.setColor(color);
    brush.setStyle(Qt::SolidPattern);
    pen.setColor(color);

    // Setting font for labelling.
    QFont font = QApplication::font();
    int pxSize = _scene.height() * 0.01;
    if (pxSize < 10)
        pxSize = 10;
    font.setPixelSize(pxSize);

    // Calculating percentage for shap value. 
    // and width of the arrow.
    float percentage = (shapValue/_absoluteTotalWeight) * 100;
    int x_displacement = percentage * 3; // for width of arrow.

    if (counter == 0) {

        QPolygonF polygon;
        polygon << QPoint(startPosition, 150);
        polygon << QPoint(startPosition + x_displacement, 150);
        polygon << QPoint(startPosition + x_displacement - 20, 170);
        polygon << QPoint(startPosition + x_displacement, 190);
        polygon << QPoint(startPosition , 190);
        _scene.addPolygon(polygon, pen, brush);

    } else {

        QPolygonF polygon;
        polygon << QPoint(startPosition, 150);
        polygon << QPoint(startPosition + x_displacement, 150);
        polygon << QPoint(startPosition + x_displacement - 20, 170);
        polygon << QPoint(startPosition + x_displacement, 190);
        polygon << QPoint(startPosition , 190);
        polygon << QPoint(startPosition - 20, 170);
        _scene.addPolygon(polygon, pen, brush);

    }

    //Labelings
    QPen Linepen;
    Linepen.setWidth(2);
    Linepen.setColor(color);
    auto line = _scene.addLine(QLineF(startPosition + x_displacement/2, 190,
                                      startPosition +x_displacement/2 + 170 ,
                                      220),
                               Linepen);
    line = _scene.addLine(QLineF(startPosition + x_displacement/2 + 170,
                                 220, startPosition + x_displacement/2 + 170 ,
                                 250 + counter * 10),
                          Linepen);


    string shapValueString = mzUtils::float2string(shapValue, 3);
    string changedLabel = _changedLabelName[label];                      
    changedLabel += " = ";
    changedLabel += shapValueString;
    QString peakFeature = QString(changedLabel.c_str());
    QString featureText = tr("%1").arg(peakFeature);

    QGraphicsTextItem* title = _scene.addText(featureText, font);
    title->setHtml(featureText);

    if (featureText.size() < 30)
        title->setPos(startPosition + x_displacement + 40, 250 + counter * 10);
    else if (featureText.size() < 40)
        title->setPos(startPosition + x_displacement, 250 + counter * 10);
    else
        title->setPos(startPosition + x_displacement - changedLabel.size(), 
                      250 + counter * 10);

    return (startPosition + x_displacement); 
}

void ClassificationWidget::setTitle()
{
    QString tagString;
    if (_group != NULL) {
        tagString = QString(_group->getName().c_str());
    } else if (_group->getSlice().compound != NULL) {
        tagString = QString(_group->getSlice().compound->name().c_str());
    } else if (!_group->getSlice().srmId.empty()) {
        tagString = QString(_group->getSlice().srmId.c_str());
    }
    QString titleText = tr("<b>%1</b>").arg(tagString);

    QFont font = QApplication::font();
    int pxSize = _scene.height() * 0.03;
    if (pxSize < 14)
        pxSize = 14;
    if (pxSize > 20)
        pxSize = 20;
    font.setPixelSize(pxSize);

    QGraphicsTextItem* title = _scene.addText(titleText, font);
    title->setHtml(titleText);
    int titleWidth = title->boundingRect().width();
    title->setPos(_scene.width() / 2 - titleWidth / 2, 10);
    title->update();
}

void ClassificationWidget::setOutputValue()
{
    QString tagString;
    tagString = "Output Value = ";
    QString outputString = tr("<b>%1</b>").arg(tagString);
    
    string outputValue = mzUtils::float2string(_totalWeight, 3);

    outputString += QString::fromStdString(outputValue);

    QFont font = QApplication::font();
    int pxSize = _scene.height() * 0.03;
    if (pxSize < 14)
        pxSize = 14;
    if (pxSize > 20)
        pxSize = 20;
    font.setPixelSize(pxSize);

    QGraphicsTextItem* outputTitle = _scene.addText(outputString, font);
    outputTitle->setHtml(outputString);
    int titleWidth = outputTitle->boundingRect().width();
    outputTitle->setPos(_scene.width() / 2 - titleWidth / 2, 100);
    outputTitle->update();
}

void ClassificationWidget::initialiseLabelName()
{
    _changedLabelName["SIGNAL_BASELINE_MEAN"] = "Mean of signal to baseline ratio";
    _changedLabelName["SIGNAL_BASELINE_STD"] = "Std of  signal to baseline ratio";
    _changedLabelName["SIGNAL_BASELINE_MIN"] = "Minimum signal to baseline ratio";
    _changedLabelName["SIGNAL_BASELINE_MAX"] = "Maximum signal to baseline ratio";
    _changedLabelName["SIGNAL_BASELINE_SIML"] = "Signal to baseline ratio similarity in peak-group";
    _changedLabelName["SIGNAL_BASELINE_AL1"] = "At least 1 good signal to baseline ratio";
    _changedLabelName["SYMMETRY_MEAN"] = "Mean of Peak-group’s symmetry";
    _changedLabelName["SYMMETRY_STD"] = "Std of symmetry in peak-groups";
    _changedLabelName["SYMMETRY_MIN"] = "Minimum symmetry value in peak-groups";
    _changedLabelName["SYMMETRY_MAX"] = "Maximum symmetry value in peak-groups";
    _changedLabelName["SYMMETRY_SIML"] = "Symmetries Similarity in peakgroup";
    _changedLabelName["SYMMETRY_AL1"] = "At least 1 good symmetric value";
    _changedLabelName["PEAK_WIDTH_MEAN"] = "Mean of peaks’ width";
    _changedLabelName["PEAK_WIDTH_STD"] = "Std of peaks’ width";
    _changedLabelName["PEAK_WIDTH_MIN"] = "Minimum peak width";
    _changedLabelName["PEAK_WIDTH_MAX"] = "Maximum peak width";
    _changedLabelName["PEAK_WIDTH_SIML"] = "Peaks’ width similarity in peakgroup";
    _changedLabelName["PEAK_WIDTH_AL1"] = "At least 1 good peak width value";
    _changedLabelName["PEAK_AREA_MEAN"] = "Mean of peak area";
    _changedLabelName["PEAK_AREA_STD"] = "Std of peak area";
    _changedLabelName["PEAK_AREA_MIN"] = "Minimum peak area";
    _changedLabelName["PEAK_AREA_MAX"] = "Maximum peak area";
    _changedLabelName["PEAK_AREA_SIML"] = "Peak areas similarity in peakgroup";
    _changedLabelName["PEAK_AREA_AL1"] = "At least 1 good peak area value";
    _changedLabelName["PEAK_GAUSS_FIT_R2_MEAN"] = "Mean of peak gaussian fit R2";
    _changedLabelName["PEAK_GAUSS_FIT_R2_STD"] = "Std of peak gaussian fit R2";
    _changedLabelName["PEAK_GAUSS_FIT_R2_MIN"] = "Minimum gaussian fit R2 value";
    _changedLabelName["PEAK_GAUSS_FIT_R2_MAX"] = "Maximum gaussian fit R2 value";
    _changedLabelName["PEAK_GAUSS_FIT_R2_SIML"] = "Gaussian fit R2 similarity in peakgroup";
    _changedLabelName["PEAK_GAUSS_FIT_R2_AL1"] = "At least 1 good gaussian fit R2 value";
    _changedLabelName["PEAK_INTENSITY_MEAN"] = "Mean of peak intensity";
    _changedLabelName["PEAK_INTENSITY_STD"]  = "Std of peak intensity";
    _changedLabelName["PEAK_INTENSITY_MIN"] = "Minimum peak intensity";
    _changedLabelName["PEAK_INTENSITY_MAX"] = "Maximum peak intensity";
    _changedLabelName["PEAK_INTENSITY_SIML"] = "Peak intensity similarity in peakgroup";
    _changedLabelName["PEAK_INTENSITY_AL1"] = "At least 1 good peak intensity value";
    _changedLabelName["NO_NOISE_MEAN"] = "Mean of no noise";
    _changedLabelName["NO_NOISE_STD"] = "Std of no noise";
    _changedLabelName["NO_NOISE_MIN"] = "Minimum no noise value";
    _changedLabelName["NO_NOISE_MAX"] = "Maximum no noise value";
    _changedLabelName["NO_NOISE_SIML"] = "No noise similarity in peakgroup";
    _changedLabelName["NO_NOISE_AL1"] = "At least 1 good no noise value";
    _changedLabelName["RT_STD"] = "Std of retention time";
    _changedLabelName["RT_SIML"] = "Retention time similarity in peakgroup";
    _changedLabelName["QUALITY_MEAN"] = "Mean of peakgroup quality";
    _changedLabelName["QUALITY_STD"] = "Std of peakgroup quality";
    _changedLabelName["QUALITY_MIN"] = "Minimum peakgroup quality";
    _changedLabelName["QUALITY_MAX"] = "Maximum peakgroup quality";
    _changedLabelName["QUALITY_SIML"] = "Quality similarity in peakgroup";
    _changedLabelName["QUALITY_AL1"] = "At least 1 good quality value";
    _changedLabelName["PeakQuality_mean_cohort_mean"] = "Mean of peakquality mean cohort";
    _changedLabelName["PeakQuality_mean_cohort_std"] = "Std of peakquality mean cohort";
    _changedLabelName["PeakQuality_mean_cohort_min"] = "Minimum peakquality mean cohort";
    _changedLabelName["PeakQuality_mean_cohort_max"] = "Maximum peakquality mean cohort";
    _changedLabelName["PeakQuality_mean_cohort_SIML"] = "Peakquality mean cohort similarity in peakgrpup";
    _changedLabelName["PeakQuality_mean_cohort_AL1"] = "Atleast 1 good peakquality mean cohort";
    _changedLabelName["RT_OVERLAP_COHORT_MEAN"] = "Mean of retention time overlap cohort";
    _changedLabelName["RT_OVERLAP_COHORT_STD"] = "Std of retention time overlap cohort";
    _changedLabelName["RT_OVERLAP_COHORT_MIN"] = "Minimum retention time overlap cohort value";
    _changedLabelName["RT_OVERLAP_COHORT_MAX"] = "Maximum retention time overlap cohort value";
    _changedLabelName["RT_OVERLAP_COHORT_SIML"] = "Retention time overlap cohort similarity in peakgroup";
    _changedLabelName["RT_OVERLAP_COHORT_AL1"] = "At least 1 good retention time overlap cohort";
    _changedLabelName["RT_MEAN_COHORT_STD"] = "Std of retention time mean cohort";
    _changedLabelName["RT_MEAN_COHORT_SIML"] = "Retention time mean cohort similarity in peakgroup";
    _changedLabelName["RT_STDDEV_COHORT_MEAN"] = "mean of retention time std cohort";
    _changedLabelName["RT_STDDEV_COHORT_STD"] = "Std of retention time std cohort";
    _changedLabelName["RT_STDDEV_COHORT_MIN"] = "Minimum retention time std cohort";
    _changedLabelName["RT_STDDEV_COHORT_MAX"] = "Maximum retention time std cohort";
    _changedLabelName["RT_STDDEV_COHORT_SIML"] = "Retention time std cohort similarity in peakgroup";
    _changedLabelName["RT_STDDEV_COHORT_AL1"] = "At least 1 good retention time std cohort value";
    _changedLabelName["PEAK_INTENSITY_STDDEV_COHORT_MEAN"] = "Mean of peak intensity std cohort";
    _changedLabelName["PEAK_INTENSITY_STDDEV_COHORT_STD"] = "Std of peak intensity std cohort";
    _changedLabelName["PEAK_INTENSITY_STDDEV_COHORT_MIN"] = "Minimum peak intensity std cohort value";
    _changedLabelName["PEAK_INTENSITY_STDDEV_COHORT_MAX"] = "Maximum peak intensity std cohort value";
    _changedLabelName["PEAK_INTENSITY_STDDEV_COHORT_SIML"] = "Peak intensity std cohort similarity in peakgroup";
    _changedLabelName["PEAK_INTENSITY_STDDEV_COHORT_AL1"] = "At least 1 good peak intensity std cohort value";
    _changedLabelName["PEAK_INTENSITY_SIML_COHORT_MEAN"] = "Mean of peak intensity similarity cohort mean";
    _changedLabelName["PEAK_INTENSITY_SIML_COHORT_STD"] = "Std of peak intensity similarity cohort mean";
    _changedLabelName["PEAK_INTENSITY_SIML_COHORT_MIN"] = "Minimum peak intensity similarity cohort mean";
    _changedLabelName["PEAK_INTENSITY_SIML_COHORT_MAX"] = "Maximum peak intensity similarity cohort mean";
    _changedLabelName["PEAK_INTENSITY_SIML_COHORT_SIML"] = "Peak intensity similarity cohort mean similarity in peakgroup";
    _changedLabelName["PEAK_INTENSITY_SIML_COHORT_AL1"] = "At least 1 good peak intensity similarity cohort mean";
    _changedLabelName["RT_DEVIATION_COHORT_MEAN"] = "Mean of retention time deviation cohort mean";
    _changedLabelName["RT_DEVIATION_COHORT_STD"] = "Std of retention time deviation cohort mean";
    _changedLabelName["RT_DEVIATION_COHORT_MIN"] = "Minimum retention time deviation cohort mean";
    _changedLabelName["RT_DEVIATION_COHORT_MAX"] = "Maximum of retention time deviation cohort mean";
    _changedLabelName["RT_DEVIATION_COHORT_SIML"] = "Retention time deviation cohort mean similarity in peakgroup";
    _changedLabelName["PEAK_INTENSITY_SIML_COHORT_AL1"] = "At least 1 good retention time deviation cohort mean";
}
