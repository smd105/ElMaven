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
        /**
         * @brief Manages all the plotting functions.
         */ 
        void showClassification();

    private:
        QDialog* _inferenceVisual;
        QVBoxLayout* _layout;
        QGraphicsScene _scene;
        QGraphicsView* _sceneView;
        TableDockWidget* _tableDock;
        PeakGroup* _group;
        // Maintains the sum for negative shap values.
        float _sumNegativeWeights;
        // Maintails the sum for positive shap values.
        float _sumPositiveWeights;
        // Sum of abs(negative weights) and positive weights.
        float _absoluteTotalWeight;
        // Sum of negative weights and positive weights. 
        float _totalWeight;

        // Maps the features defined to user friendly labes. 
        map<string, string> _changedLabelName;

        /** 
         * @brief Sets title (name of group) to the scene.   
         */ 
        void setTitle();
        /**
         * @brief Makes arrows for negative shap values. 
         * @param shapValue ShapValue to plot.
         * @param label Feature for the shapValue.
         * @param counter Number of arrow plotted.
         * @param Start position for the arrow.  
         */ 
        int makeArrowForNegatives(float shapValue, 
                                  string label, 
                                  int counter, 
                                  int startPosition);

        /**
         * @brief Makes arrows for positive shap values. 
         * @param shapValue ShapValue to plot.
         * @param label Feature for the shapValue.
         * @param Number of arrow plotted.
         * @param Start position for the arrow.  
         */ 
        int makeArrowForPositives(float shapValue, 
                                  string label, 
                                  int counter, 
                                  int startPosition);

        /**
         * @brief Inititalizes the map "_changedLabelName".
         * @details Maps the value of features defined to 
         * user defined labels.
         */ 
        void initialiseLabelName();

        /**
         * @brief Sets output value for a peakgroup to the
         * scene.
         */ 
        void setOutputValue();

};

#endif // CLASSIFICATIONWIDGET_H
