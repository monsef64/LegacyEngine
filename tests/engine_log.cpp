#include "platform/engine_log.hpp"
#include <stdexcept>
#include <cstring>
#ifndef _WIN32
#include <sys/resource.h>
#endif
int main(int argc,char** argv){
#ifndef _WIN32
    // Test crashes must not leave core dumps in the workspace.
    const rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);
#endif
    EngineLog::initialize();
    if(argc>1 && !std::strcmp(argv[1],"terminate"))throw std::runtime_error("test unhandled exception");
    if(argc>1 && !std::strcmp(argv[1],"crash")){std::raise(SIGSEGV);return 3;}
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,"test startup");
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"test warning");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"test error");
    EngineLog::Errors errors;errors.report("asset","missing texture");errors.report("asset","missing texture");
    errors.report("asset","");errors.report("asset","missing again");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,"test clean shutdown");
}
