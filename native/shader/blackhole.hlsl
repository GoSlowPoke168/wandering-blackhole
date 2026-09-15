// blackhole.hlsl - HLSL port of shader/blackhole.glsl as patched by src/shader-patch.js
// (the exact source is the .assembled.frag dump). Verified against the WebGL render by
// tools/still. The Ghostty-only paths are left out: cursor-token decode, the demo tour and
// the wall-clock pomodoro branch - the host supplies TOKEN_LEVEL, uCenter and uDriftTime.
//
// Physics after Eric Bruneton's black hole shader; see the GLSL header for the full story.

cbuffer Uniforms : register(b0) {
    float2 iResolution;  float iTime;       float TOKEN_LEVEL;
    float4 uDesktopXform;
    float2 uCenter;      float uCenterPin;  float uDriftTime;
    float DISK_TEMP,     DISK_INCL,      DISK_ROLL,       DISK_INNER;
    float DISK_OUTER,    DISK_OPACITY,   DOPPLER_MIX,     DISK_BEAM;
    float DISK_GAIN,     DISK_CONTRAST,  DISK_WIND,       DISK_SPEED;
    float EXPOSURE,      STAR_GAIN,      HOLE_RADIUS,     LENS_DEPTH;
    float WORK_AREA,     DILATION_MIN,   TOKEN_AREA_MIN,  TOKEN_AREA_MAX;
    float TOKEN_EASE,    TOKEN_REACH,    TOKEN_CALM,      TOKEN_RUSH;
    float TOKEN_HOME_X,  TOKEN_HOME_Y,   _pad0,           _pad1;
};
Texture2D    iChannel0 : register(t0);
SamplerState iSampler  : register(s0);

#define N_STEPS 48
#define B_CRIT  2.5980762

// GLSL mod() is floored; HLSL fmod() truncates. These matter for negative arguments.
float  glmod(float  x, float y) { return x - y * floor(x / y); }
float2 glmod(float2 x, float y) { return x - y * floor(x / y); }

float4 _sampleDesktop(float2 uv) {
    return iChannel0.SampleLevel(iSampler, uv * uDesktopXform.xy + uDesktopXform.zw, 0);
}

struct DiskLook {
    float temp, incl, roll, inner, outer, opac, dopp, beam,
          gain, contr, wind, speed, expo, star;
};

// ------------------------------------------------------------------- noise --
float hash21(float2 p) {
    p = frac(p * float2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return frac(p.x * p.y);
}

float vnoiseWrapY(float2 p, float perY) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float y0 = glmod(i.y, perY), y1 = glmod(i.y + 1.0, perY);
    return lerp(lerp(hash21(float2(i.x, y0)), hash21(float2(i.x + 1.0, y0)), f.x),
                lerp(hash21(float2(i.x, y1)), hash21(float2(i.x + 1.0, y1)), f.x),
                f.y);
}

float2 mirrorUV(float2 u) { return 1.0 - abs(1.0 - glmod(u, 2.0)); }

