#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D scene;      // original HDR scene (RGBA16F)
uniform sampler2D bloomBlur;  // blurred bright parts
uniform float exposure;
uniform float bloomIntensity;

void main() {
    vec3 hdrColor = texture(scene, TexCoords).rgb;
    vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;
    vec3 result = hdrColor + bloomIntensity * bloomColor;
    // tone mapping (Reinhard)
    result = vec3(1.0) - exp(-result * exposure);
    // gamma
    result = pow(result, vec3(1.0/2.2));
    FragColor = vec4(result, 1.0);
}
