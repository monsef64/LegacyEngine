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
uniform vec3 eye;
out vec2 uv;out vec3 n;
void main(){
 vec3 p=position*vec3(dimensions.x,dimensions.y,dimensions.x)*placement.w;
 if(grass!=0)p.x+=sin(time*1.5+placement.x*.4+placement.z*.3)*.04*position.y;
 float angle=grass==2?0.:yaw;
 mat2 rotation=mat2(cos(angle),-sin(angle),sin(angle),cos(angle));p.xz=rotation*p.xz;
 vec3 nn=normal/vec3(dimensions.x,dimensions.y,dimensions.x);nn.xz=rotation*nn.xz;n=normalize(terrainNormal*nn);
 if(grass==2){
  vec3 center=(terrainModel*vec4(placement.xyz,1)).xyz;
  vec3 up=normalize(mat3(terrainModel)*vec3(0,1,0));
  vec3 toward=eye-center;
  vec3 right=cross(up,toward);
  if(dot(right,right)<.000001)right=mat3(terrainModel)*vec3(1,0,0);
  right=normalize(right);
  // Keep terrain scale and vertical anchoring, but face the camera around local up.
  vec3 wp=center+right*p.x*length(terrainModel[0].xyz)+up*p.y*length(terrainModel[1].xyz);
  n=normalize(cross(right,up));
  gl_Position=vp*vec4(wp,1);
 }else gl_Position=vp*terrainModel*vec4(p+placement.xyz,1);
 uv=texcoord;
}
