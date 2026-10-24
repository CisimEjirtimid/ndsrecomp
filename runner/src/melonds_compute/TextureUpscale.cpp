// See TextureUpscale.h. The rule set below is adapted from Hyllian's
// xBR-lv2 shader (libretro/glsl-shaders, MIT); see the notice in the shader
// source string and THIRD_PARTY_ATTRIBUTION.md.
#include "TextureUpscale.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "glad/glad.h"

// PNG reading (packs) and writing (dumps). Static, so the copies other
// libraries in the runner may carry never clash.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../vendor/stb/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../../vendor/stb/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

// Settings (take effect at the next apply) and what the current cache uses.
int g_factor = 1;  // configured; 1 means 4 for Smooth and packs
NdsTextureFilter g_filter = NdsTextureFilter::Original;
std::string g_pack_dir;
bool g_pack_enabled = false;
std::string g_dump_dir;
int g_applied = 1;          // storage factor of the current cache
bool g_applied_smooth = false;
bool g_applied_pack = false;
// Pack file stems (lowercase) -> paths, scanned at apply.
std::unordered_map<std::string, std::filesystem::path> g_pack;
std::unordered_set<std::string> g_dumped;

bool g_ready = false;
GLuint g_program = 0;
GLuint g_staging = 0;
uint32_t g_staging_w = 0;
uint32_t g_staging_h = 0;

// Adapted from Hyllian's xBR-lv2 (CORNER_C variant).
//
//   Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com
//   Released under the MIT License; the full notice is reproduced in
//   THIRD_PARTY_ATTRIBUTION.md.
//
// Changes from the original fragment shader:
//   - Restructured as a compute shader. The original precomputes 21 texture
//     coordinates in a vertex stage; this fetches the same neighbourhood
//     with integer offsets from the destination texel instead, which also
//     makes the wrap behaviour explicit rather than sampler state.
//   - Sources are integer RGB6A5 rather than normalized float RGB. Colours
//     are converted to 0..1 float for the rule evaluation and written back
//     as integers.
//   - Alpha is filtered alongside colour, premultiplied. DS textures use
//     palette index 0 and the A3I5/A5I3 ramps for transparency, and the RGB
//     under a transparent texel is arbitrary, so blending it in unweighted
//     produces dark halos along every cutout edge.
//   - XBR_SCALE is a uniform so one program serves 2x and 4x.
const char* kUpscaleCompute = R"GLSL(
#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform usampler2D SrcTex;
layout(binding = 0, rgba8ui) uniform writeonly uimage2DArray DstTex;

layout(location = 0) uniform ivec2 SrcSize;
layout(location = 1) uniform int Layer;
layout(location = 2) uniform int Scale;
layout(location = 3) uniform int WrapMode;   // 0 = repeat, 1 = clamp

const vec3 rgbw = vec3(14.352, 28.176, 5.472);
const float XBR_EQ_THRESHOLD = 15.0;
const float XBR_LV1_COEFFICIENT = 0.5;
const float XBR_LV2_COEFFICIENT = 2.0;
const float coef = 2.0;

const vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );
const vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );
const vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );
const vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );
const vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );
const vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );
const vec4 Ci = vec4(0.25, 0.25, 0.25, 0.25);

ivec2 wrap(ivec2 p)
{
    if (WrapMode == 0) {
        // DS texture coordinates repeat by default, so a repeating
        // neighbourhood keeps tiled surfaces seamless across their border.
        return ivec2(((p.x % SrcSize.x) + SrcSize.x) % SrcSize.x,
                     ((p.y % SrcSize.y) + SrcSize.y) % SrcSize.y);
    }
    return clamp(p, ivec2(0), SrcSize - ivec2(1));
}

// Returns premultiplied RGB in .rgb and coverage in .a, both 0..1.
vec4 fetch(ivec2 base, int dx, int dy)
{
    uvec4 t = texelFetch(SrcTex, wrap(base + ivec2(dx, dy)), 0);
    float a = float(t.a) / 31.0;
    return vec4(vec3(t.rgb) / 63.0 * a, a);
}

vec4 df(vec4 A, vec4 B) { return abs(A - B); }
vec4 eqv(vec4 A, vec4 B) { return step(df(A, B), vec4(XBR_EQ_THRESHOLD)); }
vec4 neqv(vec4 A, vec4 B) { return vec4(1.0) - eqv(A, B); }
vec4 diffv(vec4 A, vec4 B) { return vec4(notEqual(A, B)); }

