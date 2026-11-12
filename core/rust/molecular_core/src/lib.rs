//! molecular_core — the Rust core engine of Molecular Studio.
//!
//! Exposes a C ABI (consumed by the Qt GUI through QLibrary) for:
//! - the complete GROMACS command catalog,
//! - a job runner that executes GROMACS as an unmodified subprocess and
//!   streams stdout/stderr back to the GUI through callbacks,
//! - structure (.gro/.pdb), trajectory-output (.xvg) and MDP-file parsing,
//! - an MDP lint/validator.
//!
//! The core never modifies GROMACS; it only invokes it as an external binary.

mod catalog;

use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::io::{BufRead, BufReader, Write};
use std::os::raw::{c_char, c_int, c_void};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::thread;

pub type EventCallback = extern "C" fn(level: c_int, msg: *const c_char, user: *mut c_void);

// Event levels passed to the GUI callback.
pub const EV_STDOUT: c_int = 0;
pub const EV_STDERR: c_int = 1;
pub const EV_CONTROL: c_int = 2;

// Job states reported via ms_job_status and the control event.
pub const ST_RUNNING: u8 = 0;
pub const ST_COMPLETED: u8 = 1;
pub const ST_FAILED: u8 = 2;
pub const ST_CANCELLED: u8 = 3;

// Event callback holder — Copy so it can be shared across reader threads.
#[derive(Clone, Copy)]
struct Ev(EventCallback, *mut c_void);

// SAFETY: the callback is a plain function pointer and the user pointer is
// owned by the GUI, which keeps it alive (and handles cross-thread delivery)
// for the whole lifetime of the job.
unsafe impl Send for Ev {}

impl Ev {
    fn call(&self, level: c_int, msg: &str) {
        if let Ok(cmsg) = CString::new(msg.replace('\0', "")) {
            (self.0)(level, cmsg.as_ptr(), self.1);
        }
    }
}

struct JobHandle {
    state: Arc<AtomicU8>,
    cancel: Arc<AtomicBool>,
    pid: Mutex<Option<u32>>,
}

fn jobs() -> &'static Mutex<HashMap<i32, JobHandle>> {
    static JOBS: OnceLock<Mutex<HashMap<i32, JobHandle>>> = OnceLock::new();
    JOBS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn next_id() -> i32 {
    static ID: AtomicI32 = AtomicI32::new(1);
    ID.fetch_add(1, Ordering::SeqCst)
}

// ---------------------------------------------------------------------------
// JSON helpers (hand-rolled; the core depends on nothing outside std).
// ---------------------------------------------------------------------------

pub fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

fn cstr_to_str<'a>(ptr: *const c_char) -> Option<&'a str> {
    if ptr.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(ptr) }.to_str().ok()
}

/// Copies a Rust string into a C-owned buffer (freed with ms_free_string).
fn to_c_string(s: &str) -> *mut c_char {
    match CString::new(s.replace('\0', "")) {
        Ok(cs) => cs.into_raw(),
        Err(_) => CString::default().into_raw(),
    }
}

// ---------------------------------------------------------------------------
// C ABI: version + string lifecycle
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn ms_core_version() -> *const c_char {
    static VERSION: OnceLock<CString> = OnceLock::new();
    VERSION
        .get_or_init(|| CString::new("0.3.0 (GROMACS catalog 2025.2)").unwrap())
        .as_ptr() as *const c_char
}

#[no_mangle]
pub extern "C" fn ms_free_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe { drop(CString::from_raw(ptr)) };
    }
}

// ---------------------------------------------------------------------------
// C ABI: command catalog
// ---------------------------------------------------------------------------

/// Returns a JSON array: [{"name":..,"description":..,"category":..}, ...]
#[no_mangle]
pub extern "C" fn ms_command_catalog_json() -> *mut c_char {
    let mut out = String::from("[");
    for (i, (name, desc, cat)) in catalog::CATALOG.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        out.push_str(&format!(
            "{{\"name\":\"{}\",\"description\":\"{}\",\"category\":\"{}\"}}",
            json_escape(name),
            json_escape(desc),
            json_escape(cat)
        ));
    }
    out.push(']');
    to_c_string(&out)
}

