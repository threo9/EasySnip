#pragma once
#include "document.h"
#include "settings.h"
#include "controls.h"
#include <QMainWindow>
#include <QTimer>
#include <QListWidget>

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
    void beginStyleChange();
    void endStyleChange();
    void setZoom(double zoom);
    double zoom() const;
    void setBaseImage(const QImage &image);
    void setCaptureBackground(const Document &background, const QPointF &annotationOffset = {});
    void setDocument(const Document &document);
    void setDetectedElements(const QVector<QRect> &elements);
    void setRadius(int radius);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void clear();
    void copyObject();
    void pasteObject();
    void deleteObject();
    void togglePicker();
    void finishDrawing();
    int selectedIndex() const;
    bool acceleratedPreview() const;
    bool drawing() const;
    bool canStartBackgroundDrag(const QPoint &point) const;
  signals:
    void changed();
    void selectionChanged();
    void colorPicked(QString text);
    void positionChanged(QString text);
    void toolChanged(Tool tool);
    void imageCopyRequested();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    void checkpoint();
    void updateCanvasSize();
    void redraw(bool preserveStrokeBounds = false);
    void paintContent(QPainter &painter);
    int objectAt(const QPointF &point) const;
    bool matchElement(const QPointF &point);
    QPointF imagePoint(const QPoint &point) const;
    void translate(Annotation &item, const QPointF &delta);
    Document document_;
    Settings &settings_;
    Tool tool_ = Tool::Select;
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
    bool rotating_ = false;
    bool polyline_ = false;
    bool polylineDragged_ = false;
    bool pointerDown_ = false;
    bool newAnnotation_ = false;
    bool transformCheckpoint_ = false;
    int resizeHandle_ = -1;
    bool picker_ = false;
    bool magnifier_ = false;
    bool altUsed_ = false;
    bool penStraight_ = false;
    bool strokeBoundsValid_ = false;
    bool styleChanging_ = false;
    bool styleCheckpoint_ = false;
    QPointF anchor_;
    QPointF previous_;
    QPointF hover_;
    Annotation original_;
    QOpenGLWidget *gpuPreview_ = nullptr;
};

class BrushTips : public QWidget {
  public:
    BrushTips(AnnotationCanvas *canvas, Settings &settings, QWidget *parent = nullptr);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void rebuild();
    void save();
    AnnotationCanvas *canvas_;
    Settings &settings_;
    QVector<double> sizes_;
};

class PaletteWidget : public QListWidget {
  public:
    PaletteWidget(AnnotationCanvas *canvas, Settings &settings, QWidget *parent = nullptr);
    void edit();

  protected:
    void dropEvent(QDropEvent *event) override;

  private:
    void reload();
    void saveOrder();
    AnnotationCanvas *canvas_;
    Settings &settings_;
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
    LineTypeButton *lineType_ = nullptr;
    ValueSlider *width_ = nullptr;
    ValueSlider *alpha_ = nullptr;
    ValueSlider *fontSize_ = nullptr;
    ValueSlider *rotation_ = nullptr;
    ValueSlider *outlineWidth_ = nullptr;
    OptionStrip *lineStyle_ = nullptr;
    QComboBox *shape_ = nullptr;
    QComboBox *arrow_ = nullptr;
    QPushButton *color_ = nullptr;
    QPushButton *fill_ = nullptr;
    QPushButton *outline_ = nullptr;
    PaletteWidget *palette_ = nullptr;
    QRect sourceRect_;
    QTimer refreshTimer_;
    bool refreshing_ = false;
    bool output_ = false;
};