vec4 wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h)
{
    return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));
}

// Luma of a premultiplied sample, with coverage folded in so a transparent
// texel never reads as a dark opaque one during edge detection.
float lum(vec4 c) { return dot(c.rgb, rgbw) + (1.0 - c.a) * 64.0; }

void main()
{
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize = SrcSize * Scale;
    if (dst.x >= outSize.x || dst.y >= outSize.y) return;

    ivec2 src = dst / Scale;
    vec2 fp = (vec2(dst - src * Scale) + vec2(0.5)) / float(Scale);

    vec4 vA1 = fetch(src,-1,-2), vB1 = fetch(src, 0,-2), vC1 = fetch(src, 1,-2);
    vec4 vA  = fetch(src,-1,-1), vB  = fetch(src, 0,-1), vC  = fetch(src, 1,-1);
    vec4 vD  = fetch(src,-1, 0), vE  = fetch(src, 0, 0), vF  = fetch(src, 1, 0);
    vec4 vG  = fetch(src,-1, 1), vH  = fetch(src, 0, 1), vI  = fetch(src, 1, 1);
    vec4 vG5 = fetch(src,-1, 2), vH5 = fetch(src, 0, 2), vI5 = fetch(src, 1, 2);
    vec4 vA0 = fetch(src,-2,-1), vD0 = fetch(src,-2, 0), vG0 = fetch(src,-2, 1);
    vec4 vC4 = fetch(src, 2,-1), vF4 = fetch(src, 2, 0), vI4 = fetch(src, 2, 1);

    vec4 b = vec4(lum(vB), lum(vD), lum(vH), lum(vF));
    vec4 c = vec4(lum(vC), lum(vA), lum(vG), lum(vI));
    vec4 d = b.yzwx;
    vec4 e = vec4(lum(vE));
    vec4 f = b.wxyz;
    vec4 g = c.zwxy;
    vec4 h = b.zwxy;
    vec4 i = c.wxyz;

    vec4 i4 = vec4(lum(vI4), lum(vC1), lum(vA0), lum(vG5));
    vec4 i5 = vec4(lum(vI5), lum(vC4), lum(vA1), lum(vG0));
    vec4 h5 = vec4(lum(vH5), lum(vF4), lum(vB1), lum(vD0));
    vec4 f4 = h5.yzwx;

    vec4 fx   = (Ao*fp.y + Bo*fp.x);
    vec4 fx_l = (Ax*fp.y + Bx*fp.x);
    vec4 fx_u = (Ay*fp.y + By*fp.x);

    vec4 irlv0 = diffv(e,f) * diffv(e,h);
    // CORNER_C
    vec4 irlv1 = irlv0 * ( neqv(f,b) * neqv(f,c) + neqv(h,d) * neqv(h,g) +
                 eqv(e,i) * (neqv(f,f4) * neqv(f,i4) + neqv(h,h5) * neqv(h,i5)) +
                 eqv(e,g) + eqv(e,c) );
    vec4 irlv2l = diffv(e,g) * diffv(d,g);
    vec4 irlv2u = diffv(e,c) * diffv(b,c);

    vec4 wd1 = wd( e, c,  g, i, h5, f4, h, f);
    vec4 wd2 = wd( h, d, i5, f, i4,  b, e, i);

    vec4 edri  = step(wd1, wd2) * irlv0;
    vec4 edr   = step(wd1 + vec4(0.1), wd2) * irlv1;
    vec4 edr_l = step( XBR_LV2_COEFFICIENT*df(f,g), df(h,c) ) * irlv2l * edr;
    vec4 edr_u = step( XBR_LV2_COEFFICIENT*df(h,c), df(f,g) ) * irlv2u * edr;

    vec4 px = step(df(e,f), df(e,h));

    vec4 maximos = max(max(fx_l, fx_u), fx);
    vec4 maximo  = max(max(fx_l, fx_u), fx);

    vec4 nc = edr * ( edr_l * step(Co, fx_l) +
                      edr_u * step(Ci, fx_u) +
                      (vec4(1.0) - edr_l) * (vec4(1.0) - edr_u) * step(Co, fx) );
    nc = clamp(nc, vec4(0.0), vec4(1.0));

    // Pick the winning direction: the four components are the four
    // diagonals, and at most one blend applies to any output texel.
    vec4 pxA = vec4(0.0);
    float blend = 0.0;
    vec4 srcPix[4];
    srcPix[0] = (px.x > 0.5) ? vF : vH;
    srcPix[1] = (px.y > 0.5) ? vB : vF;
    srcPix[2] = (px.z > 0.5) ? vD : vB;
    srcPix[3] = (px.w > 0.5) ? vH : vD;
    for (int k = 0; k < 4; ++k) {
        if (nc[k] > blend) { blend = nc[k]; pxA = srcPix[k]; }
    }

    vec4 outPix = mix(vE, pxA, clamp(blend, 0.0, 1.0));

    // Un-premultiply. Below the coverage floor the colour is meaningless, so
    // emit a fully transparent texel rather than amplifying noise.
    uvec4 result;
    if (outPix.a <= 0.0001) {
        result = uvec4(0u, 0u, 0u, 0u);
    } else {
        vec3 rgb = clamp(outPix.rgb / outPix.a, 0.0, 1.0);
        result = uvec4(uvec3(round(rgb * 63.0)),
                       uint(round(clamp(outPix.a, 0.0, 1.0) * 31.0)));
    }
    imageStore(DstTex, ivec3(dst, Layer), result);
}
)GLSL";