/// Detects a usable GROMACS binary. Checks GMX_BIN env, common install
/// locations and PATH. Returns "" when nothing was found.
#[no_mangle]
pub extern "C" fn ms_detect_gmx() -> *mut c_char {
    let candidates: Vec<String> = vec![
        std::env::var("GMX_BIN").unwrap_or_default(),
        "/usr/local/gromacs/bin/gmx".into(),
        "/usr/bin/gmx".into(),
        "/opt/gromacs/bin/gmx".into(),
    ];
    for c in &candidates {
        if !c.is_empty() && std::path::Path::new(c).exists() {
            return to_c_string(c);
        }
    }
    if let Ok(path_var) = std::env::var("PATH") {
        for dir in path_var.split(':') {
            let p = std::path::Path::new(dir).join("gmx");
            if p.exists() {
                return to_c_string(&p.to_string_lossy());
            }
        }
    }
    to_c_string("")
}

// ---------------------------------------------------------------------------
// C ABI: job runner
// ---------------------------------------------------------------------------

/// Spawns `cmd` in a shell under `workdir` (fallback: current dir), feeding
/// `stdin_data` (may be NULL) to the process. stdout/stderr lines and state
/// changes are reported through `cb`:
///   level 0: stdout line, level 1: stderr line,
///   level 2: control JSON {"event":"state","state":"..","exit":N}
/// Returns the job id (>0). Never blocks.
#[no_mangle]
pub extern "C" fn ms_job_submit(
    cmd: *const c_char,
    workdir: *const c_char,
    stdin_data: *const c_char,
    cb: Option<EventCallback>,
    user: *mut c_void,
) -> c_int {
    let Some(cmd) = cstr_to_str(cmd) else { return -1 };
    let workdir = cstr_to_str(workdir).unwrap_or(".");
    let stdin_data = cstr_to_str(stdin_data).map(|s| s.to_string());
    let cmd = cmd.to_string();
    let workdir = workdir.to_string();
    let callback = match cb {
        Some(f) => f,
        None => return -2,
    };

    let id = next_id();
    let state = Arc::new(AtomicU8::new(ST_RUNNING));
    let cancel = Arc::new(AtomicBool::new(false));

    jobs().lock().unwrap().insert(
        id,
        JobHandle {
            state: state.clone(),
            cancel: cancel.clone(),
            pid: Mutex::new(None),
        },
    );

    let ev = Ev(callback, user);

    let id_thread = id;
    thread::spawn(move || {
        let mut full = Command::new("/bin/sh");
        full.arg("-c").arg(&cmd).current_dir(&workdir);
        full.stdout(Stdio::piped()).stderr(Stdio::piped());
        if stdin_data.is_some() {
            full.stdin(Stdio::piped());
        }

        match full.spawn() {
            Ok(mut child) => {
                let pid = child.id();
                jobs().lock().unwrap().get(&id_thread).and_then(|h| {
                    *h.pid.lock().unwrap() = Some(pid);
                    Some(())
                });

                let stdout = child.stdout.take();
                let stderr = child.stderr.take();
                let mut stdin_pipe = child.stdin.take();

                // stdin writer (must finish so the child sees EOF)
                if let (Some(pipe), Some(data)) = (stdin_pipe.as_mut(), stdin_data.clone()) {
                    let _ = pipe.write_all(data.as_bytes());
                    let _ = pipe.flush();
                }
                drop(stdin_pipe);

                let cb_out = ev;
                let cb_err = ev;
                let out_cancel = cancel.clone();
                let t_out = thread::spawn(move || {
                    if let Some(out) = stdout {
                        for line in BufReader::new(out).lines().map_while(Result::ok) {
                            if out_cancel.load(Ordering::Relaxed) {
                                break;
                            }
                            cb_out.call(EV_STDOUT, &line);
                        }
                    }
                });
                let err_cancel = cancel.clone();
                let t_err = thread::spawn(move || {
                    if let Some(err) = stderr {
                        for line in BufReader::new(err).lines().map_while(Result::ok) {
                            if err_cancel.load(Ordering::Relaxed) {
                                break;
                            }
                            cb_err.call(EV_STDERR, &line);
                        }
                    }
                });

                let status = child.wait();
                let _ = t_out.join();
                let _ = t_err.join();

                let final_state = if cancel.load(Ordering::Relaxed) {
                    ST_CANCELLED
                } else {
                    match &status {
                        Ok(s) if s.success() => ST_COMPLETED,
                        Ok(_) => ST_FAILED,
                        Err(_) => ST_FAILED,
                    }
                };
                state.store(final_state, Ordering::SeqCst);
                jobs().lock().unwrap().remove(&id_thread);

                let exit_code = status.map(|s| s.code().unwrap_or(-1)).unwrap_or(-1);
                let state_name = match final_state {
                    ST_COMPLETED => "completed",
                    ST_FAILED => "failed",
                    ST_CANCELLED => "cancelled",
                    _ => "running",
                };
                ev.call(
                    EV_CONTROL,
                    &format!(
                        "{{\"event\":\"state\",\"job\":{},\"state\":\"{}\",\"exit\":{}}}",
                        id_thread, state_name, exit_code
                    ),
                );
            }
            Err(e) => {
                state.store(ST_FAILED, Ordering::SeqCst);
                jobs().lock().unwrap().remove(&id_thread);
                ev.call(
                    EV_CONTROL,
                    &format!(
                        "{{\"event\":\"state\",\"job\":{},\"state\":\"failed\",\"exit\":-1,\"error\":\"{}\"}}",
                        id_thread,
                        json_escape(&e.to_string())
                    ),
                );
            }
        }
    });

    id
}

