#version 440

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D frame;

void main()
{
    // Video is shown opaque; the alpha of composited frames is not meaningful here.
    fragColor = vec4(texture(frame, v_texcoord).rgb, 1.0);
}
