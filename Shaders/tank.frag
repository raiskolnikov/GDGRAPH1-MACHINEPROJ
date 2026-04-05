#version 330 core

in vec3 fragPos;
in vec2 texCoord;
in mat3 TBN;

out vec4 FragColor;

uniform sampler2D tex0;      // diffuse  (vh_megatron_film_03)
uniform sampler2D tex1;      // emissive (vh_megatron_film_03_e)
uniform sampler2D tex2;      // normal   (vh_megatron_film_03_n)

uniform vec3 ambientColor;
uniform vec3 viewPos;

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};
uniform DirLight dirLight;

struct PtLight {
    vec3  position;
    vec3  color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
};
uniform PtLight pointLight;

// calc PBR
vec3 calcLight(vec3 lightDir, vec3 lightColor, float intensity,
               vec3 norm, vec3 viewDir,
               vec3 albedo, float rough, float metal)
{
    vec3  L = normalize(lightDir);
    vec3  H = normalize(L + viewDir);
    float diff = max(dot(norm, L), 0.0);
    float shininess = mix(128.0, 2.0, rough);
    float spec = pow(max(dot(norm, H), 0.0), shininess);

    vec3 diffuse = diff * lightColor * intensity * mix(albedo, vec3(0.0), metal);
    vec3 specular = spec * lightColor * intensity * mix(vec3(0.04), albedo, metal);

    return diffuse + specular;
}

void main()
{
    // sample textures
    vec4 albedo = texture(tex0, texCoord);
    vec3 normalSample = texture(tex2, texCoord).rgb;
    vec4 orm = texture(tex1, texCoord);

    float rough = orm.g;
    float metal = orm.b;

    // normal from map
    vec3 norm = normalize(TBN * (normalSample * 2.0 - 1.0));
    vec3 viewDir = normalize(viewPos - fragPos);

    // directional light (moon)
    vec3 dirContrib = calcLight(
        -dirLight.direction,
        dirLight.color, dirLight.intensity,
        norm, viewDir,
        albedo.rgb, rough, metal
    );

    // point light
    vec3  toLight = pointLight.position - fragPos;
    float dist = length(toLight);
    float atten = 1.0 / (pointLight.constant
                          + pointLight.linear * dist
                          + pointLight.quadratic * dist * dist);

    vec3 ptContrib = calcLight(
        normalize(toLight),
        pointLight.color, pointLight.intensity * atten,
        norm, viewDir,
        albedo.rgb, rough, metal
    );

    // combine
    vec3 ambient = ambientColor * albedo.rgb;
    vec3 finalColor = ambient + dirContrib + ptContrib;

    FragColor = vec4(finalColor, albedo.a);
}