/* NIXAI v1: freestanding SmolLM2/Llama CPU inference, no libc or host API.
   Q8 weights/activation dot products, float residuals/KV cache, split-half
   RoPE, GQA, SiLU, RMSNorm. Model: HuggingFaceTB/SmolLM2-135M-Instruct. See
   models/README.md. */
#define AI_DIM 576
#define AI_HIDDEN 1536
#define AI_LAYERS 30
#define AI_KV 192
#define AI_HEAD 64
#define AI_CONTEXT 256
#define AI_VOCAB 49152
struct AiMatrix {
  const float *scale;
  const signed char *data;
  u32 rows, cols;
};
struct AiLayer {
  const float *norm, *post;
  struct AiMatrix q, k, v, o, gate, up, down;
};
static struct {
  const u8 *base;
  u32 size;
  const u32 *header, *offsets, *bytes, *merge, *prefix, *suffix;
  const char *vocabulary;
  struct AiMatrix embedding;
  struct AiLayer layers[AI_LAYERS];
  const float *norm;
  float x[AI_DIM], a[AI_DIM], b[AI_DIM], q[AI_DIM], k[AI_KV], v[AI_KV];
  float gate[AI_HIDDEN], up[AI_HIDDEN], attention[AI_CONTEXT], logits[AI_VOCAB];
  float keys[AI_LAYERS][AI_CONTEXT][AI_KV],
      values[AI_LAYERS][AI_CONTEXT][AI_KV];
  float cosines[AI_CONTEXT][32], sines[AI_CONTEXT][32];
  int ready, cancelled;
  int (*poll)(void);
} ai;
static int ai_range(u32 off, u32 bytes) {
  return off <= ai.size && bytes <= ai.size - off;
}
static const void *ai_take(u32 *offset, u32 size) {
  if (!ai_range(*offset, size))
    return (const void *)0;
  const void *p = ai.base + *offset;
  *offset += size;
  return p;
}
static int ai_matrix(u32 *offset, struct AiMatrix *m, u32 rows, u32 cols) {
  const u32 *shape = ai_take(offset, 8);
  if (!shape || shape[0] != rows || shape[1] != cols)
    return 0;
  m->rows = rows;
  m->cols = cols;
  m->scale = ai_take(offset, rows * 4);
  m->data = ai_take(offset, rows * cols);
  *offset = (*offset + 15) & ~15U;
  return m->scale && m->data;
}
static float ai_sqrt(float value) {
  float out;
  __asm__("sqrtss %1,%0" : "=x"(out) : "x"(value));
  return out;
}
static float ai_exp(float x) {
  if (x < -80.0f)
    return 0;
  if (x > 80.0f)
    x = 80;
  int n = (int)(x * 1.44269504089f + (x >= 0 ? 0.5f : -0.5f));
  float r = x - n * 0.69314718056f;
  float p =
      1 + r * (1 + r * (0.5f +
                        r * (0.1666666667f +
                             r * (0.0416666667f +
                                  r * (0.0083333333f + r * 0.0013888889f)))));
  union {
    u32 bits;
    float value;
  } power;
  power.bits = (u32)(n + 127) << 23;
  return p * power.value;
}
static float ai_sin(float x) {
  const float pi = 3.14159265359f, tau = 6.28318530718f;
  x -= ((int)(x / tau)) * tau;
  if (x > pi)
    x -= tau;
  if (x < -pi)
    x += tau;
  if (x > pi / 2)
    x = pi - x;
  if (x < -pi / 2)
    x = -pi - x;
  float xx = x * x;
  return x * (1 + xx * (-0.16666666667f +
                        xx * (0.00833333333f +
                              xx * (-0.00019841270f + xx * 0.00000275573f))));
}
static int ai_init(const void *data, u32 size) {
  ai.ready = 0;
  ai.base = data;
  ai.size = size;
  if (size < 128)
    return 0;
  const u32 *h = data;
  ai.header = h;
  if (h[0] != 0x4149584e || h[1] != 1 || h[2] != size || h[3] != 576 ||
      h[4] != 1536 || h[5] != 30 || h[6] != 9 || h[7] != 3 || h[8] != 49152 ||
      h[9] != 256)
    return 0;
  if (!ai_range(h[10], (AI_VOCAB + 1) * 4) || !ai_range(h[12], 256 * 4) ||
      !ai_range(h[13], 131072 * 16) || h[15] > 64 || h[17] > 16 ||
      !ai_range(h[14], h[15] * 4) || !ai_range(h[16], h[17] * 4))
    return 0;
  ai.offsets = (const u32 *)(ai.base + h[10]);
  ai.vocabulary = (const char *)(ai.base + h[11]);
  if (!ai_range(h[11], ai.offsets[AI_VOCAB]))
    return 0;
  for (u32 i = 0; i < AI_VOCAB; i++)
    if (ai.offsets[i] > ai.offsets[i + 1])
      return 0;
  ai.bytes = (const u32 *)(ai.base + h[12]);
  ai.merge = (const u32 *)(ai.base + h[13]);
  ai.prefix = (const u32 *)(ai.base + h[14]);
  ai.suffix = (const u32 *)(ai.base + h[16]);
  for (u32 i = 0; i < 256; i++)
    if (ai.bytes[i] >= AI_VOCAB)
      return 0;
  for (u32 i = 0; i < h[15]; i++)
    if (ai.prefix[i] >= AI_VOCAB)
      return 0;
  for (u32 i = 0; i < h[17]; i++)
    if (ai.suffix[i] >= AI_VOCAB)
      return 0;
  u32 off = h[18];
  const float *frequencies = ai_take(&off, 128);
  if (!frequencies)
    return 0;
  for (int p = 0; p < AI_CONTEXT; p++)
    for (int j = 0; j < 32; j++) {
      float angle = p * frequencies[j];
      ai.sines[p][j] = ai_sin(angle);
      ai.cosines[p][j] = ai_sin(angle + 1.57079632679f);
    }
  if (!ai_matrix(&off, &ai.embedding, AI_VOCAB, AI_DIM))
    return 0;
  for (int i = 0; i < AI_LAYERS; i++) {
    struct AiLayer *l = &ai.layers[i];
    l->norm = ai_take(&off, AI_DIM * 4);
    if (!l->norm || !ai_matrix(&off, &l->q, AI_DIM, AI_DIM) ||
        !ai_matrix(&off, &l->k, AI_KV, AI_DIM) ||
        !ai_matrix(&off, &l->v, AI_KV, AI_DIM) ||
        !ai_matrix(&off, &l->o, AI_DIM, AI_DIM))
      return 0;
    l->post = ai_take(&off, AI_DIM * 4);
    if (!l->post || !ai_matrix(&off, &l->gate, AI_HIDDEN, AI_DIM) ||
        !ai_matrix(&off, &l->up, AI_HIDDEN, AI_DIM) ||
        !ai_matrix(&off, &l->down, AI_DIM, AI_HIDDEN))
      return 0;
  }
  ai.norm = ai_take(&off, AI_DIM * 4);
  if (!ai.norm || off != size)
    return 0;
  ai.ready = 1;
  return 1;
}
/* SSE2 signed integer dot products: W8A8, sixteen columns per iteration. */
typedef signed char AiByte16 __attribute__((vector_size(16)));
typedef signed short AiShort8 __attribute__((vector_size(16)));
typedef int AiInt4 __attribute__((vector_size(16)));
static int ai_dot(const signed char *w, const signed char *x, u32 n) {
  AiInt4 sum = {0, 0, 0, 0};
  for (u32 j = 0; j < n; j += 16) {
    AiByte16 a, b;
    __builtin_memcpy(&a, w + j, 16);
    __builtin_memcpy(&b, x + j, 16);
    AiShort8 al = __builtin_convertvector(
        __builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7), AiShort8);
    AiShort8 ah = __builtin_convertvector(
        __builtin_shufflevector(a, a, 8, 9, 10, 11, 12, 13, 14, 15), AiShort8);
    AiShort8 bl = __builtin_convertvector(
        __builtin_shufflevector(b, b, 0, 1, 2, 3, 4, 5, 6, 7), AiShort8);
    AiShort8 bh = __builtin_convertvector(
        __builtin_shufflevector(b, b, 8, 9, 10, 11, 12, 13, 14, 15), AiShort8);
    sum += __builtin_ia32_pmaddwd128(al, bl);
    sum += __builtin_ia32_pmaddwd128(ah, bh);
  }
  return sum[0] + sum[1] + sum[2] + sum[3];
}
static int ai_check(void) {
  if (ai.poll && ai.poll())
    ai.cancelled = 1;
  return ai.cancelled;
}
static void ai_matvec(float *out, const struct AiMatrix *m, const float *x) {
  signed char quantized[AI_HIDDEN];
  float maximum = 0;
  for (u32 j = 0; j < m->cols; j++) {
    float v = x[j] < 0 ? -x[j] : x[j];
    if (v > maximum)
      maximum = v;
  }
  float scale = maximum > 0 ? maximum / 127.0f : 1.0f, inverse = 1.0f / scale;
  for (u32 j = 0; j < m->cols; j++) {
    float v = x[j] * inverse;
    int q = (int)(v + (v < 0 ? -0.5f : 0.5f));
    if (q > 127)
      q = 127;
    if (q < -127)
      q = -127;
    quantized[j] = (signed char)q;
  }
  for (u32 i = 0; i < m->rows; i++) {
    if (!(i & 63) && ai_check())
      return;
    out[i] = (float)ai_dot(m->data + i * m->cols, quantized, m->cols) *
             m->scale[i] * scale;
  }
}
static void ai_rms(float *out, const float *x, const float *weight) {
  float sum = 0;
  for (int j = 0; j < AI_DIM; j++)
    sum += x[j] * x[j];
  float scale = 1.0f / ai_sqrt(sum / AI_DIM + 0.00001f);
  for (int j = 0; j < AI_DIM; j++)
    out[j] = weight[j] * x[j] * scale;
}
static int ai_forward(int token, int pos, int want_logits) {
  if (!ai.ready || token < 0 || token >= AI_VOCAB || pos < 0 ||
      pos >= AI_CONTEXT)
    return 0;
  const signed char *embedding = ai.embedding.data + token * AI_DIM;
  float scale = ai.embedding.scale[token];
  for (int i = 0; i < AI_DIM; i++)
    ai.x[i] = embedding[i] * scale;
  for (int layer = 0; layer < AI_LAYERS; layer++) {
    if (ai_check())
      return 0;
    struct AiLayer *l = &ai.layers[layer];
    ai_rms(ai.a, ai.x, l->norm);
    ai_matvec(ai.q, &l->q, ai.a);
    ai_matvec(ai.k, &l->k, ai.a);
    ai_matvec(ai.v, &l->v, ai.a);
    for (int j = 0; j < AI_DIM; j += AI_HEAD)
      for (int k = 0; k < 32; k++) {
        float a = ai.q[j + k], b = ai.q[j + k + 32], c = ai.cosines[pos][k],
              s = ai.sines[pos][k];
        ai.q[j + k] = a * c - b * s;
        ai.q[j + k + 32] = b * c + a * s;
      }
    for (int j = 0; j < AI_KV; j += AI_HEAD)
      for (int k = 0; k < 32; k++) {
        float a = ai.k[j + k], b = ai.k[j + k + 32], c = ai.cosines[pos][k],
              s = ai.sines[pos][k];
        ai.k[j + k] = a * c - b * s;
        ai.k[j + k + 32] = b * c + a * s;
      }
    for (int j = 0; j < AI_KV; j++) {
      ai.keys[layer][pos][j] = ai.k[j];
      ai.values[layer][pos][j] = ai.v[j];
    }
    for (int head = 0; head < 9; head++) {
      int kv = (head / 3) * AI_HEAD;
      float max = -1e30f;
      for (int t = 0; t <= pos; t++) {
        float value = 0;
        for (int j = 0; j < AI_HEAD; j++)
          value += ai.q[head * AI_HEAD + j] * ai.keys[layer][t][kv + j];
        value *= 0.125f;
        ai.attention[t] = value;
        if (value > max)
          max = value;
      }
      float sum = 0;
      for (int t = 0; t <= pos; t++) {
        ai.attention[t] = ai_exp(ai.attention[t] - max);
        sum += ai.attention[t];
      }
      for (int j = 0; j < AI_HEAD; j++) {
        float value = 0;
        for (int t = 0; t <= pos; t++)
          value += (ai.attention[t] / sum) * ai.values[layer][t][kv + j];
        ai.a[head * AI_HEAD + j] = value;
      }
    }
    ai_matvec(ai.b, &l->o, ai.a);
    for (int j = 0; j < AI_DIM; j++)
      ai.x[j] += ai.b[j];
    ai_rms(ai.a, ai.x, l->post);
    ai_matvec(ai.gate, &l->gate, ai.a);
    ai_matvec(ai.up, &l->up, ai.a);
    for (int j = 0; j < AI_HIDDEN; j++)
      ai.gate[j] = ai.gate[j] / (1.0f + ai_exp(-ai.gate[j])) * ai.up[j];
    ai_matvec(ai.b, &l->down, ai.gate);
    for (int j = 0; j < AI_DIM; j++)
      ai.x[j] += ai.b[j];
  }
  if (want_logits) {
    ai_rms(ai.a, ai.x, ai.norm);
    ai_matvec(ai.logits, &ai.embedding, ai.a);
  }
  return !ai.cancelled;
}
static int ai_merge_pair(u32 left, u32 right, u32 *result) {
  u32 slot = ((left * 0x9e3779b1U) ^ (right * 0x85ebca6bU)) & 131071U;
  for (u32 steps = 0; steps < 131072; steps++, slot = (slot + 1) & 131071U) {
    const u32 *entry = ai.merge + slot * 4;
    if (entry[0] == 0xffffffffU)
      return -1;
    if (entry[0] == left && entry[1] == right) {
      if (entry[2] >= AI_VOCAB)
        return -1;
      *result = entry[2];
      return (int)entry[3];
    }
  }
  return -1;
}
static int ai_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int ai_digit(char c) { return c >= '0' && c <= '9'; }
static int ai_space(char c) {
  return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}
