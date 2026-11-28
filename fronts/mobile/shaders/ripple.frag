#version 440

// The touch ripple (Ripple.qml), after Android 12's: a wave from the touch
// point, soft-edged, its front lit up, the area it has covered filled with
// faint "stars" that twinkle while the finger stays down and shine brighter
// along that front. Cut to the control's rounded shape here, so the control
// needs no clipping.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;          // the control, in item pixels
    vec2 origin;        // where it was touched
    float radius;       // how far the wave has got
    float cornerRadius; // the control's shape
    float fade;         // 0..1: in on the touch, out after the release
    float time;         // seconds: what makes the stars twinkle
    float dp;           // item pixels per dp, for sizes that don't scale with the control
    vec4 color;         // the ink, straight (not premultiplied)
};

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Signed distance to a rounded box centered on 0 with half-size b.
float roundedBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 px = qt_TexCoord0 * size;

    // Cut at the control's edge, as the control itself is.
    float shape = 1.0 - smoothstep(-0.75, 0.75, roundedBox(px - size * 0.5, size * 0.5, min(cornerRadius, min(size.x, size.y) * 0.5)));

    float d = distance(px, origin);
    // A broad soft edge: it widens with the wave and is never narrower than
    // a few dp. Everything is in item pixels, and dp is the app's own scale,
    // so a larger interface scale gives a wider edge too.
    float soft = max(radius * 0.65, 28.0 * dp);
    float inside = 1.0 - smoothstep(radius - soft, radius, d);
    // The front: a gentle band of light inside the edge.
    float front = (d - (radius - soft * 0.5)) / (soft * 0.35);
    float band = exp(-front * front);
    float glow = band * inside;

    // Stars: one cell in several lit, each with its own phase, ~1.5 dp. In the
    // covered area they're just at the edge of seeing, a grain in the light
    // rather than a pattern.
    vec2 cell = floor(px / (1.5 * dp));
    float n = hash(cell);
    float lit = step(0.88, n);
    float twinkle = 0.3 + 0.7 * (0.5 + 0.5 * sin(time * 12.0 + n * 97.0));
    float star = lit * twinkle;
    // On the front they have to stand out against its light, so they get their
    // share by the whole band: cut by `inside` as the light is, they'd fade out
    // in its outer half and be lost in the inner one.
    float stars = 0.025 * star * inside + 0.09 * star * band;

    float alpha = (0.07 * inside + 0.05 * glow + stars) * fade * shape * color.a;
    fragColor = vec4(color.rgb * alpha, alpha) * qt_Opacity;
}
