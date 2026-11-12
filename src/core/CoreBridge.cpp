#include "CoreBridge.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>

// C ABI expected from the Rust core crate (`molecular_core`); the same
// signatures could be generated with cbindgen from the #[no_mangle]
// pub extern "C" functions. The job-runner entry points live in JobManager
// (they need callback plumbing).

CoreBridge::CoreBridge(QObject *parent)
    : QObject(parent)
{
#ifdef MS_CORE_LIB
    m_lib = new QLibrary(QStringLiteral(MS_CORE_LIB), this);
    if (m_lib->load()) {
        m_freeString = reinterpret_cast<MsFreeStringFn>(m_lib->resolve("ms_free_string"));
        m_catalog = reinterpret_cast<MsCatalogFn>(m_lib->resolve("ms_command_catalog_json"));
        m_detectGmx = reinterpret_cast<MsDetectGmxFn>(m_lib->resolve("ms_detect_gmx"));
        m_parseStructure = reinterpret_cast<MsParseStructFn>(m_lib->resolve("ms_parse_structure"));
        m_parseXvg = reinterpret_cast<MsParseXvgFn>(m_lib->resolve("ms_parse_xvg"));
        m_validateMdp = reinterpret_cast<MsValidateMdpFn>(m_lib->resolve("ms_validate_mdp"));
        m_indexGroups = reinterpret_cast<MsIndexGroupsFn>(m_lib->resolve("ms_index_groups"));
        m_parseTrajectory = reinterpret_cast<MsParseTrajFn>(m_lib->resolve("ms_parse_trajectory"));
        m_parseTrajectoryEx = reinterpret_cast<MsParseTrajExFn>(m_lib->resolve("ms_parse_trajectory_ex"));
        m_submitJob = reinterpret_cast<MsSubmitFn>(m_lib->resolve("ms_job_submit"));
        m_cancelJob = reinterpret_cast<MsCancelFn>(m_lib->resolve("ms_job_cancel"));

        if (auto versionFn = reinterpret_cast<MsVersionFn>(m_lib->resolve("ms_core_version"))) {
            m_coreVersion = QString::fromUtf8(versionFn());
            m_coreAvailable = (m_freeString != nullptr);
        }
    }
#endif
}

QString CoreBridge::versionString() const
{
    if (m_coreAvailable)
        return QStringLiteral("Rust core %1").arg(m_coreVersion);
    return tr("No core linked (stub mode)");
}

QString CoreBridge::takeString(char *owned) const
{
    if (!owned)
        return {};
    const QString s = QString::fromUtf8(owned);
    if (m_freeString)
        m_freeString(owned);
    return s;
}

namespace {
// Atom JSON rows: core >=0.4 emits [element,name,resname,x,y,z,radius,r,g,b];
// older cores emit [element,x,y,z,0,r,g,b]. Both are accepted so a slightly
// mismatched core build degrades gracefully instead of losing atoms.
bool atomFromJson(const QJsonArray &arr, AtomData &atom)
{
    if (arr.size() >= 10) {
        atom.element = arr.at(0).toString();
        atom.atomName = arr.at(1).toString();
        atom.resname = arr.at(2).toString();
        atom.x = static_cast<float>(arr.at(3).toDouble());
        atom.y = static_cast<float>(arr.at(4).toDouble());
        atom.z = static_cast<float>(arr.at(5).toDouble());
        atom.radius = static_cast<float>(arr.at(6).toDouble());
        atom.r = static_cast<float>(arr.at(7).toDouble());
        atom.g = static_cast<float>(arr.at(8).toDouble());
        atom.b = static_cast<float>(arr.at(9).toDouble());
        return true;
    }
    if (arr.size() >= 8) {
        atom.element = arr.at(0).toString();
        atom.atomName.clear();
        atom.resname.clear();
        atom.x = static_cast<float>(arr.at(1).toDouble());
        atom.y = static_cast<float>(arr.at(2).toDouble());
        atom.z = static_cast<float>(arr.at(3).toDouble());
        atom.radius = 0.15f;
        atom.r = static_cast<float>(arr.at(5).toDouble());
        atom.g = static_cast<float>(arr.at(6).toDouble());
        atom.b = static_cast<float>(arr.at(7).toDouble());
        return true;
    }
    return false;
}
} // namespace

QVector<GmxCommand> CoreBridge::commandCatalog() const
{
    QVector<GmxCommand> result;
    if (!m_coreAvailable || !m_catalog)
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(takeString(m_catalog()).toUtf8());
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        result.push_back({o.value("name").toString(),
                          o.value("description").toString(),
                          o.value("category").toString()});
    }
    return result;
}

QString CoreBridge::detectGmx() const
{
    if (!m_coreAvailable || !m_detectGmx)
        return {};
    return takeString(m_detectGmx());
}

