#include <yetty/yrender/gpu-allocator.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ALLOCATIONS 1024

enum yetty_yrender_alloc_type { YETTY_YRENDER_ALLOC_TYPE_BUFFER, YETTY_YRENDER_ALLOC_TYPE_TEXTURE };

struct yetty_yrender_allocation {
    char name[64];
    uint64_t size;
    enum yetty_yrender_alloc_type type;
    void *handle;
};

struct yetty_yrender_gpu_allocator_impl {
    struct yetty_ydraw_gpu_allocator base;
    WGPUDevice device;
    struct yetty_yrender_allocation allocations[MAX_ALLOCATIONS];
    size_t allocation_count;
    uint64_t total_bytes;
};

/* Forward declarations */
static void gpu_allocator_destroy(struct yetty_ydraw_gpu_allocator *self);
static WGPUBuffer gpu_allocator_create_buffer(struct yetty_ydraw_gpu_allocator *self,
                                              const WGPUBufferDescriptor *desc);
static void gpu_allocator_release_buffer(struct yetty_ydraw_gpu_allocator *self,
                                         WGPUBuffer buffer);
static WGPUTexture gpu_allocator_create_texture(struct yetty_ydraw_gpu_allocator *self,
                                                const WGPUTextureDescriptor *desc);
static void gpu_allocator_release_texture(struct yetty_ydraw_gpu_allocator *self,
                                          WGPUTexture texture);
static uint64_t gpu_allocator_total_allocated_bytes(
    const struct yetty_ydraw_gpu_allocator *self);

static const struct yetty_yrender_gpu_allocator_ops gpu_allocator_ops = {
    .destroy = gpu_allocator_destroy,
    .create_buffer = gpu_allocator_create_buffer,
    .release_buffer = gpu_allocator_release_buffer,
    .create_texture = gpu_allocator_create_texture,
    .release_texture = gpu_allocator_release_texture,
    .total_allocated_bytes = gpu_allocator_total_allocated_bytes,
};

static void label_to_string(WGPUStringView label, char *out, size_t out_size)
{
    if (!label.data || label.length == 0) {
        strncpy(out, "(unnamed)", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    size_t len = label.length;
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, label.data, len);
    out[len] = '\0';
}

static uint32_t bytes_per_pixel(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
        return 4;
    case WGPUTextureFormat_R8Unorm:
        return 1;
    case WGPUTextureFormat_RG8Unorm:
        return 2;
    default:
        return 4;
    }
}

static void gpu_allocator_destroy(struct yetty_ydraw_gpu_allocator *self)
{
    struct yetty_yrender_gpu_allocator_impl *impl = (struct yetty_yrender_gpu_allocator_impl *)self;
    free(impl);
}

static WGPUBuffer gpu_allocator_create_buffer(struct yetty_ydraw_gpu_allocator *self,
                                              const WGPUBufferDescriptor *desc)
{
    struct yetty_yrender_gpu_allocator_impl *impl = (struct yetty_yrender_gpu_allocator_impl *)self;

    if (impl->allocation_count >= MAX_ALLOCATIONS) {
        yerror("GpuAllocator: max allocations reached");
        return NULL;
    }

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(impl->device, desc);
    if (!buffer) {
        yerror("GpuAllocator: failed to create buffer");
        return NULL;
    }

    struct yetty_yrender_allocation *alloc = &impl->allocations[impl->allocation_count++];
    label_to_string(desc->label, alloc->name, sizeof(alloc->name));
    alloc->size = desc->size;
    alloc->type = YETTY_YRENDER_ALLOC_TYPE_BUFFER;
    alloc->handle = buffer;
    impl->total_bytes += desc->size;

    ydebug("GPU [+] buffer '%s': %lu bytes — total: %lu bytes", alloc->name,
           (unsigned long)desc->size, (unsigned long)impl->total_bytes);

    return buffer;
}

