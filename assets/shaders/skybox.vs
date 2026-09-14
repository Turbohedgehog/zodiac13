#version 330

in vec3 vertexPosition;

uniform mat4 matProjection;
uniform mat4 matView;
uniform mat4 matModel;

out vec3 fragPosition;

void main()
{
    // matModel carries the Y-up -> Z-up fixup rotation; apply it to the sample dir.
    fragPosition = mat3(matModel) * vertexPosition;

    // Strip translation from view and model so the skybox stays centred on the camera.
    mat4 rotView = mat4(mat3(matView));
    mat4 rotModel = mat4(mat3(matModel));
    gl_Position = matProjection * rotView * rotModel * vec4(vertexPosition, 1.0);
}
