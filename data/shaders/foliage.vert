#version 330 core
layout(location=0)in vec3 position;
layout(location=1)in vec3 normal;
layout(location=2)in vec2 texcoord;
layout(location=3)in vec4 placement;
layout(location=4)in float yaw;
uniform mat4 vp,terrainModel;
uniform vec2 dimensions;
uniform mat3 terrainNormal;
uniform float time;
uniform int grass;
out vec2 uv;out vec3 n;
void main(){
 vec3 p=position*vec3(dimensions.x,dimensions.y,dimensions.x)*placement.w;
 if(grass==1)p.x+=sin(time*1.5+placement.x*.4+placement.z*.3)*.04*position.y;
 mat2 rotation=mat2(cos(yaw),-sin(yaw),sin(yaw),cos(yaw));p.xz=rotation*p.xz;
 vec3 nn=normal/vec3(dimensions.x,dimensions.y,dimensions.x);nn.xz=rotation*nn.xz;n=normalize(terrainNormal*nn);
 gl_Position=vp*terrainModel*vec4(p+placement.xyz,1);uv=texcoord;
}
