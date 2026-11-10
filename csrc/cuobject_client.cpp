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

#include "cuobject_client.h"

#include <cuobjclient.h>

#include <cerrno>
#include <cstring>
#include <exception>
#include <new>

struct cuobj_client {
    cuObjClient *client;
};

namespace {

// The context handed to cuObjGet/cuObjPut and recovered in the callbacks.
struct io_call {
    cuobj_client_io_fn io;
    void *user;
};

ssize_t dispatch(const void *handle, char *ptr, size_t size, loff_t offset, const cufileRDMAInfo_t *info) {
    auto *call = static_cast<io_call *>(cuObjClient::getCtx(handle));
    if (call == nullptr || info == nullptr || info->desc_str == nullptr || info->desc_len < 0) {
        return -EINVAL;
    }
    // desc_len may or may not count a trailing NUL.
    size_t len = strnlen(info->desc_str, static_cast<size_t>(info->desc_len));
    return call->io(call->user, ptr, size, static_cast<int64_t>(offset), info->desc_str, len);
}

ssize_t on_get(const void *handle, char *ptr, size_t size, loff_t offset, const cufileRDMAInfo_t *info) {
    return dispatch(handle, ptr, size, offset, info);
}

ssize_t on_put(const void *handle, const char *ptr, size_t size, loff_t offset, const cufileRDMAInfo_t *info) {
    return dispatch(handle, const_cast<char *>(ptr), size, offset, info);
}

// cuObjClient keeps a reference to its ops, so they live for the program.
CUObjOps_t ops = {on_get, on_put};

int status(cuObjErr_t err) {
    switch (err) {
    case CU_OBJ_SUCCESS:
        return 0;
    case CU_OBJ_INVALID_VALUE:
        return -EINVAL;
    case CU_OBJ_WRONG_PROTOCOL:
        return -EPROTO;
    default:
        return -EIO;
    }
}

} // namespace

extern "C" {

int cuobj_client_create(struct cuobj_client **out) {
    if (out == nullptr) {
        return -EINVAL;
    }
    try {
        auto *wrapper = new (std::nothrow) cuobj_client{nullptr};
        if (wrapper == nullptr) {
            return -ENOMEM;
        }
        wrapper->client = new cuObjClient(ops, CUOBJ_PROTO_RDMA_DC_V1);
        *out = wrapper;
        return 0;
    } catch (const std::bad_alloc &) {
        return -ENOMEM;
    } catch (...) {
        return -EIO;
    }
}

void cuobj_client_destroy(struct cuobj_client *client) {
    if (client == nullptr) {
        return;
    }
    try {
        delete client->client;
    } catch (...) {
    }
    delete client;
}

int cuobj_client_connected(struct cuobj_client *client) {
    if (client == nullptr) {
        return 0;
    }
    try {
        return client->client->isConnected() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int cuobj_client_register(struct cuobj_client *client, void *ptr, size_t size) {
    if (client == nullptr || ptr == nullptr || size == 0) {
        return -EINVAL;
    }
    try {
        return status(client->client->cuMemObjGetDescriptor(ptr, size));
    } catch (...) {
        return -EIO;
    }
}

int cuobj_client_deregister(struct cuobj_client *client, void *ptr) {
    if (client == nullptr || ptr == nullptr) {
        return -EINVAL;
    }
    try {
        return status(client->client->cuMemObjPutDescriptor(ptr));
    } catch (...) {
        return -EIO;
    }
}

ssize_t cuobj_client_max_request_size(struct cuobj_client *client, void *ptr) {
    if (client == nullptr || ptr == nullptr) {
        return -EINVAL;
    }
    try {
        return client->client->cuMemObjGetMaxRequestCallbackSize(ptr);
    } catch (...) {
        return -EIO;
    }
}

ssize_t cuobj_client_get(struct cuobj_client *client, void *ptr, size_t size, cuobj_client_io_fn io, void *user) {
    if (client == nullptr || ptr == nullptr || io == nullptr) {
        return -EINVAL;
    }
    io_call call{io, user};
    try {
        return client->client->cuObjGet(&call, ptr, size);
    } catch (...) {
        return -EIO;
    }
}

ssize_t cuobj_client_put(struct cuobj_client *client, void *ptr, size_t size, cuobj_client_io_fn io, void *user) {
    if (client == nullptr || ptr == nullptr || io == nullptr) {
        return -EINVAL;
    }
    io_call call{io, user};
    try {
        return client->client->cuObjPut(&call, ptr, size);
    } catch (...) {
        return -EIO;
    }
}

int cuobj_client_token(struct cuobj_client *client, void *ptr, size_t size, size_t buffer_offset,
                       enum cuobj_client_op op, char **out) {
    if (client == nullptr || ptr == nullptr || out == nullptr) {
        return -EINVAL;
    }
    cuObjOpType_t operation;
    switch (op) {
    case CUOBJ_CLIENT_GET:
        operation = CUOBJ_GET;
        break;
    case CUOBJ_CLIENT_PUT:
        operation = CUOBJ_PUT;
        break;
    default:
        return -EINVAL;
    }
    try {
        char *token = nullptr;
        int rc = status(client->client->cuMemObjGetRDMAToken(ptr, size, buffer_offset, operation, &token));
        if (rc < 0) {
            return rc;
        }
        if (token == nullptr) {
            return -EIO;
        }
        *out = token;
        return 0;
    } catch (...) {
        return -EIO;
    }
}

int cuobj_client_token_put(struct cuobj_client *client, char *token) {
    if (client == nullptr || token == nullptr) {
        return -EINVAL;
    }
    try {
        return status(client->client->cuMemObjPutRDMAToken(token));
    } catch (...) {
        return -EIO;
    }
}

enum cuobj_client_memory cuobj_client_memory_type(const void *ptr) {
    try {
        switch (cuObjClient::getMemoryType(ptr)) {
        case CUOBJ_MEMORY_SYSTEM:
            return CUOBJ_CLIENT_MEMORY_SYSTEM;
        case CUOBJ_MEMORY_CUDA_MANAGED:
            return CUOBJ_CLIENT_MEMORY_CUDA_MANAGED;
        case CUOBJ_MEMORY_CUDA_DEVICE:
            return CUOBJ_CLIENT_MEMORY_CUDA_DEVICE;
        case CUOBJ_MEMORY_UNKNOWN:
            return CUOBJ_CLIENT_MEMORY_UNKNOWN;
        default:
            return CUOBJ_CLIENT_MEMORY_INVALID;
        }
    } catch (...) {
        return CUOBJ_CLIENT_MEMORY_INVALID;
    }
}

int cuobj_client_version(void) {
    return CUOBJ_CLIENT_MAJOR_VERSION * 1000 + CUOBJ_CLIENT_MINOR_VERSION;
}

} // extern "C"