StructureData CoreBridge::parseStructure(const QString &path) const
{
    StructureData data;
    m_lastError.clear();
    if (!m_coreAvailable || !m_parseStructure) {
        m_lastError = QStringLiteral("core-not-linked");
        return data;
    }
    const QByteArray pathUtf8 = QFile::encodeName(path);
    const QJsonDocument doc =
        QJsonDocument::fromJson(takeString(m_parseStructure(pathUtf8.constData())).toUtf8());
    const QJsonObject o = doc.object();
    if (o.contains("error")) {
        m_lastError = o.value("error").toString();
        return data;
    }
    data.title = o.value("title").toString();
    data.natoms = o.value("natoms").toInt(0);
    const QJsonArray box = o.value("box").toArray();
    for (int i = 0; i < 3 && i < box.size(); ++i)
        data.box[i] = static_cast<float>(box.at(i).toDouble());
    for (const QJsonValue &a : o.value("atoms").toArray()) {
        AtomData atom;
        if (atomFromJson(a.toArray(), atom))
            data.atoms.push_back(atom);
    }
    return data;
}

XvgData CoreBridge::parseXvg(const QString &path) const
{
    XvgData data;
    if (!m_coreAvailable || !m_parseXvg)
        return data;
    const QByteArray pathUtf8 = QFile::encodeName(path);
    const QJsonDocument doc =
        QJsonDocument::fromJson(takeString(m_parseXvg(pathUtf8.constData())).toUtf8());
    const QJsonObject o = doc.object();
    if (o.contains("error"))
        return data;
    data.xlabel = o.value("xlabel").toString();
    data.ylabel = o.value("ylabel").toString();
    for (const QJsonValue &s : o.value("series").toArray()) {
        const QJsonObject so = s.toObject();
        XvgSeries series;
        series.legend = so.value("legend").toString();
        for (const QJsonValue &p : so.value("points").toArray()) {
            const QJsonArray xy = p.toArray();
            if (xy.size() >= 2)
                series.points.push_back(QPointF(xy.at(0).toDouble(), xy.at(1).toDouble()));
        }
        data.series.push_back(series);
    }
    return data;
}

QVector<IndexGroupInfo> CoreBridge::indexGroups(const QString &path) const
{
    QVector<IndexGroupInfo> result;
    if (!m_coreAvailable || !m_indexGroups)
        return result;
    const QByteArray pathUtf8 = QFile::encodeName(path);
    const QJsonDocument doc =
        QJsonDocument::fromJson(takeString(m_indexGroups(pathUtf8.constData())).toUtf8());
    for (const QJsonValue &v : doc.object().value("groups").toArray()) {
        const QJsonObject o = v.toObject();
        result.push_back({o.value("name").toString(), o.value("count").toInt(0)});
    }
    return result;
}

QVector<StructureData> CoreBridge::parseTrajectory(const QString &path, bool skipSolvent) const
{
    QVector<StructureData> frames;
    m_lastError.clear();
    if (!m_coreAvailable || !(m_parseTrajectoryEx || m_parseTrajectory)) {
        m_lastError = QStringLiteral("core-not-linked");
        return frames;
    }
    const QByteArray pathUtf8 = QFile::encodeName(path);
    char *owned = m_parseTrajectoryEx
        ? m_parseTrajectoryEx(pathUtf8.constData(), skipSolvent ? 1 : 0)
        : m_parseTrajectory(pathUtf8.constData());
    const QJsonDocument doc =
        QJsonDocument::fromJson(takeString(owned).toUtf8());
    const QJsonObject root = doc.object();
    if (root.contains("error")) {
        m_lastError = root.value("error").toString();
        return frames;
    }
    for (const QJsonValue &f : root.value("frames").toArray()) {
        const QJsonObject o = f.toObject();
        StructureData data;
        data.title = o.value("title").toString();
        data.natoms = o.value("natoms").toInt(0);
        for (const QJsonValue &a : o.value("atoms").toArray()) {
            AtomData atom;
            if (atomFromJson(a.toArray(), atom))
                data.atoms.push_back(atom);
        }
        frames.push_back(data);
    }
    return frames;
}

QVector<MdpIssue> CoreBridge::validateMdp(const QString &content) const
{
    QVector<MdpIssue> result;
    if (!m_coreAvailable || !m_validateMdp)
        return result;
    const QByteArray body = content.toUtf8();
    const QJsonDocument doc =
        QJsonDocument::fromJson(takeString(m_validateMdp(body.constData())).toUtf8());
    for (const QJsonValue &v : doc.object().value("issues").toArray()) {
        const QJsonObject o = v.toObject();
        result.push_back({o.value("severity").toString(),
                          o.value("key").toString(),
                          o.value("message").toString()});
    }
    return result;
}

int CoreBridge::submitJob(const QString &command, const QString &workdir,
                          const QString &stdinData, MsEventCallback cb, void *user) const
{
    if (!m_coreAvailable || !m_submitJob)
        return -1;
    const QByteArray cmdUtf8 = QFile::encodeName(command);
    const QByteArray dirUtf8 = workdir.isEmpty() ? QByteArray(".") : QFile::encodeName(workdir);
    const QByteArray stdinUtf8 = stdinData.toUtf8();
    return m_submitJob(cmdUtf8.constData(), dirUtf8.constData(),
                       stdinData.isEmpty() ? nullptr : stdinUtf8.constData(),
                       cb, user);
}

bool CoreBridge::cancelJob(int jobId) const
{
    if (!m_coreAvailable || !m_cancelJob)
        return false;
    return m_cancelJob(jobId);
}
