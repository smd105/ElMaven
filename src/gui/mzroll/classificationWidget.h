#ifndef CLASSIFICATIONWIDGET_H
#define CLASSIFICATIONWIDGET_H

#include "stable.h"

class TableDockWidget;
class PeakGroup;
class Compound;

class ClassificationWidget : public QGraphicsView
{
    Q_OBJECT
    public:
    ClassificationWidget(TableDockWidget * tabledock);

    public slots:
    void showClassification();

    private:
    QDialog* _inferenceVisual;
    QVBoxLayout* _layout;
    QGraphicsScene _scene;
    QGraphicsView* _sceneView;
    TableDockWidget* _tableDock;
    PeakGroup* _group;
    float _sumNegativeWeights;
    float _sumPositiveWeights;
    float minNegative;
    float maxNegative;
    float minPositive;
    float maxPositive;
    int _totalArrowCount;

    map<string, string> _changedLabelName;

    void setTitle();
    int makeArrowForNegatives(float width, string label, int counter, int startPosition);
    int makeArrowForPositives(float width, string label, int counter, int startPosition);
    void plotAxes(int type, float startX, float EndX, float min, float max);
    void addNameInList(QColor color, QString featureName, int iterator);
    void initialiseLabelName();

};

#endif // CLASSIFICATIONWIDGET_H
