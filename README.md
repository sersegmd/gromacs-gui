# Molecular Studio

A native desktop graphical environment that unifies GROMACS tools into a
single, organized and user-friendly workspace.

Molecular Studio does **not** replace, modify, or extend GROMACS. It provides
researchers with an integrated interface for:

- managing molecular projects,
- preparing simulations (`.mdp` parameters, topology, structure files),
- executing GROMACS operations,
- monitoring computational jobs,
- visualizing molecular structures and trajectories,
- analyzing simulation results.

## Architecture

```
┌─────────────────────────────────────────┐
│           Qt 6 GUI  (C++)               │
│  Project Manager │ MDP Editor           │
│  Job Monitor     │ Console              │
│  3D Viewer (VTK, planned)               │
└──────────────────┬──────────────────────┘
                   │ C ABI (cbindgen) via QLibrary
┌──────────────────┴──────────────────────┐
│           Core Engine  (Rust)           │
│  Job scheduler │ GROMACS runner         │
│  File parsers  │ Results analysis       │
└──────────────────┬──────────────────────┘
                   │ subprocess (unmodified)
┌──────────────────┴──────────────────────┐
│                GROMACS                  │
└─────────────────────────────────────────┘
```

- **GUI (C++ / Qt 6)** — this repository. All user interaction, project views,
  editors and rendering.
- **Core (Rust)** — a separate crate exporting a C ABI (`ms_core_version`,
  `ms_job_submit`, …). Loaded at runtime with `QLibrary`; while absent, the GUI
  runs in *stub mode* so development of both halves stays decoupled.
- **GROMACS** — invoked only as an external binary. Molecular Studio never
  writes inside a GROMACS installation.

## Repository layout

```
core/rust/molecular_core/       # Rust core engine (C ABI, std-only, no deps)
├── Cargo.toml
└── src/
    ├── lib.rs                  # FFI: job runner, structure/xvg parsers, MDP linter
    └── catalog.rs              # complete GROMACS command catalog (101 tools, 2025.2)
src/
├── main.cpp                    # QApplication bootstrap + CI demo/screenshot harness
├── app/MainWindow.{h,cpp}      # main window, menus, docks, pipeline wiring
├── gui/
│   ├── ProjectPanel.{h,cpp}        # project tree grouped by GROMACS file types
│   ├── MdpEditorPanel.{h,cpp}      # .mdp editor + templates + live Rust-core linter
│   ├── MdpTemplates.h              # shared em/ions/nvt/npt/md parameter templates
│   ├── JobMonitorPanel.{h,cpp}     # job table driven by real core job events
│   ├── ConsolePanel.{h,cpp}        # unified log for GROMACS stdout/stderr
│   ├── RunCommandDialog.{h,cpp}    # browse & run any of the 101 catalog commands
│   │                               #   + index-group picker + stdin selections
│   ├── SimulationWizard.{h,cpp}    # system templates (protein / +ligand / membrane /
│   │                               #   polymer) × 8-step chain + checkpoint resume
│   ├── AnalysisDialog.{h,cpp}      # presets → xvg plot, multi-file comparison,
│   │                               #   300-dpi PDF export, group selection
│   ├── ResourceEstimatorDialog.{h,cpp} # RAM / ns-day / wall-time heuristic planner
│   └── ViewerWidget.{h,cpp}        # 3D viewer (OpenGL, CPK, bonds) + trajectory
│                                   #   playback (play/pause/step/speed)
└── core/
    ├── CoreBridge.{h,cpp}      # runtime loader for the Rust core (C ABI via QLibrary)
    ├── JobManager.{h,cpp}      # Qt signal wrapper over core callbacks + WorkflowRunner
    └── ErrorHints.h            # smart GROMACS fatal-error translation (cause + fix)
```

## Building (Linux)

Tested distribution targets: Debian, Ubuntu, and Kali Linux.

```bash
# Dependencies (Debian / Kali / Ubuntu)
sudo apt install build-essential cmake qt6-base-dev libgl1-mesa-dev

# 1) Build the Rust core
cd core/rust/molecular_core && cargo build --release && cd ../../..

# 2) Configure & build the GUI, pointing it at the core library
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DMS_CORE_LIB="$PWD/core/rust/molecular_core/target/release/libmolecular_core.so"
cmake --build build -j$(nproc)

# Run
./build/molecular-studio
```

GROMACS itself is auto-detected (`/usr/bin/gmx`, `$GMX_BIN`, or `$PATH`).

## Status

Milestone 4 — fully functional:

- Rust core: 105-command catalogue (extracted from GROMACS 2025.2, verified
  against the official tool list), threaded job runner with live
  stdout/stderr streaming and cancellation, .gro/.pdb structure parser with
  per-atom name/residue/vdW-radius data, multi-model PDB trajectory parser
  with optional **solvent filtering** (`ms_parse_trajectory_ex`), index-group
  detection, .xvg parser, MDP validator.
- GUI: project manager, MDP editor with live validation, task monitor,
  console with **smart error translation** (cause + suggested fix for ~20
  known failure families), 3D OpenGL viewer with **four rendering styles**
  (Ball & Stick, Space-Filling, **Protein Backbone**, **Protein Ribbon** —
  a smooth rainbow-coloured cartoon tube through the Cα trace) and
  trajectory playback, simulation wizard with **four system templates**
  following the standard biomolecular workflow (structure topology → box →
  solvation → ion neutralisation → minimisation → NVT → NPT → production),
  analysis dashboard with automatic data-file discovery, group selection,
  **multi-run comparison** and **300-dpi PDF export**, resource estimator,
  GPU auto-detection, bundled demo structure, built-in Arabic user guide.
- Reliability:
  - **strictly sequential pipelines** — wizard steps execute one after
    another through `WorkflowRunner`; nothing runs in parallel, so the
    preparation→run chains can no longer race;
  - **pre-flight input checks** — every pipeline step and analysis run
    verifies its required files first and fails with a clear message
    instead of a cryptic engine error mid-chain;
  - **interactive stdin answers** (ion selection etc.) are delivered
    automatically, so long chains no longer hang on a hidden prompt;
  - **analysis data discovery** — the dashboard scans the project folder
    for energy/run-input/trajectory files and blocks reports whose data
    does not exist yet;
  - **stub-mode awareness** — a red banner + permanent status-bar indicator
    when the engine library is not linked, and every failure names its real
    cause;
  - **File → Open Structure…** opens any `.pdb/.gro` on disk;
  - **movie-frame preparation** button converts a raw trajectory into a
    playable frame sequence and loads it automatically;
  - **Show solvent checkbox** and a **system info strip** in the viewer;
  - **Help → Load sample structure** plays the bundled lysozyme demo from
    `resources/demo/`.

The user-facing interface is deliberately implementation-agnostic: no raw
command lines, tool browsers or internal paths are shown — the wizard,
dashboard and viewer speak in workflow language only.

Next milestones: parallel job queues, portable packaging (AppImage/Flatpak).

## License

MIT — see [LICENSE](LICENSE).
