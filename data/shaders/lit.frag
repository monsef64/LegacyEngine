#version 330 core
in vec3 vNormal;
in vec3 vWorldPosition;
in vec2 vCheckerUV;
in vec2 vUV;
uniform sampler2D uMaterialTexture, uNormalTexture;
uniform bool uHasMaterialTexture, uBumped, uTransparent;
uniform float uNormalStrength;
uniform vec2 uTiling;
uniform bool uCheckerboard;
uniform sampler2D uCheckerTexture;
uniform sampler2DArrayShadow uShadowTexture;
uniform float uSpecular;
uniform vec3 uCameraPosition, uColor;
uniform int uLightCount;
uniform bool uSoftShadows;
uniform vec4 uLightPosition[16];  // xyz, type
uniform vec4 uLightColor[16];     // rgb * intensity, range
uniform vec4 uLightDirection[16]; // xyz, outer cone cosine
uniform vec4
    uLightShadow[16]; // layer, bias, inner cone cosine, active resolution / atlas resolution
uniform mat4 uShadowMatrix[12];
uniform int uCascadeCount[16];
uniform vec4 uCascadeSplits[16];
uniform vec4 uShadowView; // forward, -dot(near-plane center, forward)
out vec4 fragColor;
int cubeFace(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        return d.x >= 0.0 ? 0 : 1;
    }
    if (a.y >= a.z) {
        return d.y >= 0.0 ? 2 : 3;
    }
    return d.z >= 0.0 ? 4 : 5;
}
// Each lookup bilinearly filters four *depth comparisons*, not stored depths.
// This keeps shadow boundaries continuous as they move between map texels.
float shadowTap(
    vec2 uv, int layer, float depth, vec2 center, vec2 gradient, float scale, vec2 texel) {
    uv = clamp(uv, texel * .5, vec2(scale) - texel * .5);
    float receiverDepth = depth + dot(gradient, (uv - center) / scale);
    return texture(uShadowTexture, vec4(uv, float(layer), receiverDepth));
}
float mapVisibility(int i, int layer, vec3 normal, vec3 toLight, vec3 worldDx, vec3 worldDy) {
    mat4 lightMatrix = uShadowMatrix[layer];
    vec4 projected = lightMatrix * vec4(vWorldPosition, 1.0);
    if (projected.w <= 0.0) {
        return 1.0;
    }
    vec3 p = projected.xyz / projected.w * .5 + .5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) {
        return 1.0;
    }
    vec2 texel = 1.0 / vec2(textureSize(uShadowTexture, 0).xy);
    float scale = uLightShadow[i].w;
    // Analytic projection derivatives from unconditional world derivatives.
    // Taking dFdx inside this divergent light/shadow branch is undefined.
    vec4 dx = lightMatrix * vec4(worldDx, 0.0), dy = lightMatrix * vec4(worldDy, 0.0);
    vec3 dpdx = (dx.xyz * projected.w - projected.xyz * dx.w) * (.5 / (projected.w * projected.w));
    vec3 dpdy = (dy.xyz * projected.w - projected.xyz * dy.w) * (.5 / (projected.w * projected.w));
    float determinant = dpdx.x * dpdy.y - dpdx.y * dpdy.x;
    vec2 gradient = vec2(0.0);
    if (abs(determinant) > 1e-10) {
        gradient =
            clamp(vec2(dpdy.y * dpdx.z - dpdx.y * dpdy.z, dpdx.x * dpdy.z - dpdy.x * dpdx.z) /
                      determinant,
                  vec2(-8.0),
                  vec2(8.0));
    }
    float bias = uLightShadow[i].y * (1.0 + 2.0 * (1.0 - max(dot(normal, toLight), 0.0)));
    // Cover the half-texel footprint of the hardware bilinear comparison.
    bias += .5 * dot(abs(gradient), texel / scale);
    float depth = p.z - bias;
    vec2 center = p.xy * scale;
    if (!uSoftShadows || scale < .3) {
        return shadowTap(center, layer, depth, center, gradient, scale, texel);
    }
    vec2 coord = center / texel + .5;
    vec2 fraction = fract(coord), base = (floor(coord) - .5) * texel;
    float sum = 0.0;
    if (scale < .6) {
        // Separable 3x3 tent, integrated exactly into four bilinear taps.
        vec2 wx = vec2(3.0 - 2.0 * fraction.x, 1.0 + 2.0 * fraction.x);
        vec2 wy = vec2(3.0 - 2.0 * fraction.y, 1.0 + 2.0 * fraction.y);
        vec2 ox = vec2((2.0 - fraction.x) / wx.x - 1.0, fraction.x / wx.y + 1.0);
        vec2 oy = vec2((2.0 - fraction.y) / wy.x - 1.0, fraction.y / wy.y + 1.0);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                sum += wx[x] * wy[y] *
                       shadowTap(base + vec2(ox[x], oy[y]) * texel,
                                 layer,
                                 depth,
                                 center,
                                 gradient,
                                 scale,
                                 texel);
            }
        }
        return sum / 16.0;
    }
    // Separable 5x5 tent, integrated into nine bilinear taps (not 25).
    vec3 wx = vec3(4.0 - 3.0 * fraction.x, 7.0, 1.0 + 3.0 * fraction.x);
    vec3 wy = vec3(4.0 - 3.0 * fraction.y, 7.0, 1.0 + 3.0 * fraction.y);
    vec3 ox = vec3(
        (3.0 - 2.0 * fraction.x) / wx.x - 2.0, (3.0 + fraction.x) / 7.0, fraction.x / wx.z + 2.0);
    vec3 oy = vec3(
        (3.0 - 2.0 * fraction.y) / wy.x - 2.0, (3.0 + fraction.y) / 7.0, fraction.y / wy.z + 2.0);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            sum += wx[x] * wy[y] *
                   shadowTap(base + vec2(ox[x], oy[y]) * texel,
                             layer,
                             depth,
                             center,
                             gradient,
                             scale,
                             texel);
        }
    }
    return sum / 144.0;
}
float visibility(int i, vec3 normal, vec3 toLight, vec3 worldDx, vec3 worldDy) {
    int base = int(uLightShadow[i].x);
    if (base < 0) {
        return 1.0;
    }
    if (int(uLightPosition[i].w) == 1) {
        return mapVisibility(i,
                             base + cubeFace(vWorldPosition - uLightPosition[i].xyz),
                             normal,
                             toLight,
                             worldDx,
                             worldDy);
    }
    if (uCascadeCount[i] <= 1) {
        return mapVisibility(i, base, normal, toLight, worldDx, worldDy);
    }
    float depth = dot(vWorldPosition, uShadowView.xyz) + uShadowView.w;
    vec4 splits = uCascadeSplits[i];
    if (depth < 0.0 || depth > splits.w) {
        return 1.0;
    }
    int cascade = 0;
    if (depth > splits.x) {
        cascade = 1;
    }
    if (depth > splits.y) {
        cascade = 2;
    }
    if (depth > splits.z) {
        cascade = 3;
    }
    float value = mapVisibility(i, base + cascade, normal, toLight, worldDx, worldDy);
    float start = cascade == 0 ? 0.0 : splits[cascade - 1];
    float end = splits[cascade];
    float blend = smoothstep(mix(start, end, 0.9), end, depth);
    if (blend > 0.0) {
        float next = cascade == 3
                         ? 1.0
                         : mapVisibility(i, base + cascade + 1, normal, toLight, worldDx, worldDy);
        value = mix(value, next, blend);
    }
    return value;
}
void main() {
    vec3 worldDx = dFdx(vWorldPosition), worldDy = dFdy(vWorldPosition);
    vec2 uv = vUV * uTiling;
    vec2 uvDx = dFdx(uv), uvDy = dFdy(uv);
    vec3 normal = normalize(vNormal);
    if (uBumped) {
        float det = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
        if (abs(det) > 1e-10) {
            vec3 t = (worldDx * uvDy.y - worldDy * uvDx.y) / det;
            vec3 b = (-worldDx * uvDy.x + worldDy * uvDx.x) / det;
            t -= normal * dot(normal, t);
            if (dot(t, t) > 1e-10) {
                t = normalize(t);
                b = normalize(cross(normal, t)) * sign(dot(cross(normal, t), b));
                vec3 mapped = textureGrad(uNormalTexture, uv, uvDx, uvDy).xyz * 2.0 - 1.0;
                mapped.xy *= uNormalStrength;
                vec3 n = t * mapped.x + b * mapped.y + normal * mapped.z;
                if (dot(n, n) > 1e-10) {
                    normal = normalize(n);
                }
            }
        }
    }
    vec3 albedo = uColor;
    vec4 texel = uHasMaterialTexture ? texture(uMaterialTexture, uv) : vec4(1);
    albedo *= texel.rgb;
    float alpha = uTransparent ? texel.a : 1.0;
    if (alpha < 0.001) {
        discard;
    }
    if (uCheckerboard) {
        albedo *= texture(uCheckerTexture, vCheckerUV).rgb;
    }
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 result = albedo * .12;
    for (int i = 0; i < uLightCount; ++i) {
        int type = int(uLightPosition[i].w);
        vec3 delta = uLightPosition[i].xyz - vWorldPosition;
        float dist = length(delta), attenuation = 1.0;
        vec3 toLight = -uLightDirection[i].xyz;
        if (type != 0) {
            if (dist >= uLightColor[i].w) {
                continue;
            }
            toLight = delta / max(dist, .0001);
            float falloff = max(1.0 - dist * dist / (uLightColor[i].w * uLightColor[i].w), 0.0);
            attenuation = falloff * falloff / (1.0 + .09 * dist + .032 * dist * dist);
            if (type == 2) {
                attenuation *= smoothstep(
                    uLightDirection[i].w, uLightShadow[i].z, dot(-toLight, uLightDirection[i].xyz));
            }
        }
        float diffuse = max(dot(normal, toLight), 0.0);
        if (diffuse <= 0.0 || attenuation <= 0.0) {
            continue;
        }
        vec3 halfDirection = normalize(toLight + viewDirection);
        float specular = uSpecular * pow(max(dot(normal, halfDirection), 0.0), 64.0);
        result += (albedo * diffuse + vec3(specular)) * uLightColor[i].rgb * attenuation *
                  visibility(i, normal, toLight, worldDx, worldDy);
    }
    fragColor = vec4(result, alpha);
}
