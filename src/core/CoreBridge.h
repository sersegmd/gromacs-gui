#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

class QLibrary;

struct GmxCommand {
    QString name;
    QString description;
    QString category;
};

struct AtomData {
    QString element;
    QString atomName;       // e.g. CA, N, O (backbone detection in the viewer)
    QString resname;        // e.g. LYS, SOL
    float x, y, z;          // nm
    float radius = 0.15f;   // vdW radius (nm) for space-filling rendering
    float r, g, b;          // CPK colour
};

struct StructureData {
    QString title;
    int natoms = 0;
    float box[3] = {0, 0, 0};
    QVector<AtomData> atoms;
};

struct XvgSeries {
    QString legend;
    QVector<QPointF> points;
};

struct XvgData {
    QString xlabel;
    QString ylabel;
    QVector<XvgSeries> series;
};

//! A make_ndx-style default index group reported by the core.
struct IndexGroupInfo {
    QString name;
    int count = 0;
};

struct MdpIssue {
    QString severity;   // error | warning | info
    QString key;
    QString message;
};

//! Core event callback: level 0 = stdout, 1 = stderr, 2 = control JSON.
using MsEventCallback = void (*)(int level, const char *msg, void *user);

/**
 * @brief C++ handle to the Rust core library (C ABI via QLibrary).
 *
 * Every call forwards to `molecular_core`. When the library cannot be
 * loaded the GUI degrades to stub mode; isCoreAvailable() reports it.
 */
class CoreBridge : public QObject
{
    Q_OBJECT

public:
    explicit CoreBridge(QObject *parent = nullptr);

    QString versionString() const;
    bool coreAvailable() const { return m_coreAvailable; }

    //! Full GROMACS command catalog from the Rust core (101 entries).
    QVector<GmxCommand> commandCatalog() const;

    //! Path of a detected GROMACS binary; empty when none was found.
    QString detectGmx() const;

    //! Parses .gro/.pdb into typed data for the 3D viewer.
    StructureData parseStructure(const QString &path) const;

    //! Parses a GROMACS .xvg analysis output for plotting.
    XvgData parseXvg(const QString &path) const;

    //! Lints an MDP file body.
    QVector<MdpIssue> validateMdp(const QString &content) const;

    //! make_ndx-style default index groups of a .gro/.pdb structure file.
    QVector<IndexGroupInfo> indexGroups(const QString &path) const;

    //! Parses a multi-model PDB trajectory (gmx trjconv -o dump.pdb) into frames.
    //! When skipSolvent is set, water atoms are dropped for faster rendering.
    //! On failure returns an empty vector; lastError() explains why
    //! ("core-not-linked", "cannot read file", "no frames parsed", …).
    QVector<StructureData> parseTrajectory(const QString &path, bool skipSolvent = false) const;

    //! Human-readable failure reason of the last parse/catalog call (empty if ok).
    QString lastError() const { return m_lastError; }

    //! Submits a shell command to the core job runner; returns job id > 0.
    int submitJob(const QString &command, const QString &workdir,
                  const QString &stdinData, MsEventCallback cb, void *user) const;

    //! Cancels a running job. True when a termination signal was sent.
    bool cancelJob(int jobId) const;

private:
    // FFI entry types (must match molecular_core declarations).
    using MsVersionFn      = const char *(*)(void);
    using MsFreeStringFn   = void (*)(char *);
    using MsCatalogFn      = char *(*)(void);
    using MsDetectGmxFn    = char *(*)(void);
    using MsParseStructFn  = char *(*)(const char *);
    using MsParseXvgFn     = char *(*)(const char *);
    using MsValidateMdpFn  = char *(*)(const char *);
    using MsIndexGroupsFn  = char *(*)(const char *);
    using MsParseTrajFn    = char *(*)(const char *);
    using MsParseTrajExFn  = char *(*)(const char *, int);
    using MsSubmitFn       = int (*)(const char *, const char *, const char *,
                                     MsEventCallback, void *);
    using MsCancelFn       = bool (*)(int);

    QString takeString(char *owned) const;

    bool m_coreAvailable = false;
    QString m_coreVersion;

    QLibrary *m_lib = nullptr;
    MsFreeStringFn m_freeString = nullptr;
    MsCatalogFn m_catalog = nullptr;
    MsDetectGmxFn m_detectGmx = nullptr;
    MsParseStructFn m_parseStructure = nullptr;
    MsParseXvgFn m_parseXvg = nullptr;
    MsValidateMdpFn m_validateMdp = nullptr;
    MsIndexGroupsFn m_indexGroups = nullptr;
    MsParseTrajFn m_parseTrajectory = nullptr;
    MsParseTrajExFn m_parseTrajectoryEx = nullptr;
    mutable QString m_lastError;
    MsSubmitFn m_submitJob = nullptr;
    MsCancelFn m_cancelJob = nullptr;
};
