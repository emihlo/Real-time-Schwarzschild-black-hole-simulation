#version 330 core

out vec4 outColor;

uniform sampler2D uScene;
uniform sampler2D uPrev;
uniform sampler2D uBloom;

uniform int   uMode;          // 0 accum, 1 bright, 2 blur, 3 composite
uniform float uWeight;
uniform float uThreshold;
uniform vec2  uTexel;
uniform vec2  uDir;
uniform float uExposure;
uniform float uBloomStrength;
uniform vec2  uRes;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14),
                 0.0, 1.0);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / uRes;

    if (uMode == 0) {
        vec3 s = texture(uScene, uv).rgb;
        vec3 p = texture(uPrev,  uv).rgb;
        outColor = vec4(mix(p, s, uWeight), 1.0);
    }
    else if (uMode == 1) {
        vec3 c = texture(uScene, uv).rgb;
        float l = max(max(c.r, c.g), c.b);
        float k = smoothstep(uThreshold, uThreshold * 2.0, l);
        outColor = vec4(c * k, 1.0);
    }
    else if (uMode == 2) {
        vec2 o = uTexel * uDir;
        vec3 c = texture(uScene, uv).rgb * 0.227027;
        c += (texture(uScene, uv + o * 1.0).rgb +
              texture(uScene, uv - o * 1.0).rgb) * 0.1945946;
        c += (texture(uScene, uv + o * 2.0).rgb +
              texture(uScene, uv - o * 2.0).rgb) * 0.1216216;
        c += (texture(uScene, uv + o * 3.0).rgb +
              texture(uScene, uv - o * 3.0).rgb) * 0.0540540;
        c += (texture(uScene, uv + o * 4.0).rgb +
              texture(uScene, uv - o * 4.0).rgb) * 0.0162162;
        outColor = vec4(c, 1.0);
    }
    else {
        vec3 c = texture(uScene, uv).rgb
               + texture(uBloom, uv).rgb * uBloomStrength;
        c *= uExposure;
        c = aces(c);

        // restore some chroma lost to the filmic curve
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = clamp(mix(vec3(l), c, 1.35), 0.0, 1.0);

        c = pow(c, vec3(1.0 / 2.2));

        float d = (hash12(gl_FragCoord.xy) - 0.5) / 255.0;
        outColor = vec4(c + d, 1.0);
    }
}