GLuint compile_compute(const char* source)
{
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[4096] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[texup] compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[4096] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[texup] link failed: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void ensure_staging(uint32_t width, uint32_t height)
{
    if (g_staging != 0 && g_staging_w == width && g_staging_h == height)
        return;
    if (g_staging != 0) glDeleteTextures(1, &g_staging);
    glGenTextures(1, &g_staging);
    glBindTexture(GL_TEXTURE_2D, g_staging);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI,
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    g_staging_w = width;
    g_staging_h = height;
}

// RGB6A5 (r, g, b 0..63, a 0..31) to and from 8-bit RGBA.
uint8_t c6_to_8(uint8_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }
uint8_t a5_to_8(uint8_t v) { return static_cast<uint8_t>((v * 255 + 15) / 31); }
uint8_t c8_to_6(uint8_t v) { return static_cast<uint8_t>(v >> 2); }
uint8_t a8_to_5(uint8_t v) { return static_cast<uint8_t>((v * 31 + 127) / 255); }

// `<w>x<h>_<hash16>`: FNV-1a 64 over the decoded texels.
std::string texture_name(uint32_t width, uint32_t height, const void* decoded) {
    const auto* bytes = static_cast<const uint8_t*>(decoded);
    const size_t n = static_cast<size_t>(width) * height * 4u;
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    char name[64];
    std::snprintf(name, sizeof(name), "%ux%u_%016llx", width, height,
                  static_cast<unsigned long long>(hash));
    return name;
}

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void scan_pack() {
    g_pack.clear();
    if (g_pack_dir.empty()) return;
    std::error_code error;
    const std::filesystem::path root = std::filesystem::u8path(g_pack_dir);
    if (!std::filesystem::is_directory(root, error)) return;
    // Subfolders are allowed, so authors can group textures by scene.
    for (std::filesystem::recursive_directory_iterator it(root, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const std::filesystem::path& path = it->path();
        if (lower(path.extension().string()) != ".png") continue;
        g_pack.emplace(lower(path.stem().string()), path);
    }
}

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
    return !out->empty();
}

void write_callback(void* context, void* data, int size) {
    static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data),
                                                size);
}

void dump_texture(const std::string& name, uint32_t width, uint32_t height,
                  const uint8_t* texels) {
    if (!g_dumped.insert(name).second) return;
    std::error_code error;
    const std::filesystem::path dir = std::filesystem::u8path(g_dump_dir);
    std::filesystem::create_directories(dir, error);
    const std::filesystem::path path = dir / (name + ".png");
    if (std::filesystem::exists(path, error)) return;
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4u);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = c6_to_8(texels[i]);
        rgba[i + 1] = c6_to_8(texels[i + 1]);
        rgba[i + 2] = c6_to_8(texels[i + 2]);
        rgba[i + 3] = a5_to_8(texels[i + 3]);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out ||
        !stbi_write_png_to_func(write_callback, &out, static_cast<int>(width),
                                static_cast<int>(height), 4, rgba.data(),
                                static_cast<int>(width * 4u)))
        std::fprintf(stderr, "[texpack] could not dump %s\n", name.c_str());
}

