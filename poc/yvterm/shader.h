/*
 * poc/yvterm/shader.h — the text grid WGSL embedded as a C string.
 *
 * The probe renders through raw wgpu* calls (no yrender binder), so the
 * shader is compiled directly from this string at startup. The canonical
 * source lives next to it in text.wgsl; this embedded copy is what the
 * binary actually loads, so there is no runtime file IO.
 */
#ifndef POC_YVTERM_SHADER_H
#define POC_YVTERM_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif

extern const char poc_yvterm_text_wgsl[];

#ifdef __cplusplus
}
#endif

#endif /* POC_YVTERM_SHADER_H */