/// Returns the job state (0 running, 1 completed, 2 failed, 3 cancelled,
/// -1 unknown id).
#[no_mangle]
pub extern "C" fn ms_job_status(id: c_int) -> c_int {
    match jobs().lock().unwrap().get(&id) {
        Some(h) => h.state.load(Ordering::SeqCst) as c_int,
        None => -1,
    }
}

/// Cancels a running job (SIGTERM via `kill`). Returns true if a signal
/// was sent.
#[no_mangle]
pub extern "C" fn ms_job_cancel(id: c_int) -> bool {
    let handle = jobs().lock().unwrap().get(&id).map(|h| {
        h.cancel.store(true, Ordering::Relaxed);
        *h.pid.lock().unwrap()
    });
    match handle {
        Some(Some(pid)) => {
            let _ = Command::new("kill").arg(pid.to_string()).status();
            true
        }
        _ => false,
    }
}

// ---------------------------------------------------------------------------
// C ABI: structure parsing (.gro / .pdb)
// ---------------------------------------------------------------------------

fn element_from_atom_name(name: &str) -> String {
    let letters: String = name.chars().filter(|c| c.is_ascii_alphabetic()).collect();
    if letters.is_empty() {
        return "X".into();
    }
    let first = letters.as_bytes()[0] as char;
    let upper = first.to_ascii_uppercase();
    // Two-letter guesses are limited to ions/rare atoms that GROMACS names
    // explicitly; e.g. "CA" is an alpha-carbon, not calcium.
    if letters.len() >= 2 {
        let two: String = letters.chars().take(2).map(|c| c.to_ascii_uppercase()).collect();
        if ["CL", "NA", "MG", "ZN", "BR", "SE", "CS", "LI"].contains(&two.as_str()) {
            return two;
        }
    }
    upper.to_string()
}

fn cpk_color(element: &str) -> (f64, f64, f64) {
    match element {
        "C" => (0.30, 0.30, 0.30),
        "O" => (0.90, 0.20, 0.15),
        "N" => (0.15, 0.25, 0.90),
        "H" => (0.95, 0.95, 0.95),
        "S" => (0.95, 0.85, 0.20),
        "P" => (1.00, 0.55, 0.00),
        "FE" => (0.80, 0.20, 0.20),
        "NA" => (0.65, 0.35, 0.85),
        "CL" => (0.20, 0.85, 0.45),
        _ => (0.75, 0.40, 0.85),
    }
}

/// Van der Waals radii in nm (space-filling rendering); default 0.15.
fn vdW_radius(element: &str) -> f64 {
    match element {
        "H" => 0.12,
        "C" => 0.17,
        "N" => 0.155,
        "O" => 0.152,
        "S" => 0.18,
        "P" => 0.18,
        "CL" => 0.175,
        "NA" => 0.227,
        "MG" => 0.173,
        "ZN" => 0.139,
        "FE" => 0.204,
        "K" => 0.275,
        "CA" => 0.231,
        _ => 0.15,
    }
}

fn parse_gro(
    content: &str,
) -> Option<(String, Vec<(String, String, String, f64, f64, f64)>, [f64; 3])> {
    let lines: Vec<&str> = content.lines().collect();
    if lines.len() < 3 {
        return None;
    }
    let title = lines[0].trim().to_string();
    let natoms: usize = lines[1].trim().parse().ok()?;
    let mut atoms = Vec::with_capacity(natoms.min(1_000_000));
    for line in lines.iter().take(2 + natoms).skip(2) {
        if line.chars().count() >= 30 {
            let name = line[10..15].trim().to_string();
            let resname = line.get(5..10).unwrap_or("").trim().to_string();
            let x: f64 = line[20..28].trim().parse().unwrap_or(0.0);
            let y: f64 = line[28..36].trim().parse().unwrap_or(0.0);
            let z: f64 = line[36..44].trim().parse().unwrap_or(0.0);
            let element = element_from_atom_name(&name);
            atoms.push((element, name, resname, x, y, z));
        }
    }
    // Box line (last line) — v1(x) v2(y) v3(z) for orthogonal boxes.
    let mut box3 = [0.0f64; 3];
    if let Some(last) = lines.last() {
        let vals: Vec<f64> = last.split_whitespace().filter_map(|v| v.parse().ok()).collect();
        if vals.len() >= 3 {
            box3 = [vals[0], vals[1], vals[2]];
        }
    }
    Some((title, atoms, box3))
}

