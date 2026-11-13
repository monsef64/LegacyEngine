#include "physics/collider_debug.hpp"
#include "core/profiler.hpp"
#include "rendering/shader_data.hpp"
#include "scene/camera_gizmo.hpp"
#include "audio/audio_gizmo.hpp"
#include "lighting/lighting.hpp"
#include "rendering/renderer.hpp"

#include "core/components.hpp"

#include <glad/gl.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <array>

namespace {









GLuint compileShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[1024] = "";
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader compilation failed:\n%s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Returns 0 on failure (the reason is logged).
GLuint createProgram(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vertexShader   = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

    GLuint program = 0;
    if (vertexShader && fragmentShader) {
        program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024] = "";
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader linking failed:\n%s", log);
            glDeleteProgram(program);
            program = 0;
        }
    }

    // Once linked (or failed), the program no longer needs the shader objects.
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

} // namespace

bool Renderer::init()
{
    if (!gladLoadGL(SDL_GL_GetProcAddress)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load the OpenGL functions");
        return false;
    }
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    if(!skyboxRenderer_.init())return false;
    try{
        unlitProgram_=createProgram(loadShaderData("unlit.vert").c_str(),loadShaderData("unlit.frag").c_str());
        litProgram_=createProgram(loadShaderData("lit.vert").c_str(),loadShaderData("lit.frag").c_str());
    }catch(const std::exception& error){SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Shader data: %s",error.what());shutdown();return false;}
    if (!unlitProgram_ || !litProgram_) {
        shutdown();
        return false;
    }

    unlitModelViewProjection_ = glGetUniformLocation(unlitProgram_, "uModelViewProjection");
    unlitColor_               = glGetUniformLocation(unlitProgram_, "uColor");
    litModel_ = glGetUniformLocation(litProgram_, "uModel");
    litCameraPosition_ = glGetUniformLocation(litProgram_, "uCameraPosition");
    litCheckerboard_ = glGetUniformLocation(litProgram_, "uCheckerboard");
    litCheckerTexture_ = glGetUniformLocation(litProgram_, "uCheckerTexture");
    litSpecular_ = glGetUniformLocation(litProgram_, "uSpecular");
    litModelViewProjection_   = glGetUniformLocation(litProgram_, "uModelViewProjection");
    litNormalMatrix_          = glGetUniformLocation(litProgram_, "uNormalMatrix");
    litColor_                 = glGetUniformLocation(litProgram_, "uColor");

    lightCountLocation_=glGetUniformLocation(litProgram_,"uLightCount");
    shadowSamplerLocation_=glGetUniformLocation(litProgram_,"uShadowTexture");
    const char* lightNames[]{"uLightPosition","uLightColor","uLightDirection","uLightShadow"};
    for(int i=0;i<Lighting::maxLights;++i)for(int field=0;field<4;++field)
        lightLocations_[i][field]=glGetUniformLocation(litProgram_,(std::string(lightNames[field])+"["+std::to_string(i)+"]").c_str());
    for(int i=0;i<Lighting::maxShadowLayers;++i)
        shadowMatrixLocations_[i]=glGetUniformLocation(litProgram_,("uShadowMatrix["+std::to_string(i)+"]").c_str());

    litTransparent_=glGetUniformLocation(litProgram_,"uTransparent");
    litTexture_=glGetUniformLocation(litProgram_,"uMaterialTexture");litNormalTexture_=glGetUniformLocation(litProgram_,"uNormalTexture");
    litHasTexture_=glGetUniformLocation(litProgram_,"uHasMaterialTexture");litBumped_=glGetUniformLocation(litProgram_,"uBumped");
    litStrength_=glGetUniformLocation(litProgram_,"uNormalStrength");litTiling_=glGetUniformLocation(litProgram_,"uTiling");
    // Small generated texture: no external asset paths, with mipmaps to reduce
    // shimmer at distance and nearest magnification for crisp checker edges.
    const unsigned char checks[]{0,0,0,255,255,255,255,255,255,0,0,0};
    glGenTextures(1, &checkerTexture_);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,2,2,0,GL_RGB,GL_UNSIGNED_BYTE,checks);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    // Lines often lie exactly on a surface: the grid on the plane, for example.
    // Pushing surfaces a hair back in the depth buffer lets those lines win the
    // depth test instead of showing through in patches.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    return true;
}

