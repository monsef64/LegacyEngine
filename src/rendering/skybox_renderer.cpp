#include "rendering/skybox_renderer.hpp"
#include "rendering/shader_data.hpp"
#include <glad/gl.h>
#include <stb_image.h>
#include <memory>
#include <vector>
#include <iostream>
namespace {
unsigned shader(unsigned type,const char* source){auto id=glCreateShader(type);glShaderSource(id,1,&source,nullptr);glCompileShader(id);int ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(id,sizeof(log),nullptr,log);std::cerr<<log<<'\n';glDeleteShader(id);return 0;}return id;}
bool inverse(const Mat4& m,Mat4& out){
    double a[4][8]{};for(int r=0;r<4;++r)for(int c=0;c<4;++c){a[r][c]=m.m[c*4+r];a[r][c+4]=r==c;}
    for(int c=0;c<4;++c){int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;if(std::abs(a[pivot][c])<1e-12)return false;
        for(int i=0;i<8;++i)std::swap(a[c][i],a[pivot][i]);double scale=a[c][c];for(auto& v:a[c])v/=scale;
        for(int r=0;r<4;++r)if(r!=c){double k=a[r][c];for(int i=0;i<8;++i)a[r][i]-=k*a[c][i];}}
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)out.m[c*4+r]=float(a[r][c+4]);return true;
}
std::vector<unsigned char> readImage(const AssetManager& assets,const std::string& path){
    if(path.empty())throw std::runtime_error("Assign all six skybox faces, or an HDR panorama");
    auto file=assets.resolve(path);auto bytes=std::filesystem::file_size(file);if(bytes>64*1024*1024)throw std::runtime_error("Skybox image exceeds 64 MiB");
    std::ifstream in(file,std::ios::binary);std::vector<unsigned char> data(static_cast<std::size_t>(bytes));in.read(reinterpret_cast<char*>(data.data()),bytes);if(!in)throw std::runtime_error("Cannot read skybox image");return data;
}
}
bool SkyboxRenderer::init(){


    std::string vs,fs;try{vs=loadShaderData("skybox.vert");fs=loadShaderData("skybox.frag");}catch(const std::exception& e){error=e.what();SDL_LogError(SDL_LOG_CATEGORY_RENDER,"%s",e.what());return false;}
    auto v=shader(GL_VERTEX_SHADER,vs.c_str()),f=shader(GL_FRAGMENT_SHADER,fs.c_str());if(!v || !f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
    program_=glCreateProgram();glAttachShader(program_,v);glAttachShader(program_,f);glLinkProgram(program_);glDeleteShader(v);glDeleteShader(f);
    int ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);if(!ok){shutdown();return false;}glGenVertexArrays(1,&vao_);return true;
}
void SkyboxRenderer::clear(){if(texture_)glDeleteTextures(1,&texture_);texture_=0;loaded_=false;signature_.clear();checked_={};error.clear();}
void SkyboxRenderer::shutdown(){clear();if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);vao_=program_=0;}
void SkyboxRenderer::draw(const Skybox& sky,const AssetManager& assets,const Mat4& vp){
    if(!validSkybox(sky)){error="Invalid Skybox properties";return;}
    Mat4 inv;if(!inverse(vp,inv))return;
    std::vector<std::string> paths=sky.type==1?std::vector<std::string>{sky.hdr}:std::vector<std::string>{sky.right,sky.left,sky.top,sky.bottom,sky.back,sky.front};
    std::string key=std::to_string(sky.type);for(const auto& p:paths)key+='|'+std::to_string(p.size())+':'+p;
    const auto now=std::chrono::steady_clock::now();
    if(now-checked_>std::chrono::seconds(1) || signature_.compare(0,key.size()+1,key+'|')!=0){
        checked_=now;std::string stamp=key+'|';
        for(const auto& p:paths)try{auto file=assets.resolve(p);stamp+=std::to_string(std::filesystem::last_write_time(file).time_since_epoch().count())+':'+std::to_string(std::filesystem::file_size(file))+';';}catch(...){stamp+="missing;";}
        if(stamp!=signature_){
            signature_=stamp;loaded_=false;if(texture_)glDeleteTextures(1,&texture_);texture_=0;
            try{
                const unsigned target=sky.type==1?GL_TEXTURE_2D:GL_TEXTURE_CUBE_MAP;
                glActiveTexture(GL_TEXTURE4);glGenTextures(1,&texture_);glBindTexture(target,texture_);
                int faceSize=0;
                for(std::size_t face=0;face<paths.size();++face){auto encoded=readImage(assets,paths[face]);int w=0,h=0,c=0;
                    if(!stbi_info_from_memory(encoded.data(),int(encoded.size()),&w,&h,&c) || w<=0 || h<=0)throw std::runtime_error("Invalid skybox image: "+paths[face]);
                    if(sky.type==1){
                        if(w>4096 || h>2048 || !stbi_is_hdr_from_memory(encoded.data(),int(encoded.size())))throw std::runtime_error("HDR panorama must be Radiance .hdr, at most 4096 x 2048");
                        std::unique_ptr<float,decltype(&stbi_image_free)> pixels(stbi_loadf_from_memory(encoded.data(),int(encoded.size()),&w,&h,&c,3),stbi_image_free);if(!pixels)throw std::runtime_error("Cannot decode HDR panorama");
                        for(std::size_t i=0;i<std::size_t(w)*h*3;++i)if(!std::isfinite(pixels.get()[i]) || pixels.get()[i]<0)pixels.get()[i]=0;else pixels.get()[i]=std::min(pixels.get()[i],65504.0f);
                        glTexImage2D(target,0,GL_RGB16F,w,h,0,GL_RGB,GL_FLOAT,pixels.get());
                    }else{
                        if(w!=h || w>2048 || (faceSize && w!=faceSize))throw std::runtime_error("Skybox faces must be equal square images, at most 2048 x 2048");faceSize=w;
                        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(encoded.data(),int(encoded.size()),&w,&h,&c,4),stbi_image_free);if(!pixels)throw std::runtime_error("Cannot decode skybox face");
                        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+unsigned(face),0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.get());
                    }
                }
                glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(target,GL_TEXTURE_WRAP_S,sky.type==1?GL_REPEAT:GL_CLAMP_TO_EDGE);glTexParameteri(target,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);if(sky.type==0)glTexParameteri(target,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
                loaded_=true;error.clear();
            }catch(const std::exception& e){error=e.what();if(texture_)glDeleteTextures(1,&texture_);texture_=0;}
        }
    }
    glUseProgram(program_);glUniformMatrix4fv(glGetUniformLocation(program_,"inverseVP"),1,GL_FALSE,inv.m);
    glUniform3f(glGetUniformLocation(program_,"tint"),sky.color.x,sky.color.y,sky.color.z);glUniform1i(glGetUniformLocation(program_,"hasTexture"),loaded_);glUniform1i(glGetUniformLocation(program_,"isHDR"),sky.type==1);
    glUniform1i(glGetUniformLocation(program_,"faces"),4);glUniform1i(glGetUniformLocation(program_,"panorama"),5);
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_CUBE_MAP,sky.type==0?texture_:0);glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,sky.type==1?texture_:0);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);
    glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glActiveTexture(GL_TEXTURE0);
}
