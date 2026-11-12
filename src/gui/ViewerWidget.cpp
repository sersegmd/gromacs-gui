#include "ViewerWidget.h"

#include <QDebug>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>

namespace {

constexpr float kBallNm = 0.05f;   // ball radius (nm) for Ball & Stick points
constexpr float kRibbonNm = 0.12f; // ribbon tube radius (nm)
constexpr int kRibbonSegments = 8; // tube cross-section resolution
constexpr int kSplineSteps = 5;    // Catmull-Rom subdivisions per Cα→Cα segment

// Covalent radii (nm) for bond detection.
float covalentRadius(const QString &element)
{
    if (element == "H") return 0.037f;
    if (element == "C") return 0.077f;
    if (element == "N") return 0.075f;
    if (element == "O") return 0.073f;
    if (element == "S") return 0.104f;
    if (element == "P") return 0.106f;
    return 0.08f;
}

// Standard blue→red rainbow along the residue chain.
QVector3D residueColor(float fraction)
{
    const float hue = (1.0f - fraction) * 0.66f;   // 0.66 (blue) → 0 (red)
    QColor c;
    c.setHsvF(qBound(0.0f, hue, 1.0f), 0.75f, 0.92f);
    return QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF()));
}

} // namespace

ViewerWidget::ViewerWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::ClickFocus);
    m_statusText = QObject::tr(
        "Load a .gro / .pdb file from the project tree to render it here.\n"
        "Drag = orbit · Wheel = zoom · Double-click = reset");

    m_timer.setInterval(m_intervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (m_frames.isEmpty())
            return;
        const int next = (m_frameIndex + 1) % m_frames.size();
        setFrame(next);
    });
}

// ---------------------------------------------------------------------------
// Trajectory playback (F6)
// ---------------------------------------------------------------------------

void ViewerWidget::setTrajectory(const QVector<StructureData> &frames)
{
    m_frames = frames;
    m_timer.stop();
    m_playing = false;
    emit playingChanged(false);
    if (!m_frames.isEmpty()) {
        setFrame(0);
    } else {
        m_statusText = QObject::tr("Trajectory contained no frames.");
        update();
    }
}

void ViewerWidget::playPause()
{
    if (m_frames.isEmpty())
        return;
    m_playing = !m_playing;
    if (m_playing)
        m_timer.start();
    else
        m_timer.stop();
    emit playingChanged(m_playing);
}

void ViewerWidget::nextFrame()
{
    if (!m_frames.isEmpty())
        setFrame((m_frameIndex + 1) % m_frames.size());
}

void ViewerWidget::prevFrame()
{
    if (!m_frames.isEmpty())
        setFrame((m_frameIndex - 1 + m_frames.size()) % m_frames.size());
}

void ViewerWidget::setFrame(int index)
{
    if (m_frames.isEmpty())
        return;
    m_frameIndex = qBound(0, index, m_frames.size() - 1);
    setStructure(m_frames.at(m_frameIndex));
    m_statusText = QObject::tr("Frame %1 / %2 — %3 atoms  (%4)")
                       .arg(m_frameIndex + 1)
                       .arg(m_frames.size())
                       .arg(m_frames.at(m_frameIndex).natoms)
                       .arg(m_frames.at(m_frameIndex).title.left(40));
    emit frameChanged(m_frameIndex, m_frames.size());
    update();
}

void ViewerWidget::setSpeedMs(int ms)
{
    m_intervalMs = qBound(20, ms, 2000);
    m_timer.setInterval(m_intervalMs);
}

void ViewerWidget::setDisplayMode(int mode)
{
    m_displayMode = static_cast<DisplayMode>(mode);
    if ((m_displayMode == Ribbon || m_displayMode == Backbone) && !m_hasBackbone) {
        // Nothing to draw in protein-oriented styles — stay on atoms and say why.
        m_statusText = QObject::tr(
            "No protein backbone (Cα) detected in this structure — "
            "showing atoms instead.");
        m_displayMode = BallAndStick;
    }
    update();
}

// ---------------------------------------------------------------------------
// GL setup + rendering
// ---------------------------------------------------------------------------

void ViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glClearColor(0.97f, 0.97f, 0.96f, 1.0f);

    m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 130
        in vec3 position;
        in vec3 color;
        in float radius;
        uniform mat4 mvp;
        uniform float pointScale;   // pixels per attribute unit
        uniform float useVdW;       // 1 = size by per-atom vdW radius
        out vec3 vColor;
        void main() {
            gl_Position = mvp * vec4(position, 1.0);
            float sizeNm = mix(1.0, radius, useVdW);
            gl_PointSize = clamp(pointScale * sizeNm, 1.5, 220.0);
            vColor = color;
        })");
    m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 130
        in vec3 vColor;
        uniform int uMode;   // 0 = points, 1 = lines, 2 = triangles
        out vec4 fragColor;
        void main() {
            if (uMode == 0) {
                vec2 c = gl_PointCoord * 2.0 - 1.0;
                float r2 = dot(c, c);
                if (r2 > 1.0) discard;
                fragColor = vec4(vColor * (1.0 - 0.45 * r2), 1.0);
            } else if (uMode == 1) {
                fragColor = vec4(vColor, 1.0);
            } else {
                // gentle fake shading for the ribbon tube
                fragColor = vec4(vColor * 0.92, 1.0);
            }
        })");
    // Location binding must happen after the shaders exist, before link().
    m_program.bindAttributeLocation("position", 0);
    m_program.bindAttributeLocation("color", 1);
    m_program.bindAttributeLocation("radius", 2);
    m_program.link();
}

void ViewerWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

// pixels per nm at the current zoom, matching the perspective view distance

void ViewerWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_atomCount == 0 || !m_program.isLinked())
        return;

    QMatrix4x4 projection;
    const float aspect = float(width()) / qMax(1, height());
    const float distance = m_extent * 2.2f / m_zoom;
    projection.perspective(45.0f, aspect, 0.05f, distance + m_extent * 6.0f);

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -distance);
    view.rotate(m_pitch, 1.0f, 0.0f, 0.0f);
    view.rotate(m_yaw, 0.0f, 1.0f, 0.0f);
    view.translate(-m_center);

    const QMatrix4x4 mvp = projection * view;

    // Scene radius (m_extent) maps roughly onto half the viewport height.
    const float ppn = float(height()) / (2.2f * m_extent) * m_zoom
                      * float(QOpenGLWidget::devicePixelRatioF());

    m_program.bind();
    m_program.setUniformValue("mvp", mvp);

    DisplayMode mode = m_displayMode;
    if ((mode == Ribbon || mode == Backbone) && !m_hasBackbone)
        mode = BallAndStick;

    switch (mode) {
    case SpaceFilling:
        m_program.setUniformValue("pointScale", ppn);
        m_program.setUniformValue("useVdW", 1.0f);
        drawPoints(m_atomBuffer, m_atomCount, 7);
        break;
    case Backbone:
        m_program.setUniformValue("pointScale", ppn * kBallNm * 0.6f);
        m_program.setUniformValue("useVdW", 0.0f);
        drawLines(m_traceBuffer, m_traceCount, 3.0f);
        break;
    case Ribbon:
        m_program.setUniformValue("pointScale", ppn);
        m_program.setUniformValue("useVdW", 0.0f);
        drawTriangles(m_ribbonBuffer, m_ribbonCount);
        break;
    case BallAndStick:
    default:
        m_program.setUniformValue("pointScale", ppn * kBallNm);
        m_program.setUniformValue("useVdW", 0.0f);
        drawPoints(m_atomBuffer, m_atomCount, 7);
        // bonds on top, so sticks stay visible
        if (m_bondCount > 0 && m_bondBuffer.isCreated())
            drawLines(m_bondBuffer, m_bondCount, 2.0f);
        break;
    }
    m_program.release();
}

void ViewerWidget::drawPoints(QOpenGLBuffer &buffer, int count, int strideFloats)
{
    buffer.bind();
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, int(sizeof(float) * strideFloats));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, int(sizeof(float) * 3), 3,
                                 int(sizeof(float) * strideFloats));
    m_program.enableAttributeArray(2);
    m_program.setAttributeBuffer(2, GL_FLOAT, int(sizeof(float) * 6), 1,
                                 int(sizeof(float) * strideFloats));
    m_program.setUniformValue("uMode", 0);
    glDrawArrays(GL_POINTS, 0, count);
    m_program.disableAttributeArray(2);
    buffer.release();
}

void ViewerWidget::drawLines(QOpenGLBuffer &buffer, int count, float width)
{
    if (count <= 0 || !buffer.isCreated())
        return;
    buffer.bind();
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, int(sizeof(float) * 6));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, int(sizeof(float) * 3), 3,
                                 int(sizeof(float) * 6));
    m_program.disableAttributeArray(2);
    m_program.setAttributeValue(2, 1.0f);   // generic radius, unused for lines
    m_program.setUniformValue("uMode", 1);
    glLineWidth(width);
    glDrawArrays(GL_LINES, 0, count * 2);
    buffer.release();
}

void ViewerWidget::drawTriangles(QOpenGLBuffer &buffer, int count)
{
    if (count <= 0 || !buffer.isCreated())
        return;
    buffer.bind();
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, int(sizeof(float) * 6));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, int(sizeof(float) * 3), 3,
                                 int(sizeof(float) * 6));
    m_program.disableAttributeArray(2);
    m_program.setAttributeValue(2, 1.0f);
    m_program.setUniformValue("uMode", 2);
    glDrawArrays(GL_TRIANGLES, 0, count);
    buffer.release();
}