fn parse_pdb(
    content: &str,
) -> Option<(String, Vec<(String, String, String, f64, f64, f64)>, [f64; 3])> {
    let mut title = String::from("PDB structure");
    let mut atoms = Vec::new();
    let mut box3 = [0.0f64; 3];
    for line in content.lines() {
        if let Some(t) = line.strip_prefix("TITLE    ") {
            if title == "PDB structure" {
                title = t.trim().to_string();
            }
        } else if line.starts_with("CRYST1") {
            let vals: Vec<f64> = line[6..54]
                .split_whitespace()
                .filter_map(|v| v.parse().ok())
                .collect();
            if vals.len() >= 3 {
                box3 = [vals[0], vals[1], vals[2]];
            }
        } else if line.starts_with("ATOM  ") || line.starts_with("HETATM") {
            if line.len() >= 54 {
                let name = line[12..16].trim().to_string();
                let resname = line.get(17..20).unwrap_or("").trim().to_string();
                let x: f64 = line[30..38].trim().parse().unwrap_or(0.0);
                let y: f64 = line[38..46].trim().parse().unwrap_or(0.0);
                let z: f64 = line[46..54].trim().parse().unwrap_or(0.0);
                let elem = line.get(76..78).map(|s| s.trim()).filter(|s| !s.is_empty());
                let element =
                    elem.map(String::from).unwrap_or_else(|| element_from_atom_name(&name));
                atoms.push((element, name, resname, x / 10.0, y / 10.0, z / 10.0)); // Å → nm
            }
        }
    }
    if atoms.is_empty() {
        None
    } else {
        Some((title, atoms, box3))
    }
}

/// Parses a structure file (.gro or .pdb) into JSON:
/// {"title":..,"natoms":N,"box":[a,b,c],
///  "atoms":[[element,x,y,z,radius,r,g,b], ...]}   (coordinates in nm)
#[no_mangle]
pub extern "C" fn ms_parse_structure(path: *const c_char) -> *mut c_char {
    let Some(path) = cstr_to_str(path) else { return to_c_string("{\"error\":\"null path\"}") };
    let Ok(content) = std::fs::read_to_string(path) else {
        return to_c_string("{\"error\":\"cannot read file\"}");
    };

    let is_gro = path.to_lowercase().ends_with(".gro");
    let parsed = if is_gro {
        parse_gro(&content)
    } else {
        parse_pdb(&content)
    };
    let Some((title, atoms, box3)) = parsed else {
        return to_c_string("{\"error\":\"unrecognized structure format\"}");
    };

    let mut out = format!(
        "{{\"title\":\"{}\",\"natoms\":{},\"box\":[{:.3},{:.3},{:.3}],\"atoms\":[",
        json_escape(&title),
        atoms.len(),
        box3[0],
        box3[1],
        box3[2]
    );
    for (i, (element, name, resname, x, y, z)) in atoms.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        let (r, g, b) = cpk_color(element);
        out.push_str(&format!(
            "[\"{}\",\"{}\",\"{}\",{:.3},{:.3},{:.3},{:.3},{:.3},{:.3},{:.3}]",
            json_escape(element),
            json_escape(name),
            json_escape(resname),
            x,
            y,
            z,
            vdW_radius(element),
            r,
            g,
            b
        ));
    }
    out.push_str("]}");
    to_c_string(&out)
}

// ---------------------------------------------------------------------------
// C ABI: xvg parsing (GROMACS analysis output)
// ---------------------------------------------------------------------------

