#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <gpu/app.hpp>

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    auto* app = new gpu::GeoApp();
    if (!app->init()) return SDL_APP_FAILURE;
    *appstate = app;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* ev) {
    auto* app = static_cast<gpu::GeoApp*>(appstate);
    if (ev->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    app->handleEvent(ev);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<gpu::GeoApp*>(appstate);
    app->update();
    app->render();
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* app = static_cast<gpu::GeoApp*>(appstate);
    if (app) {
        app->cleanup();
        delete app;
    }
    SDL_Quit();
}