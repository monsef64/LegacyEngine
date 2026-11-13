#version 330 core
in vec2 uv;out vec4 fragColor;
uniform mat4 inverseVP;uniform vec3 tint;uniform bool hasTexture,isHDR;
uniform samplerCube faces;uniform sampler2D panorama;
void main(){
    vec4 nearP=inverseVP*vec4(uv,-1,1),farP=inverseVP*vec4(uv,1,1);
    vec3 d=normalize(farP.xyz/farP.w-nearP.xyz/nearP.w);
    vec3 c=vec3(1);
    if(hasTexture){if(isHDR){vec2 p=vec2(atan(d.z,d.x)/6.28318530718+0.5,0.5-asin(clamp(d.y,-1.0,1.0))/3.14159265359);c=texture(panorama,p).rgb;}
        else c=texture(faces,d).rgb;}
    c=max(c*tint,vec3(0));if(isHDR && hasTexture)c=pow(c/(c+vec3(1)),vec3(1.0/2.2));
    fragColor=vec4(c,1);
}
