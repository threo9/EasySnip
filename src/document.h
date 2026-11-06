#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QVector>

enum class Tool {
    Select,
    Rectangle,
    Ellipse,
    RoundedRectangle,
    Line,
    Arrow,
    Polyline,
    Pen,
    Highlighter,
    Mosaic,
    Blur,
    Eraser,
    Text,
    Number,
    Magnify
};
QString toolName(Tool tool);
QString toolKey(Tool tool);
bool isLineTool(Tool tool);
bool isEffectTool(Tool tool);

struct Annotation {
    Tool tool = Tool::Rectangle;
    QRectF rect;
    QVector<QPointF> points;
    QColor color = QColor("#ff536b");
    QColor fill = Qt::transparent;
    QColor outline = Qt::black;
    double width = 3;
    double rotation = 0;
    double fontSize = 24;
    double outlineWidth = 0;
    int lineStyle = int(Qt::SolidLine);
    int shape = 0;
    int arrowStyle = 0;
    int number = 1;
    QString text;
    QRectF bounds() const;
    QJsonObject toJson() const;
    static Annotation fromJson(const QJsonObject &object);
};

struct Document {
    QImage image;
    QVector<Annotation> annotations;
    QVector<QRect> detectedElements;
    int cornerRadius = 0;
    QRect sourceRect;
    QImage render(bool frame = false) const;
    QJsonObject toJson() const;
    static Document fromJson(const QJsonObject &object, const QImage &image);
};

QImage gaussianBlur(const QImage &image, int radius);
QString colorText(const QColor &color, const QString &format, bool uppercase);
QString decodeCodes(const QImage &image);
