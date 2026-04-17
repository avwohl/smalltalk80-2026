// st80-2026 — Shaders.metal
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Fullscreen quad + texture sampler. Used by MetalRenderer to blit
// the Smalltalk display RGBA8 buffer to the MTKView drawable.

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut st80_vertex(uint vid [[vertex_id]]) {
    // Triangle-strip quad: NDC corners and matching UVs. UV y is
    // flipped so the image's (0,0) origin lands at the top-left of
    // the drawable rather than the bottom-left.
    constexpr float2 positions[4] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0)
    };
    constexpr float2 uvs[4] = {
        float2(0.0, 1.0),
        float2(1.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 0.0)
    };

    VertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.uv = uvs[vid];
    return out;
}

fragment float4 st80_fragment(VertexOut in [[stage_in]],
                              texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(address::clamp_to_edge, filter::nearest);
    return tex.sample(s, in.uv);
}
