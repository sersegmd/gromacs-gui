#pragma once

#include <QOpenGLWidget>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QVector3D>

#include "core/CoreBridge.h"   // StructureData, AtomData

/**
 * @brief Real 3D molecular viewer (OpenGL) with trajectory playback.
 *
 * Rendering styles (setDisplayMode):
 *   - Ball & Stick   — CPK-coloured atom points + grey bond lines
 *   - Space-Filling  — van der Waals sized atoms (molecular surface feel)
 *   - Backbone       — the Cα trace of the protein as a thin line
 *   - Ribbon         — a smooth cartoon tube through the Cα trace, rainbow
 *                      coloured by residue order, the standard way to show
 *                      a protein's three-dimensional fold
 *
 * Structures are parsed by the core (.gro/.pdb) and pushed in via
 * setStructure(). Multi-frame trajectories can be loaded with
 * setTrajectory() and played back with play/pause, step buttons and a speed
 * control. Mouse drag orbits, wheel zooms, double-click resets.
 */
class ViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum DisplayMode { BallAndStick = 0, SpaceFilling, Backbone, Ribbon };

    explicit ViewerWidget(QWidget *parent = nullptr);

    //! Pushes parsed structure data into the GL buffers.
    void setStructure(const StructureData &data);

    //! Loads a multi-frame trajectory and displays frame 0 (paused).
    void setTrajectory(const QVector<StructureData> &frames);

    bool isPlaying() const { return m_playing; }
    int frameIndex() const { return m_frameIndex; }
    int frameCount() const { return m_frames.size(); }
    QString statusText() const { return m_statusText; }

public slots:
    void resetView();
    void playPause();
    void nextFrame();
    void prevFrame();
    //! Jumps to a frame (0-based); clamps to the loaded range.
    void setFrame(int index);
    //! Playback interval in ms (smaller = faster).
    void setSpeedMs(int ms);
    //! Switches the structure rendering style (ViewerWidget::DisplayMode).
    void setDisplayMode(int mode);

signals:
    //! Emitted whenever the displayed frame changes (drives the slider).
    void frameChanged(int index, int total);
    //! Emitted when play/pause toggles.
    void playingChanged(bool playing);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void buildBonds();
    void buildBackbone();
    void buildRibbon();
    //! Draws one interleaved pos+colour(+radius) buffer as points.
    void drawPoints(QOpenGLBuffer &buffer, int count, int strideFloats);
    //! Draws one interleaved pos+colour buffer as lines.
    void drawLines(QOpenGLBuffer &buffer, int count, float width);
    //! Draws the ribbon triangle soup.
    void drawTriangles(QOpenGLBuffer &buffer, int count);

    struct Bond { QVector3D a, b; };

    QVector<AtomData> m_atoms;
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_atomBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_bondBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_traceBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_ribbonBuffer{QOpenGLBuffer::VertexBuffer};
    int m_atomCount = 0;
    int m_bondCount = 0;
    int m_traceCount = 0;       // line segments in the Cα trace
    int m_ribbonCount = 0;      // triangle vertices in the ribbon
    bool m_hasBackbone = false; // protein with Cα atoms detected?
    DisplayMode m_displayMode = BallAndStick;

    // trajectory playback
    QVector<StructureData> m_frames;
    int m_frameIndex = -1;
    bool m_playing = false;
    int m_intervalMs = 150;
    QTimer m_timer;

    QVector3D m_center;
    float m_extent = 5.0f;    // scene radius (nm)
    float m_zoom = 1.0f;
    float m_yaw = 25.0f;
    float m_pitch = 15.0f;
    QPoint m_lastMouse;
    QString m_statusText;
};
