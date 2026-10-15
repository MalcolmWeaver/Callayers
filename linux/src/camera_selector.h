#pragma once
#include "capture.h"
#include <QComboBox>
#include <QSignalBlocker>

// Adding the first item must not silently select an unopened camera.
inline int PopulateCameraSelector(QComboBox& selector, const QVector<CameraChoice>& cameras,
                                  const QString& savedIdentity, const QString& savedName) {
    const QSignalBlocker blocked(selector);
    selector.clear();
    selector.setPlaceholderText(savedIdentity.isEmpty() ? "Choose your webcam" :
        (savedName.isEmpty() ? "Saved webcam unavailable" : savedName + " — unavailable"));
    int match = -1;
    for (int i = 0; i < cameras.size(); ++i) {
        selector.addItem(cameras[i].name, cameras[i].identity);
        if (cameras[i].identity == savedIdentity) match = i;
    }
    selector.setCurrentIndex(match);
    return match;
}
