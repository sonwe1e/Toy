cbuffer ComposeConstants : register(b0) {
    // QMatrix4x4::copyDataTo() exports mathematical rows. Keep four explicit float4 rows instead
    // of relying on an implicit HLSL matrix packing convention at this native Qt boundary.
    float4 clipFromItemRow0 : packoffset(c0);
    float4 clipFromItemRow1 : packoffset(c1);
    float4 clipFromItemRow2 : packoffset(c2);
    float4 clipFromItemRow3 : packoffset(c3);
    float4 destinationRect : packoffset(c4);
    float4 sourceUvRect : packoffset(c5);
    float opacity : packoffset(c6.x);
    float3 composePadding : packoffset(c6.y);
};

struct ComposeVertexOutput {
    // Independently compiled D3D11 stages must retain this exact register prefix. The build-time
    // reflection check enforces SV_POSITION r0, opacity r1, and UV r2 for every pixel shader.
    float4 position : SV_POSITION;
    nointerpolation float opacity : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

ComposeVertexOutput ComposeVertexShader(uint vertexId : SV_VertexID) {
    // Four vertices form a strip: top-left, top-right, bottom-left, bottom-right.
    const float2 corner = float2(vertexId & 1, (vertexId >> 1) & 1);
    const float2 itemPosition = destinationRect.xy + (corner * destinationRect.zw);

    ComposeVertexOutput output;
    const float4 item = float4(itemPosition, 0.0f, 1.0f);
    output.position = float4(
        dot(clipFromItemRow0, item),
        dot(clipFromItemRow1, item),
        dot(clipFromItemRow2, item),
        dot(clipFromItemRow3, item));
    output.uv = sourceUvRect.xy + (corner * sourceUvRect.zw);
    output.opacity = opacity;
    return output;
}

float4 BlackPixelShader(
    // SV_POSITION reserves input r0 so opacity remains aligned with the vertex shader's r1.
    float4 position : SV_POSITION,
    nointerpolation float opacity : TEXCOORD0) : SV_TARGET {
    const float alpha = saturate(opacity);
    return float4(0.0f, 0.0f, 0.0f, alpha);
}