static void gpu_allocator_release_buffer(struct yetty_ydraw_gpu_allocator *self,
                                         WGPUBuffer buffer)
{
    struct yetty_yrender_gpu_allocator_impl *impl = (struct yetty_yrender_gpu_allocator_impl *)self;

    if (!buffer) {
        return;
    }

    for (size_t i = 0; i < impl->allocation_count; i++) {
        if (impl->allocations[i].type == YETTY_YRENDER_ALLOC_TYPE_BUFFER &&
            impl->allocations[i].handle == buffer) {
            impl->total_bytes -= impl->allocations[i].size;
            ydebug("GPU [-] buffer '%s': %lu bytes — total: %lu bytes", impl->allocations[i].name,
                   (unsigned long)impl->allocations[i].size, (unsigned long)impl->total_bytes);
            memmove(&impl->allocations[i], &impl->allocations[i + 1],
                    (impl->allocation_count - i - 1) * sizeof(struct yetty_yrender_allocation));
            impl->allocation_count--;
            break;
        }
    }

    wgpuBufferRelease(buffer);
}

static WGPUTexture gpu_allocator_create_texture(struct yetty_ydraw_gpu_allocator *self,
                                                const WGPUTextureDescriptor *desc)
{
    struct yetty_yrender_gpu_allocator_impl *impl = (struct yetty_yrender_gpu_allocator_impl *)self;

    if (impl->allocation_count >= MAX_ALLOCATIONS) {
        yerror("GpuAllocator: max allocations reached");
        return NULL;
    }

    WGPUTexture texture = wgpuDeviceCreateTexture(impl->device, desc);
    if (!texture) {
        yerror("GpuAllocator: failed to create texture");
        return NULL;
    }

    uint64_t size = (uint64_t)desc->size.width * desc->size.height * desc->size.depthOrArrayLayers *
                    bytes_per_pixel(desc->format);

    struct yetty_yrender_allocation *alloc = &impl->allocations[impl->allocation_count++];
    label_to_string(desc->label, alloc->name, sizeof(alloc->name));
    alloc->size = size;
    alloc->type = YETTY_YRENDER_ALLOC_TYPE_TEXTURE;
    alloc->handle = texture;
    impl->total_bytes += size;

    ydebug("GPU [+] texture '%s': %ux%u = %lu bytes — total: %lu bytes", alloc->name,
           desc->size.width, desc->size.height, (unsigned long)size,
           (unsigned long)impl->total_bytes);

    return texture;
}

static void gpu_allocator_release_texture(struct yetty_ydraw_gpu_allocator *self,
                                          WGPUTexture texture)
{
    struct yetty_yrender_gpu_allocator_impl *impl = (struct yetty_yrender_gpu_allocator_impl *)self;

    if (!texture) {
        return;
    }

    for (size_t i = 0; i < impl->allocation_count; i++) {
        if (impl->allocations[i].type == YETTY_YRENDER_ALLOC_TYPE_TEXTURE &&
            impl->allocations[i].handle == texture) {
            impl->total_bytes -= impl->allocations[i].size;
            ydebug("GPU [-] texture '%s': %lu bytes — total: %lu bytes", impl->allocations[i].name,
                   (unsigned long)impl->allocations[i].size, (unsigned long)impl->total_bytes);
            memmove(&impl->allocations[i], &impl->allocations[i + 1],
                    (impl->allocation_count - i - 1) * sizeof(struct yetty_yrender_allocation));
            impl->allocation_count--;
            break;
        }
    }

    wgpuTextureRelease(texture);
}

static uint64_t gpu_allocator_total_allocated_bytes(
    const struct yetty_ydraw_gpu_allocator *self)
{
    const struct yetty_yrender_gpu_allocator_impl *impl =
        (const struct yetty_yrender_gpu_allocator_impl *)self;
    return impl->total_bytes;
}

struct yetty_yrender_gpu_allocator_result yetty_yrender_gpu_allocator_create(WGPUDevice device)
{
    if (!device) {
        return YETTY_ERR(yetty_yrender_gpu_allocator, "device is null");
    }

    struct yetty_yrender_gpu_allocator_impl *impl =
        calloc(1, sizeof(struct yetty_yrender_gpu_allocator_impl));
    if (!impl) {
        return YETTY_ERR(yetty_yrender_gpu_allocator, "failed to allocate");
    }

    impl->base.ops = &gpu_allocator_ops;
    impl->device = device;

    return YETTY_OK(yetty_yrender_gpu_allocator, &impl->base);
}