// ---------------------------------------------------------------------------
// Geometry construction
// ---------------------------------------------------------------------------

void ViewerWidget::buildBonds()
{
    // O(N²) is acceptable up to ~8k atoms; larger structures render as points.
    QList<Bond> bonds;
    const QVector<AtomData> &atoms = m_atoms;
    if (atoms.size() <= 8000) {
        for (int i = 0; i < atoms.size(); ++i) {
            const float ri = covalentRadius(atoms[i].element);
            for (int j = i + 1; j < atoms.size(); ++j) {
                const float cutoff = (ri + covalentRadius(atoms[j].element)) * 1.35f;
                const float dx = atoms[i].x - atoms[j].x;
                const float dy = atoms[i].y - atoms[j].y;
                const float dz = atoms[i].z - atoms[j].z;
                if (dx * dx + dy * dy + dz * dz < cutoff * cutoff)
                    bonds.push_back({QVector3D(atoms[i].x, atoms[i].y, atoms[i].z),
                                     QVector3D(atoms[j].x, atoms[j].y, atoms[j].z)});
            }
        }
    }
    // Same interleaved layout as atoms (pos+color per vertex) so bonds use
    // the identical attribute setup and cannot break independently.
    QVector<float> bondData;
    bondData.reserve(bonds.size() * 12);
    static const float grey[3] = {0.30f, 0.30f, 0.34f};
    for (const Bond &b : bonds) {
        bondData << b.a.x() << b.a.y() << b.a.z() << grey[0] << grey[1] << grey[2];
        bondData << b.b.x() << b.b.y() << b.b.z() << grey[0] << grey[1] << grey[2];
    }
    m_bondCount = bonds.size();
    if (m_bondCount > 0) {
        m_bondBuffer.create();
        m_bondBuffer.bind();
        m_bondBuffer.allocate(bondData.constData(), bondData.size() * int(sizeof(float)));
        m_bondBuffer.release();
    }
}

void ViewerWidget::buildBackbone()
{
    m_hasBackbone = false;
    m_traceCount = 0;
    m_ribbonCount = 0;

    // Cα trace in file order (PDB/gro files list residues sequentially).
    struct CaPoint { QVector3D pos; QString resname; };
    QVector<CaPoint> cas;
    for (const AtomData &a : m_atoms) {
        if (a.atomName.compare(QStringLiteral("CA"), Qt::CaseInsensitive) == 0)
            cas.push_back({QVector3D(a.x, a.y, a.z), a.resname});
    }
    if (cas.size() < 3)
        return;   // not a protein (or too small) — atom styles remain available
    m_hasBackbone = true;

    // Cα trace lines, rainbow-coloured per residue.
    QVector<float> traceData;
    traceData.reserve((cas.size() - 1) * 12);
    for (int i = 0; i + 1 < cas.size(); ++i) {
        const QVector3D c0 = residueColor(float(i) / float(cas.size() - 1));
        const QVector3D c1 = residueColor(float(i + 1) / float(cas.size() - 1));
        traceData << cas[i].pos.x() << cas[i].pos.y() << cas[i].pos.z()
                  << c0.x() << c0.y() << c0.z();
        traceData << cas[i + 1].pos.x() << cas[i + 1].pos.y() << cas[i + 1].pos.z()
                  << c1.x() << c1.y() << c1.z();
    }
    m_traceCount = cas.size() - 1;
    m_traceBuffer.create();
    m_traceBuffer.bind();
    m_traceBuffer.allocate(traceData.constData(), traceData.size() * int(sizeof(float)));
    m_traceBuffer.release();

    // Ribbon: smooth Catmull-Rom spline through the trace, extruded into a
    // tube (triangle soup) with rainbow colours along the sequence.
    QVector<QVector3D> spline;
    QVector<float> splineFrac;   // residue fraction along the chain
    spline.reserve((cas.size() - 1) * kSplineSteps + 1);
    auto point = [&](int i) {
        return cas[qBound(0, i, cas.size() - 1)].pos;
    };
    for (int i = 0; i + 1 < cas.size(); ++i) {
        const QVector3D &p0 = point(i - 1);
        const QVector3D &p1 = point(i);
        const QVector3D &p2 = point(i + 1);
        const QVector3D &p3 = point(i + 2);
        for (int s = 0; s < kSplineSteps; ++s) {
            const float t = float(s) / float(kSplineSteps);
            const float t2 = t * t;
            const float t3 = t2 * t;
            spline.push_back(0.5f * ((2.0f * p1) + (-p0 + p2) * t
                                     + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                                     + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3));
            splineFrac.push_back(float(i + t) / float(cas.size() - 1));
        }
    }
    spline.push_back(cas.last().pos);
    splineFrac.push_back(1.0f);
    if (spline.size() < 2)
        return;

    QVector<float> ribbon;
    ribbon.reserve(int(spline.size()) * kRibbonSegments * 6 * 6);
    const float r = kRibbonNm;
    for (int k = 0; k + 1 < spline.size(); ++k) {
        const QVector3D tangent =
            (spline[qMin(k + 1, spline.size() - 1)] - spline[qMax(k - 1, 0)]).normalized();
        QVector3D up(0.0f, 0.0f, 1.0f);
        if (qAbs(tangent.z()) > 0.9f)
            up = QVector3D(1.0f, 0.0f, 0.0f);
        const QVector3D normal = QVector3D::crossProduct(tangent, up).normalized();
        const QVector3D binormal = QVector3D::crossProduct(tangent, normal).normalized();

        QVector3D ringA[kRibbonSegments];
        QVector3D ringB[kRibbonSegments];
        for (int s = 0; s < kRibbonSegments; ++s) {
            const float theta = float(s) / float(kRibbonSegments) * 2.0f * float(M_PI);
            const QVector3D offset = (normal * float(qCos(theta))
                                      + binormal * float(qSin(theta))) * r;
            ringA[s] = spline[k] + offset;
            ringB[s] = spline[k + 1] + offset;
        }
        const QVector3D colA = residueColor(splineFrac.at(k));
        const QVector3D colB = residueColor(splineFrac.at(k + 1));
        for (int s = 0; s < kRibbonSegments; ++s) {
            const int s2 = (s + 1) % kRibbonSegments;
            auto push = [&ribbon](const QVector3D &p, const QVector3D &c) {
                ribbon << p.x() << p.y() << p.z() << c.x() << c.y() << c.z();
            };
            push(ringA[s], colA);
            push(ringB[s], colB);
            push(ringB[s2], colB);
            push(ringA[s], colA);
            push(ringB[s2], colB);
            push(ringA[s2], colA);
        }
    }
    m_ribbonCount = ribbon.size() / 6;
    m_ribbonBuffer.create();
    m_ribbonBuffer.bind();
    m_ribbonBuffer.allocate(ribbon.constData(), ribbon.size() * int(sizeof(float)));
    m_ribbonBuffer.release();
}

