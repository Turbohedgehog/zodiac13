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
uniform int checkerTextured;
uniform vec4 checkerFirst;
uniform vec4 checkerSecond;
uniform float cellSize;
// Debug view of a station's light, stepped by F11/F12: 0 off, 1 white albedo, 2 only the baked
// light on white, 3 the baked light's node cubes in hashed colors, 4 the baked light less the
// players' shadows, 5 the lamps and flashlights in the clusters, 6 the sun, 7 the fill, 8 the sheen alone.
uniform int debugView;

// A normal this close to an axis is a face along the grid; a face whose normal.z is above
// kSteepFace is nearly flat (a roof) and takes X, not Z, to square its checker against.
const float kAlongGrid = 0.999;
const float kSteepFace = 0.9;
// Pulls shadow lookups off the lit surface, against acne on the faces they sample.
const float kNormalOffset = 0.04;
const float kCubeBias = 0.02;
// In meters: a depth bias in the map's own units grew with the station, past a wall's thickness.
const float kDirectionalBiasM = 0.08;
// A highlight is cut where the cosine of its angle crosses the edge, over a soft rim this wide.
const float kHighlightEdge = 0.65;
const float kHighlightSoft = 0.04;
// A flashlight sits at the eye, so its highlight must be tight and faint or it floods the face it looks at.
const float kFlashEdge = 0.97;
const float kFlashGloss = 0.3;
// Matte metal: a faint highlight over a weakened diffuse.
const float kMetalSpecular = 0.45;
const float kMetalDiffuse = 0.65;
// The tile's brightness as a height: its seams and rivets tilt the highlight's normal by this much.
const float kBump = 6.0;
// A grazing view brightens a face by this share of its baked light.
const float kRim = 0.5;
const float kRimPower = 3.0;
// How much of the face's color the sheen takes on: 0 white as plastic's, 1 tinted as a metal's.
const float kSheenTint = 0.5;
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
// How far, in cells, a face in an opaque cell looks sideways for a free one to take its light from.
const int kBorrowReach = 3;
// What a free cell the lamps' light never reached adds to its distance, so a lit one a bit farther wins.
const int kUnlitPenalty = 100;

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
    // The map's depth range in meters is 2/|the z row|, as the transform is an ortho times a look-at.
    float bias = kDirectionalBiasM*0.5*length(vec3(viewProjection[0][2], viewProjection[1][2], viewProjection[2][2]));
    return coords.z - bias > textureLod(map, coords.xy, 0.0).r ? 0.0 : 1.0;
}

vec3 FacingNormal(vec3 normal, bool twoSided, vec3 toLight)
{
    return (twoSided && dot(normal, toLight) < 0.0) ? -normal : normal;
}

float Highlight(float cosine, float edge)
{
    return smoothstep(edge - kHighlightSoft, edge + kHighlightSoft, cosine);
}