void Renderer::shutdown()
{
    skyboxRenderer_.shutdown();
    materialTextures_.clear();materialCache_.clear();
    for (GpuMesh& mesh : meshes_) {
        glDeleteBuffers(1, &mesh.vertexBuffer);
        glDeleteVertexArrays(1, &mesh.vertexArray);
    }
    meshes_.clear();
    glDeleteTextures(1,&shadowTexture_);glDeleteFramebuffers(1,&shadowFramebuffer_);
    shadowTexture_=shadowFramebuffer_=0;
    glDeleteTextures(1, &checkerTexture_);
    checkerTexture_ = 0;
    glDeleteBuffers(1, &colliderVertexBuffer_);
    glDeleteVertexArrays(1, &colliderVertexArray_);
    colliderVertexBuffer_ = colliderVertexArray_ = 0;

    glDeleteProgram(litProgram_);
    glDeleteProgram(unlitProgram_);
    litProgram_   = 0;
    unlitProgram_ = 0;
}

MeshHandle Renderer::createMesh(const MeshData& data)
{
    GpuMesh mesh;
    mesh.vertexCount = static_cast<int>(data.vertices.size());
    for(const auto& vertex:data.vertices)mesh.radius=std::max(mesh.radius,std::sqrt(dot(vertex.position,vertex.position)));
    mesh.lines       = data.topology == MeshData::Topology::Lines;

    glGenVertexArrays(1, &mesh.vertexArray);
    glBindVertexArray(mesh.vertexArray);

    glGenBuffers(1, &mesh.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                 data.vertices.data(), GL_STATIC_DRAW);

    // Attribute 0 is the position and attribute 1 the normal, as the shaders
    // expect.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));

    glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,u)));
    glBindVertexArray(0);

    meshes_.push_back(mesh);
    return {static_cast<std::uint32_t>(meshes_.size() - 1)};
}

void Renderer::draw(World& world, const Mat4& viewProjection, int width, int height, Vec3 cameraPosition, int x, int y, bool editorView)
{
    materialTextures_.beginFrame();
    glDisable(GL_SCISSOR_TEST);
    glViewport(x, y, width, height);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    {ProfileScope scope(ProfilePart::Lights);prepareLighting(world,viewProjection,cameraPosition);}
    glViewport(x,y,width,height);
    const Skybox* activeSky=nullptr;
    world.each<Skybox>([&](Entity,const Skybox& sky){if(!activeSky && sky.enabled)activeSky=&sky;});
    {ProfileScope scope(ProfilePart::Skybox);if(activeSky)skyboxRenderer_.draw(*activeSky,materialAssets_,viewProjection);}
    ProfileScope surfaces(ProfilePart::Surfaces);
    glUseProgram(litProgram_);
    glUniform3f(litCameraPosition_, cameraPosition.x, cameraPosition.y, cameraPosition.z);
    glUniform1i(litCheckerTexture_, 0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D_ARRAY,shadowTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);

    auto resolveMaterial=[&](const MeshRenderer& renderer){
        MaterialProperties material;material.color=renderer.color;material.specular=renderer.specular;
        if(!renderer.material.empty())try{material=materialCache_.get(materialAssets_,renderer.material);}catch(const std::exception& e){materialError=e.what();}
        material=applyMaterialOverrides(material,renderer.materialValues,renderer.materialOverrides);
        if(!validMaterial(material)){materialError="Invalid material overrides";material=MaterialProperties{};}
        return material;
    };
    const Lighting::Frustum frustum(viewProjection);
    auto drawMesh=[&](const Transform& transform,const MeshRenderer& renderer,const MaterialProperties& material) {
            if (renderer.mesh.id >= meshes_.size()) {
                return; // no mesh to draw
            }
            const GpuMesh& mesh = meshes_[renderer.mesh.id];
            const float bound=mesh.radius*std::max({std::abs(transform.scale.x),std::abs(transform.scale.y),std::abs(transform.scale.z)});
            if(!frustum.visible(transform.position,bound))return;

            const Mat4 model =
                transformMatrix(transform.position, transform.rotation, transform.scale);
            const Mat4 modelViewProjection = multiply(viewProjection, model);

            if (mesh.lines) {
                glUseProgram(unlitProgram_);
                glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, modelViewProjection.m);
                glUniform3f(unlitColor_, renderer.color.x, renderer.color.y, renderer.color.z);
            } else {
                // A negative scale on one axis (or on all three) mirrors the
                // object, which turns the outside of its triangles from
                // counter-clockwise to clockwise.
                const Vec3& scale    = transform.scale;
                const bool  mirrored = scale.x * scale.y * scale.z < 0.0f;
                glFrontFace(mirrored ? GL_CW : GL_CCW);

                const Mat3 normals = normalMatrix(transform.rotation, transform.scale);
                glUseProgram(litProgram_);
                glUniformMatrix4fv(litModel_, 1, GL_FALSE, model.m);
                glUniform1i(litCheckerboard_, renderer.checkerboard ? 1 : 0);
                const auto texture=materialTextures_.get(materialAssets_,material.texture,materialError);
                const auto normal=material.shader==1?materialTextures_.get(materialAssets_,material.normalMap,materialError):0;
                glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,normal);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,checkerTexture_);
                glUniform1i(litTransparent_,material.shader==2);
                glUniform1i(litTexture_,2);glUniform1i(litNormalTexture_,3);glUniform1i(litHasTexture_,texture!=0);glUniform1i(litBumped_,normal!=0);
                glUniform1f(litStrength_,material.normalStrength);glUniform2f(litTiling_,material.tilingX,material.tilingY);
                glUniform1f(litSpecular_,material.specular);
                glUniformMatrix4fv(litModelViewProjection_, 1, GL_FALSE, modelViewProjection.m);
                glUniformMatrix3fv(litNormalMatrix_, 1, GL_FALSE, normals.m);
                glUniform3f(litColor_,material.color.x,material.color.y,material.color.z);
            }

            glBindVertexArray(mesh.vertexArray);
            glDrawArrays(mesh.lines ? GL_LINES : GL_TRIANGLES, 0, mesh.vertexCount);
        };
    struct TransparentDraw{Transform transform;MeshRenderer renderer;MaterialProperties material;float distance;};
    std::vector<TransparentDraw> transparent;
    glDisable(GL_BLEND);glDepthMask(GL_TRUE);
    world.each<Transform,MeshRenderer>([&](Entity e,const Transform& t,const MeshRenderer& r){
        if(world.has<Camera>(e))return;
        auto material=resolveMaterial(r);
        if(material.shader==2){auto delta=t.position-cameraPosition;transparent.push_back({t,r,material,dot(delta,delta)});}
        else drawMesh(t,r,material);
    });
    std::stable_sort(transparent.begin(),transparent.end(),[](const auto& a,const auto& b){return a.distance>b.distance;});
    if(!transparent.empty()){
        glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);glDepthMask(GL_FALSE);
        for(const auto& item:transparent)drawMesh(item.transform,item.renderer,item.material);
        glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    }
    if(editorView) {
        if(audioModel_.id==UINT32_MAX)audioModel_=createMesh(audioSourceGlyph());
        world.each<AudioSource,Transform>([&](Entity,const AudioSource&,const Transform& t){
            Transform glyph=t;glyph.scale={1,1,1};
            MeshRenderer icon{audioModel_,{.35f,.8f,1.0f},false,0};drawMesh(glyph,icon,resolveMaterial(icon));
        });

    }
}

