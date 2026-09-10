#include "barchartwidget.h"

#include <QPainter>
#include <QFontMetrics>
#include <algorithm>

namespace {
constexpr int kRowHeight   = 28;
constexpr int kLabelWidth  = 110;
constexpr int kValueWidth  = 45;
constexpr int kTitleHeight = 26;
}

BarChartWidget::BarChartWidget(QWidget *parent)
    : QWidget(parent)
{
}

void BarChartWidget::setBars(const QList<Bar> &bars)
{
    m_bars = bars;
    updateGeometry();   // sizeHint changed, let the layout know
    update();           // schedule a repaint
}

void BarChartWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

QSize BarChartWidget::sizeHint() const
{
    const int height = kTitleHeight + m_bars.size() * kRowHeight + 10;
    return QSize(360, std::max(height, 80));
}

void BarChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int y = 0;

    if (!m_title.isEmpty()) {
        QFont titleFont = font();
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(palette().text().color());
        p.drawText(QRect(0, y, width(), kTitleHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, m_title);
        y += kTitleHeight;
        p.setFont(font());
    }

    if (m_bars.isEmpty()) {
        p.setPen(QColor("#9e9e9e"));
        p.drawText(QRect(0, y, width(), kRowHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, tr("No data"));
        return;
    }

    // Scale bars against the largest value so the widest one fills the space.
    // std::max_element over a projection avoids a manual loop.
    const auto maxIt = std::max_element(m_bars.begin(), m_bars.end(),
        [](const Bar &a, const Bar &b) { return a.value < b.value; });
    const int maxValue = std::max(1, maxIt->value);   // never divide by zero

    const int barAreaWidth = width() - kLabelWidth - kValueWidth - 8;

    for (const Bar &bar : m_bars) {
        const QRect labelRect(0, y, kLabelWidth, kRowHeight);
        p.setPen(palette().text().color());
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                   fontMetrics().elidedText(bar.label, Qt::ElideRight, kLabelWidth - 6));

        const int barWidth = bar.value > 0
            ? std::max(2, barAreaWidth * bar.value / maxValue)
            : 0;

        const QRect barRect(kLabelWidth, y + 5, barWidth, kRowHeight - 12);

        if (barWidth > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(bar.color);
            p.drawRoundedRect(barRect, 3, 3);
        }

        const QRect valueRect(kLabelWidth + barAreaWidth + 4, y, kValueWidth, kRowHeight);
        p.setPen(palette().text().color());
        p.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(bar.value));

        y += kRowHeight;
    }
}