// The diffuse of one light from `toLight`, and in `gloss` its crisp highlight; glass is lit on both faces.
vec3 Shade(vec3 normal, vec3 shineNormal, bool twoSided, vec3 toLight, vec3 toView, vec3 color, out vec3 gloss)
{
    gloss = vec3(0.0);
    vec3 lit = FacingNormal(normal, twoSided, toLight);
    float diffuse = max(dot(lit, toLight), 0.0);
    if (diffuse <= 0.0) return vec3(0.0);
    gloss = color*Highlight(dot(toView, reflect(-toLight, FacingNormal(shineNormal, twoSided, toLight))), kFlashEdge)*kFlashGloss;
    return color*diffuse*kMetalDiffuse;
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
vec3 CellLight(vec3 point, vec3 normal, vec3 shineNormal, vec3 toView, out vec3 sheen)
{
    sheen = vec3(0.0);
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
        // A floor beside an oblique wall lies in cells the wall's centre test makes opaque, up the whole
        // wall: borrow the cell of the nearest free neighbour, beside it first, then above.
        point = surface;
        ivec3 own = ivec3(floor(point)) - cellOrigin;
        int nearestDistance = 0;
        for (int dz = 0; dz <= 1; dz++)
        {
            for (int dy = -kBorrowReach; dy <= kBorrowReach; dy++)
            {
                for (int dx = -kBorrowReach; dx <= kBorrowReach; dx++)
                {
                    ivec3 shift = ivec3(dx, dy, dz);
                    ivec3 neighbour = own + shift;
                    if (any(lessThan(neighbour, ivec3(0))) || any(greaterThanEqual(neighbour, cellCubes*kNodeCells))) continue;
                    uint label = texelFetch(cellLabels, neighbour, 0).r;
                    if (label >= uint(kPieces)) continue;
                    ivec3 nodeCube = neighbour/kNodeCells;
                    vec3 nodeLight = texelFetch(cellLight, ivec3(nodeCube.xy, nodeCube.z + int(label)*cellCubes.z), 0).rgb;
                    int distance = shift.x*shift.x + shift.y*shift.y + shift.z*shift.z
                                   + (nodeLight.r + nodeLight.g + nodeLight.b > 0.0 ? 0 : kUnlitPenalty);
                    if (ownPiece >= uint(kPieces) || distance < nearestDistance)
                    {
                        ownPiece = label;
                        cell = neighbour;
                        nearestDistance = distance;
                        point = vec3(cellOrigin + neighbour) + 0.5;
                    }
                }
            }
        }
        if (ownPiece >= uint(kPieces))
        {
            cell = own;
            if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, cellCubes*kNodeCells))) return vec3(0.0);
        }
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
    // Many lamps add up past 1, and the sheen sits above the compressed light: it follows no more than a full one.
    vec3 shineLight = min(light, vec3(1.0));
    float oneSided = length(direction);
    float facing = oneSided > 0.0 ? max(dot(normal, direction/oneSided), 0.0) : 0.0;
    if (oneSided > 0.0)
    {
        vec3 halfway = normalize(direction/oneSided + toView);
        sheen = shineLight*oneSided*kMetalSpecular*facing*Highlight(dot(shineNormal, halfway), kHighlightEdge);
    }
    sheen += shineLight*kRim*pow(1.0 - max(dot(shineNormal, toView), 0.0), kRimPower);
    return light*mix(1.0, kBakedFloor + (1.0 - kBakedFloor)*facing, oneSided)*kMetalDiffuse;
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

// One tile per meter by the world position and the face's dominant axis; the gradients come from
// the unwrapped position, so the repeat leaves no seam in the mip level.
vec4 BlockTexel(vec3 position, vec3 normal)
{
    vec3 a = abs(normal);
    vec2 p = (a.x > a.y && a.x > a.z) ? vec2(position.y, -position.z)
           : (a.y > a.z) ? vec2(position.x, -position.z) : vec2(position.x, -position.y);
    return textureGrad(texture0, fract(p), dFdx(p), dFdy(p));
}

float TileHeight(vec2 p, vec2 gradX, vec2 gradY)
{
    return dot(textureGrad(texture0, fract(p), gradX, gradY).rgb, vec3(0.299, 0.587, 0.114));
}

// The face's normal tilted by the slope of the tile's brightness, along the tile's own axes
// (as in BlockTexel); the gradients keep the bump from aliasing at a distance.
vec3 BumpedNormal(vec3 position, vec3 normal)
{
    vec3 a = abs(normal);
    vec2 p;
    vec3 alongU;
    vec3 alongV;
    if (a.x > a.y && a.x > a.z)
    {
        p = vec2(position.y, -position.z);
        alongU = vec3(0.0, 1.0, 0.0);
        alongV = vec3(0.0, 0.0, -1.0);
    }
    else if (a.y > a.z)
    {
        p = vec2(position.x, -position.z);
        alongU = vec3(1.0, 0.0, 0.0);
        alongV = vec3(0.0, 0.0, -1.0);
    }
    else
    {
        p = vec2(position.x, -position.y);
        alongU = vec3(1.0, 0.0, 0.0);
        alongV = vec3(0.0, -1.0, 0.0);
    }
    vec2 gradX = dFdx(p);
    vec2 gradY = dFdy(p);
    vec2 texel = 1.0/vec2(textureSize(texture0, 0));
    float du = TileHeight(p + vec2(texel.x, 0.0), gradX, gradY) - TileHeight(p - vec2(texel.x, 0.0), gradX, gradY);
    float dv = TileHeight(p + vec2(0.0, texel.y), gradX, gradY) - TileHeight(p - vec2(0.0, texel.y), gradX, gradY);
    return normalize(normal - kBump*0.5*(du*alongU + dv*alongV));
}

