#define _POSIX_C_SOURCE 200809L

#include "pipeline.h"

#include "image.h"
#include "slang_process.h"
#include "utils.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define GLWALL_MAX_PASSES 32
#define GLWALL_MAX_TEXTURES 64
#define GLWALL_MAX_PARAMETERS 256

struct glwall_kv {
    char *key;
    char *value;
};

struct glwall_named_texture {
    char *name;
    char *path;
    GLuint tex;
    int32_t w;
    int32_t h;
};

struct glwall_param_default {
    char *name;
    float value;
    float last_set;
};

struct glwall_pass {
    char *shader_path;

    bool filter_linear;
    char scale_type[16];
    float scale;
    float scale_x;
    float scale_y;

    GLuint program;
    GLuint fbo;
    GLuint tex;
    int32_t out_w;
    int32_t out_h;

    GLint loc_Time;
    GLint loc_FrameTime;
    GLint loc_FrameCount;
    GLint loc_FrameDirection;
    GLint loc_OutputSize;
    GLint loc_SourceSize;
    GLint loc_OriginalSize;
    GLint loc_FinalViewportSize;
    GLint loc_MVP;
    GLuint time_query;
    double gpu_time_accum;
    int gpu_time_samples;

    int param_count;
    struct glwall_param_default params[GLWALL_MAX_PARAMETERS];
    GLint param_locs[GLWALL_MAX_PARAMETERS];

    int sampler_count;
    GLint sampler_locs[GLWALL_MAX_TEXTURES];
    int sampler_units[GLWALL_MAX_TEXTURES];

    int sampler_types[GLWALL_MAX_TEXTURES];
    int sampler_indices[GLWALL_MAX_TEXTURES];

    GLint sampler_size_locs[GLWALL_MAX_TEXTURES];
};

struct glwall_pipeline {
    struct glwall_state *state;

    int pass_count;
    struct glwall_pass passes[GLWALL_MAX_PASSES];

    int named_texture_count;
    struct glwall_named_texture named_textures[GLWALL_MAX_TEXTURES];

    int32_t last_viewport_w;
    int32_t last_viewport_h;

    GLuint quad_vs;
    GLuint quad_prog_vs_only;
    GLuint last_bound_tex[GLWALL_MAX_TEXTURES];
};

static bool ends_with(const char *s, const char *suffix) {
    if (!s || !suffix)
        return false;
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    if (su > sl)
        return false;
    return memcmp(s + sl - su, suffix, su) == 0;
}

static char *strdup_or_null(const char *s) {
    if (!s)
        return NULL;
    char *d = strdup(s);
    return d;
}

static char *trim_inplace(char *s) {
    if (!s)
        return s;

    while (*s && isspace((unsigned char)*s))
        s++;

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
    return s;
}

static void strip_comment_inplace(char *line) {
    if (!line)
        return;
    bool in_quote = false;
    for (char *p = line; *p; p++) {
        if (*p == '"')
            in_quote = !in_quote;
        if (!in_quote && (*p == '#')) {
            *p = '\0';
            return;
        }
        if (!in_quote && (*p == ';')) {

            *p = '\0';
            return;
        }
    }
}

static char *unquote_inplace(char *s) {
    if (!s)
        return s;
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        s[n - 1] = '\0';
        return s + 1;
    }
    return s;
}

static char *path_dirname(const char *path) {
    if (!path)
        return NULL;
    const char *slash = strrchr(path, '/');
    if (!slash)
        return strdup(".");
    size_t len = (size_t)(slash - path);
    if (len == 0)
        return strdup("/");
    char *d = malloc(len + 1);
    if (!d)
        return NULL;
    memcpy(d, path, len);
    d[len] = '\0';
    return d;
}

static char *path_join(const char *dir, const char *rel) {
    if (!dir || !rel)
        return NULL;
    if (rel[0] == '/')
        return strdup(rel);
    size_t dl = strlen(dir);
    size_t rl = strlen(rel);
    bool need_slash = dl > 0 && dir[dl - 1] != '/';
    size_t len = dl + (need_slash ? 1 : 0) + rl;
    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, dir, dl);
    size_t pos = dl;
    if (need_slash)
        out[pos++] = '/';
    memcpy(out + pos, rel, rl);
    out[len] = '\0';
    return out;
}

static bool parse_bool(const char *v, bool def) {
    if (!v)
        return def;
    if (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "yes") == 0)
        return true;
    if (strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0 || strcasecmp(v, "no") == 0)
        return false;
    return def;
}

