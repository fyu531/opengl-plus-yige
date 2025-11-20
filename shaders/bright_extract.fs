#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D scene;
void main() {
    vec3 col = texture(scene, TexCoords).rgb;
    float brightness = dot(col, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > 1.0) FragColor = vec4(col, 1.0); else FragColor = vec4(0.0);
}
