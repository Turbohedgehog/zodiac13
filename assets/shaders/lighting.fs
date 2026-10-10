#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

// lights.h: kMaxLights.
#define MAX_LIGHTS 32
#define CUBE_FACES 6

// Point lights in meters: position and radius, color scaled by intensity and the shadow
// slot (-1 without one), and the box a light without a shadow stays in (its room).
uniform int lightCount;
uniform vec4 lightPositionRadius[MAX_LIGHTS];
uniform vec4 lightColorShadow[MAX_LIGHTS];
uniform vec4 lightReachMin[MAX_LIGHTS];
uniform vec4 lightReachMax[MAX_LIGHTS];
// A spot's direction and the cosine of half its cone; a cosine below -1 shines every way.
uniform vec4 lightDirectionCone[MAX_LIGHTS];
// Tiles of shadowTileSize pixels, shadowColumns to a row, one per slot and cube face in
// order: the distance to the nearest caster over the light's radius.
uniform sampler2D shadowAtlas;
uniform int shadowTileSize;
uniform int shadowColumns;

// Directional lights: the sun, and a dim fill from the other side that keeps the hull's
// shaded faces from going black. With a shadow, each lights only where its depth map sees.
uniform int sunEnabled;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform int sunShadowEnabled;
uniform mat4 sunViewProjection;
uniform sampler2D sunShadow;
uniform int fillEnabled;
uniform vec3 fillDirection;
uniform vec3 fillColor;
uniform int fillShadowEnabled;
uniform mat4 fillViewProjection;
uniform sampler2D fillShadow;

// Z-up hemispheric ambient: `ambient` from above, `ambientGround` from below.
uniform vec3 ambient;
uniform vec3 ambientGround;
uniform vec3 viewPos;
// Station blocks: a checker of one square per grid cell, from the world position.
uniform int checkerEnabled;
uniform int checkerEmissive;
uniform vec4 checkerFirst;
uniform vec4 checkerSecond;
uniform float cellSize;

// Pulls shadow lookups off the lit surface, against acne on the faces they sample.
const float kNormalOffset = 0.04;
const float kCubeBias = 0.02;
const float kDirectionalBias = 0.0015;
const float kShininess = 16.0;
// Of a spot's cone, the outer part where its light fades to the edge.
const float kConeSoftness = 0.25;
// The share of the fill a face turned straight away from it still gets.
const float kFillFloor = 0.3;
// The light above which it is compressed toward 1, so many lamps together don't burn out.
const float kKnee = 0.6;

// shadow_maps_state.cpp: kCubeFaceViews, the way each face looks and its up.
const vec3 kFaceForward[CUBE_FACES] = vec3[](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0),
                                             vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 kFaceUp[CUBE_FACES] = vec3[](vec3(0, -1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1),
                                        vec3(0, -1, 0), vec3(0, -1, 0));

int FaceOf(vec3 direction)
{
    vec3 size = abs(direction);
    if (size.x >= size.y && size.x >= size.z) return direction.x > 0.0 ? 0 : 1;
    if (size.y >= size.z) return direction.y > 0.0 ? 2 : 3;
    return direction.z > 0.0 ? 4 : 5;
}

// As the face was drawn: raylib's MatrixLookAt and a 90 degree perspective.
float StoredDistance(int slot, vec3 direction)
{
    int face = FaceOf(direction);
    vec3 forward = kFaceForward[face];
    vec3 back = -forward;
    vec3 right = normalize(cross(kFaceUp[face], back));
    vec3 up = cross(back, right);
    float depth = dot(direction, forward);
    vec2 uv = vec2(dot(direction, right), dot(direction, up))/depth*0.5 + 0.5;
    int tile = slot*CUBE_FACES + face;
    ivec2 corner = ivec2(tile % shadowColumns, tile/shadowColumns)*shadowTileSize;
    ivec2 texel = clamp(ivec2(uv*float(shadowTileSize)), ivec2(0), ivec2(shadowTileSize - 1));
    return texelFetch(shadowAtlas, corner + texel, 0).r;
}

float PointShadow(int slot, vec3 fromLight, float radius)
{
    if (slot < 0) return 1.0;
    return length(fromLight)/radius - kCubeBias > StoredDistance(slot, fromLight) ? 0.0 : 1.0;
}

