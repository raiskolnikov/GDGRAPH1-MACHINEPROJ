#version 330 core
// output
out vec4 FragColor;
//input
in vec3 texCoord;
// cubemap texture
uniform samplerCube skybox;

void main()
{
	//sample cubmap using direction vector
	FragColor = texture(skybox, texCoord);
}