#pragma once

#include <QWidget>

/**
 * @brief Central placeholder for the future 3D molecular viewer.
 *
 * The real widget will embed VTK (QVTKOpenGLNativeWidget) in a later
 * milestone; this one keeps the layout honest in the meantime.
 */
class ViewerPlaceholder : public QWidget
{
    Q_OBJECT

public:
    explicit ViewerPlaceholder(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_message;
};