void upload_layer(GLuint array, uint32_t width, uint32_t height, uint32_t layer,
                  const void* texels) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, array);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(layer),
                    static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                    1, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, texels);
}

// The pack's image for this texture, resampled to the storage factor and
// converted to RGB6A5. False when there is none or it does not fit.
bool load_replacement(const std::string& name, uint32_t width, uint32_t height,
                      std::vector<uint8_t>* out) {
    const auto found = g_pack.find(name);
    if (found == g_pack.end()) return false;
    std::vector<uint8_t> file;
    int iw = 0, ih = 0, channels = 0;
    stbi_uc* image = nullptr;
    if (read_file(found->second, &file))
        image = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
                                      &iw, &ih, &channels, 4);
    if (!image) {
        std::fprintf(stderr, "[texpack] %s: not a readable PNG\n",
                     name.c_str());
        g_pack.erase(found);
        return false;
    }
    const uint32_t k = static_cast<uint32_t>(iw) / width;
    if (k == 0 || static_cast<uint32_t>(iw) != width * k ||
        static_cast<uint32_t>(ih) != height * k) {
        std::fprintf(stderr,
                     "[texpack] %s: %dx%d is not a whole multiple of %ux%u\n",
                     name.c_str(), iw, ih, width, height);
        stbi_image_free(image);
        g_pack.erase(found);
        return false;
    }
    const uint32_t f = static_cast<uint32_t>(g_applied);
    const uint32_t ow = width * f, oh = height * f;
    out->resize(static_cast<size_t>(ow) * oh * 4u);
    // Box-average when the image is a whole multiple of the storage, else
    // the nearest source texel.
    const uint32_t box = (k >= f && k % f == 0) ? k / f : 0;
    for (uint32_t y = 0; y < oh; ++y) {
        for (uint32_t x = 0; x < ow; ++x) {
            uint32_t sum[4] = {};
            uint32_t count = 0;
            if (box) {
                for (uint32_t by = 0; by < box; ++by)
                    for (uint32_t bx = 0; bx < box; ++bx) {
                        const stbi_uc* p =
                            image + ((static_cast<size_t>(y * box + by) * iw) +
                                     x * box + bx) * 4u;
                        for (int c = 0; c < 4; ++c) sum[c] += p[c];
                        ++count;
                    }
            } else {
                const uint32_t sx = (x * k + k / 2) / f, sy = (y * k + k / 2) / f;
                const stbi_uc* p =
                    image + (static_cast<size_t>(sy) * iw + sx) * 4u;
                for (int c = 0; c < 4; ++c) sum[c] = p[c];
                count = 1;
            }
            uint8_t* d = out->data() + (static_cast<size_t>(y) * ow + x) * 4u;
            d[0] = c8_to_6(static_cast<uint8_t>(sum[0] / count));
            d[1] = c8_to_6(static_cast<uint8_t>(sum[1] / count));
            d[2] = c8_to_6(static_cast<uint8_t>(sum[2] / count));
            d[3] = a8_to_5(static_cast<uint8_t>(sum[3] / count));
        }
    }
    stbi_image_free(image);
    return true;
}

// The DS texels, each repeated factor x factor times.
void replicate(uint32_t width, uint32_t height, const uint8_t* texels,
               std::vector<uint8_t>* out) {
    const uint32_t f = static_cast<uint32_t>(g_applied);
    const uint32_t ow = width * f;
    out->resize(static_cast<size_t>(ow) * height * f * 4u);
    for (uint32_t y = 0; y < height * f; ++y) {
        const uint8_t* row = texels + static_cast<size_t>(y / f) * width * 4u;
        uint8_t* d = out->data() + static_cast<size_t>(y) * ow * 4u;
        for (uint32_t x = 0; x < ow; ++x)
            std::copy_n(row + (x / f) * 4u, 4, d + x * 4u);
    }
}

