#pragma once
#include "model.h"
#include <QImage>
#include <QString>
#include <QVector>

QImage ComposeFrame(const QImage& webcam, const QVector<Card>& cards, const QVector<Annotation>& annotations, quint64 hiddenTextId);
QString ConsumerName();