static float parse_float(const char *v, float def) {
    if (!v)
        return def;
    char *end = NULL;
    errno = 0;
    float f = strtof(v, &end);
    if (end == v || errno != 0)
        return def;
    return f;
}

static int parse_int(const char *v, int def) {
    if (!v)
        return def;
    char *end = NULL;
    errno = 0;
    long n = strtol(v, &end, 10);
    if (end == v || errno != 0)
        return def;
    if (n < INT_MIN || n > INT_MAX)
        return def;
    return (int)n;
}

static const char *kv_get(struct glwall_kv *kvs, int count, const char *key) {
    for (int i = 0; i < count; i++) {
        if (kvs[i].key && strcmp(kvs[i].key, key) == 0)
            return kvs[i].value;
    }
    return NULL;
}

static void kvs_free(struct glwall_kv *kvs, int count) {
    for (int i = 0; i < count; i++) {
        free(kvs[i].key);
        free(kvs[i].value);
    }
    free(kvs);
}

static bool preset_parse_kv(const char *preset_path, struct glwall_kv **out_kvs, int *out_count) {
    *out_kvs = NULL;
    *out_count = 0;

    char *text = read_file(preset_path);
    if (!text)
        return false;

    int cap = 128;
    int count = 0;
    struct glwall_kv *kvs = calloc((size_t)cap, sizeof(*kvs));
    if (!kvs) {
        free(text);
        return false;
    }

    char *saveptr = NULL;
    for (char *line = strtok_r(text, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        strip_comment_inplace(line);
        char *t = trim_inplace(line);
        if (!*t)
            continue;

        char *eq = strchr(t, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *k = trim_inplace(t);
        char *v = trim_inplace(eq + 1);
        v = unquote_inplace(v);

        if (!*k)
            continue;

        if (count == cap) {
            cap *= 2;
            struct glwall_kv *nk = realloc(kvs, sizeof(*kvs) * (size_t)cap);
            if (!nk) {
                kvs_free(kvs, count);
                free(text);
                return false;
            }
            kvs = nk;
            memset(kvs + count, 0, sizeof(*kvs) * (size_t)(cap - count));
        }

        kvs[count].key = strdup_or_null(k);
        kvs[count].value = strdup_or_null(v);
        if (!kvs[count].key || !kvs[count].value) {
            kvs_free(kvs, count + 1);
            free(text);
            return false;
        }
        count++;
    }

    free(text);
    *out_kvs = kvs;
    *out_count = count;
    return true;
}

static char *strip_version_directive(const char *source) {
    const char *line_start = source;
    const char *version_line = NULL;
    const char *version_end = NULL;

    while (*line_start) {
        const char *content_start = line_start;
        while (*content_start && (*content_start == ' ' || *content_start == '\t'))
            content_start++;

        if (strncmp(content_start, "#version", 8) == 0) {
            version_line = line_start;
            version_end = strchr(content_start, '\n');
            if (version_end)
                version_end++;
            else
                version_end = content_start + strlen(content_start);
            break;
        }

        const char *next_line = strchr(line_start, '\n');
        if (next_line)
            line_start = next_line + 1;
        else
            break;
    }

    if (!version_line)
        return strdup(source);

    size_t before_len = (size_t)(version_line - source);
    size_t after_len = strlen(version_end);

    if (before_len > SIZE_MAX - after_len - 1)
        return NULL;

    char *result = malloc(before_len + after_len + 1);
    if (!result)
        return NULL;

    memcpy(result, source, before_len);
    memcpy(result + before_len, version_end, after_len);
    result[before_len + after_len] = '\0';

    return result;
}

static char *concat2(const char *a, const char *b) {
    size_t al = strlen(a), bl = strlen(b);
    if (al > SIZE_MAX - bl - 1)
        return NULL;
    char *out = malloc(al + bl + 1);
    if (!out)
        return NULL;
    memcpy(out, a, al);
    memcpy(out + al, b, bl);
    out[al + bl] = '\0';
    return out;
}

static const char *quad_vertex_shader_src = "#version 330 core\n"
                                            "out vec2 vTexCoord;\n"
                                            "const vec2 pos[4] = vec2[](vec2(-1.0, -1.0), "
                                            "vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));\n"
                                            "const vec2 uv[4]  = vec2[](vec2(0.0, 0.0),  vec2(1.0, "
                                            "0.0),  vec2(0.0, 1.0),  vec2(1.0, 1.0));\n"
                                            "void main(){\n"
                                            "  gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);\n"
                                            "  vTexCoord = uv[gl_VertexID];\n"
                                            "}\n";

static const char *ra_fragment_header =
    "#version 330 core\n"
    "in vec2 vTexCoord;\n"
    "out vec4 FragColor;\n"
    "#define COMPAT_VARYING in\n"
    "#define COMPAT_ATTRIBUTE in\n"
    "#define COMPAT_TEXTURE texture\n"
    "#define TEX0 vTexCoord\n"
    "#define gl_FragColor FragColor\n"
    "uniform sampler2D Source;\n"
    "uniform sampler2D Original;\n"
    "/* Per-pass state block: mapped into existing uniform names via macros */\n"
    "layout(std140, binding = 1) uniform glwall_pass_block {\n"
    "  vec4 pass_SourceSize;\n"
    "  vec4 pass_OriginalSize;\n"
    "  vec4 pass_OutputSize;\n"
    "  vec4 pass_FinalViewportSize;\n"
    "};\n"
    "#define SourceSize pass_SourceSize\n"
    "#define OriginalSize pass_OriginalSize\n"
    "#define OutputSize pass_OutputSize\n"
    "#define FinalViewportSize pass_FinalViewportSize\n"
    "uniform int FrameCount;\n"
    "uniform float FrameTime;\n"
    "uniform float FrameDirection;\n";

static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len > 0) {
            char *info_log = malloc((size_t)log_len);
            if (info_log) {
                glGetShaderInfoLog(shader, log_len, NULL, info_log);
                LOG_ERROR("Shader compilation failed: %s", info_log);
                free(info_log);
            }
        }
        glDeleteShader(shader);
        return 0;
    }

    (void)state;
    return shader;
}

