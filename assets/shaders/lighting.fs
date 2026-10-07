#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

#define     MAX_LIGHTS              4
#define     LIGHT_DIRECTIONAL      0
#define     LIGHT_POINT            1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

uniform Light lights[MAX_LIGHTS];
// Z-up hemispheric ambient: `ambient` from above, `ambientGround` from below.
uniform vec4 ambient;
uniform vec4 ambientGround;
uniform vec3 viewPos;

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1)
        {
            vec3 light = vec3(0.0);

            if (lights[i].type == LIGHT_DIRECTIONAL)
                light = -normalize(lights[i].target - lights[i].position);

            if (lights[i].type == LIGHT_POINT)
                light = normalize(lights[i].position - fragPosition);

            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb*NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);
            specular += specCo;
        }
    }

    finalColor = (texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0)));
    vec3 hemisphere = mix(ambientGround.rgb, ambient.rgb, normal.z*0.5 + 0.5);
    finalColor += texelColor*vec4(hemisphere, 0.0)*colDiffuse;
    // The lit sum above scales alpha too; glass and previews keep their own.
    finalColor.a = texelColor.a*colDiffuse.a;
}
