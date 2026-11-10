// Copyright 2026 Rain Jiang
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Generates the Rust bindings for the C wrappers in `csrc/` with bindgen,
//! then compiles the wrappers over NVIDIA's C++ cuObject libraries and links
//! them. The `server` feature covers `libcuobjserver`
//! (`csrc/cuobject_sys.{h,cpp}`), the `client` feature `libcuobjclient`
//! (`csrc/cuobject_client.{h,cpp}`).
//!
//! `CUOBJSERVER_ROOT` and `CUOBJCLIENT_ROOT` point at a prefix holding
//! `include/` and `lib/` when a library is not installed where the build
//! looks; the CUDA toolkit under `CUDA_HOME` (default `/usr/local/cuda`) and
//! the system paths are searched as well. On docs.rs (`DOCS_RS` set) only the
//! bindings are generated; nothing is compiled or linked.

use std::path::PathBuf;

/// One cuObject library and the wrapper over it.
struct Library {
    /// The Cargo feature that enables it.
    feature: &'static str,
    /// The library's own header.
    header: &'static str,
    /// The wrapper, as `csrc/<wrapper>.{h,cpp}`.
    wrapper: &'static str,
    /// Prefix override.
    root_env: &'static str,
    /// Libraries to link, in order.
    link: &'static [&'static str],
    /// Other headers the library's header includes, with the package that
    /// ships each.
    requires: &'static [(&'static str, &'static str)],
}

const LIBRARIES: [Library; 2] = [
    Library {
        feature: "server",
        header: "cuobjserver.h",
        wrapper: "cuobject_sys",
        root_env: "CUOBJSERVER_ROOT",
        link: &["cuobjserver"],
        requires: &[],
    },
    Library {
        feature: "client",
        header: "cuobjclient.h",
        wrapper: "cuobject_client",
        root_env: "CUOBJCLIENT_ROOT",
        // libcuobjclient needs libcufile; naming it lets the linker resolve it
        // from the same search path.
        link: &["cuobjclient", "cufile"],
        // cuobjclient.h includes cufile.h, which includes the CUDA driver API
        // header; a toolkit may hold the cuObject packages without it.
        requires: &[("cuda.h", "cuda-cudart-dev-<version>")],
    },
];

fn main() {
    println!("cargo:rerun-if-env-changed=CUDA_HOME");
    println!("cargo:rerun-if-env-changed=DOCS_RS");
    let enabled: Vec<&Library> = LIBRARIES
        .iter()
        .filter(|lib| {
            let var = format!("CARGO_FEATURE_{}", lib.feature.to_uppercase());
            std::env::var_os(var).is_some()
        })
        .collect();
    for lib in &enabled {
        println!("cargo:rerun-if-changed=csrc/{}.h", lib.wrapper);
        println!("cargo:rerun-if-changed=csrc/{}.cpp", lib.wrapper);
        println!("cargo:rerun-if-env-changed={}", lib.root_env);
    }

    generate_bindings(&enabled);
    if enabled.is_empty() || std::env::var_os("DOCS_RS").is_some() {
        return;
    }

    let mut build = cc::Build::new();
    build
        .cpp(true)
        .include("csrc")
        .flag_if_supported("-std=c++17")
        .warnings(true);
    for lib in &enabled {
        build.file(format!("csrc/{}.cpp", lib.wrapper));
        locate(&mut build, lib);
    }
    build.compile("cuobject_sys");
    for lib in &enabled {
        for name in lib.link {
            println!("cargo:rustc-link-lib={name}");
        }
    }
    println!("cargo:rustc-link-lib=stdc++");
}

/// Adds the include and library directories that hold `lib`'s header.
fn locate(build: &mut cc::Build, lib: &Library) {
    // (include dir, lib dir) prefixes in search order.
    let mut prefixes = Vec::new();
    if let Some(root) = std::env::var_os(lib.root_env).map(PathBuf::from) {
        prefixes.push((root.join("include"), root.join("lib")));
    }
    // The CUDA toolkit ships the client (and on some releases the server)
    // under targets/<arch>-linux, next to cuda.h and cufile.h.
    let cuda = std::env::var_os("CUDA_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/usr/local/cuda"));
    for target in ["sbsa-linux", "x86_64-linux"] {
        let target = cuda.join("targets").join(target);
        prefixes.push((target.join("include"), target.join("lib")));
    }

    let system = [
        PathBuf::from("/usr/include"),
        PathBuf::from("/usr/local/include"),
    ];
    let mut includes: Vec<PathBuf> = Vec::new();
    for (include, libdir) in &prefixes {
        if include.join(lib.header).is_file() {
            build.include(include);
            println!("cargo:rustc-link-search=native={}", libdir.display());
            println!("cargo:include={}", include.display());
            includes.push(include.clone());
        }
    }
    if includes.is_empty() && !system.iter().any(|dir| dir.join(lib.header).is_file()) {
        println!(
            "cargo:warning={} not found under {}, CUDA_HOME or the system include paths; \
             install its -dev package or set {}",
            lib.header, lib.root_env, lib.root_env
        );
    }

    for (header, package) in lib.requires {
        let have = |dir: &PathBuf| dir.join(header).is_file();
        if includes.iter().chain(&system).any(have) {
            continue;
        }
        // Fall back to the toolkit's top-level include directories.
        let fallback = [
            cuda.join("include"),
            PathBuf::from("/usr/local/cuda/include"),
        ];
        match fallback.iter().find(|dir| have(dir)) {
            Some(dir) => {
                build.include(dir);
            }
            None => println!(
                "cargo:warning={header}, needed by {}, not found next to it, under CUDA_HOME \
                 or in /usr/local/cuda; install {package} for the same CUDA version",
                lib.header
            ),
        }
    }
}

/// Writes `$OUT_DIR/bindings.rs`, which `src/lib.rs` includes. The wrapper
/// headers only need the C standard headers, so this works without the
/// cuObject libraries installed.
fn generate_bindings(enabled: &[&Library]) {
    let out = PathBuf::from(std::env::var_os("OUT_DIR").expect("OUT_DIR is set by cargo"))
        .join("bindings.rs");
    if enabled.is_empty() {
        std::fs::write(&out, "").expect("writing bindings.rs");
        return;
    }
    let mut builder = bindgen::Builder::default();
    for lib in enabled {
        builder = builder.header(format!("csrc/{}.h", lib.wrapper));
    }
    builder
        .allowlist_function("cuobj_.*")
        .allowlist_type("cuobj_.*")
        // The wrapper only ever passes or returns the listed values.
        .rustified_enum("cuobj_client_.*")
        .use_core()
        .rust_target(bindgen::RustTarget::stable(85, 0).expect("supported Rust target"))
        .rust_edition(bindgen::RustEdition::Edition2024)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("generating bindings for the csrc/ wrappers")
        .write_to_file(&out)
        .expect("writing bindings.rs");
}