static int ai_segment(const char *s) {
  if (s[0] == '\'') {
    if (s[1] == 's' || s[1] == 't' || s[1] == 'm' || s[1] == 'd')
      return 2;
    if ((s[1] == 'r' && s[2] == 'e') || (s[1] == 'v' && s[2] == 'e') ||
        (s[1] == 'l' && s[2] == 'l'))
      return 3;
  }
  int i = (s[0] == ' ' && s[1] && !ai_space(s[1]) && !ai_digit(s[1])) ? 1 : 0;
  if (ai_letter(s[i])) {
    do {
      i++;
    } while (ai_letter(s[i]));
    return i;
  }
  if (ai_digit(s[i]))
    return i + 1;
  if (!ai_space(s[i])) {
    do {
      i++;
    } while (s[i] && !ai_space(s[i]) && !ai_letter(s[i]) && !ai_digit(s[i]));
    return i;
  }
  i = 0;
  while (s[i] && ai_space(s[i]))
    i++;
  if (i > 1 && s[i] && s[i - 1] == ' ' && !ai_digit(s[i]))
    i--;
  return i;
}
static int ai_tokenize(const char *text, int *tokens, int cap) {
  int out = 0;
  for (int offset = 0; text[offset];) {
    int n = ai_segment(text + offset);
    if (n <= 0 || n > 255)
      return -1;
    u32 part[256];
    for (int j = 0; j < n; j++)
      part[j] = ai.bytes[(u8)text[offset + j]];
    int count = n;
    while (count > 1) {
      int best = -1, rank = 0x7fffffff;
      u32 merged = 0;
      for (int j = 0; j < count - 1; j++) {
        u32 result;
        int r = ai_merge_pair(part[j], part[j + 1], &result);
        if (r >= 0 && r < rank) {
          rank = r;
          best = j;
          merged = result;
        }
      }
      if (best < 0)
        break;
      part[best] = merged;
      for (int j = best + 1; j < count - 1; j++)
        part[j] = part[j + 1];
      count--;
    }
    if (out + count > cap)
      return -1;
    for (int j = 0; j < count; j++)
      tokens[out++] = (int)part[j];
    offset += n;
  }
  return out;
}
static int ai_prompt(const char *text, int *tokens, int cap) {
  int n = (int)ai.header[15], tail = (int)ai.header[17];
  if (n + tail >= cap)
    return -1;
  for (int i = 0; i < n; i++)
    tokens[i] = (int)ai.prefix[i];
  int user = ai_tokenize(text, tokens + n, cap - n - tail);
  if (user < 0)
    return -1;
  n += user;
  for (int i = 0; i < tail; i++)
    tokens[n++] = (int)ai.suffix[i];
  return n;
}
static int ai_argmax(void) {
  int best = 0;
  for (int i = 1; i < AI_VOCAB; i++)
    if (ai.logits[i] > ai.logits[best])
      best = i;
  return best;
}