/// Parses a GROMACS .xvg file into JSON:
/// {"series":[{"legend":..,"points":[[x,y],...]}]}
#[no_mangle]
pub extern "C" fn ms_parse_xvg(path: *const c_char) -> *mut c_char {
    let Some(path) = cstr_to_str(path) else { return to_c_string("{\"error\":\"null path\"}") };
    let Ok(content) = std::fs::read_to_string(path) else {
        return to_c_string("{\"error\":\"cannot read file\"}");
    };

    let mut legends: Vec<String> = Vec::new();
    let mut series: Vec<Vec<(f64, f64)>> = Vec::new();
    let mut xlabel = String::new();
    let mut ylabel = String::new();

    for line in content.lines() {
        let t = line.trim();
        if t.starts_with('#') || t.is_empty() {
            continue;
        }
        if t.starts_with('@') {
            let a = t.trim_start_matches('@').trim();
            if let Some(rest) = a.strip_prefix("xaxis label") {
                xlabel = rest.trim().trim_matches('"').to_string();
            } else if let Some(rest) = a.strip_prefix("yaxis label") {
                ylabel = rest.trim().trim_matches('"').to_string();
            } else if a.starts_with('s') {
                // Series legend pattern: s<n> legend "..."
                let rest = &a[1..];
                let digits: String =
                    rest.chars().take_while(|c| c.is_ascii_digit()).collect();
                let after = rest[digits.len()..].trim_start();
                if !digits.is_empty() {
                    if let Some(legend_part) = after.strip_prefix("legend") {
                        let idx = digits.parse::<usize>().unwrap_or(0);
                        let legend =
                            legend_part.trim().trim_matches('"').to_string();
                        while legends.len() <= idx {
                            legends.push(String::new());
                        }
                        legends[idx] = legend;
                    }
                }
            }
            continue;
        }
        let vals: Vec<f64> = t.split_whitespace().filter_map(|v| v.parse().ok()).collect();
        if vals.is_empty() {
            continue;
        }
        let x = vals[0];
        if series.is_empty() {
            let n = vals.len().saturating_sub(1).max(1);
            for _ in 0..n {
                series.push(Vec::new());
            }
        }
        for (si, val) in vals.iter().skip(1).enumerate() {
            if si < series.len() {
                series[si].push((x, *val));
            }
        }
    }

    let mut out = format!(
        "{{\"xlabel\":\"{}\",\"ylabel\":\"{}\",\"series\":[",
        json_escape(&xlabel),
        json_escape(&ylabel)
    );
    for (i, points) in series.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        let legend = legends.get(i).cloned().unwrap_or_default();
        out.push_str(&format!("{{\"legend\":\"{}\",\"points\":[", json_escape(&legend)));
        for (j, (x, y)) in points.iter().enumerate() {
            if j > 0 {
                out.push(',');
            }
            out.push_str(&format!("[{:.6},{:.6}]", x, y));
        }
        out.push_str("]}");
    }
    out.push_str("]}");
    to_c_string(&out)
}

// ---------------------------------------------------------------------------
// C ABI: index-group detection (make_ndx-style default groups)
// ---------------------------------------------------------------------------

const WATER_RESNAMES: &[&str] = &["SOL", "HOH", "WAT", "TIP3", "TIP4", "SPC"];
const ION_RESNAMES: &[&str] = &[
    "NA", "CL", "K", "MG", "CA", "ZN", "CS", "LI", "BR", "I", "SR", "BA", "MN", "FE", "CU",
];
const AMINO_ACIDS: &[&str] = &[
    "ALA", "ARG", "ASN", "ASP", "CYS", "GLN", "GLU", "GLY", "HIS", "ILE", "LEU", "LYS", "MET",
    "PHE", "PRO", "SER", "THR", "TRP", "TYR", "VAL", "HID", "HIE", "HIP", "CYX", "ASH", "GLH",
    "LYN", "HIS1", "HIS2",
];

fn is_protein_resname(res: &str) -> bool {
    AMINO_ACIDS.contains(&res.to_uppercase().as_str())
}