void main()
{
    vec3 normal = normalize(fragNormal);
    vec4 texelColor = texture(texture0, fragTexCoord);
    if (checkerEnabled == 1)
    {
        texelColor = CheckerSquare(fragPosition, normal) ? checkerFirst : checkerSecond;
        if (checkerTextured == 1)
        {
            texelColor = BlockTexel(fragPosition, normal);
        }
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
    vec3 shineNormal = (checkerEnabled == 1 && checkerTextured == 1) ? BumpedNormal(fragPosition, normal) : normal;
    vec3 sheen = vec3(0.0);
    vec3 light = vec3(0.0);
    if (cellLightEnabled == 1)
    {
        vec3 baked = CellLight(facing/cellSize, normal, shineNormal, toView, sheen);
        light += max(baked - PlayerShadows(normal, twoSided, offset), vec3(0.0));
    }

    vec3 bakedEnd = light;
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
        vec3 gloss;
        vec3 shaded = Shade(normal, shineNormal, twoSided, -fromLight/distance, toView, colorShadow.rgb, gloss);
        if (shaded == vec3(0.0)) continue;
        float lit = fade*fade*PointShadow(int(colorShadow.w), offset - positionRadius.xyz, radius);
        light += shaded*lit;
        sheen += gloss*lit;
    }
    vec3 clusterEnd = light;
    if (sunEnabled == 1)
    {
        vec3 gloss;
        float sunLit = DirectionalShadow(sunShadowEnabled == 1, sunViewProjection, sunShadow, offset);
        light += Shade(normal, shineNormal, twoSided, -sunDirection, toView, sunColor, gloss)*sunLit;
        sheen += gloss*sunLit;
    }
    vec3 sunEnd = light;
    if (fillEnabled == 1)
    {
        light += Fill(normal, twoSided, -fillDirection, fillColor)*
                 DirectionalShadow(fillShadowEnabled == 1, fillViewProjection, fillShadow, offset);
    }
    vec3 fillEnd = light;
    vec3 hemisphere = mix(ambientGround, ambient, normal.z*0.5 + 0.5);

    if (debugView == 1)
    {
        texelColor = vec4(1.0);
    }
    if (debugView == 2 && cellLightEnabled == 1)
    {
        vec3 unused;
        finalColor = vec4(Compress(CellLight(facing/cellSize, normal, shineNormal, toView, unused)), 1.0);
        return;
    }
    if (debugView == 4)
    {
        finalColor = vec4(Compress(bakedEnd), 1.0);
        return;
    }
    if (debugView == 8)
    {
        finalColor = vec4(Compress(sheen), 1.0);
        return;
    }
    if (debugView >= 5 && debugView <= 7)
    {
        vec3 shown = debugView == 5 ? clusterEnd - bakedEnd : debugView == 6 ? sunEnd - clusterEnd : fillEnd - sunEnd;
        finalColor = vec4(Compress(shown), 1.0);
        return;
    }
    if (debugView == 3)
    {
        ivec3 cube = (ivec3(floor(facing/cellSize)) - cellOrigin)/kNodeCells;
        uint hash = uint(cube.x)*73856093u ^ uint(cube.y)*19349663u ^ uint(cube.z)*83492791u;
        finalColor = vec4(float(hash & 255u), float((hash >> 8) & 255u), float((hash >> 16) & 255u), 255.0)/255.0;
        return;
    }
    // The sheen goes on top of the compressed light: inside it, a bright wall would swallow the highlight.
    vec3 shine = sheen*mix(vec3(1.0), texelColor.rgb, kSheenTint);
    finalColor = vec4(texelColor.rgb*colDiffuse.rgb*Compress(light + hemisphere) + shine, 1.0);
    // Glass and previews keep their own alpha.
    finalColor.a = texelColor.a*colDiffuse.a;
}