float DirectionalShadow(bool enabled, mat4 viewProjection, sampler2D map, vec3 position)
{
    if (!enabled) return 1.0;
    vec4 clip = viewProjection*vec4(position, 1.0);
    vec3 coords = clip.xyz/clip.w*0.5 + 0.5;
    if (any(lessThan(coords, vec3(0.0))) || any(greaterThan(coords, vec3(1.0)))) return 1.0;
    return coords.z - kDirectionalBias > textureLod(map, coords.xy, 0.0).r ? 0.0 : 1.0;
}

vec3 FacingNormal(vec3 normal, bool twoSided, vec3 toLight)
{
    return (twoSided && dot(normal, toLight) < 0.0) ? -normal : normal;
}

// Diffuse plus Blinn-Phong highlight of one light from `toLight`; glass is lit on both faces.
vec3 Shade(vec3 normal, bool twoSided, vec3 toLight, vec3 toView, vec3 color)
{
    vec3 lit = FacingNormal(normal, twoSided, toLight);
    float diffuse = max(dot(lit, toLight), 0.0);
    if (diffuse <= 0.0) return vec3(0.0);
    float highlight = pow(max(dot(toView, reflect(-toLight, lit)), 0.0), kShininess);
    return color*(diffuse + highlight);
}

// Wrapped around the faces turned away too, as light scattered from all around would be.
vec3 Fill(vec3 normal, bool twoSided, vec3 toLight, vec3 color)
{
    return color*mix(kFillFloor, 1.0, dot(FacingNormal(normal, twoSided, toLight), toLight)*0.5 + 0.5);
}

// Unchanged up to kKnee, then rising ever slower toward 1 with no kink.
vec3 Compress(vec3 light)
{
    vec3 over = max(light - kKnee, 0.0);
    return min(light, vec3(kKnee)) + (1.0 - kKnee)*(1.0 - exp(-over/(1.0 - kKnee)));
}

void main()
{
    vec3 normal = normalize(fragNormal);
    vec4 texelColor = texture(texture0, fragTexCoord);
    if (checkerEnabled == 1)
    {
        // Half a cell inward, so a face lying on a cell boundary doesn't flicker between cells.
        ivec3 cell = ivec3(floor(fragPosition/cellSize - normal*0.5));
        texelColor = ((cell.x + cell.y + cell.z) & 1) == 0 ? checkerFirst : checkerSecond;
        if (checkerEmissive == 1)
        {
            finalColor = texelColor*colDiffuse;
            return;
        }
    }
    bool twoSided = texelColor.a < 1.0;
    vec3 toView = normalize(viewPos - fragPosition);
    vec3 offset = fragPosition + normal*kNormalOffset;
    // Inside the solid, so faces on a room's walls count as the room's.
    vec3 inward = fragPosition - normal*cellSize*0.5;
    vec3 light = vec3(0.0);

    for (int i = 0; i < lightCount; i++)
    {
        if (any(lessThan(inward, lightReachMin[i].xyz)) || any(greaterThan(inward, lightReachMax[i].xyz))) continue;
        vec3 fromLight = fragPosition - lightPositionRadius[i].xyz;
        float radius = lightPositionRadius[i].w;
        float distance = length(fromLight);
        if (distance >= radius) continue;
        float fade = 1.0 - (distance*distance)/(radius*radius);
        float cone = lightDirectionCone[i].w;
        float along = dot(fromLight/distance, lightDirectionCone[i].xyz);
        if (along <= cone) continue;
        fade *= smoothstep(cone, mix(cone, 1.0, kConeSoftness), along);
        vec3 shaded = Shade(normal, twoSided, -fromLight/distance, toView, lightColorShadow[i].rgb);
        if (shaded == vec3(0.0)) continue;
        int slot = int(lightColorShadow[i].w);
        light += shaded*fade*fade*PointShadow(slot, offset - lightPositionRadius[i].xyz, radius);
    }
    if (sunEnabled == 1)
    {
        light += Shade(normal, twoSided, -sunDirection, toView, sunColor)*
                 DirectionalShadow(sunShadowEnabled == 1, sunViewProjection, sunShadow, offset);
    }
    if (fillEnabled == 1)
    {
        light += Fill(normal, twoSided, -fillDirection, fillColor)*
                 DirectionalShadow(fillShadowEnabled == 1, fillViewProjection, fillShadow, offset);
    }
    vec3 hemisphere = mix(ambientGround, ambient, normal.z*0.5 + 0.5);

    finalColor = vec4(texelColor.rgb*colDiffuse.rgb*Compress(light + hemisphere), 1.0);
    // Glass and previews keep their own alpha.
    finalColor.a = texelColor.a*colDiffuse.a;
}
