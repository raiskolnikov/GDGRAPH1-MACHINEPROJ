#version 330 core
//vertex attribute
layout(location = 0) in vec3 aPos;
//output
out vec3 texCoord;
//matrices
uniform mat4 projection;
uniform mat4 view;

void main(){
	//transform vertex position to clipsapce
	vec4 pos = projection * view * vec4(aPos, 1.0);
	// set clipspace position
	gl_Position = vec4(pos.x, pos.y, pos.w, pos.w);
	//pass cube position to frag shader
	texCoord = aPos;
}