void Renderer::drawCollider(const Transform& transform, const Collider& collider, const Mat4& viewProjection,float opacity)
{
    drawWire(colliderWireframe(collider,transform.scale),transform,viewProjection,{0,1,0},opacity);
}
void Renderer::drawCamera(const Transform& transform,const Camera& camera,const Mat4& viewProjection){
    drawWire(cameraFrustumWire(camera),transform,viewProjection,{.92f,.94f,.98f});
}
void Renderer::drawAudioSource(const Transform& transform,const AudioSource& source,const Mat4& viewProjection){
    drawWire(audioSourceRange(source),transform,viewProjection,{.35f,.8f,1.0f});
}
void Renderer::drawWire(const MeshData& wire,const Transform& transform,const Mat4& viewProjection,Vec3 color,float opacity){
    if(wire.vertices.empty())return;
    if (!colliderVertexArray_) {
        glGenVertexArrays(1, &colliderVertexArray_);
        glGenBuffers(1, &colliderVertexBuffer_);
        glBindVertexArray(colliderVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, colliderVertexBuffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, position)));
    }
    glBindVertexArray(colliderVertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, colliderVertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(wire.vertices.size() * sizeof(Vertex)),
                 wire.vertices.data(), GL_STREAM_DRAW);
    const Mat4 mvp = multiply(viewProjection, transformMatrix(transform.position, transform.rotation, {1, 1, 1}));
    glUseProgram(unlitProgram_);
    glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, mvp.m);
    glUniform3f(unlitColor_, color.x,color.y,color.z);
    // Show the entire collider, including the back and the plane's hidden slab.
    const bool depthTest = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    const bool blend=glIsEnabled(GL_BLEND);
    if(opacity<1){glEnable(GL_BLEND);glBlendColor(0,0,0,opacity);glBlendFuncSeparate(GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA,GL_ZERO,GL_ONE);}
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(wire.vertices.size()));
    if(opacity<1){glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);if(!blend)glDisable(GL_BLEND);}
    if (depthTest) glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void Renderer::prepareLighting(World& world,const Mat4& viewProjection,Vec3 cameraPosition){
    auto lights=Lighting::gather(world,viewProjection,cameraPosition);
    GLint framebuffer=0,readFramebuffer=0,frontFace=GL_CCW;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFramebuffer);glGetIntegerv(GL_FRONT_FACE,&frontFace);
    bool needsShadows=false;for(const auto& l:lights)needsShadows|=l.shadowLayer>=0;
    if(needsShadows && !shadowTexture_){
        glGenTextures(1,&shadowTexture_);glBindTexture(GL_TEXTURE_2D_ARRAY,shadowTexture_);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT24,Lighting::shadowResolution,Lighting::shadowResolution,Lighting::maxShadowLayers,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
        const float border[]{1,1,1,1};glTexParameterfv(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_BORDER_COLOR,border);
        glGenFramebuffers(1,&shadowFramebuffer_);
    }
    struct Caster{const GpuMesh* mesh;Mat4 model;Vec3 position;float radius;bool mirrored;};
    std::vector<Caster> casters;
    if(needsShadows)world.each<Transform,MeshRenderer>([&](Entity e,const Transform& t,const MeshRenderer& r){
        if(world.has<Camera>(e) || r.mesh.id>=meshes_.size())return;
        const auto& mesh=meshes_[r.mesh.id];if(mesh.lines)return;
        MaterialProperties material;
        if(!r.material.empty())try{material=materialCache_.get(materialAssets_,r.material);}catch(const std::exception& e){materialError=e.what();}
        material=applyMaterialOverrides(material,r.materialValues,r.materialOverrides);
        if(material.shader==2)return; // alpha-blended surfaces do not cast opaque shadow silhouettes

        casters.push_back({&mesh,transformMatrix(t.position,t.rotation,t.scale),t.position,
            mesh.radius*std::max({std::abs(t.scale.x),std::abs(t.scale.y),std::abs(t.scale.z)}),t.scale.x*t.scale.y*t.scale.z<0});
    });
    std::array<Mat4,Lighting::maxShadowLayers> matrices{};
    if(needsShadows){
        glBindFramebuffer(GL_FRAMEBUFFER,shadowFramebuffer_);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,shadowTexture_,0,0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Shadow framebuffer incomplete; rendering lights without shadows");
            for(auto& l:lights)l.shadowLayer=-1;
        }
        glUseProgram(unlitProgram_);glViewport(0,0,Lighting::shadowResolution,Lighting::shadowResolution);
        glPolygonOffset(2,4);
        for(const auto& light:lights)if(light.shadowLayer>=0){
            for(int face=0;face<(light.type==1?6:1);++face){
                const int resolution=lightShadowResolution(light.settings);
                glViewport(0,0,resolution,resolution);
                const int layer=light.shadowLayer+face;
                const Mat4 vp=Lighting::shadowMatrix(light,cameraPosition,face);matrices[layer]=vp;
                glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,shadowTexture_,0,layer);
                glClear(GL_DEPTH_BUFFER_BIT);
                const Lighting::Frustum shadowFrustum(vp);
                for(const auto& caster:casters){
                    if(!shadowFrustum.visible(caster.position,caster.radius))continue;
                    const Mat4 mvp=multiply(vp,caster.model);
                    glUniformMatrix4fv(unlitModelViewProjection_,1,GL_FALSE,mvp.m);
                    glFrontFace(caster.mirrored?GL_CW:GL_CCW);
                    glBindVertexArray(caster.mesh->vertexArray);glDrawArrays(GL_TRIANGLES,0,caster.mesh->vertexCount);
                }
            }
        }
        glPolygonOffset(1,1);glFrontFace(frontFace);glBindVertexArray(0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFramebuffer);
    }
    glUseProgram(litProgram_);
    glUniform1i(lightCountLocation_,static_cast<int>(lights.size()));glUniform1i(shadowSamplerLocation_,1);
    for(std::size_t i=0;i<lights.size();++i){
        const auto& l=lights[i];const auto& s=l.settings;
        glUniform4f(lightLocations_[i][0],l.position.x,l.position.y,l.position.z,float(l.type));
        glUniform4f(lightLocations_[i][1],s.color.x*s.intensity,s.color.y*s.intensity,s.color.z*s.intensity,s.range);
        glUniform4f(lightLocations_[i][2],l.direction.x,l.direction.y,l.direction.z,std::cos(s.outerAngle*kPi/180));
        glUniform4f(lightLocations_[i][3],float(l.shadowLayer),s.shadowBias,std::cos(s.innerAngle*kPi/180),float(lightShadowResolution(s))/Lighting::shadowResolution);
    }
    for(int i=0;i<Lighting::maxShadowLayers;++i)glUniformMatrix4fv(shadowMatrixLocations_[i],1,GL_FALSE,matrices[i].m);
}