/// Derives the default index groups the way `gmx make_ndx` does, from a
/// .gro/.pdb file. Returns JSON:
/// {"groups":[{"name":"System","count":N}, ...]}
#[no_mangle]
pub extern "C" fn ms_index_groups(path: *const c_char) -> *mut c_char {
    let Some(path) = cstr_to_str(path) else {
        return to_c_string("{\"error\":\"null path\"}");
    };
    let Ok(content) = std::fs::read_to_string(path) else {
        return to_c_string("{\"error\":\"cannot read file\"}");
    };

    // (resname, atomname) per atom, in file order.
    let mut atoms: Vec<(String, String)> = Vec::new();
    let is_gro = path.to_lowercase().ends_with(".gro");
    if is_gro {
        let lines: Vec<&str> = content.lines().collect();
        if lines.len() >= 2 {
            if let Ok(natoms) = lines[1].trim().parse::<usize>() {
                for line in lines.iter().take(2 + natoms).skip(2) {
                    if line.chars().count() >= 15 {
                        atoms.push((line[5..10].trim().to_string(), line[10..15].trim().to_string()));
                    }
                }
            }
        }
    } else {
        for line in content.lines() {
            if (line.starts_with("ATOM  ") || line.starts_with("HETATM")) && line.len() >= 20 {
                atoms.push((line[17..20].trim().to_string(), line[12..16].trim().to_string()));
            }
        }
    }

    let mut n_system = 0usize;
    let mut n_protein = 0usize;
    let mut n_backbone = 0usize;
    let mut n_ca = 0usize;
    let mut n_water = 0usize;
    let mut n_ion = 0usize;
    let mut n_other = 0usize;

    for (res, atom) in &atoms {
        let ru = res.to_uppercase();
        let au = atom.to_uppercase();
        n_system += 1;
        let protein = is_protein_resname(&ru);
        if protein {
            n_protein += 1;
            if ["N", "CA", "C", "O"].contains(&au.as_str()) {
                n_backbone += 1;
            }
            if au == "CA" {
                n_ca += 1;
            }
        } else if WATER_RESNAMES.contains(&ru.as_str()) {
            n_water += 1;
        } else if ION_RESNAMES.contains(&ru.as_str()) {
            n_ion += 1;
        } else {
            n_other += 1;
        }
    }

    let mut out = String::from("{\"groups\":[");
    let mut push = |out: &mut String, first: &mut bool, name: &str, count: usize| {
        if !*first {
            out.push(',');
        }
        *first = false;
        out.push_str(&format!("{{\"name\":\"{}\",\"count\":{}}}", name, count));
    };
    let mut first = true;
    push(&mut out, &mut first, "System", n_system);
    if n_protein > 0 {
        push(&mut out, &mut first, "Protein", n_protein);
        push(&mut out, &mut first, "Backbone", n_backbone);
        push(&mut out, &mut first, "C-alpha", n_ca);
    }
    if n_water > 0 {
        push(&mut out, &mut first, "Water", n_water);
    }
    if n_ion > 0 {
        push(&mut out, &mut first, "Ion", n_ion);
    }
    let non_protein = n_system - n_protein;
    if non_protein > 0 {
        push(&mut out, &mut first, "Non-Protein", non_protein);
    }
    if n_other > 0 {
        push(&mut out, &mut first, "Other", n_other);
    }
    out.push_str("]}");
    to_c_string(&out)
}

// ---------------------------------------------------------------------------
// C ABI: trajectory parsing (multi-model PDB / single-frame gro)
// ---------------------------------------------------------------------------

/// Parses a trajectory container into frames. Supported inputs:
/// - multi-model .pdb (models separated by ENDMDL, as written by
///   `gmx trjconv -o dump.pdb`),
/// - single-frame .gro/.pdb (returns one frame).
/// Capped at 2000 frames. Returns the same atom layout as ms_parse_structure:
/// {"frames":[{"title":..,"natoms":N,"atoms":[[element,x,y,z,0,r,g,b],...]}]}
#[no_mangle]
pub extern "C" fn ms_parse_trajectory(path: *const c_char) -> *mut c_char {
    ms_parse_trajectory_impl(path, 0)
}

/// Extended variant: `skip_solvent != 0` drops water atoms (SOL/HOH/WAT/TIP…)
/// so the viewer renders the solute instantly for large solvated systems.
#[no_mangle]
pub extern "C" fn ms_parse_trajectory_ex(path: *const c_char, skip_solvent: c_int) -> *mut c_char {
    ms_parse_trajectory_impl(path, skip_solvent)
}

