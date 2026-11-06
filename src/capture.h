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
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
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
    Document selectionBackground() const;
    Document selectionDocument() const;
    QImage backgroundImage() const;
    void requestOutput(const QString &action = "finish");
    void cancel();
    void createEditorToolbar();
    void syncSelection();
    void selectTool(QAction *action);
    void syncStyle();
    void editStyle();
    void clearSelection();
    void updateDetection();
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
    QBoxLayout *toolbarActions_ = nullptr;
    QToolBar *annotationTools_ = nullptr;
    QToolBar *outputTools_ = nullptr;
    QWidget *styleBar_ = nullptr;
    QWidget *presets_ = nullptr;
    AnnotationCanvas *canvas_ = nullptr;
    QActionGroup *tools_ = nullptr;
    QAction *adjustSelection_ = nullptr;
    QAction *undo_ = nullptr;
    QAction *redo_ = nullptr;
    QPushButton *color_ = nullptr;
    QDoubleSpinBox *strokeWidth_ = nullptr;
    QSpinBox *fontSize_ = nullptr;
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