static GLuint create_program(struct glwall_state *state, const char *vs_src, const char *fs_src) {
    GLuint vs = compile_shader(state, GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(state, GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
        char *log = malloc((size_t)log_len);
        if (log) {
            glGetProgramInfoLog(prog, log_len, NULL, log);
            LOG_ERROR("Program link failed: %s", log);
            free(log);
        }
        glDeleteProgram(prog);
        return 0;
    }

    (void)state;
    return prog;
}

static void pass_init_defaults(struct glwall_pass *p) {
    memset(p, 0, sizeof(*p));
    p->filter_linear = true;
    strcpy(p->scale_type, "viewport");
    p->scale = 1.0f;
    p->scale_x = 0.0f;
    p->scale_y = 0.0f;
    p->time_query = 0;
    p->gpu_time_accum = 0.0;
    p->gpu_time_samples = 0;
    for (int pi = 0; pi < GLWALL_MAX_PARAMETERS; ++pi) {
        p->params[pi].last_set = NAN;
    }
}

static bool load_texture_png(struct glwall_state *state, const char *path, GLuint *out_tex,
                             int32_t *out_w, int32_t *out_h) {
    struct glwall_image img;
    if (!load_png_rgba8(path, &img)) {
        LOG_ERROR("Failed to load PNG texture '%s'", path);
        return false;
    }

    int32_t w = img.width_px;
    int32_t h = img.height_px;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba);
    glBindTexture(GL_TEXTURE_2D, 0);

    free_glwall_image(&img);

    *out_tex = tex;
    *out_w = w;
    *out_h = h;

    (void)state;
    return true;
}

static void delete_pass_resources(struct glwall_pass *p) {
    if (p->program)
        glDeleteProgram(p->program);
    if (p->fbo)
        glDeleteFramebuffers(1, &p->fbo);
    if (p->tex)
        glDeleteTextures(1, &p->tex);
    if (p->time_query)
        glDeleteQueries(1, &p->time_query);

    free(p->shader_path);

    for (int i = 0; i < p->param_count; i++)
        free(p->params[i].name);

    memset(p, 0, sizeof(*p));
}

static void delete_named_textures(struct glwall_pipeline *pl) {
    for (int i = 0; i < pl->named_texture_count; i++) {
        free(pl->named_textures[i].name);
        free(pl->named_textures[i].path);
        if (pl->named_textures[i].tex)
            glDeleteTextures(1, &pl->named_textures[i].tex);
    }
    pl->named_texture_count = 0;
}

