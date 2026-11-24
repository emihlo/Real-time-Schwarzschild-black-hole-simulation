#version 330 core

out vec4 outColor;

uniform vec2  uRes;
uniform float uTime;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;
uniform int   uSteps;
uniform int   uLens;
uniform int   uDisk;
uniform float uDiskIn;
uniform float uDiskOut;
uniform float uDiskTemp;
uniform float uDiskBright;
uniform float uRotSpeed;
uniform vec2  uJitter;

const float M   = 0.5;        // rs = 2M = 1
const float RS  = 1.0;
const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;

// ---------------------------------------------------------------- hashing

float hash13(vec3 p3)
{
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash13(i + vec3(0.0, 0.0, 0.0));
    float b = hash13(i + vec3(1.0, 0.0, 0.0));
    float c = hash13(i + vec3(0.0, 1.0, 0.0));
    float d = hash13(i + vec3(1.0, 1.0, 0.0));
    float e = hash13(i + vec3(0.0, 0.0, 1.0));
    float g = hash13(i + vec3(1.0, 0.0, 1.0));
    float h = hash13(i + vec3(0.0, 1.0, 1.0));
    float k = hash13(i + vec3(1.0, 1.0, 1.0));

    float x00 = mix(a, b, f.x);
    float x10 = mix(c, d, f.x);
    float x01 = mix(e, g, f.x);
    float x11 = mix(h, k, f.x);

    return mix(mix(x00, x10, f.y), mix(x01, x11, f.y), f.z);
}

float fbm(vec3 p, int oct)
{
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 6; ++i) {
        if (i >= oct) break;
        sum += amp * vnoise(p);
        p = p * 2.03 + vec3(11.7, 3.1, 7.3);
        amp *= 0.5;
    }
    return sum;
}

// ---------------------------------------------------------------- colour

vec3 blackbody(float T)
{
    T = clamp(T, 900.0, 40000.0);

    vec3 c;
    c.r = 56100000.0 * pow(T, -1.5) + 148.0;
    c.g = 100.04 * log(T) - 623.6;
    if (T > 6500.0) c.g = 35200000.0 * pow(T, -1.5) + 184.0;
    c.b = 194.18 * log(T) - 1448.6;

    c = clamp(c / 255.0, 0.0, 1.0);

    // keep hot gas luminous rather than washing out to grey
    float m = max(max(c.r, c.g), c.b);
    c /= max(m, 1e-3);

    // saturation boost (ACES desaturates bright highlights)
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = clamp(mix(vec3(l), c, 1.45), 0.0, 1.0);
    return c;
}

// ---------------------------------------------------------------- sky

float stars(vec3 d, float scale, float density, float sharp, float seed)
{
    vec3 p  = d * scale + seed;
    vec3 id = floor(p);
    vec3 f  = p - id;

    float h = hash13(id);
    if (h > density) return 0.0;

    vec3 sp = vec3(hash13(id + 1.31),
                   hash13(id + 5.73),
                   hash13(id + 9.17));

    float dist2 = dot(f - sp, f - sp);
    float b = pow(h / density, 14.0);
    return b * exp(-dist2 * sharp);
}

vec3 starfield(vec3 d)
{
    vec3 col = vec3(0.0);

    // faint galactic band
    vec3 axis = normalize(vec3(0.31, 0.86, -0.41));
    float band = exp(-pow(dot(d, axis) * 2.4, 2.0));
    float neb  = fbm(d * 3.2 + 4.0, 5);
    col += vec3(0.008, 0.011, 0.022) * (0.10 + 1.8 * band * neb);

    // two layers of stars, kept below the bloom threshold
    col += vec3(0.72, 0.82, 1.00) * 0.55 * stars(d, 460.0, 0.34, 9.0, 0.0);
    col += vec3(1.00, 0.86, 0.68) * 0.28 * stars(d, 190.0, 0.26, 7.0, 13.7);

    return col;
}

// ---------------------------------------------------------------- disk

float geoRhs(float u)
{
    float m = (uLens == 1) ? M : 0.0;
    return -u + 3.0 * m * u * u;
}

