#version 330 core
in vec2 uv;in vec3 n;out vec4 fragColor;
uniform sampler2D albedo;uniform int textured;uniform vec3 tint;
void main(){vec4 c=textured==1?texture(albedo,uv):vec4(1);if(c.a<.4)discard;
 float light=.45+.55*abs(dot(normalize(n),normalize(vec3(.4,1,.25))));fragColor=vec4(c.rgb*tint*light,1);}