static void ensure_pass_target(struct glwall_pass *p, int w, int h) {
    if (p->tex && p->out_w == w && p->out_h == h && p->fbo)
        return;

    if (p->fbo)
        glDeleteFramebuffers(1, &p->fbo);
    if (p->tex)
        glDeleteTextures(1, &p->tex);

    p->out_w = w;
    p->out_h = h;

    glGenTextures(1, &p->tex);
    glBindTexture(GL_TEXTURE_2D, p->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    p->filter_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    p->filter_linear ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &p->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("FBO incomplete for pass (status=0x%x)", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void record_param_defaults(struct glwall_pass *p, const char *shader_src) {

    const char *cur = shader_src;
    while (cur && *cur) {
        const char *line_end = strchr(cur, '\n');
        size_t len = line_end ? (size_t)(line_end - cur) : strlen(cur);

        if (len > 16 && memcmp(cur, "#pragma parameter", 16) == 0) {
            char *line = strndup(cur, len);
            if (!line)
                break;

            char *s = line;

            char *tok = strtok(s, " \t");
            tok = strtok(NULL, " \t");
            char *name = strtok(NULL, " \t");
            char *desc = strtok(NULL, "\"");
            (void)desc;
            (void)tok;

            char *last_quote = strrchr(line, '"');
            float def_val = 0.0f;
            if (last_quote) {
                char *after = last_quote + 1;
                while (*after && isspace((unsigned char)*after))
                    after++;
                def_val = parse_float(after, 0.0f);
            }

            if (name && p->param_count < GLWALL_MAX_PARAMETERS) {
                p->params[p->param_count].name = strdup(name);
                p->params[p->param_count].value = def_val;
                p->param_count++;
            }
            free(line);
        }

        if (!line_end)
            break;
        cur = line_end + 1;
    }
}

static void pass_resolve_uniforms(struct glwall_pass *p) {
    p->loc_Time = glGetUniformLocation(p->program, "Time");
    if (p->loc_Time == -1)
        p->loc_Time = glGetUniformLocation(p->program, "iTime");

    p->loc_FrameTime = glGetUniformLocation(p->program, "FrameTime");
    if (p->loc_FrameTime == -1)
        p->loc_FrameTime = glGetUniformLocation(p->program, "iTimeDelta");

    p->loc_FrameCount = glGetUniformLocation(p->program, "FrameCount");
    if (p->loc_FrameCount == -1)
        p->loc_FrameCount = glGetUniformLocation(p->program, "iFrame");

    p->loc_FrameDirection = glGetUniformLocation(p->program, "FrameDirection");

    p->loc_OutputSize = glGetUniformLocation(p->program, "OutputSize");
    p->loc_SourceSize = glGetUniformLocation(p->program, "SourceSize");
    p->loc_OriginalSize = glGetUniformLocation(p->program, "OriginalSize");
    p->loc_FinalViewportSize = glGetUniformLocation(p->program, "FinalViewportSize");
    p->loc_MVP = glGetUniformLocation(p->program, "MVP");

    for (int i = 0; i < p->param_count; i++) {
        p->param_locs[i] = glGetUniformLocation(p->program, p->params[i].name);
    }
}

static void pass_add_sampler(struct glwall_pipeline *pl, struct glwall_pass *p, const char *name,
                             int unit) {
    if (p->sampler_count >= GLWALL_MAX_TEXTURES)
        return;

    GLint loc = glGetUniformLocation(p->program, name);
    if (loc == -1)
        return;

    char size_name[256];
    snprintf(size_name, sizeof(size_name), "%sSize", name);
    GLint size_loc = glGetUniformLocation(p->program, size_name);

    p->sampler_locs[p->sampler_count] = loc;
    p->sampler_units[p->sampler_count] = unit;
    p->sampler_size_locs[p->sampler_count] = size_loc;

    if (strcmp(name, "Source") == 0) {
        p->sampler_types[p->sampler_count] = 1;
        p->sampler_indices[p->sampler_count] = 0;
    } else if (strcmp(name, "Original") == 0) {
        p->sampler_types[p->sampler_count] = 2;
        p->sampler_indices[p->sampler_count] = 0;
    } else if (strncmp(name, "Pass", 4) == 0) {
        int idx = atoi(name + 4);
        p->sampler_types[p->sampler_count] = 3;
        p->sampler_indices[p->sampler_count] = idx;
    } else if (strcmp(name, "sound") == 0) {
        p->sampler_types[p->sampler_count] = 5;
        p->sampler_indices[p->sampler_count] = 0;
    } else {
        p->sampler_types[p->sampler_count] = 4;
        p->sampler_indices[p->sampler_count] = -1;
        for (int ni = 0; ni < pl->named_texture_count; ++ni) {
            if (pl->named_textures[ni].name && strcmp(pl->named_textures[ni].name, name) == 0) {
                p->sampler_indices[p->sampler_count] = ni;
                break;
            }
        }
    }
    p->sampler_count++;
}

static void pass_bind_common_samplers(struct glwall_pipeline *pl, struct glwall_pass *p) {

    int unit = 0;
    pass_add_sampler(pl, p, "Source", unit++);
    pass_add_sampler(pl, p, "Original", unit++);

    for (int i = 0; i < pl->pass_count; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Pass%d", i);
        pass_add_sampler(pl, p, name, unit++);
    }

    for (int i = 0; i < pl->named_texture_count; i++) {
        pass_add_sampler(pl, p, pl->named_textures[i].name, unit++);
    }

    pass_add_sampler(pl, p, "sound", unit++);
}

static bool build_pass_program(struct glwall_state *state, struct glwall_pipeline *pl,
                               struct glwall_pass *p, const char *shader_file_path) {
    char *raw = read_file(shader_file_path);
    if (!raw) {
        LOG_ERROR("Unable to read shader '%s'", shader_file_path);
        return false;
    }

    char *san = slang_process_to_gl330(raw);
    free(raw);
    if (!san)
        return false;

    record_param_defaults(p, san);

    char *fs_body = strip_version_directive(san);
    free(san);
    if (!fs_body)
        return false;

    char *fs_src = concat2(ra_fragment_header, fs_body);
    free(fs_body);
    if (!fs_src)
        return false;

    GLuint prog = create_program(state, quad_vertex_shader_src, fs_src);
    free(fs_src);

    if (!prog) {
        LOG_ERROR("Failed to build program for pass shader '%s'", shader_file_path);
        return false;
    }

    p->program = prog;
    pass_resolve_uniforms(p);
    pass_bind_common_samplers(pl, p);

    glUseProgram(p->program);
    for (int si = 0; si < p->sampler_count; ++si) {
        if (p->sampler_locs[si] != -1) {
            glUniform1i(p->sampler_locs[si], p->sampler_units[si]);
        }
    }
    glUseProgram(0);

    if (p->time_query == 0) {
        glGenQueries(1, &p->time_query);
    }
    return true;
}

static bool load_named_textures(struct glwall_state *state, struct glwall_pipeline *pl,
                                const char *preset_dir, struct glwall_kv *kvs, int kv_count) {
    const char *textures = kv_get(kvs, kv_count, "textures");
    if (!textures || !*textures)
        return true;

    char *list = strdup(textures);
    if (!list)
        return false;

    char *saveptr = NULL;
    for (char *name = strtok_r(list, ";", &saveptr); name; name = strtok_r(NULL, ";", &saveptr)) {
        char *n = trim_inplace(name);
        if (!*n)
            continue;
        const char *path_val = kv_get(kvs, kv_count, n);
        if (!path_val) {
            LOG_WARN("Preset lists texture '%s' but no path was provided", n);
            continue;
        }

        if (pl->named_texture_count >= GLWALL_MAX_TEXTURES)
            break;

        char *rel = strdup(path_val);
        if (!rel)
            continue;
        char *u = unquote_inplace(rel);
        char *tex_path = path_join(preset_dir, u);
        if (!tex_path) {
            free(rel);
            continue;
        }

        struct glwall_named_texture *t = &pl->named_textures[pl->named_texture_count++];
        t->name = strdup(n);
        t->path = strdup(tex_path);
        t->tex = 0;
        t->w = 0;
        t->h = 0;

        if (!t->name || !t->path) {
            free(rel);
            free(tex_path);
            return false;
        }

        if (!ends_with(tex_path, ".png")) {
            LOG_WARN("Texture '%s' is not PNG (%s); skipping", n, tex_path);
        } else {
            GLuint tex = 0;
            int32_t w = 0, h = 0;
            if (load_texture_png(state, tex_path, &tex, &w, &h)) {
                t->tex = tex;
                t->w = w;
                t->h = h;
            }
        }

        free(rel);
        free(tex_path);
    }

    free(list);
    return true;
}

static void pipeline_free(struct glwall_pipeline *pl) {
    if (!pl)
        return;

    for (int i = 0; i < pl->pass_count; i++) {
        delete_pass_resources(&pl->passes[i]);
    }
    delete_named_textures(pl);
    free(pl);
}

bool pipeline_is_active(const struct glwall_state *state) {
    return state && state->pipeline != NULL;
}

bool pipeline_init_from_preset(struct glwall_state *state, const char *preset_path) {
    assert(state);
    if (!preset_path)
        return false;

    struct glwall_kv *kvs = NULL;
    int kv_count = 0;
    if (!preset_parse_kv(preset_path, &kvs, &kv_count))
        return false;

    struct glwall_pipeline *pl = calloc(1, sizeof(*pl));
    if (!pl) {
        kvs_free(kvs, kv_count);
        return false;
    }
    pl->state = state;

    const char *shaders = kv_get(kvs, kv_count, "shaders");
    int pass_count = parse_int(shaders, 0);
    if (pass_count <= 0 || pass_count > GLWALL_MAX_PASSES) {
        LOG_ERROR("Invalid preset 'shaders' count: %s", shaders ? shaders : "(null)");
        kvs_free(kvs, kv_count);
        pipeline_free(pl);
        return false;
    }
    pl->pass_count = pass_count;

    char *preset_dir = path_dirname(preset_path);
    if (!preset_dir) {
        kvs_free(kvs, kv_count);
        pipeline_free(pl);
        return false;
    }

    if (!load_named_textures(state, pl, preset_dir, kvs, kv_count)) {
        free(preset_dir);
        kvs_free(kvs, kv_count);
        pipeline_free(pl);
        return false;
    }

    for (int i = 0; i < pl->pass_count; i++) {
        struct glwall_pass *p = &pl->passes[i];
        pass_init_defaults(p);

        char key_shader[32];
        snprintf(key_shader, sizeof(key_shader), "shader%d", i);
        const char *shader_rel = kv_get(kvs, kv_count, key_shader);
        if (!shader_rel) {
            LOG_ERROR("Preset missing '%s'", key_shader);
            free(preset_dir);
            kvs_free(kvs, kv_count);
            pipeline_free(pl);
            return false;
        }

        char *rel = strdup(shader_rel);
        if (!rel) {
            free(preset_dir);
            kvs_free(kvs, kv_count);
            pipeline_free(pl);
            return false;
        }
        char *u = unquote_inplace(rel);
        char *shader_path = path_join(preset_dir, u);
        free(rel);
        if (!shader_path) {
            free(preset_dir);
            kvs_free(kvs, kv_count);
            pipeline_free(pl);
            return false;
        }

        p->shader_path = shader_path;

        char key_filter[64];
        snprintf(key_filter, sizeof(key_filter), "filter_linear%d", i);
        p->filter_linear = parse_bool(kv_get(kvs, kv_count, key_filter), true);

        char key_scale_type[64];
        snprintf(key_scale_type, sizeof(key_scale_type), "scale_type%d", i);
        const char *st = kv_get(kvs, kv_count, key_scale_type);
        if (st && *st) {
            char *tmp = strdup(st);
            if (tmp) {
                char *stt = unquote_inplace(tmp);
                stt = trim_inplace(stt);
                strncpy(p->scale_type, stt, sizeof(p->scale_type) - 1);
                p->scale_type[sizeof(p->scale_type) - 1] = '\0';
                free(tmp);
            }
        }

        char key_scale[64];
        snprintf(key_scale, sizeof(key_scale), "scale%d", i);
        p->scale = parse_float(kv_get(kvs, kv_count, key_scale), 1.0f);

        char key_sx[64];
        snprintf(key_sx, sizeof(key_sx), "scale_x%d", i);
        p->scale_x = parse_float(kv_get(kvs, kv_count, key_sx), 0.0f);

        char key_sy[64];
        snprintf(key_sy, sizeof(key_sy), "scale_y%d", i);
        p->scale_y = parse_float(kv_get(kvs, kv_count, key_sy), 0.0f);

        if (!build_pass_program(state, pl, p, shader_path)) {
            free(preset_dir);
            kvs_free(kvs, kv_count);
            pipeline_free(pl);
            return false;
        }
    }

    free(preset_dir);
    kvs_free(kvs, kv_count);

    pipeline_cleanup(state);
    state->pipeline = pl;
    LOG_INFO("Preset loaded: %d passes", pl->pass_count);
    return true;
}

void pipeline_cleanup(struct glwall_state *state) {
    if (!state)
        return;
    if (state->pipeline) {
        pipeline_free(state->pipeline);
        state->pipeline = NULL;
        state->current_program = 0;
    }
}

static void set_size_vec4(GLint loc, int w, int h) {
    if (loc == -1)
        return;
    float fw = (float)w;
    float fh = (float)h;
    float iw = w > 0 ? 1.0f / fw : 0.0f;
    float ih = h > 0 ? 1.0f / fh : 0.0f;
    glUniform4f(loc, fw, fh, iw, ih);
}

static void pipeline_prepare_alloc(struct glwall_pipeline *pl, int viewport_w, int viewport_h) {
    if (pl->last_viewport_w == viewport_w && pl->last_viewport_h == viewport_h)
        return;

    pl->last_viewport_w = viewport_w;
    pl->last_viewport_h = viewport_h;

    for (int i = 0; i < GLWALL_MAX_TEXTURES; ++i)
        pl->last_bound_tex[i] = 0;

    int in_w = viewport_w;
    int in_h = viewport_h;

    for (int i = 0; i < pl->pass_count; i++) {
        struct glwall_pass *p = &pl->passes[i];
        int base_w = viewport_w;
        int base_h = viewport_h;
        if (strcasecmp(p->scale_type, "source") == 0) {
            base_w = in_w;
            base_h = in_h;
        }

        int out_w = base_w;
        int out_h = base_h;

        if (p->scale_x > 0.0f)
            out_w = (int)(base_w * p->scale_x);
        else if (p->scale > 0.0f)
            out_w = (int)(base_w * p->scale);

        if (p->scale_y > 0.0f)
            out_h = (int)(base_h * p->scale_y);
        else if (p->scale > 0.0f)
            out_h = (int)(base_h * p->scale);

        if (out_w <= 0)
            out_w = 1;
        if (out_h <= 0)
            out_h = 1;

        if (i != pl->pass_count - 1) {
            ensure_pass_target(p, out_w, out_h);
        }

        in_w = out_w;
        in_h = out_h;
    }
}

static void bind_sampler_and_size(const struct glwall_state *state, struct glwall_pipeline *pl,
                                  const struct glwall_pass *pass, int sampler_index,
                                  GLuint source_tex, int source_w, int source_h,
                                  GLuint original_tex, int original_w, int original_h,
                                  int current_pass_index) {
    GLint size_loc = pass->sampler_size_locs[sampler_index];
    int unit = pass->sampler_units[sampler_index];

    GLuint tex = 0;
    int w = 1, h = 1;

    int stype = pass->sampler_types[sampler_index];
    int sidx = pass->sampler_indices[sampler_index];

    switch (stype) {
    case 1:
        tex = source_tex;
        w = source_w;
        h = source_h;
        break;
    case 2:
        tex = original_tex;
        w = original_w;
        h = original_h;
        break;
    case 3:
        if (sidx >= 0 && sidx < pl->pass_count && sidx < current_pass_index) {
            tex = pl->passes[sidx].tex;
            w = pl->passes[sidx].out_w;
            h = pl->passes[sidx].out_h;
        }
        break;
    case 5:
        if (state->audio_enabled && state->audio.backend_ready)
            tex = state->audio.texture;
        w = state->audio.tex_width_px;
        h = state->audio.tex_height_px;
        break;
    case 4:
        if (sidx >= 0 && sidx < pl->named_texture_count) {
            tex = pl->named_textures[sidx].tex;
            if (pl->named_textures[sidx].w > 0)
                w = pl->named_textures[sidx].w;
            if (pl->named_textures[sidx].h > 0)
                h = pl->named_textures[sidx].h;
        }
        break;
    default:
        break;
    }

    glActiveTexture(GL_TEXTURE0 + unit);
    if (pl->last_bound_tex[unit] != tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        pl->last_bound_tex[unit] = tex;
    }
    set_size_vec4(size_loc, w, h);
}

void pipeline_render_frame(struct glwall_output *output, float time_sec, float dt_sec,
                           int frame_index) {
    assert(output);
    assert(output->state);

    struct glwall_state *state = output->state;
    if (!state->pipeline)
        return;

    struct glwall_pipeline *pl = state->pipeline;

    pipeline_prepare_alloc(pl, output->width_px, output->height_px);

    GLuint original_tex = 0;
    int original_w = 1, original_h = 1;

    if (state->source_image_texture != 0) {
        original_tex = state->source_image_texture;
        original_w = state->source_image_width_px;
        original_h = state->source_image_height_px;
    }

    if (original_tex == 0) {
        if (state->source_image_texture == 0) {
            uint8_t px[4] = {255, 0, 0, 255};
            glGenTextures(1, &state->source_image_texture);
            glBindTexture(GL_TEXTURE_2D, state->source_image_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glBindTexture(GL_TEXTURE_2D, 0);
            state->source_image_width_px = 1;
            state->source_image_height_px = 1;
        }
        original_tex = state->source_image_texture;
        original_w = state->source_image_width_px;
        original_h = state->source_image_height_px;
    }

    GLuint src_tex = original_tex;
    int src_w = original_w;
    int src_h = original_h;

    glDisable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLint prev_fbo = -1;
    for (int i = 0; i < pl->pass_count; i++) {
        struct glwall_pass *p = &pl->passes[i];

        bool is_last = (i == pl->pass_count - 1);
        int out_w = is_last ? output->width_px : p->out_w;
        int out_h = is_last ? output->height_px : p->out_h;

        int target_fbo = is_last ? 0 : (int)p->fbo;
        if (target_fbo != prev_fbo) {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target_fbo);
            prev_fbo = target_fbo;
        }

        glViewport(0, 0, out_w, out_h);

        if (pl->state->current_program != p->program) {
            glUseProgram(p->program);
            pl->state->current_program = p->program;
        }

        if (p->loc_Time != -1)
            glUniform1f(p->loc_Time, time_sec);
        if (p->loc_FrameTime != -1)
            glUniform1f(p->loc_FrameTime, dt_sec);
        if (p->loc_FrameCount != -1)
            glUniform1i(p->loc_FrameCount, frame_index);
        if (p->loc_FrameDirection != -1)
            glUniform1f(p->loc_FrameDirection, 1.0f);

        if (state->pass_ubo) {
            float pass_ubo_data[16];
            float fw = (float)src_w;
            float fh = (float)src_h;
            pass_ubo_data[0] = fw;
            pass_ubo_data[1] = fh;
            pass_ubo_data[2] = fw > 0.0f ? 1.0f / fw : 0.0f;
            pass_ubo_data[3] = fh > 0.0f ? 1.0f / fh : 0.0f;

            float ow = (float)original_w;
            float oh = (float)original_h;
            pass_ubo_data[4] = ow;
            pass_ubo_data[5] = oh;
            pass_ubo_data[6] = ow > 0.0f ? 1.0f / ow : 0.0f;
            pass_ubo_data[7] = oh > 0.0f ? 1.0f / oh : 0.0f;

            float ow2 = (float)out_w;
            float oh2 = (float)out_h;
            pass_ubo_data[8] = ow2;
            pass_ubo_data[9] = oh2;
            pass_ubo_data[10] = ow2 > 0.0f ? 1.0f / ow2 : 0.0f;
            pass_ubo_data[11] = oh2 > 0.0f ? 1.0f / oh2 : 0.0f;

            float vfw = (float)output->width_px;
            float vfh = (float)output->height_px;
            pass_ubo_data[12] = vfw;
            pass_ubo_data[13] = vfh;
            pass_ubo_data[14] = vfw > 0.0f ? 1.0f / vfw : 0.0f;
            pass_ubo_data[15] = vfh > 0.0f ? 1.0f / vfh : 0.0f;

            glBindBuffer(GL_UNIFORM_BUFFER, state->pass_ubo);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(pass_ubo_data), pass_ubo_data);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        } else {
            set_size_vec4(p->loc_OutputSize, out_w, out_h);
            set_size_vec4(p->loc_SourceSize, src_w, src_h);
            set_size_vec4(p->loc_OriginalSize, original_w, original_h);
            set_size_vec4(p->loc_FinalViewportSize, output->width_px, output->height_px);
        }

        for (int pi = 0; pi < p->param_count; pi++) {
            if (p->param_locs[pi] != -1) {
                float v = p->params[pi].value;
                if (!isnan(p->params[pi].last_set) && p->params[pi].last_set == v) {
                } else {
                    glUniform1f(p->param_locs[pi], v);
                    p->params[pi].last_set = v;
                }
            }
        }

        for (int si = 0; si < p->sampler_count; si++) {
            bind_sampler_and_size(state, pl, p, si, src_tex, src_w, src_h, original_tex, original_w,
                                  original_h, i);
        }

        if (state->profiling_enabled && p->time_query != 0) {
            glBeginQuery(GL_TIME_ELAPSED, p->time_query);
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (state->profiling_enabled && p->time_query != 0) {
            glEndQuery(GL_TIME_ELAPSED);
            GLint available = 0;
            glGetQueryObjectiv(p->time_query, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint64 result = 0;
                glGetQueryObjectui64v(p->time_query, GL_QUERY_RESULT, &result);
                double gpu_ms = (double)result / 1e6;
                p->gpu_time_accum += gpu_ms;
                p->gpu_time_samples += 1;
                if (p->gpu_time_samples >= 60) {
                    double avg = p->gpu_time_accum / (double)p->gpu_time_samples;
                    LOG_INFO("Pipeline pass %d avg GPU time: %.3f ms (samples=%d)", i, avg,
                             p->gpu_time_samples);
                    p->gpu_time_accum = 0.0;
                    p->gpu_time_samples = 0;
                }
            }

            if (!is_last) {
                src_tex = p->tex;
                src_w = p->out_w;
                src_h = p->out_h;
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glUseProgram(0);
    state->current_program = 0;
}
