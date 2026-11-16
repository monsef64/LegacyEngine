#version 330 core
in vec2 uv;out vec4 fragColor;
uniform sampler2D sourceImage,depthImage,bloomImage;
uniform int pass,flags;uniform vec2 texel,direction;
uniform mat4 inverseVP,previousVP;uniform vec3 eye;uniform float time,motionScale;
uniform float bloomIntensity,bloomThreshold,exposure,contrast,saturation,temperature,vignetteIntensity,focusDistance,focusRange,dofStrength,motionStrength,grainIntensity,aberrationStrength,aoIntensity,aoRadius;
bool effect(int bit){return (flags&bit)!=0;}
vec3 image(vec2 p){return texture(sourceImage,clamp(p,texel*.5,vec2(1)-texel*.5)).rgb;}
vec3 world(vec2 p,float d){vec4 w=inverseVP*vec4(p*2.-1.,d*2.-1.,1);return w.xyz/w.w;}
float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
void main(){
    if(pass==0){vec3 c=(image(uv+texel*vec2(-.5,-.5))+image(uv+texel*vec2(.5,-.5))+image(uv+texel*vec2(-.5,.5))+image(uv+texel*.5))*.25;float bright=max(max(c.r,c.g),c.b);fragColor=vec4(c*max(bright-bloomThreshold,0.)/max(bright,.0001),1);return;}
    if(pass==1){vec2 d=direction*texel;vec3 c=image(uv)*.227027;c+=(image(uv+d*1.384615)+image(uv-d*1.384615))*.316216;c+=(image(uv+d*3.230769)+image(uv-d*3.230769))*.070270;fragColor=vec4(c,1);return;}
    vec3 c=image(uv);float depth=1.;vec3 p=vec3(0);float distanceToEye=0.;vec3 surfaceNormal=vec3(0,1,0);
    if((flags&(8|16|256))!=0){depth=texture(depthImage,uv).r;p=world(uv,depth);distanceToEye=length(p-eye);if(effect(256))surfaceNormal=normalize(cross(dFdx(p),dFdy(p)));}
    // FXAA directional edge search/filter. Flat pixels stop after four neighbors.
    if(effect(128)){
        vec3 nw=image(uv+texel*vec2(-1,-1)),ne=image(uv+texel*vec2(1,-1)),sw=image(uv+texel*vec2(-1,1)),se=image(uv+texel*vec2(1,1));
        float m=luma(c),a=luma(nw),b=luma(ne),d=luma(sw),e=luma(se),lo=min(m,min(min(a,b),min(d,e))),hi=max(m,max(max(a,b),max(d,e)));
        if(hi-lo>max(.0312,hi*.125)){vec2 dir=vec2(-((a+b)-(d+e)),(a+d)-(b+e));float reduce=max((a+b+d+e)*.03125,.0078125);dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),vec2(-8),vec2(8))*texel;
            vec3 ca=(image(uv+dir*(-1./6.))+image(uv+dir*(1./6.)))*.5;
            vec3 cb=ca*.5+(image(uv-dir*.5)+image(uv+dir*.5))*.25;float l=luma(cb);c=l<lo||l>hi?ca:cb;}
    }
    if(effect(8)&&depth<.999999){float coc=clamp((abs(distanceToEye-focusDistance)-focusRange*.5)/max(focusRange,.01),0.,1.)*dofStrength;vec2 r=texel*coc*6.;vec3 sum=c;float weight=1.;for(int i=0;i<6;++i){float angle=float(i)*1.047198;vec2 q=clamp(uv+vec2(cos(angle),sin(angle))*r,vec2(0),vec2(1));float nd=texture(depthImage,q).r;float delta=abs(length(world(q,nd)-eye)-distanceToEye);float w=1./(1.+delta/max(focusRange,.01));sum+=image(q)*w;weight+=w;}c=mix(c,sum/weight,coc);}
    if(effect(16)&&motionScale>0.&&depth<.999999){vec4 prev=previousVP*vec4(p,1);if(prev.w>0.){vec2 velocity=clamp(uv-(prev.xy/prev.w*.5+.5),vec2(-.04),vec2(.04))*motionStrength*motionScale;vec3 sum=c;for(int i=1;i<4;++i)sum+=image(uv-velocity*float(i)/3.);c=sum*.25;}}
    if(effect(256)&&depth<.999999){vec3 normal=surfaceNormal;if(dot(normal,eye-p)<0.)normal=-normal;float occ=0.;float radiusPixels=clamp(aoRadius/max(distanceToEye,.1)/texel.y,2.,32.);for(int i=0;i<6;++i){float a=float(i)*2.399963;vec2 q=uv+vec2(cos(a),sin(a))*texel*radiusPixels*(.4+.1*float(i));if(any(lessThan(q,vec2(0)))||any(greaterThan(q,vec2(1))))continue;float z=texture(depthImage,q).r;if(z>=.999999)continue;vec3 delta=world(q,z)-p;float len=length(delta);occ+=max(dot(normal,delta/max(len,.0001))-.08,0.)*max(1.-len/aoRadius,0.);}c*=clamp(1.-occ*(aoIntensity/3.),.1,1.);}
    if(effect(64)){vec2 offset=(uv-.5)*aberrationStrength;c.r=mix(c.r,image(uv+offset).r,.65);c.b=mix(c.b,image(uv-offset).b,.65);}
    if(effect(1))c+=texture(bloomImage,uv).rgb*bloomIntensity;
    if(effect(2)){c*=exp2(exposure)*vec3(1.+temperature*.1,1.,1.-temperature*.1);c=mix(vec3(luma(c)),c,saturation);c=(c-.5)*contrast+.5;}
    if(effect(4)){vec2 d=(uv-.5)*2.;c*=1.-vignetteIntensity*smoothstep(.25,1.3,dot(d,d));}
    if(effect(32)){float n=fract(sin(dot(gl_FragCoord.xy+time,vec2(12.9898,78.233)))*43758.5453)-.5;c+=n*grainIntensity;}
    fragColor=vec4(max(c,vec3(0)),1);
}
