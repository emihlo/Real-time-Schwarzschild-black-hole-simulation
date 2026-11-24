#version 330 core

in vec2 vUV;
in vec4 vColor;
uniform sampler2D uFont;
out vec4 outColor;

void main()
{
    float coverage = texture(uFont, vUV).a;
    outColor = vec4(vColor.rgb, vColor.a * coverage);
}