void ViewerWidget::setStructure(const StructureData &data)
{
    makeCurrent();
    m_atoms = data.atoms;
    m_atomCount = m_atoms.size();

    // Interleaved layout: pos(3) + colour(3) + vdW radius(1) — the radius
    // drives the space-filling point size in the vertex shader.
    QVector<float> atomData;
    atomData.reserve(m_atomCount * 7);
    QVector3D sum(0, 0, 0);
    for (const AtomData &a : m_atoms) {
        atomData << a.x << a.y << a.z << a.r << a.g << a.b
                 << (a.radius > 0.0f ? a.radius : 0.15f);
        sum += QVector3D(a.x, a.y, a.z);
    }
    m_center = m_atomCount > 0 ? sum / float(m_atomCount) : QVector3D();
    float extent = 1.0f;
    for (const AtomData &a : m_atoms) {
        const QVector3D d(a.x - m_center.x(), a.y - m_center.y(), a.z - m_center.z());
        extent = qMax(extent, d.length());
    }
    m_extent = extent;

    m_atomBuffer.create();
    m_atomBuffer.bind();
    m_atomBuffer.allocate(atomData.constData(), atomData.size() * int(sizeof(float)));
    m_atomBuffer.release();

    buildBonds();
    buildBackbone();
    doneCurrent();

    m_statusText = QObject::tr("%1 — %2 atoms, %3 bonds%4")
                       .arg(data.title.left(60), QString::number(m_atomCount),
                            QString::number(m_bondCount),
                            m_hasBackbone ? QString() : QObject::tr(" (no protein backbone)"));
    update();
}

void ViewerWidget::resetView()
{
    m_zoom = 1.0f;
    m_yaw = 25.0f;
    m_pitch = 15.0f;
    update();
}

void ViewerWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMouse = event->pos();
}

void ViewerWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint delta = event->pos() - m_lastMouse;
    m_lastMouse = event->pos();
    m_yaw += delta.x() * 0.4f;
    m_pitch = qBound(-89.0f, m_pitch + delta.y() * 0.4f, 89.0f);
    update();
}

void ViewerWidget::wheelEvent(QWheelEvent *event)
{
    const float factor = event->angleDelta().y() > 0 ? 1.15f : 1.0f / 1.15f;
    m_zoom = qBound(0.05f, m_zoom * factor, 40.0f);
    update();
}

void ViewerWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    resetView();
}
