#version 330 core
out vec4 FragColor;
in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;

uniform vec3 camPos;
uniform vec3 lightPosA;
uniform vec3 lightColorA;
uniform vec3 lightPosB;
uniform vec3 lightColorB;
uniform float time;

// ----------------------------------------------------------------------------
// PBR helper functions (NDF GGX, Geometry Schlick-GGX, Fresnel Schlick)
// ----------------------------------------------------------------------------
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N,H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float denom = (NdotH2*(a2-1.0) + 1.0);
    denom = 3.14159265 * denom * denom;
    return a2 / max(denom, 0.000001);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N,V), 0.0);
    float NdotL = max(dot(N,L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
// ----------------------------------------------------------------------------

void main() {
    vec3 albedo = pow(texture(albedoMap, TexCoords).rgb, vec3(2.2)); // gamma to linear
    float metal = texture(metallicMap, TexCoords).r;
    float roughness = texture(roughnessMap, TexCoords).r;
    float ao = texture(aoMap, TexCoords).r;
    vec3 N = normalize(Normal);
    vec3 V = normalize(camPos - WorldPos);
    vec3 R = reflect(-V, N);

    // Calculate F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metal);

    // lights
    vec3 Lo = vec3(0.0);
    for (int i=0;i<2;i++){
        vec3 Lpos = (i==0) ? lightPosA : lightPosB;
        vec3 Lcolor = (i==0) ? lightColorA : lightColorB;
        vec3 L = normalize(Lpos - WorldPos);
        vec3 H = normalize(V + L);
        float distance = length(Lpos - WorldPos);
        float attenuation = 1.0 / (distance * distance);
        float NdotL = max(dot(N, L), 0.0);
        // Cook-Torrance
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 numerator = D * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denom;
        vec3 kD = (1.0 - F) * (1.0 - metal);
        vec3 diffuse = kD * albedo / 3.14159265;
        vec3 radiance = Lcolor * attenuation;
        Lo += (diffuse + specular) * radiance * NdotL;
    }

    // ambient: simple ao factor
    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lo;

    // HDR tonemap (we will keep HDR for bloom pipeline) - but still output linear
    FragColor = vec4(color, 1.0);
}
