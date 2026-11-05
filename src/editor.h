#pragma once
#include "document.h"
#include "settings.h"
#include <QMainWindow>
#include <QTimer>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QOpenGLWidget;

class AnnotationCanvas : public QWidget {
    Q_OBJECT
  public:
    AnnotationCanvas(Document document, Settings &settings, QWidget *parent = nullptr);
    const Document &document() const;
    void setTool(Tool tool);
    Tool tool() const;
    Annotation style() const;
    void setStyle(const Annotation &style);
    void setZoom(double zoom);
    double zoom() const;
    void setBaseImage(const QImage &image);
    void setRadius(int radius);
    void undo();
    void redo();
    void clear();
    void copyObject();
    void pasteObject();
    void deleteObject();
    void togglePicker();
    void finishDrawing();
    int selectedIndex() const;
    bool acceleratedPreview() const;
  signals:
    void changed();
    void selectionChanged();
    void colorPicked(QString text);
    void positionChanged(QString text);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    void checkpoint();
    void redraw();
    void paintContent(QPainter &painter);
    int objectAt(const QPointF &point) const;
    QPointF imagePoint(const QPoint &point) const;
    void translate(Annotation &item, const QPointF &delta);
    Document document_;
    Settings &settings_;
    Tool tool_ = Tool::Rectangle;
    Annotation style_;
    QImage rendered_;
    QVector<QVector<Annotation>> undo_;
    QVector<QVector<Annotation>> redo_;
    int selected_ = -1;
    int number_ = 1;
    double zoom_ = 1;
    bool drawing_ = false;
    bool moving_ = false;
    bool resizing_ = false;
    bool polyline_ = false;
    bool picker_ = false;
    bool penStraight_ = false;
    QPointF anchor_;
    QPointF previous_;
    QPointF hover_;
    Annotation original_;
    QOpenGLWidget *gpuPreview_ = nullptr;
};

class EditorWindow : public QMainWindow {
    Q_OBJECT
  public:
    EditorWindow(Document document, Settings &settings, const QRect &sourceRect = {},
                 QWidget *parent = nullptr);
    AnnotationCanvas *canvas() const;
    void acknowledgeOutput();
  signals:
    void outputRequested(Document document, QString action, bool skipAutoSave);
    void abandoned(Document document);

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void syncInspector();
    void updateStyle();
    void refreshBackground();
    void editPalette();
    void recognize();
    Settings &settings_;
    AnnotationCanvas *canvas_ = nullptr;
    QDoubleSpinBox *width_ = nullptr;
    QSpinBox *alpha_ = nullptr;
    QSpinBox *fontSize_ = nullptr;
    QDoubleSpinBox *rotation_ = nullptr;
    QSpinBox *outlineWidth_ = nullptr;
    QComboBox *lineStyle_ = nullptr;
    QComboBox *shape_ = nullptr;
    QComboBox *arrow_ = nullptr;
    QPushButton *color_ = nullptr;
    QPushButton *fill_ = nullptr;
    QPushButton *outline_ = nullptr;
    QListWidget *palette_ = nullptr;
    QRect sourceRect_;
    QTimer refreshTimer_;
    bool refreshing_ = false;
    bool output_ = false;
};