bool run_xbr(GLuint dst_array, uint32_t width, uint32_t height, uint32_t layer,
             const void* decoded) {
    // The texture cache calls this from inside the renderer's frame setup, so
    // every binding touched here is restored before returning.
    GLint prev_program = 0;
    GLint prev_tex2d = 0;
    GLint prev_active = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d);

    ensure_staging(width, height);
    glBindTexture(GL_TEXTURE_2D, g_staging);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height),
                    GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, decoded);

    glUseProgram(g_program);
    glBindImageTexture(0, dst_array, 0, GL_TRUE, 0, GL_WRITE_ONLY,
                       GL_RGBA8UI);
    glUniform2i(0, static_cast<GLint>(width), static_cast<GLint>(height));
    glUniform1i(1, static_cast<GLint>(layer));
    glUniform1i(2, g_applied);
    glUniform1i(3, 0);  // repeat: the DS default wrap for texture coordinates

    const GLuint out_w = width * static_cast<GLuint>(g_applied);
    const GLuint out_h = height * static_cast<GLuint>(g_applied);
    glDispatchCompute((out_w + 7u) / 8u, (out_h + 7u) / 8u, 1u);
    // The cache's very next action can be a sample from this array.
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    glUseProgram(static_cast<GLuint>(prev_program));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex2d));
    glActiveTexture(static_cast<GLenum>(prev_active));
    return true;
}

}  // namespace

void nds_texture_upscale_set_factor(int factor)
{
    g_factor = (factor == 2 || factor == 4) ? factor : 1;
}

void nds_texture_set_filter(NdsTextureFilter filter) { g_filter = filter; }

NdsTextureFilter nds_texture_filter() { return g_filter; }

void nds_texture_pack_set_dir(const std::string& dir)
{
    g_pack_dir = dir;
}

bool nds_texture_pack_available()
{
    std::error_code error;
    return !g_pack_dir.empty() &&
           std::filesystem::is_directory(std::filesystem::u8path(g_pack_dir),
                                         error);
}

void nds_texture_pack_set_enabled(bool enabled) { g_pack_enabled = enabled; }

bool nds_texture_pack_enabled() { return g_pack_enabled; }

void nds_texture_dump_set_dir(const std::string& dir)
{
    g_dump_dir = dir;
    g_dumped.clear();
}

int nds_texture_upscale_apply()
{
    const bool pack = g_pack_enabled && nds_texture_pack_available();
    bool smooth = g_filter == NdsTextureFilter::Smooth;
    if (smooth && !g_program) {
        g_program = compile_compute(kUpscaleCompute);
        if (!g_program) {
            std::fprintf(stderr,
                "[texup] xBR filter unavailable; using the DS texels\n");
            smooth = false;
        }
    }
    g_ready = g_program != 0;
    const int storage = g_factor > 1 ? g_factor : 4;
    g_applied = (smooth || pack) ? storage : 1;
    g_applied_smooth = smooth;
    g_applied_pack = pack;
    if (pack) scan_pack();
    else g_pack.clear();
    std::fprintf(stderr, "[texup] textures: %s%s, %dx storage%s\n",
                 smooth ? "smooth (xBR-lv2)" : "original",
                 pack ? " + texture pack" : "", g_applied,
                 pack ? (" (" + std::to_string(g_pack.size()) +
                         " replacements)").c_str()
                      : "");
    return g_applied;
}

int nds_texture_upscale_factor()
{
    return g_applied;
}

bool nds_texture_upscale_ready()
{
    return g_ready;
}

bool nds_texture_upscale_init()
{
    nds_texture_upscale_apply();
    return true;
}

void nds_texture_upscale_shutdown()
{
    if (g_staging) glDeleteTextures(1, &g_staging);
    if (g_program) glDeleteProgram(g_program);
    g_staging = 0;
    g_program = 0;
    g_staging_w = 0;
    g_staging_h = 0;
    g_ready = false;
}

bool nds_texture_upscale_dispatch(uint32_t dst_array, uint32_t width,
                                  uint32_t height, uint32_t layer,
                                  const void* decoded)
{
    if (!decoded || dst_array == 0) return false;
    const auto* texels = static_cast<const uint8_t*>(decoded);
    const bool dump = !g_dump_dir.empty();
    if (dump || g_applied_pack) {
        const std::string name = lower(texture_name(width, height, decoded));
        if (dump) dump_texture(name, width, height, texels);
        if (g_applied_pack) {
            static std::vector<uint8_t> replacement;
            if (load_replacement(name, width, height, &replacement)) {
                upload_layer(dst_array, width * g_applied, height * g_applied,
                             layer, replacement.data());
                return true;
            }
        }
    }
    if (g_applied == 1) return false;
    if (g_applied_smooth && g_program)
        return run_xbr(dst_array, width, height, layer, decoded);
    static std::vector<uint8_t> repeated;
    replicate(width, height, texels, &repeated);
    upload_layer(dst_array, width * g_applied, height * g_applied, layer,
                 repeated.data());
    return true;
}
