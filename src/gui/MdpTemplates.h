#pragma once

#include <QString>

// MDP templates shipped with Molecular Studio, following the official
// GROMACS biomolecular workflow conventions (steep minimisation → NVT →
// NPT → production MD). Kept in one place so the MDP editor, the
// simulation wizard and the pipeline runner all use identical content.

namespace MdpTemplates {

inline QString ions()
{
    return QStringLiteral(
        "; Ions setup (pre-genion minimisation)\n"
        "integrator  = steep\n"
        "emtol       = 1000.0\n"
        "emstep      = 0.01\n"
        "nsteps      = 50000\n"
        "nstlist     = 10\n"
        "cutoff-scheme = Verlet\n"
        "ns_type     = grid\n"
        "coulombtype = PME\n"
        "rcoulomb    = 1.0\n"
        "rvdw        = 1.0\n"
        "pbc         = xyz\n");
}

inline QString minim()
{
    return QStringLiteral(
        "; Energy minimisation\n"
        "integrator  = steep\n"
        "emtol       = 1000.0\n"
        "emstep      = 0.01\n"
        "nsteps      = 50000\n"
        "nstlist     = 10\n"
        "cutoff-scheme = Verlet\n"
        "ns_type     = grid\n"
        "coulombtype = PME\n"
        "rcoulomb    = 1.0\n"
        "rvdw        = 1.0\n"
        "pbc         = xyz\n");
}

inline QString nvt()
{
    return QStringLiteral(
        "; NVT equilibration\n"
        "define      = -DFLEXIBLE\n"
        "integrator  = md\n"
        "nsteps      = 50000\n"
        "dt          = 0.002\n"
        "nstxout-compressed = 500\n"
        "continuation  = no\n"
        "constraints = h-bonds\n"
        "cutoff-scheme = Verlet\n"
        "coulombtype = PME\n"
        "rcoulomb    = 1.0\n"
        "rvdw        = 1.0\n"
        "tcoupl      = V-rescale\n"
        "tc-grps     = Protein Non-Protein\n"
        "tau_t       = 0.1 0.1\n"
        "ref_t       = 300 300\n"
        "pcoupl      = no\n"
        "pbc         = xyz\n"
        "gen_vel     = yes\n"
        "gen_temp    = 300\n"
        "gen_seed    = -1\n");
}

inline QString npt()
{
    return QStringLiteral(
        "; NPT equilibration\n"
        "define      = -DFLEXIBLE\n"
        "integrator  = md\n"
        "nsteps      = 50000\n"
        "dt          = 0.002\n"
        "continuation = yes\n"
        "constraints = h-bonds\n"
        "cutoff-scheme = Verlet\n"
        "coulombtype = PME\n"
        "rcoulomb    = 1.0\n"
        "rvdw        = 1.0\n"
        "tcoupl      = V-rescale\n"
        "tc-grps     = Protein Non-Protein\n"
        "tau_t       = 0.1 0.1\n"
        "ref_t       = 300 300\n"
        "pcoupl      = C-rescale\n"
        "pcoupltype  = isotropic\n"
        "tau_p       = 2.0\n"
        "ref_p       = 1.0\n"
        "compressibility = 4.5e-5\n"
        "pbc         = xyz\n");
}

inline QString production()
{
    return QStringLiteral(
        "; Production MD\n"
        "integrator  = md\n"
        "nsteps      = 25000000\n"
        "dt          = 0.002\n"
        "nstxout-compressed = 5000\n"
        "nstenergy   = 500\n"
        "nstlog      = 500\n"
        "continuation = yes\n"
        "constraints = h-bonds\n"
        "cutoff-scheme = Verlet\n"
        "coulombtype = PME\n"
        "rcoulomb    = 1.0\n"
        "rvdw        = 1.0\n"
        "tcoupl      = V-rescale\n"
        "tc-grps     = System\n"
        "tau_t       = 0.1\n"
        "ref_t       = 300\n"
        "pcoupl      = C-rescale\n"
        "pcoupltype  = isotropic\n"
        "tau_p       = 2.0\n"
        "ref_p       = 1.0\n"
        "compressibility = 4.5e-5\n"
        "pbc         = xyz\n");
}

} // namespace MdpTemplates
