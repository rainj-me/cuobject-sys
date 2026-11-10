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

// C wrapper over the C++ cuObject server library (libcuobjserver).
// Every function returns 0 or a positive byte count on success and a negative
// errno on failure; exceptions never cross this boundary.

#ifndef CUOBJECT_SYS_H
#define CUOBJECT_SYS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque cuObject server (a `cuObjServer`).
struct cuobj_server;
/// Opaque registered buffer or scatter-gather handle (a `struct rdma_buffer`).
struct cuobj_buffer;

/// Creates a server bound to the RDMA device that owns `ip`. `num_dcis`,
/// `cq_depth` and `max_sge` of `0` keep the library defaults.
int cuobj_server_create(const char *ip, uint16_t port, int num_dcis, unsigned cq_depth, int max_sge,
                        struct cuobj_server **out);
/// Destroys a server. Every registration must be released first.
void cuobj_server_destroy(struct cuobj_server *server);

/// Allocates a 4 KiB-aligned host buffer. The library offers no matching
/// free; callers may register any host memory instead.
void *cuobj_alloc_host(struct cuobj_server *server, size_t size);
/// Registers `size` bytes at `ptr` for RDMA.
int cuobj_register(struct cuobj_server *server, void *ptr, size_t size, struct cuobj_buffer **out);
/// Releases a handle from `cuobj_register`.
void cuobj_deregister(struct cuobj_server *server, struct cuobj_buffer *buffer);

/// One `(offset, size)` range of a registered buffer.
struct cuobj_sg_entry {
    uint64_t offset;
    size_t size;
};
/// Builds a transfer handle over `count` ranges of the registered `base`
/// (the library supports at most `max_sge` entries, 10 by default); one GET or
/// PUT then moves all ranges in order as one RDMA operation.
int cuobj_sg_buffer(struct cuobj_server *server, struct cuobj_buffer *base, const struct cuobj_sg_entry *entries,
                    size_t count, struct cuobj_buffer **out);
/// Releases a handle from `cuobj_sg_buffer` (never with `cuobj_deregister`).
void cuobj_sg_buffer_put(struct cuobj_server *server, struct cuobj_buffer *sg);

/// Allocates a channel; `-EBUSY` when all are in use. A channel must not be
/// used by two threads at once.
int cuobj_channel_alloc(struct cuobj_server *server, uint16_t *out);
/// Returns a channel from `cuobj_channel_alloc`.
void cuobj_channel_free(struct cuobj_server *server, uint16_t channel);

/// RDMA-writes `size` bytes from `buffer + local_offset` into the client
/// memory described by `descriptor` at `remote_addr` and waits for it.
/// Returns the byte count or a negative errno (`-EPROTO` for a malformed or
/// mismatched descriptor); `verbs_status` receives the `ibv_wc_status`.
ssize_t cuobj_get(struct cuobj_server *server, const char *key, struct cuobj_buffer *buffer, uint64_t local_offset,
                  uint64_t remote_addr, size_t size, const char *descriptor, uint16_t channel, int *verbs_status);
/// RDMA-reads `size` bytes from client memory into `buffer + local_offset`
/// and waits for it; results as `cuobj_get`.
ssize_t cuobj_put(struct cuobj_server *server, const char *key, struct cuobj_buffer *buffer, uint64_t local_offset,
                  uint64_t remote_addr, size_t size, const char *descriptor, uint16_t channel, int *verbs_status);

/// One completed asynchronous operation; mirrors `cuObjAsyncEvent_t`.
struct cuobj_completion {
    /// The handle given at submission.
    void *handle;
    /// The operation's result: bytes moved, or a negative errno.
    ssize_t status;
};
/// Submits a GET on `channel` and returns at once with `0` or a negative
/// errno; the non-null `handle` comes back through
/// `cuobj_process_completions`.
ssize_t cuobj_get_async(struct cuobj_server *server, const char *key, struct cuobj_buffer *buffer,
                        uint64_t local_offset, uint64_t remote_addr, size_t size, const char *descriptor,
                        uint16_t channel, void *handle);
/// The channel's pollable, non-blocking completion descriptor, owned by the
/// library (never close it), or `-1` when none is available.
int cuobj_channel_completion_fd(struct cuobj_server *server, uint16_t channel);
/// Drains completions behind `fd` into `out` and returns the count filled or a
/// negative errno. When it returns `max`, call again before waiting on `fd`:
/// the remaining completions raise no new notification.
int cuobj_process_completions(struct cuobj_server *server, int fd, struct cuobj_completion *out, int max);

/// Version of `cuobjserver.h` the wrapper was compiled against, as
/// `major * 1000 + minor`.
int cuobj_version(void);

#ifdef __cplusplus
}
#endif

#endif