fn ms_parse_trajectory_impl(path: *const c_char, skip_solvent: c_int) -> *mut c_char {
    let Some(path) = cstr_to_str(path) else {
        return to_c_string("{\"error\":\"null path\"}");
    };
    let Ok(content) = std::fs::read_to_string(path) else {
        return to_c_string("{\"error\":\"cannot read file\"}");
    };

    const MAX_FRAMES: usize = 2000;
    let is_gro = path.to_lowercase().ends_with(".gro");
    let drop_solvent = skip_solvent != 0;

    // Build frame text chunks.
    let chunks: Vec<String> = if is_gro {
        vec![content]
    } else {
        let mut parts = Vec::new();
        let mut cur = String::new();
        for line in content.lines() {
            if line.starts_with("ENDMDL") {
                if !cur.is_empty() {
                    parts.push(std::mem::take(&mut cur));
                }
                continue;
            }
            cur.push_str(line);
            cur.push('\n');
        }
        if !cur.is_empty() {
            parts.push(cur);
        }
        parts
    };

    let mut out = String::from("{\"frames\":[");
    let mut written = 0usize;
    let mut model_no = 0usize;
    for chunk in &chunks {
        if written >= MAX_FRAMES {
            break;
        }
        let parsed = if is_gro {
            parse_gro(chunk)
        } else {
            parse_pdb(chunk)
        };
        let Some((title, atoms, _box3)) = parsed else {
            continue;
        };
        let atoms: Vec<_> = atoms
            .into_iter()
            .filter(|(_, _, resname, _, _, _)| !drop_solvent || !is_water_resname(resname))
            .collect();
        if atoms.is_empty() {
            continue;
        }
        model_no += 1;
        if written > 0 {
            out.push(',');
        }
        out.push_str(&format!(
            "{{\"title\":\"{} #{}\",\"natoms\":{},\"atoms\":[",
            json_escape(&title),
            model_no,
            atoms.len()
        ));
        for (i, (element, name, resname, x, y, z)) in atoms.iter().enumerate() {
            if i > 0 {
                out.push(',');
            }
            let (r, g, b) = cpk_color(element);
            out.push_str(&format!(
                "[\"{}\",\"{}\",\"{}\",{:.3},{:.3},{:.3},{:.3},{:.3},{:.3},{:.3}]",
                json_escape(element),
                json_escape(name),
                json_escape(resname),
                x,
                y,
                z,
                vdW_radius(element),
                r,
                g,
                b
            ));
        }
        out.push_str("]}");
        written += 1;
    }

    if written == 0 {
        return to_c_string("{\"error\":\"no frames parsed\"}");
    }
    out.push_str("]}");
    to_c_string(&out)
}

/// Common water model residue names (GROMACS SOL/SPC, PDB HOH, CHARMM WAT,
/// AMBER TIP3P, plus generic TIP* variants).
fn is_water_resname(res: &str) -> bool {
    let u = res.trim().to_uppercase();
    if u.is_empty() {
        return false;
    }
    matches!(u.as_str(), "SOL" | "HOH" | "WAT" | "SPC" | "DOD")
        || u.starts_with("TIP")
}

// ---------------------------------------------------------------------------
// C ABI: MDP validator
// ---------------------------------------------------------------------------

const VALID_INTEGRATORS: &[&str] = &[
    "md", "md-vv", "md-vv-avek", "sd", "bd", "steep", "cg", "l-bfgs", "nm", "qmmm",
];

