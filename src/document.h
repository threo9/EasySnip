#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QVector>

enum class Tool {
    Select = 0,
    Rectangle = 1,
    Ellipse = 2,
    RoundedRectangle = 3,
    Line = 4,
    Arrow = 5,
    Polyline = 6,
    Pen = 7,
    // Serialized ID 8 is retired; keep subsequent tools compatible with existing records.
    Mosaic = 9,
    Blur = 10,
    Eraser = 11,
    Text = 12,
    Number = 13,
    Magnify = 14
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
    QPainterPath effectPath() const;
    bool sameContent(const Annotation &other) const;
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
bool hasOnlyOpaquePixels(const QImage &image);
QString colorText(const QColor &color, const QString &format, bool uppercase);
QString decodeCodes(const QImage &image);
