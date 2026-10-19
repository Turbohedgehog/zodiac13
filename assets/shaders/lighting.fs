#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

#define CUBE_FACES 6

// light_clusters.h: the frame's point lights (kLightTexels each, laid out there), then a header
// per cluster (offset and count) and the clusters' light indices, four to a texel.
uniform sampler2D lightTable;
uniform int lightTableWidth;
uniform int clusterHeaders;
uniform int clusterIndices;
// Tiles of clusterTile pixels times depth slices even in log(depth): clusterDepth is near and
// log(far/near).
uniform ivec3 clusterCounts;
uniform int clusterTile;
uniform vec2 clusterDepth;
uniform vec3 viewForward;
// Tiles of shadowTileSize pixels, shadowColumns to a row: per slot two cubes of six faces in
// order, the distance to the nearest block and to the nearest player, over the light's radius.
uniform sampler2D shadowAtlas;
uniform int shadowTileSize;
uniform int shadowColumns;

// The lamps' light baked into cells (cell_light/cell_light.h): per cell of the layout the piece
// of its meter cube it belongs to (255 for an opaque cell), and per piece of each cube its light,
// then its direction toward its lamps, in kPieces stacks of the cubes along z each.
uniform int cellLightEnabled;
uniform usampler3D cellLabels;
uniform sampler3D cellLight;
uniform ivec3 cellOrigin;
uniform ivec3 cellCubes;

// Lamps near players, with their shadow slot in the color's w: a lamp's light is baked, so
// where a player stands between it and a face, its share is taken back.
#define MAX_PLAYER_SHADOW_LAMPS 32
uniform int playerShadowCount;
uniform vec4 playerShadowLamps[MAX_PLAYER_SHADOW_LAMPS];
uniform vec4 playerShadowColors[MAX_PLAYER_SHADOW_LAMPS];

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

// A normal this close to an axis is a face along the grid; a face whose normal.z is above
// kSteepFace is nearly flat (a roof) and takes X, not Z, to square its checker against.
const float kAlongGrid = 0.999;
const float kSteepFace = 0.9;
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
const int kLightTexels = 5;
const int kBlocksLayer = 0;
const int kPlayersLayer = 1;
// light_layout.h: a node spans kNodeCells cells a side, a cube keeps kPieces pieces apart.
const int kNodeCells = 4;
const int kPieces = 3;
// The share of baked light a face turned away from its lamps still gets.
const float kBakedFloor = 0.35;
// Meters the baked light's sample moves toward the viewer: a pixel on a seam rasterized a hair
// inside the wall comes back out to the side it is seen from.
const float kSeamNudge = 0.005;
// A slope's face lies inside its block's opaque box: its light is sought this many cells along
// the normal, and taken unchecked for walls if none is free there (a flight of slopes).
const int kSurfaceSteps = kNodeCells;

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
float StoredDistance(int slot, int layer, vec3 direction)
{
    int face = FaceOf(direction);
    vec3 forward = kFaceForward[face];
    vec3 back = -forward;
    vec3 right = normalize(cross(kFaceUp[face], back));
    vec3 up = cross(back, right);
    float depth = dot(direction, forward);
    vec2 uv = vec2(dot(direction, right), dot(direction, up))/depth*0.5 + 0.5;
    int tile = (slot*2 + layer)*CUBE_FACES + face;
    ivec2 corner = ivec2(tile % shadowColumns, tile/shadowColumns)*shadowTileSize;
    ivec2 texel = clamp(ivec2(uv*float(shadowTileSize)), ivec2(0), ivec2(shadowTileSize - 1));
    return texelFetch(shadowAtlas, corner + texel, 0).r;
}

