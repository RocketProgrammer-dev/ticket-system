#pragma once

#include <QWidget>
#include <QList>
#include <QPair>
#include <QColor>

// A minimal horizontal bar chart drawn with QPainter.
//
// Written by hand deliberately: Qt Charts is licensed under GPL or a
// commercial licence, NOT LGPL. Using it would force the whole application to
// be GPL, which is not a choice an intern gets to make on the company's
// behalf. Forty lines of painting avoids the entire problem.

class BarChartWidget : public QWidget
{
    Q_OBJECT

public:
    struct Bar {
        QString label;
        int     value = 0;
        QColor  color;
    };

    explicit BarChartWidget(QWidget *parent = nullptr);

    void setBars(const QList<Bar> &bars);
    void setTitle(const QString &title);

    QSize sizeHint() const override;

protected:
    // Called by Qt whenever the widget needs redrawing. You never call this
    // yourself — you call update(), and Qt schedules a paintEvent.
    void paintEvent(QPaintEvent *event) override;

private:
    QList<Bar> m_bars;
    QString    m_title;
};
