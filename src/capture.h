#pragma once
#include "document.h"
#include "native.h"
#include "settings.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>
#include <QVariantAnimation>

class CaptureOverlay : public QWidget {
    Q_OBJECT
  public:
    CaptureOverlay(Native::Desktop desktop, Settings &settings, const QRect &initial = {});
    QRect selectedGlobal() const;
  signals:
    void captured(Document document, QRect globalRect);
    void canceled(Document document);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;

  private:
    Document selectionDocument() const;
    QImage backgroundImage() const;
    void finish();
    void cancel();
    void updateDetection();
    void setHover(const QRect &rectangle);
    void refresh();
    void setDimensions();
    void placeToolbar();
    void constrainSelection();
    int handleAt(const QPoint &point) const;
    Native::Desktop desktop_;
    Settings &settings_;
    QRect selection_;
    QRect hover_;
    QRect animatedHover_;
    QVariantAnimation detectionAnimation_;
    QPoint anchor_;
    QPoint previous_;
    QPoint mouse_;
    bool pressed_ = false;
    bool space_ = false;
    bool moving_ = false;
    bool cursor_ = false;
    bool elements_ = false;
    bool refreshing_ = false;
    bool done_ = false;
    int handle_ = -1;
    int radius_ = 0;
    double ratio_ = 0;
    QString typed_;
    QWidget *toolbar_ = nullptr;
    QTimer detectionTimer_;
    QTimer refreshTimer_;
    QFutureWatcher<QVector<QRect>> elementsWatcher_;
    QVector<QRect> elementRects_;
    QVector<QRect> detectedElements_;
    QPoint elementPoint_;
    int elementIndex_ = 0;
};
