# cuobject-sys

Raw FFI bindings to NVIDIA's cuObject libraries for S3-over-RDMA (GPUDirect object loading): the server (`libcuobjserver`, feature `server`, on by default) and the client (`libcuobjclient`, feature `client`). Most users want the safe [`cuobject`](https://crates.io/crates/cuobject) crate instead.

`libcuobjserver` is C++, so the crate compiles a small C wrapper (`csrc/cuobject_sys.{h,cpp}`) over it and generates the Rust bindings from `csrc/cuobject_sys.h` with [bindgen](https://crates.io/crates/bindgen) at build time. No C++ exception crosses the boundary: calls return `0` or a byte count on success and a negative errno on failure.

## Requirements

- Linux (x86_64 or aarch64) with an RDMA-capable NIC.
- For the `server` feature: `libcuobjserver` and its headers, for example the `libcuobjserver-dev` package from NVIDIA's CUDA repository. Tested with 2.1. It does not depend on a CUDA toolkit version.
- For the `client` feature: **CUDA 13.4**. `libcuobjclient` (1.3), `libcufile` and `cuda.h` (which `cufile.h` includes) must all come from the 13.4 toolkit: `libcuobjclient-dev-13-4`, `libcufile-dev-13-4` and `cuda-cudart-dev-13-4`, plus `libibverbs-dev`. Tested on driver 580.
- A C++17 compiler.
- libclang, for bindgen (for example `libclang-dev`; set `LIBCLANG_PATH` if it is not found).

On Ubuntu 24.04 with NVIDIA's CUDA repository configured:

```sh
apt-get install libcuobjserver-dev \
    libcuobjclient-dev-13-4 libcufile-dev-13-4 cuda-cudart-dev-13-4 \
    libibverbs-dev librdmacm-dev libclang-dev clang
```

When several CUDA toolkits are installed (for example 13.0 from a base image next to 13.4), build with `CUDA_HOME=/usr/local/cuda-13.4` so the headers and `libcufile` match `libcuobjclient`.

The build script searches `$CUOBJSERVER_ROOT/{include,lib}` and `$CUOBJCLIENT_ROOT/{include,lib}`, the CUDA toolkit under `$CUDA_HOME` (default `/usr/local/cuda`, `targets/<arch>-linux`) and the system include paths. On docs.rs only the bindings are generated; nothing is compiled or linked.

## License

Apache-2.0. `libcuobjserver` itself is NVIDIA software under NVIDIA's license; this crate neither bundles nor redistributes it.