float2 rot(float2 v, float a) {
    float c = cos(a), s = sin(a);
    return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

float2 lissa(float t) {
    return float2(0.75 * sin(t * 0.37) + 0.25 * sin(t * 0.83 + 1.0),
                  0.70 * sin(t * 0.54 + 2.1) + 0.30 * sin(t * 1.07));
}

float3 blackbody(float T) {
    float t = clamp(T, 1500.0, 40000.0) / 100.0;
    float r = t <= 66.0 ? 1.0
                        : clamp(1.292936 * pow(t - 60.0, -0.1332047), 0.0, 1.0);
    float g = t <= 66.0 ? clamp(0.3900816 * log(t) - 0.6318414, 0.0, 1.0)
                        : clamp(1.1298909 * pow(t - 60.0, -0.0755148), 0.0, 1.0);
    float b = t >= 66.0 ? 1.0
                        : (t <= 19.0 ? 0.0
                                     : clamp(0.5432068 * log(t - 10.0) - 1.1962540, 0.0, 1.0));
    return float3(r, g, b);
}

float3 stars(float3 d) {
    float2 sph = float2(atan2(d.x, -d.z), asin(clamp(d.y, -1.0, 1.0)));
    float2 g   = sph * 40.0;
    float2 id  = floor(g);
    float  h   = hash21(id);
    if (h < 0.92) return float3(0.0, 0.0, 0.0);
    float2 f   = frac(g) - 0.5;
    float2 off = (float2(hash21(id + 17.3), hash21(id + 31.7)) - 0.5) * 0.7;
    float spark = smoothstep(0.10, 0.0, length(f - off));
    float tw    = 0.7 + 0.3 * sin(iTime * (0.5 + 2.0 * hash21(id + 5.1)) + 40.0 * h);
    float3 tint = lerp(float3(1.0, 0.82, 0.60), float3(0.75, 0.85, 1.0), hash21(id + 2.9));
    return tint * spark * tw * ((h - 0.92) / 0.08);
}

// ------------------------------------------------------------------- image --
float4 mainImage(float2 fragCoord) {
    float2 res    = iResolution;
    float2 uv     = fragCoord / res;
    float  aspect = res.x / res.y;
    float  yUp    = 1.0 - uv.y;
    float  t      = uDriftTime;

    DiskLook L = { DISK_TEMP, DISK_INCL, DISK_ROLL, DISK_INNER, DISK_OUTER, DISK_OPACITY,
                   DOPPLER_MIX, DISK_BEAM, DISK_GAIN, DISK_CONTRAST, DISK_WIND, DISK_SPEED,
                   EXPOSURE, STAR_GAIN };

    float rin  = max(L.inner, 1.6);
    float rout = max(L.outer, rin + 0.5);

    // ---- token mode: master intensity I, size sz, drift centre ----
    float  I, sz;
    float2 center;
    {
        float lvl = TOKEN_LEVEL;
        if (lvl < 0.0) return float4(0.0, 0.0, 0.0, 0.0);
        float g = pow(clamp(lvl, 0.0, 1.0), TOKEN_EASE);
        I = lerp(0.10, 1.0, g);
        float rhMin = sqrt(TOKEN_AREA_MIN * aspect / 3.1415927);
        float rhMax = sqrt(TOKEN_AREA_MAX * aspect / 3.1415927);
        float rhT = lerp(rhMin, rhMax, g) * (HOLE_RADIUS / 0.08);
        sz = rhT / max(HOLE_RADIUS, 1e-4);
        float marg = min(rhT * lerp(1.45, 0.90, g), 0.5 * (1.0 - WORK_AREA - 0.03));
        float xPad = marg / aspect;
        float2 fullLo = float2(min(xPad, 0.5), marg);
        float2 fullHi = float2(max(0.5, 1.0 - xPad),
                               max(marg, 1.0 - (WORK_AREA + 0.03 + marg)));
        float2 corner = clamp(float2(TOKEN_HOME_X, TOKEN_HOME_Y), fullLo, fullHi);
        float  reach  = lerp(0.06, max(TOKEN_REACH, 0.06), g);
        float2 lo = float2(lerp(corner.x, fullLo.x, reach), fullLo.y);
        float2 hi = float2(fullHi.x, lerp(corner.y, fullHi.y, reach));
        float2 room   = max((hi - lo) * 0.5, float2(0.0, 0.0));
        float2 wobAmp = min(float2(0.010 + 0.030 * g, 0.010 + 0.030 * g), max(room * 0.35, float2(0.006, 0.006)));
        float2 ampEff = max(room - wobAmp, float2(0.0, 0.0));
        float2 wander = lerp(lissa(t * TOKEN_CALM), lissa(t * TOKEN_RUSH), g);
        center = (lo + hi) * 0.5 + wander * ampEff
               + wobAmp * float2(cos(t * 0.8), sin(t * 1.0));
    }
    center = lerp(center, uCenter, clamp(uCenterPin, 0.0, 1.0));
    float vis = smoothstep(0.0, 0.10, I);
    if (vis <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    float rh = HOLE_RADIUS * sz;

    float dil = lerp(1.0, DILATION_MIN, I);
    float shield = vis * smoothstep(WORK_AREA, WORK_AREA + 0.18, yUp);

    float2 p    = (uv - center) * float2(aspect, 1.0);
    float  plen = length(p);

    float  W  = B_CRIT / max(rh, 1e-4);
    float2 pr = rot(float2(p.x, -p.y), L.roll) * W;
    float  b  = length(pr);

    float window = exp(-pow(plen / (7.0 * rh), 2.0));

    float bmax = rout + 3.0;
    float Z0   = max(14.0, rout + 5.0);

    // ================= far field: analytic weak deflection ==================
    if (b >= bmax) {
        float u    = Z0 * rsqrt(Z0 * Z0 + b * b);
        float defl = (2.0 / (W * W)) / max(plen, 1e-4)
                   * (1.29 * u + 0.07) * max(LENS_DEPTH - 2.14 * u + 0.75, 0.0)
                   * window * shield;
        float2 dir = p / max(plen, 1e-5);
        float3 term;
        float ab = 0.035 * smoothstep(1.0, 2.0, b / bmax);
        [unroll] for (int c = 0; c < 3; c++) {
            float  k   = 1.0 + (float(c) - 1.0) * ab;
            float2 sp  = p - dir * defl * k;
            float2 suv = mirrorUV(center + sp / float2(aspect, 1.0));
            term[c]    = _sampleDesktop(suv)[c];
        }
        float3 d = normalize(float3(-(pr / b) * (2.0 / b), -1.0));
        float3 _rgb = term + stars(d) * L.star * window * shield;
        float  _a = clamp(smoothstep(0.25, 1.2, defl * iResolution.y)
                        + L.star * window * shield * 0.5, 0.0, 1.0);
        return float4(_rgb * _a, _a);   // premultiplied
    }

    // ====================== near field: trace the geodesic ==================
    float3 x  = float3(pr, Z0);
    float3 v  = float3(0.0, 0.0, -1.0);
    float  h2 = dot(pr, pr);

    float  ci = cos(L.incl), si = sin(L.incl);
    float3 n  = float3(0.0, si, ci);
    float3 e2 = float3(0.0, ci, -si);
    float  sdir = L.speed < 0.0 ? -1.0 : 1.0;
    float  spd  = abs(L.speed);

    float3 emitc = float3(0.0, 0.0, 0.0);
    float  trans = 1.0;
    bool   captured = false;
    float  sPrev = dot(x, n);
    float3 xPrev = x;

    [loop] for (int i = 0; i < N_STEPS; i++) {
        float r2 = dot(x, x);
        if (r2 < 1.0) { captured = true; break; }
        if (x.z < -Z0 && v.z < 0.0) break;
        if (r2 > 4.0 * Z0 * Z0) break;
        float r  = sqrt(r2);
        float dt = clamp(0.16 * r, 0.03, 1.5);
        float3 a = -1.5 * h2 * x / (r2 * r2 * r);
        v += a * (0.5 * dt);
        x += v * dt;
        r2 = dot(x, x);
        r  = sqrt(r2);
        a  = -1.5 * h2 * x / (r2 * r2 * r);
        v += a * (0.5 * dt);

        float s = dot(x, n);
        if (s * sPrev < 0.0 && trans > 0.02) {
            float  tc = sPrev / (sPrev - s);
            float3 xc = lerp(xPrev, x, tc);
            float  rc = length(xc);
            if (rc > rin && rc < rout) {
                float band = smoothstep(rin, rin * 1.25, rc)
                           * (1.0 - smoothstep(rout * 0.70, rout, rc));

                float phi   = atan2(dot(xc, e2), xc.x);
                float turns = phi / 6.2831853;
                float kep   = pow(rin / rc, 1.5);
                float gloc  = sqrt(max(1.0 - 1.5 / rc, 0.02));
                float swirl = rc * L.wind * 0.12 - t * kep * spd * gloc * dil * sdir;
                float streaks = vnoiseWrapY(float2(rc * 2.8, turns * 19.0 + swirl * 3.0), 19.0) * 0.65 +
                                vnoiseWrapY(float2(rc * 1.0, turns * 9.0  + swirl * 1.5 + 7.0), 9.0) * 0.35;
                streaks = 0.35 + L.contr * streaks * streaks;

                float3 gasdir = normalize(cross(n, xc)) * sdir;
                float  beta   = clamp(rsqrt(max(2.0 * (rc - 1.0), 0.2)), 0.0, 0.99);
                float  g      = gloc / max(1.0 + beta * dot(gasdir, normalize(v)), 0.05);
                g = lerp(1.0, g, L.dopp);

                float  xpr   = max(1.0 - sqrt(rin / rc), 0.0);
                float  tprof = pow(rin / rc, 0.75) * pow(xpr, 0.25) / 0.488;
                float3 cbb   = blackbody(L.temp * tprof * g);
                float  boost = pow(g, L.beam);

                float density = band * streaks;
                emitc += trans * cbb * (L.gain * 2.2 * density * tprof * tprof * boost);
                trans *= 1.0 - clamp(L.opac * density, 0.0, 1.0);
            }
        }
        sPrev = s;
        xPrev = x;
    }
    if (!captured && dot(x, x) < 4.0) captured = true;

    float3 bg = float3(0.0, 0.0, 0.0);
    if (!captured) {
        float3 d = normalize(v);
        bg += stars(d) * L.star * window * shield;
        if (d.z < -0.05) {
            float  tpl = (-LENS_DEPTH - x.z) / d.z;
            float3 hp  = x + d * tpl;
            float2 q   = rot(hp.xy, -L.roll) / W;
            float2 sp  = float2(q.x, -q.y);
            float2 suv = mirrorUV(center + (p + (sp - p) * window * shield) / float2(aspect, 1.0));
            float  toward = smoothstep(0.05, 0.35, -d.z);
            bg += _sampleDesktop(suv).rgb * toward;
        }
    }

    float3 col = bg * trans + (1.0 - exp(-emitc * L.expo));
    return float4(col, 1.0);
}

struct VSOut { float4 pos : SV_POSITION; };
VSOut VS(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
// SV_Position is already top-down with pixel centres, which is what the GLSL footer
// builds from gl_FragCoord.
float4 PS(VSOut i) : SV_TARGET { return mainImage(i.pos.xy); }
