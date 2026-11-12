#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * @brief Smart GROMACS error translation.
 *
 * Scans the tail of a failed job's stderr for known GROMACS fatal-error
 * patterns and produces a human-readable explanation + suggested fix, so
 * the console shows "what went wrong and how to fix it" instead of only
 * the raw message.
 */
namespace ErrorHints {

struct Hint {
    const char *pattern;   // case-insensitive substring of stderr
    const char *cause;     // why this happens
    const char *fix;       // concrete suggested action
};

const Hint kHints[] = {
    {"Invalid delimiter",
     "A file/path argument contains quotes or an unexpected separator.",
     "Remove stray quotes/spaces in -f/-o/-p arguments; paths with spaces must be quoted."},
    {"No such file or directory",
     "An input file given with -f/-s/-p/-c was not found in the working directory.",
     "Check the file name and the working directory; run the previous pipeline step first."},
    {"Cannot open file",
     "GROMACS could not open an input or output file (missing file or no write permission).",
     "Verify the file exists and that the project directory is writable."},
    {"Invalid order for director",
     "The file was not sorted as required (e.g. .gro/.pdb atom order vs topology).",
     "Re-generate the coordinate file from the same topology; keep atom order consistent."},
    {"atom name ... does not match",
     "Atom names in the coordinate file differ from the topology (pdb2gmx/topology mismatch).",
     "Regenerate the topology with pdb2gmx from the same structure; do not mix force fields."},
    {"Atom name mismatch",
     "Atom names in the coordinate file differ from the topology.",
     "Regenerate the topology with pdb2gmx, or fix the .gro/.pdb atom names."},
    {"Number of atoms in Topology",
     "The topology and the coordinate file describe different numbers of atoms.",
     "Use the .gro/.pdb written by the same step that produced the .top (e.g. solvated_ions.gro with the updated topology)."},
    {"Unknown bond_atomtype",
     "The force field cannot describe a bond between these atom types (often a ligand or unusual residue).",
     "Generate ligand parameters with CGenFF/Acpype and #include them in the topology."},
    {"Syntax error",
     "The .mdp file (or another text input) has invalid syntax or an unknown key.",
     "Open the .mdp in the MDP editor and fix the reported line; unknown keys are fatal in grompp."},
    {"Unknown or unsupported parameter",
     "The .mdp contains a parameter this GROMACS version does not know.",
     "Remove or rename the parameter (see 'gmx grompp -h' for valid keys)."},
    {"Too many warnings",
     "grompp stopped because warnings exceeded the limit.",
     "Fix the reported warnings, or pass -maxwarn N (only when you understand them)."},
    {"Water molecule starting at atom",
     "A water molecule exploded during MD (bad geometry/velocity, usually after bad minimisation).",
     "Re-run minimisation, lower dt (e.g. 0.001), or increase minimisation steps before production."},
    {"Size of tc-grps ... does not match",
     "The temperature-coupling groups do not cover (or overlap) the whole system.",
     "Set tc-grps to Protein Non-Protein (or System) and give ref_t/tau_t for each group."},
    {"Tcoupl group ... is not affected",
     "tc-grps references a group that does not exist in the current system.",
     "Use groups from 'make_ndx' output, e.g. Protein and Non-Protein."},
    {"Invalid delimiter for filename in #include",
     "The topology has a broken #include statement.",
     "Check the .top #include lines for missing quotes or paths."},
    {"Device mismatch",
     "GPU/MPI configuration of mdrun differs from the one used by grompp.",
     "Run mdrun on the same hardware configuration, or re-run grompp."},
    {"Cannot find matching coordinates",
     "Restart/checkpoint (-t/-cpi) does not match the run input.",
     "Use the matching .cpt/.tpr pair; never mix checkpoints from different runs."},
    {"Segmentation fault",
     "GROMACS crashed (memory or build issue).",
     "Re-run with fewer threads (-ntomp), verify the binary matches the CPU, and check the .log file."},
    {"Not enough memory",
     "The system is too large for the available RAM.",
     "Use the Resource Estimator (Tools menu) to size the system, or run on a larger machine."},
    {"Fatal error",
     "GROMACS reported a fatal error (see the lines above for the specific cause).",
     "Read the first line after 'Fatal error' — it names the file or parameter responsible."},
};

//! Returns a formatted explanation block for the given stderr tail,
//! or an empty string when nothing matches.
inline QString explain(const QString &stderrTail)
{
    if (stderrTail.trimmed().isEmpty())
        return QString();
    for (const Hint &h : kHints) {
        if (stderrTail.contains(h.pattern, Qt::CaseInsensitive)) {
            return QStringLiteral("── Diagnosis ──\n%1\n── Suggested fix ──\n%2")
                .arg(QString::fromUtf8(h.cause), QString::fromUtf8(h.fix));
        }
    }
    return QString();
}

} // namespace ErrorHints