/// Lints an MDP file body. Returns JSON:
/// {"issues":[{"severity":"error|warning|info","key":..,"message":..}, ...]}
#[no_mangle]
pub extern "C" fn ms_validate_mdp(content: *const c_char) -> *mut c_char {
    let Some(content) = cstr_to_str(content) else {
        return to_c_string("{\"error\":\"null input\"}");
    };

    let mut params: HashMap<String, String> = HashMap::new();
    let mut unknown_keys: Vec<String> = Vec::new();
    // Known MDP keys (common subset — anything else is flagged as suspicious).
    const KNOWN: &[&str] = &[
        "integrator", "dt", "nsteps", "emtol", "emstep", "nstlist", "nstxout",
        "nstvout", "nstfout", "nstlog", "nstenergy", "nstcalcenergy", "nstxout-compressed",
        "compressed-x-grps", "cutoff-scheme", "ns-type", "nstcgmcs", "continuation",
        "constraints", "constraint-algorithm", "lincs-iter", "lincs-order", "morse",
        "rigid-water", "define", "tcoupl", "tcoupl-,tc-grps", "tc-grps", "tau-t",
        "tau_t", "ref-t", "ref_t", "pcoupl", "pcoupltype", "tau-p", "tau_p",
        "ref-p", "ref_p", "compressibility", "refcoord-scaling", "nstpcouple",
        "nsttcouple", "gen-vel", "gen_vel", "gen-temp", "gen-temp", "gen-seed",
        "gen_seed", "coulombtype", "rcoulomb", "vdwtype", "vdw-modifier", "rvdw",
        "rlist", "pbc", " DispCorr", "dispcorr", "fourierspacing", "fourier-nx",
        "ewald-rtol", "pme-order", "epsilon-r", "epsilon-rf", "implicit-solvent",
        "simulation-type", "nstcomm", "comm-mode", "nstcomm", "bd-fric", "ld-seed",
        "acc-grps", "freezegrps", "acc-grps", "tinit", "init-step", "simulation-part",
        "refcoord_scaling", "disre", "disre-weighting", "disre-mixed", "disre-fc",
        "disre-tau", "nstdhre",
    ];

    for line in content.lines() {
        let t = line.trim();
        if t.is_empty() || t.starts_with(';') || t.starts_with('#') {
            continue;
        }
        let t = t.split(';').next().unwrap_or("").trim();
        if let Some((k, v)) = t.split_once('=') {
            let key = k.trim().to_lowercase().replace('_', "-");
            let key = key.as_str().replace("tau-t", "tau_t").replace("ref-t", "ref_t")
                .replace("gen-vel", "gen_vel").replace("gen-temp", "gen_temp")
                .replace("gen-seed", "gen_seed").replace("em-tol", "emtol")
                .replace("em-step", "emstep").replace("nst-xout", "nstxout");
            params.insert(key, v.trim().to_string());
        }
    }

    let mut issues: Vec<(String, String, String)> = Vec::new(); // (severity, key, message)
    let get = |k: &str| params.get(k).cloned();

    // integrator
    match get("integrator") {
        None => issues.push(("error".into(), "integrator".into(),
            "Missing integrator — grompp cannot run without it".into())),
        Some(v) if !VALID_INTEGRATORS.contains(&v.to_lowercase().as_str()) => {
            issues.push(("error".into(), "integrator".into(),
                format!("Unknown integrator '{}' — use md, sd, steep, cg, nm…", v)))
        }
        _ => {}
    }

    // nsteps
    if get("nsteps").is_none() {
        issues.push(("warning".into(), "nsteps".into(),
            "nsteps not set (grompp defaults to 0 — the run would exit immediately)".into()));
    } else if get("nsteps").and_then(|v| v.parse::<i64>().ok()) == Some(0) {
        issues.push(("warning".into(), "nsteps".into(),
            "nsteps = 0 — the run performs a single evaluation and exits".into()));
    }

    // dt sanity for molecular dynamics
    if let Some(dt) = get("dt").and_then(|v| v.parse::<f64>().ok()) {
        let it = get("integrator").unwrap_or_default();
        if it.starts_with("md") && dt > 0.005 {
            issues.push(("warning".into(), "dt".into(),
                format!("dt = {} fs is large for MD with constraints; typical production values are 0.001–0.004", dt)));
        }
    }

    // thermostat / barostat consistency
    let tcoupl = get("tcoupl").unwrap_or_else(|| "no".into());
    if tcoupl.to_lowercase() != "no" && get("ref_t").is_none() {
        issues.push(("error".into(), "ref-t".into(),
            "tcoupl is active but ref-t is missing — grompp will fail".into()));
    }
    if tcoupl.to_lowercase() != "no" && get("tc_grps").is_none() {
        issues.push(("warning".into(), "tc-grps".into(),
            "tcoupl active but tc-grps not set (defaults to System)".into()));
    }
    let pcoupl = get("pcoupl").unwrap_or_else(|| "no".into());
    if pcoupl.to_lowercase() != "no" {
        if get("ref_p").is_none() {
            issues.push(("error".into(), "ref-p".into(),
                "pcoupl is active but ref-p is missing — grompp will fail".into()));
        }
        if get("pcoupltype").is_none() {
            issues.push(("warning".into(), "pcoupltype".into(),
                "pcoupl active but pcoupltype not set (defaults to isotropic)".into()));
        }
    }

    // electrostatics
    if get("coulombtype").is_none() {
        issues.push(("info".into(), "coulombtype".into(),
            "coulombtype not set; PME is the standard choice for biomolecular systems".into()));
    }

    // velocities
    let it = get("integrator").unwrap_or_default();
    if it.starts_with("md") && get("continuation").and_then(|v| v.parse::<bool>().ok()) == Some(false)
        && get("gen_vel").is_none()
    {
        issues.push(("info".into(), "gen-vel".into(),
            "Initial MD run without continuation: consider gen-vel = yes".into()));
    }

    // unknown keys
    for key in params.keys() {
        let norm = key.replace('_', "-");
        let found = KNOWN.iter().any(|k| {
            k.trim().to_lowercase().replace('_', "-") == norm
        });
        if !found {
            unknown_keys.push(key.clone());
        }
    }
    for k in unknown_keys {
        issues.push(("warning".into(), k.clone(),
            format!("Unknown MDP parameter '{}' — GROMACS treats unknown keys as fatal warnings during grompp", k)));
    }

    let mut out = String::from("{\"issues\":[");
    for (i, (sev, key, msg)) in issues.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        out.push_str(&format!(
            "{{\"severity\":\"{}\",\"key\":\"{}\",\"message\":\"{}\"}}",
            json_escape(sev),
            json_escape(key),
            json_escape(msg)
        ));
    }
    out.push_str("]}");
    to_c_string(&out)
}
