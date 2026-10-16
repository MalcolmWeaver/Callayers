#pragma once
#include "model.h"
#include <QImage>
#include <QString>
#include <QVector>

QImage ComposeFrame(const QImage& webcam, const QImage& overlay);
QString ConsumerName();