void sampleDisk(vec3 p, float r, vec3 traceDir,
                out vec3 emission, out float alpha)
{
    float om  = sqrt(M / (r * r * r));
    float ang = atan(p.z, p.x) - om * uTime * uRotSpeed + 1.1 * log(r / uDiskIn);
    float lr  = log(r / uDiskIn);

    vec3 base = vec3(cos(ang), sin(ang), 0.0) * 2.1
              + vec3(0.0, 0.0, lr * 6.5);

    // domain warp gives wispy, sheared filaments instead of smooth blobs
    float warp = fbm(base * 0.85 + 3.7, 3) - 0.5;
    vec3 q = base + warp * vec3(3.2, 3.2, 1.8);

    float n1  = fbm(q, 6);
    float rid = 1.0 - abs(2.0 * fbm(q * 2.7 + 12.0, 4) - 1.0);
    float lane = smoothstep(0.52, 0.78, fbm(q * 1.45 + 21.0, 4));

    float dens = smoothstep(0.40, 0.68, n1) * 0.72
               + smoothstep(0.58, 0.94, rid) * 0.28;
    dens  = mix(0.20, 1.0, clamp(dens, 0.0, 1.0));
    dens *= 1.0 - 0.55 * lane;
    dens *= 0.86 + 0.14 * sin(lr * 11.0 + n1 * 4.0);

    float eIn  = smoothstep(0.0, 0.55, r - uDiskIn);
    float eOut = 1.0 - smoothstep(uDiskOut - 2.2, uDiskOut, r);
    float edge = eIn * eOut;
    float prof = pow(uDiskIn / r, 2.2);

    // relativistic Doppler + gravitational shift
    vec3 nObs  = -traceDir;
    vec3 omega = vec3(0.0, om, 0.0);
    vec3 beta  = cross(omega, p);
    float b2   = dot(beta, beta);
    float gam  = inversesqrt(max(1.0 - b2, 0.02));
    float mu   = dot(beta, nObs);
    float g    = sqrt(max(1.0 - RS / r, 0.0)) / max(gam * (1.0 - mu), 0.05);
    g = clamp(g, 0.08, 5.0);

    float T = uDiskTemp * pow(uDiskIn / max(r, uDiskIn), 0.75) * g;
    float I = uDiskBright * prof * dens * edge * pow(g, 3.0);

    emission = blackbody(T) * I;
    alpha    = clamp(0.22 + 0.72 * dens, 0.0, 0.94) * edge;
}

// ---------------------------------------------------------------- main

void main()
{
    vec2 frag = gl_FragCoord.xy + uJitter;
    vec2 uv   = (2.0 * frag - uRes) / uRes.y;

    vec3 rd = normalize(uCamFwd
                      + uTanHalfFov * (uv.x * uCamRight + uv.y * uCamUp));
    vec3 ro = uCamPos;

    float r0 = length(ro);
    vec3 e1  = ro / r0;
    vec3 vp  = rd - e1 * dot(rd, e1);
    float vt = length(vp);
    float vr = dot(rd, e1);

    vec3  col  = vec3(0.0);
    float trans = 1.0;

    // perfectly radial ray: either plunges or escapes
    if (vt < 1e-4) {
        col = (vr < 0.0) ? vec3(0.0) : starfield(rd);
        outColor = vec4(col, 1.0);
        return;
    }

    vec3 e2 = vp / vt;

    float u    = 1.0 / r0;
    float w    = -u * vr / vt;      // du/dphi
    float phi  = 0.0;
    float gPrev = e1.y;             // sin/cos of the disk-plane crossing function
    float phiMax = TAU * 2.5;

    bool escaped = false;

    for (int i = 0; i < uSteps; ++i) {
        float r = 1.0 / u;
        float h = clamp(0.035 * r, 0.02, 0.09);

        // RK4 for u'' = -u + 3 M u^2
        float k1u = w;
        float k1w = geoRhs(u);
        float k2u = w + 0.5 * h * k1w;
        float k2w = geoRhs(u + 0.5 * h * k1u);
        float k3u = w + 0.5 * h * k2w;
        float k3w = geoRhs(u + 0.5 * h * k2u);
        float k4u = w + h * k3w;
        float k4w = geoRhs(u + h * k3u);

        float un  = u + (h / 6.0) * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
        float wn  = w + (h / 6.0) * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);

        if (un > 1.0) break;    // crossed the horizon

        float phiN = phi + h;

        if (uDisk == 1) {
            float gN = e1.y * cos(phiN) + e2.y * sin(phiN);

            if (gPrev * gN < 0.0) {
                float t    = gPrev / (gPrev - gN);
                float phiC = phi + h * t;
                float uC   = u + (un - u) * t;
                float rC   = 1.0 / uC;

                if (rC > uDiskIn && rC < uDiskOut && phiC > 0.02) {
                    float wC = w + (wn - w) * t;

                    vec3 dirC = e1 * cos(phiC) + e2 * sin(phiC);
                    vec3 tang = -e1 * sin(phiC) + e2 * cos(phiC);
                    vec3 pC   = dirC / uC;
                    vec3 dp   = (tang - (wC / uC) * dirC) / uC;
                    vec3 traceDir = normalize(dp);

                    vec3 emission;
                    float a;
                    sampleDisk(pC, rC, traceDir, emission, a);

                    col   += trans * emission;
                    trans *= (1.0 - a);
                }
            }
            gPrev = gN;
        }

        u   = un;
        w   = wn;
        phi = phiN;

        if (u < 1.0 / 150.0 && w < 0.0) {
            escaped = true;
            break;
        }
        if (phi > phiMax) break;
    }

    if (escaped && trans > 0.001) {
        float uu  = u;
        vec3 dirC = e1 * cos(phi) + e2 * sin(phi);
        vec3 tang = -e1 * sin(phi) + e2 * cos(phi);
        vec3 dp   = (tang - (w / uu) * dirC) / uu;
        vec3 outDir = normalize(dp);
        col += trans * starfield(outDir);
    }

    outColor = vec4(col, 1.0);
}
