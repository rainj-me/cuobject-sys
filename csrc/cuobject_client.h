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

// C wrapper over the C++ cuObject client library (libcuobjclient).
// Every function returns 0 or a positive byte count on success and a negative
// errno on failure; exceptions never cross this boundary.

#ifndef CUOBJECT_CLIENT_H
#define CUOBJECT_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque cuObject client (a `cuObjClient` on the RDMA DC protocol).
struct cuobj_client;

/// Called by `cuobj_client_get` and `cuobj_client_put`, possibly on another
/// thread and possibly more than once, for each chunk of the transfer: `size`
/// bytes at `ptr`, `offset` bytes into the object. `token` (`token_len` bytes,
/// not NUL-terminated) describes the chunk's memory for the server. The
/// callback asks the server to move the chunk and returns the bytes it moved
/// or a negative errno.
typedef ssize_t (*cuobj_client_io_fn)(void *user, char *ptr, size_t size, int64_t offset, const char *token,
                                      size_t token_len);

/// Creates a client.
int cuobj_client_create(struct cuobj_client **out);
/// Destroys a client. Every registration must be released first.
void cuobj_client_destroy(struct cuobj_client *client);
/// Whether the client is connected (`1`) or not (`0`).
int cuobj_client_connected(struct cuobj_client *client);

/// Registers `size` bytes at `ptr` (host or GPU memory) for RDMA.
int cuobj_client_register(struct cuobj_client *client, void *ptr, size_t size);
/// Releases a registration from `cuobj_client_register`.
int cuobj_client_deregister(struct cuobj_client *client, void *ptr);
/// The largest chunk one callback moves for the registration at `ptr`.
ssize_t cuobj_client_max_request_size(struct cuobj_client *client, void *ptr);

/// GETs `size` bytes of an object into registered memory at `ptr`, calling
/// `io(user, ...)` for each chunk; returns the bytes moved.
ssize_t cuobj_client_get(struct cuobj_client *client, void *ptr, size_t size, cuobj_client_io_fn io, void *user);
/// PUTs `size` bytes of an object from registered memory at `ptr`, calling
/// `io(user, ...)` for each chunk; returns the bytes moved.
ssize_t cuobj_client_put(struct cuobj_client *client, void *ptr, size_t size, cuobj_client_io_fn io, void *user);

/// Direction of a token from `cuobj_client_token`.
enum cuobj_client_op {
    CUOBJ_CLIENT_GET = 0,
    CUOBJ_CLIENT_PUT = 1,
};
/// Creates a token for `size` bytes at `buffer_offset` of the registration at
/// `ptr`, for a transfer driven outside `cuobj_client_get`/`put`. The
/// NUL-terminated token is released with `cuobj_client_token_put`, before the
/// registration.
int cuobj_client_token(struct cuobj_client *client, void *ptr, size_t size, size_t buffer_offset,
                       enum cuobj_client_op op, char **out);
/// Releases a token from `cuobj_client_token`.
int cuobj_client_token_put(struct cuobj_client *client, char *token);

/// Memory types reported by `cuobj_client_memory_type`; mirrors
/// `cuObjMemoryType_t`.
enum cuobj_client_memory {
    CUOBJ_CLIENT_MEMORY_SYSTEM = 0,
    CUOBJ_CLIENT_MEMORY_CUDA_MANAGED = 1,
    CUOBJ_CLIENT_MEMORY_CUDA_DEVICE = 2,
    CUOBJ_CLIENT_MEMORY_UNKNOWN = 3,
    CUOBJ_CLIENT_MEMORY_INVALID = 4,
};
/// The kind of memory at `ptr`.
enum cuobj_client_memory cuobj_client_memory_type(const void *ptr);

/// Version of `cuobjclient.h` the wrapper was compiled against, as
/// `major * 1000 + minor`.
int cuobj_client_version(void);

#ifdef __cplusplus
}
#endif

#endif