float PointShadow(int slot, vec3 fromLight, float radius)
{
    if (slot < 0) return 1.0;
    float nearest = min(StoredDistance(slot, kBlocksLayer, fromLight), StoredDistance(slot, kPlayersLayer, fromLight));
    return length(fromLight)/radius - kCubeBias > nearest ? 0.0 : 1.0;
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

vec4 TableTexel(int index)
{
    return texelFetch(lightTable, ivec2(index % lightTableWidth, index/lightTableWidth), 0);
}

int ClusterOf(vec3 position)
{
    ivec2 tile = min(ivec2(gl_FragCoord.xy)/clusterTile, clusterCounts.xy - 1);
    float depth = max(dot(position - viewPos, viewForward), clusterDepth.x);
    int slice = clamp(int(log(depth/clusterDepth.x)/clusterDepth.y*float(clusterCounts.z)), 0, clusterCounts.z - 1);
    return (slice*clusterCounts.y + tile.y)*clusterCounts.x + tile.x;
}

// The baked light at `point` (in cells) on a face turned to `normal`: the eight nearest nodes,
// weighed by distance, each through the cell of its cube nearest the point's cell. A node adds
// nothing when that cell is opaque, or when the point's own cube is cut off from it on the way:
// a wall in the point's cube, between the point and the neighbour.
vec3 CellLight(vec3 point, vec3 normal)
{
    vec3 surface = point;
    ivec3 cell;
    uint ownPiece = uint(kPieces);
    for (int step = 0; step <= kSurfaceSteps && ownPiece >= uint(kPieces); step++)
    {
        point = surface + normal*float(step);
        cell = ivec3(floor(point)) - cellOrigin;
        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, cellCubes*kNodeCells))) break;
        ownPiece = texelFetch(cellLabels, cell, 0).r;
    }
    if (ownPiece >= uint(kPieces))
    {
        point = surface;
        cell = ivec3(floor(point)) - cellOrigin;
        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, cellCubes*kNodeCells))) return vec3(0.0);
    }
    ivec3 ownCube = cell/kNodeCells;
    vec3 cubePoint = (point - vec3(cellOrigin))/float(kNodeCells) - 0.5;
    ivec3 base = ivec3(floor(cubePoint));
    vec3 along = cubePoint - vec3(base);
    vec3 light = vec3(0.0);
    vec3 direction = vec3(0.0);
    float weights = 0.0;
    for (int corner = 0; corner < 8; corner++)
    {
        ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        ivec3 cube = base + offset;
        if (any(lessThan(cube, ivec3(0))) || any(greaterThanEqual(cube, cellCubes))) continue;
        ivec3 nearest = clamp(cell, cube*kNodeCells, cube*kNodeCells + kNodeCells - 1);
        uint piece = texelFetch(cellLabels, nearest, 0).r;
        if (piece >= uint(kPieces)) continue;
        ivec3 gateway = clamp(nearest, ownCube*kNodeCells, ownCube*kNodeCells + kNodeCells - 1);
        if (ownPiece < uint(kPieces) && texelFetch(cellLabels, gateway, 0).r != ownPiece) continue;
        vec3 shares = mix(1.0 - along, along, vec3(offset));
        float weight = shares.x*shares.y*shares.z;
        ivec3 texel = ivec3(cube.xy, cube.z + int(piece)*cellCubes.z);
        light += weight*texelFetch(cellLight, texel, 0).rgb;
        direction += weight*texelFetch(cellLight, texel + ivec3(0, 0, kPieces*cellCubes.z), 0).xyz;
        weights += weight;
    }
    if (weights <= 0.0) return vec3(0.0);
    light /= weights;
    direction /= weights;
    float oneSided = length(direction);
    float facing = oneSided > 0.0 ? max(dot(normal, direction/oneSided), 0.0) : 0.0;
    return light*mix(1.0, kBakedFloor + (1.0 - kBakedFloor)*facing, oneSided);
}

// What the lamps' baked light loses where players stand between them and the face: blocks
// nearer than the face mean the lamp never lit it.
vec3 PlayerShadows(vec3 normal, bool twoSided, vec3 offset)
{
    vec3 taken = vec3(0.0);
    for (int i = 0; i < playerShadowCount; i++)
    {
        vec3 fromLight = fragPosition - playerShadowLamps[i].xyz;
        float radius = playerShadowLamps[i].w;
        float distance = length(fromLight);
        if (distance >= radius) continue;
        vec3 toLight = -fromLight/distance;
        float diffuse = max(dot(FacingNormal(normal, twoSided, toLight), toLight), 0.0);
        if (diffuse <= 0.0) continue;
        int slot = int(playerShadowColors[i].w);
        vec3 sampled = offset - playerShadowLamps[i].xyz;
        float stored = length(sampled)/radius - kCubeBias;
        if (stored > StoredDistance(slot, kBlocksLayer, sampled) || stored <= StoredDistance(slot, kPlayersLayer, sampled)) continue;
        float fade = 1.0 - (distance*distance)/(radius*radius);
        taken += playerShadowColors[i].rgb*fade*fade*diffuse;
    }
    return taken;
}

