//https://github.com/SaschaWillems/Vulkan/blob/master/data/shaders/glsl/computeparticles/particle.frag
#version 450

layout (set = 1, binding = 0) uniform sampler2D samplerColorMap;
layout (set = 1, binding = 1) uniform sampler2D samplerGradientRamp;

layout (location = 0) in vec4 inColor;
layout (location = 1) in float inGradientPos;

layout (location = 0) out vec4 outFragColor;

void main ()
{
	vec3 color = texture(samplerGradientRamp, vec2(inGradientPos, 0.0)).rgb;
	outFragColor.rgb = texture(samplerColorMap, gl_PointCoord).rgb * color;
}
