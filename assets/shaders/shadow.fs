#version 330

in vec3 fragPosition;

// A lamp's shadow cube stores the distance over the light's radius; the sun's map only
// needs depth, which it writes without a color buffer.
uniform vec3 lightPosition;
uniform float lightRadius;

out vec4 finalColor;

void main()
{
    finalColor = vec4(length(fragPosition - lightPosition)/lightRadius);
}
