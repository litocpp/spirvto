[[vk::binding(0, 0)]] cbuffer Params {
    row_major float2x3 transform;
    float weights[2];
};
[[vk::binding(1, 0)]] Texture2D<float4> image;
[[vk::binding(1, 0)]] SamplerState imageSampler;

float4 sampleImage(Texture2D<float4> textureValue, SamplerState samplerValue, float2 uv) {
    return textureValue.Sample(samplerValue, uv);
}

float4 main(float2 uv : TEXCOORD0) : SV_Target {
    return sampleImage(image, imageSampler, uv) * (transform[1][2] + weights[1]);
}
