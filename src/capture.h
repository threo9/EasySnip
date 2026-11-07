#pragma once
#include "document.h"
#include "native.h"
#include "settings.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>
#include <QVariantAnimation>
#include <functional>

class AnnotationCanvas;
class QAction;
class QActionGroup;
class ValueSlider;
class QPushButton;
class QComboBox;
class QLabel;
class QToolBar;
class QBoxLayout;

class CaptureOverlay : public QWidget {
    Q_OBJECT
  public:
    CaptureOverlay(Native::Desktop desktop, Settings &settings, const QRect &initial = {});
    QRect selectedGlobal() const;
    void completeOutput(bool successful, bool closeCapture = true);
    void setHistory(int count, std::function<Document(int)> loader);
  signals:
    void outputRequested(Document document, QRect globalRect, QString action, bool skipAutoSave);
    void canceled(Document document);
    void draftAbandoned(Document document);

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
    bool eventFilter(QObject *object, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    friend int runSelfTests(const QString &directory, bool native);
    Document selectionBackground() const;
    Document selectionDocument() const;
    QImage backgroundImage() const;
    void requestOutput(const QString &action = "finish");
    void cancel();
    void createEditorToolbar();
    void syncSelection();
    void selectTool(QAction *action);
    void deactivateTool();
    void updateResizeBorder();
    void syncStyle();
    void editStyle();
    void clearSelection();
    void updateDetection();
    void updateDetectionAt(const QPoint &point);
    void applyDetection(const QVector<QRect> &rectangles);
    void invalidateDetection();
    void setHover(const QRect &rectangle);
    void refresh();
    void setDimensions();
    void placeToolbar();
    void constrainSelection();
    void replayHistory(int direction);
    bool magnifierVisible() const;
    void moveCursor(const QPoint &step, Qt::KeyboardModifiers modifiers);
    bool backgroundDragEvent(QMouseEvent *event);
    int handleAt(const QPoint &point) const;
    Native::Desktop desktop_;
    Settings &settings_;
    QRect selection_;
    QRect resizeStart_;
    QImage resizePreview_;
    QPoint resizePreviewOrigin_;
    QRect hover_;
    QRect animatedHover_;
    QVariantAnimation detectionAnimation_;
    QPoint anchor_;
    QPoint previous_;
    QPoint mouse_;
    QPoint backgroundDragAnchor_;
    Qt::KeyboardModifiers backgroundDragModifiers_;
    bool pressed_ = false;
    bool space_ = false;
    bool moving_ = false;
    bool regionResizing_ = false;
    bool backgroundMovePending_ = false;
    bool movingFromCanvas_ = false;
    bool cursor_ = false;
    bool elements_ = false;
    bool refreshing_ = false;
    bool done_ = false;
    bool selecting_ = true;
    bool outputPending_ = false;
    bool resumeRefresh_ = false;
    int magnifierOverride_ = -1;
    bool altUsed_ = false;
    int handle_ = -1;
    int radius_ = 0;
    double ratio_ = 0;
    QString typed_;
    QWidget *toolbar_ = nullptr;
    QWidget *editorToolbar_ = nullptr;
    QWidget *resizeBorder_ = nullptr;
    QBoxLayout *toolbarActions_ = nullptr;
    QBoxLayout *strengthLayout_ = nullptr;
    QToolBar *annotationTools_ = nullptr;
    QToolBar *outputTools_ = nullptr;
    QWidget *styleBar_ = nullptr;
    QWidget *presets_ = nullptr;
    QWidget *colorPresets_ = nullptr;
    QWidget *brushTips_ = nullptr;
    AnnotationCanvas *canvas_ = nullptr;
    QActionGroup *tools_ = nullptr;
    QAction *undo_ = nullptr;
    QAction *redo_ = nullptr;
    QPushButton *color_ = nullptr;
    QComboBox *toolVariant_ = nullptr;
    QComboBox *effectShape_ = nullptr;
    QWidget *strokeOptions_ = nullptr;
    QWidget *fontOptions_ = nullptr;
    QWidget *opacityOptions_ = nullptr;
    ValueSlider *strokeWidth_ = nullptr;
    ValueSlider *fontSize_ = nullptr;
    ValueSlider *opacity_ = nullptr;
    QTimer detectionTimer_;
    QTimer refreshTimer_;
    QFutureWatcher<QVector<QRect>> elementsWatcher_;
    QVector<QRect> elementRects_;
    QVector<QRect> detectedElements_;
    QPoint elementPoint_;
    int elementIndex_ = 0;
    int detectionGeneration_ = 0;
    int queryGeneration_ = 0;
    struct CaptureState {
        Document document;
        QRect selection;
        bool selecting = true;
        Tool tool = Tool::Select;
        QString recordId;
    };
    QMap<int, CaptureState> historyStates_;
    std::function<Document(int)> historyLoader_;
    int historyCount_ = 0;
    int historyIndex_ = -1;
};
