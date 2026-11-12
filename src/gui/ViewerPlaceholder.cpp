#include "ViewerPlaceholder.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

ViewerPlaceholder::ViewerPlaceholder(QWidget *parent)
    : QWidget(parent)
{
    m_message = tr(
        "3D molecular viewer\n"
        "(VTK integration — planned milestone)\n\n"
        "Structures (.gro / .pdb) and trajectories (.xtc / .trr)\n"
        "will render here once the viewer module lands.");
}

void ViewerPlaceholder::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xf6, 0xf6, 0xf4));

    // Subtle "bond + atom" motif so the panel does not look dead.
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center = rect().center();
    const double r = qMin(width(), height()) * 0.12;

    QPainterPath bond;
    bond.moveTo(center - QPointF(r * 1.6, -r * 1.2));
    bond.lineTo(center + QPointF(r * 1.2, r * 1.6));
    painter.setPen(QPen(QColor(0xc9, 0xd5, 0xe0), 4.0));
    painter.drawPath(bond);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x4a, 0x90, 0xd9));
    painter.drawEllipse(center - QPointF(r * 1.6, -r * 1.2), r, r);
    painter.setBrush(QColor(0x6a, 0xb0, 0x6a));
    painter.drawEllipse(center + QPointF(r * 1.2, r * 1.6), r, r);

    painter.setPen(QColor(0x66, 0x66, 0x66));
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    painter.drawText(rect().adjusted(0, height() / 3, 0, 0),
                     Qt::AlignHCenter | Qt::AlignTop, m_message);
}
