/*
 * yplot-update.c — hand-written CMD_UPDATE handling + lifecycle callouts.
 *
 * The generated yplot-gen.c installs yetty_yplot_instance_update as the
 * per-instance ops->update (schema: `update: extern`) and calls the
 * created/destroying callouts around instance lifetime (schema:
 * `lifecycle_extern: true`). Everything yplot-specific about the update
 * wire format and the animation-timer integration lives here, so the
 * generated file stays a pure codegen output.
 */

#include <yetty/yplot/yplot-gen.h>

#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <string.h>

/* yplot-time.c — hooks the instance into the shared animation timer when
 * the compiled bytecode references LOAD_T. Forward-declared here instead
 * of a header: the pair is private to the yplot module. */
struct yetty_ycore_void_result yetty_yplot_time_attach(struct yetty_ydraw_composite *instance);
void yetty_yplot_time_detach(struct yetty_ydraw_composite *instance);

/* Push a fresh slice of samples into one of the instance's data buffers.
 * Writes both into the in-memory wire (so the next re-finalize / cold
 * re-upload picks up the same bytes) and directly to GPU via the binder's
 * write_buffer_chunk op (so no whole-buffer re-upload occurs). */
static struct yetty_ycore_void_result yplot_update_data_chunk(
    struct yetty_ydraw_composite *instance, uint32_t buffer_index, uint32_t sample_offset,
    const float *data, size_t count)
{
    if (!instance || !instance->buffer_data || !instance->binder) {
        return YETTY_ERR(yetty_ycore_void, "update_data_chunk: invalid instance");
    }
    if (!data || count == 0) {
        return YETTY_OK_VOID();
    }
    if (instance->type != YETTY_YPLOT_TYPE_ID) {
        return YETTY_ERR(yetty_ycore_void, "update_data_chunk: not a yplot instance");
    }

    /* Walk the storage payload to find buffer_index's [len][samples...]
     * slot inside the merged region. Storage starts right after the
     * uniforms in the wire payload. */
    uint32_t *wire = (uint32_t *)instance->buffer_data;
    uint32_t *storage = wire + 2 + YETTY_YPLOT_UNIFORMS_WORDS;

    uint32_t bytecode_len = storage[0];
    uint32_t *walk = storage + 1u + bytecode_len; /* points at data_count */
    uint32_t data_count = *walk++;
    if (buffer_index >= data_count) {
        return YETTY_ERR(yetty_ycore_void, "update_data_chunk: buffer_index out of range");
    }
    for (uint32_t i = 0; i < buffer_index; i++) {
        uint32_t entry_len = *walk++;
        walk += entry_len;
    }
    uint32_t this_len = *walk; /* len_buffer_index */
    if ((size_t)sample_offset + count > (size_t)this_len) {
        return YETTY_ERR(yetty_ycore_void, "update_data_chunk: chunk would overflow buffer length");
    }

    /* Destination word in the merged storage region (in u32 units, then × 4
     * for bytes — every word is a 32-bit float bitcast). */
    uint32_t *dst_samples = walk + 1u + sample_offset;
    size_t bytes = count * sizeof(float);

    /* 1) Keep the in-memory wire consistent — any re-finalize / cold
     *    re-upload (e.g. after the binder resizes its slot) will see the
     *    latest samples without going back through the high-level
     *    yetty_yplot_render path. */
    memcpy(dst_samples, data, bytes);

    /* 2) Push the same bytes to GPU directly — single wgpuQueueWriteBuffer
     *    at the precomputed offset, no whole-buffer re-upload. */
    size_t byte_offset_in_storage = (size_t)((uint8_t *)dst_samples - (uint8_t *)storage);
    struct yetty_ycore_void_result write_res = instance->binder->ops->write_buffer_chunk(
        instance->binder, /*buffer_index=*/0, byte_offset_in_storage, data, bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "update_data_chunk: binder write failed");
    return YETTY_OK_VOID();
}

/* CMD_UPDATE payload schema (defined by yplot):
 *   u32 buffer_index   — index into the `data` array of the yplot
 *   u32 sample_offset  — first sample to overwrite (in f32s into THAT buffer)
 *   u32 count          — number of f32 samples
 *   f32 samples[count] — new sample values
 *
 * Under the generic CMD_UPDATE dispatcher the first u32 of the payload is
 * peeled off as `target_field` (= buffer_index here); the rest arrives as
 * `body` / `body_size`, so body[0..3] = sample_offset, body[4..7] = count,
 * body[8..] = samples. */
struct yetty_ycore_void_result yetty_yplot_instance_update(struct yetty_ydraw_composite *instance,
                                                           uint32_t target_field, const void *body,
                                                           size_t body_size)
{
    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "yplot update: instance NULL");
    }
    if (!body || body_size < 8u) {
        return YETTY_ERR(yetty_ycore_void, "yplot update: body header truncated");
    }
    const uint32_t *header = (const uint32_t *)body;
    uint32_t sample_offset = header[0];
    uint32_t count = header[1];
    size_t expected = 8u + (size_t)count * sizeof(float);
    if (body_size < expected) {
        return YETTY_ERR(yetty_ycore_void, "yplot update: body samples truncated");
    }
    const float *samples = (const float *)((const uint8_t *)body + 8u);
    return yplot_update_data_chunk(instance, /*buffer_index=*/target_field, sample_offset, samples,
                                   count);
}

/* Lifecycle callouts — see the decls in yplot-gen.h. The timer attach is
 * a no-op on static plots (yplot-time.c checks the wire flags itself). */
struct yetty_ycore_void_result yetty_yplot_instance_created(struct yetty_ydraw_composite *instance)
{
    return yetty_yplot_time_attach(instance);
}

void yetty_yplot_instance_destroying(struct yetty_ydraw_composite *instance)
{
    yetty_yplot_time_detach(instance);
}
