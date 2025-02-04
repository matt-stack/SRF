/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022,NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "srf/memory/resources/memory_resource.hpp"

#include <cuda.h>
#include <glog/logging.h>

namespace srf::memory {

class pinned_memory_resource final : public memory_resource<::cuda::memory_kind::pinned>
{
    void* do_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) final
    {
        // don't allocate anything if the user requested zero bytes
        if (0 == bytes)
        {
            return nullptr;
        }

        // If the requested alignment isn't supported, use default
        // alignment = (rmm::detail::is_supported_alignment(alignment)) ? alignment :
        // rmm::detail::RMM_DEFAULT_HOST_ALIGNMENT;

        void* ptr{nullptr};
        auto status = cudaMallocHost(&ptr, bytes);
        if (cudaSuccess != status)
        {
            throw std::bad_alloc{};
        }
        return ptr;
    }

    void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) final
    {
        if (nullptr == ptr)
        {
            return;
        }
        auto status = cudaFreeHost(ptr);
        CHECK(status == cudaSuccess);
    }

    memory_kind_type do_kind() const final
    {
        return memory_kind_type::pinned;
    }

  public:
    pinned_memory_resource() : memory_resource("pinned") {}
    ~pinned_memory_resource() override = default;
};

};  // namespace srf::memory