// Unchanged up to kKnee, then rising ever slower toward 1 with no kink.
vec3 Compress(vec3 light)
{
    vec3 over = max(light - kKnee, 0.0);
    return min(light, vec3(kKnee)) + (1.0 - kKnee)*(1.0 - exp(-over/(1.0 - kKnee)));
}

// One square per grid cell. A face along the grid takes the cell it looks out of, half a cell
// inward so a face on a cell boundary doesn't flicker between cells; an oblique one squares up
// in its own plane, horizontal along the face and the other way up it.
bool CheckerSquare(vec3 position, vec3 normal)
{
    if (max(abs(normal.x), max(abs(normal.y), abs(normal.z))) > kAlongGrid)
    {
        ivec3 cell = ivec3(floor(position/cellSize - normal*0.5));
        return ((cell.x + cell.y + cell.z) & 1) == 0;
    }
    vec3 along = abs(normal.z) > kSteepFace ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 across = normalize(cross(along, normal));
    vec3 up = cross(normal, across);
    ivec2 square = ivec2(floor(vec2(dot(position, across), dot(position, up))/cellSize));
    return ((square.x + square.y) & 1) == 0;
}

void main()
{
    vec3 normal = normalize(fragNormal);
    vec4 texelColor = texture(texture0, fragTexCoord);
    if (checkerEnabled == 1)
    {
        texelColor = CheckerSquare(fragPosition, normal) ? checkerFirst : checkerSecond;
        if (checkerEmissive == 1)
        {
            finalColor = texelColor*colDiffuse;
            return;
        }
    }
    bool twoSided = texelColor.a < 1.0;
    vec3 toView = normalize(viewPos - fragPosition);
    vec3 offset = fragPosition + normal*kNormalOffset;
    // In the free cell the face looks into: a wall between two rooms shows each its own side.
    vec3 facing = fragPosition + normal*cellSize*0.5 + toView*kSeamNudge;
    vec3 light = vec3(0.0);
    if (cellLightEnabled == 1)
    {
        vec3 baked = CellLight(facing/cellSize, normal);
        light += max(baked - PlayerShadows(normal, twoSided, offset), vec3(0.0));
    }

    vec4 header = TableTexel(clusterHeaders + ClusterOf(fragPosition));
    int first = int(header.x);
    int count = int(header.y);
    for (int n = first; n < first + count; n++)
    {
        int base = int(TableTexel(clusterIndices + n/4)[n % 4])*kLightTexels;
        vec4 positionRadius = TableTexel(base);
        vec4 reachMin = TableTexel(base + 2);
        vec4 reachMax = TableTexel(base + 3);
        if (any(lessThan(facing, reachMin.xyz)) || any(greaterThan(facing, reachMax.xyz))) continue;
        vec3 fromLight = fragPosition - positionRadius.xyz;
        float radius = positionRadius.w;
        float distance = length(fromLight);
        if (distance >= radius) continue;
        float fade = 1.0 - (distance*distance)/(radius*radius);
        vec4 directionCone = TableTexel(base + 4);
        float along = dot(fromLight/distance, directionCone.xyz);
        if (along <= directionCone.w) continue;
        fade *= smoothstep(directionCone.w, mix(directionCone.w, 1.0, kConeSoftness), along);
        vec4 colorShadow = TableTexel(base + 1);
        vec3 shaded = Shade(normal, twoSided, -fromLight/distance, toView, colorShadow.rgb);
        if (shaded == vec3(0.0)) continue;
        light += shaded*fade*fade*PointShadow(int(colorShadow.w), offset - positionRadius.xyz, radius);
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
