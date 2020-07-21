#include "numeric_treewidgetitem.h"
#include "PeakGroup.h"

static PeakGroup::ClassifiedLabel labelForString(const QString& labelString)
{
    if (labelString == "PeakGroup::ClassifiedLabel::Signal") {
        return PeakGroup::ClassifiedLabel::Signal;
    } else if (labelString == "PeakGroup::ClassifiedLabel::Noise") {
        return PeakGroup::ClassifiedLabel::Noise;
    } else if (labelString == "PeakGroup::ClassifiedLabel::Correlation") {
        return PeakGroup::ClassifiedLabel::Correlation;
    } else if (labelString == "PeakGroup::ClassifiedLabel::Pattern") {
        return PeakGroup::ClassifiedLabel::Pattern;
    } else if (labelString == "PeakGroup::ClassifiedLabel::CorrelationAndPattern") {
        return PeakGroup::ClassifiedLabel::CorrelationAndPattern;
    }
    return PeakGroup::ClassifiedLabel::None;
}

bool NumericTreeWidgetItem::operator<( const QTreeWidgetItem & other ) const{
    int sortCol = treeWidget()->sortColumn();

    // takes care of sorting based on PeakML class labels
    QVariant thisUserData = this->data(sortCol, Qt::UserRole);
    QVariant otherUserData = other.data(sortCol, Qt::UserRole);
    auto thisLabel = labelForString(thisUserData.value<QString>());
    auto otherLabel = labelForString(otherUserData.value<QString>());
    if (thisLabel != PeakGroup::ClassifiedLabel::None
        || otherLabel != PeakGroup::ClassifiedLabel::None) {
        return thisLabel < otherLabel;
    }

    QString thisText = text(sortCol);
    QString otherText = other.text(sortCol);

    // lambda to compute real value of a numeric string
    auto textToDouble = [](QString text) {
        QRegExp exp("^([-+])?([0-9]*\\.?[0-9]+)(?:[eE]([-+])?([0-9]+))?$");
        if (!text.contains(exp))
            return make_pair(false, 0.0);

        double mantissaScale = exp.cap(1) == "-" ? -1.0 : 1.0;
        double mantissa = exp.cap(2).toDouble() * mantissaScale;
        double exponentScale = exp.cap(3) == "-" ? -1.0 : 1.0;
        double exponent = exp.cap(4).toDouble() * exponentScale;
        return make_pair(true, mantissa * (pow(10, exponent)));
    };

    auto convertedThisResult = textToDouble(thisText);
    auto convertedOtherResult = textToDouble(otherText);
    auto thisConversionSuccessful = convertedThisResult.first;
    auto otherConversionSuccessful = convertedOtherResult.first;
    auto thisConvertedNumber = convertedThisResult.second;
    auto otherConvertedNumber = convertedOtherResult.second;
    if (thisConversionSuccessful && otherConversionSuccessful) {
        return (thisConvertedNumber < otherConvertedNumber);
    } else if (thisConversionSuccessful) {
        thisText = QString::fromStdString(to_string(thisConvertedNumber).c_str());
    } else if (otherConversionSuccessful) {
        otherText = QString::fromStdString(to_string(otherConvertedNumber).c_str());
    }

    QCollator collator;
    collator.setNumericMode(true);
    return collator.compare(thisText , otherText) < 0;
}
