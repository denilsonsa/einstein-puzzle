#include "screen.h"
#include "exceptions.h"
#include "unicode.h"


Screen::Screen()
{
    screen = NULL;
    mouseVisible = false;
    regionsList = NULL;
    maxRegionsList = 0;
#if SDL_MAJOR_VERSION > 1
    window = NULL;
    renderer = NULL;
    sdlTexture = NULL;
    mouseCursor = NULL;
#else
    mouseImage = NULL;
    mouseSave = NULL;
#endif
}

Screen::~Screen()
{
    SDL_SetCursor(cursor);
#if SDL_MAJOR_VERSION > 1
    SDL_ShowCursor(SDL_ENABLE);
    if (mouseCursor) SDL_FreeCursor(mouseCursor);
#else
    if (mouseImage) SDL_FreeSurface(mouseImage);
    if (mouseSave) SDL_FreeSurface(mouseSave);
#endif
    if (regionsList) free(regionsList);
}


const VideoMode Screen::getVideoMode() const
{
    return VideoMode(screen->w, screen->h, screen->format->BitsPerPixel, fullScreen);
}


void Screen::setMode(const VideoMode& mode)
{
    fullScreen = mode.isFullScreen();
#if SDL_MAJOR_VERSION > 1
    int flags = (fullScreen)?SDL_WINDOW_FULLSCREEN_DESKTOP:SDL_WINDOW_RESIZABLE;
    if(renderer) SDL_DestroyRenderer(renderer);
    if(window) SDL_DestroyWindow(window);
    SDL_CreateWindowAndRenderer(mode.getWidth(), mode.getHeight(), flags, &window, &renderer);
    if(window!=NULL && renderer!=NULL) {
        screen = SDL_CreateRGBSurface(0, mode.getWidth(), mode.getHeight(), 32, 
            0x00ff0000,0x0000ff00, 0x000000ff, 0xff000000);
        sdlTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 
            mode.getWidth(), mode.getHeight());
    }
#else
    int flags = SDL_SWSURFACE /*| SDL_OPENGL*/;
    if (fullScreen)
        flags = flags | SDL_FULLSCREEN;
    screen = SDL_SetVideoMode(mode.getWidth(), mode.getHeight(), mode.getBpp(), flags);
#endif
    if (! screen)
        throw Exception(L"Couldn't set video mode: " + 
                fromMbcs((SDL_GetError())));
}


std::vector<VideoMode> Screen::getFullScreenModes() const
{
    std::vector<VideoMode> modes;
    return modes;
}

        
int Screen::getWidth() const
{
    if (screen) 
        return screen->w;
    else 
        throw Exception(L"No video mode selected"); 
}


int Screen::getHeight() const
{
    if (screen) 
        return screen->h;
    else 
        throw Exception(L"No video mode selected");
}

void Screen::centerMouse()
{
#if SDL_MAJOR_VERSION > 1
    if(window!=NULL) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        SDL_WarpMouseInWindow(window, w / 2, h / 2);
    }
#else
    if (screen) 
        SDL_WarpMouse(screen->w / 2, screen->h / 2);
#endif
    else 
        throw Exception(L"No video mode selected");
}

void Screen::setMouseImage(SDL_Surface *image)
{
#if SDL_MAJOR_VERSION > 1
    if(mouseCursor) {
        SDL_FreeCursor(mouseCursor);
        mouseCursor = NULL;
    }
    if (! image) return;

    mouseCursor = SDL_CreateColorCursor(image, 0, 0);

#else
    if (mouseImage) {
        SDL_FreeSurface(mouseImage);
        mouseImage = NULL;
    }
    if (mouseSave) {
        SDL_FreeSurface(mouseSave);
        mouseSave = NULL;
    }

    if (! image) return;
    
    mouseImage = SDL_DisplayFormat(image);
    if (! mouseImage) 
        throw Exception(L"Error creating surface");
    //mouseSave = SDL_DisplayFormat(image);
    mouseSave = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCCOLORKEY,
            image->w, image->h, screen->format->BitsPerPixel,
            screen->format->Rmask, screen->format->Gmask,
            screen->format->Bmask, screen->format->Amask);
    if (! mouseSave) {
        SDL_FreeSurface(mouseImage);
        throw Exception(L"Error creating buffer surface");
    }
    SDL_SetColorKey(mouseImage, SDL_SRCCOLORKEY, 
            SDL_MapRGB(mouseImage->format, 0, 0, 0));
#endif
}


void Screen::hideMouse()
{
    if (! mouseVisible)
        return;
   
    if (! niceCursor) {
        mouseVisible = false;
        return;
    }

#if SDL_MAJOR_VERSION > 1
    SDL_ShowCursor(SDL_DISABLE);
#else
    if (mouseSave) {
        SDL_Rect src = { 0, 0, mouseSave->w, mouseSave->h };
        SDL_Rect dst = { saveX, saveY, mouseSave->w, mouseSave->h };
        if (src.w > 0) {
            SDL_BlitSurface(mouseSave, &src, screen, &dst);
            addRegionToUpdate(dst.x, dst.y, dst.w, dst.h);
        }
    }
#endif
    mouseVisible = false;
}

void Screen::showMouse()
{
    if (mouseVisible)
        return;
    
    if (! niceCursor) {
        mouseVisible = true;
        return;
    }
    
#if SDL_MAJOR_VERSION > 1
    if(mouseCursor != NULL)
        SDL_SetCursor(mouseCursor);
    SDL_ShowCursor(SDL_ENABLE);
#else

    if (mouseImage && mouseSave) {
        int x, y;
        SDL_GetMouseState(&x, &y);
        saveX = x;
        saveY = y;
        SDL_Rect src = { 0, 0, mouseSave->w, mouseSave->h };
        SDL_Rect dst = { x, y, mouseImage->w, mouseImage->h };
        if (src.w > 0) {
            SDL_BlitSurface(screen, &dst, mouseSave, &src);
            SDL_BlitSurface(mouseImage, &src, screen, &dst);
            addRegionToUpdate(dst.x, dst.y, dst.w, dst.h);
        }
    }
#endif
    mouseVisible = true;
}

void Screen::updateMouse()
{
#if SDL_MAJOR_VERSION > 1
    // Nothing
#else
    hideMouse();
    showMouse();
#endif
}

void Screen::flush()
{
#if SDL_MAJOR_VERSION > 1
    // do the actual swap...
    SDL_UpdateTexture(sdlTexture, NULL, screen->pixels, screen->pitch);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent(renderer);
#else
    if (! regions.size()) return;
    
    if (! regionsList) {
        regionsList = (SDL_Rect*)malloc(sizeof(SDL_Rect) * regions.size());
        if (! regionsList) {
            regions.clear();
            throw Exception(L"Error allocating regions buffer");
        }
        maxRegionsList = regions.size();
    } else {
        if (maxRegionsList < (int)regions.size()) {
            SDL_Rect *r = (SDL_Rect*)realloc(regionsList, 
                    sizeof(SDL_Rect) * regions.size());
            if (! r) {
                regions.clear();
                free(regionsList);
                throw Exception(L"Error incrementing regions buffer");
            }
            regionsList = r;
            maxRegionsList = regions.size();
        }
    }

    int j = 0;
    for (std::list<SDL_Rect>::iterator i = regions.begin();
            i != regions.end(); i++, j++)
        regionsList[j] = *i;

    SDL_UpdateRects(screen, regions.size(), regionsList);
    regions.clear();
#endif
}


void Screen::addRegionToUpdate(int x, int y, int w, int h)
{
#if SDL_MAJOR_VERSION > 1
    (void)x; (void)y; (void)w; (void)h;
#else
    if (((x >= getWidth()) || (y >= getHeight())) || (0 >= w) || (0 >= h))
        return;
    if ((x + w < 0) || (y + h < 0))
        return;
    if (x + w > getWidth())
        w = getWidth() - x;
    if (y + h > getHeight())
        h = getHeight() - y;
    if (0 > x) {
        w = w + x;
        x = 0;
    }
    if (0 > y) {
        h = h + y;
        y = 0;
    }
    SDL_Rect r = { x, y, w, h };
    regions.push_back(r);
#endif
}


void Screen::setPixel(int x, int y, int r, int g, int b)
{
    SDL_LockSurface(screen);
    int bpp = screen->format->BytesPerPixel;
    Uint32 pixel = SDL_MapRGB(screen->format, r, g, b);
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8*)screen->pixels + y * screen->pitch + x * bpp;

    switch(bpp) {
        case 1:
            *p = pixel;
            break;

        case 2:
            *(Uint16 *)p = pixel;
            break;

        case 3:
            if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                p[0] = (pixel >> 16) & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = pixel & 0xff;
            } else {
                p[0] = pixel & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = (pixel >> 16) & 0xff;
            }
            break;

        case 4:
            *(Uint32 *)p = pixel;
            break;
    }
    SDL_UnlockSurface(screen);
}


void Screen::draw(int x, int y, SDL_Surface *tile)
{
    SDL_Rect src = { 0, 0, tile->w, tile->h };
    SDL_Rect dst = { x, y, tile->w, tile->h };
    SDL_BlitSurface(tile, &src, screen, &dst);
}

void Screen::setCursor(bool nice)
{
    if (nice == niceCursor)
        return;
 #if SDL_MAJOR_VERSION > 1
    if (niceCursor)
        SDL_SetCursor(mouseCursor);
    else
        SDL_SetCursor(cursor);
    niceCursor = nice;
 #else   
    bool oldVisible = mouseVisible;
    if (mouseVisible)
        hideMouse();
    niceCursor = nice;

    if (niceCursor)
        SDL_SetCursor(emptyCursor);
    else
        SDL_SetCursor(cursor);
    
    if (oldVisible)
        showMouse();
#endif
}

void Screen::initCursors()
{
    cursor = SDL_GetCursor();
    Uint8 t = 0;
    emptyCursor = SDL_CreateCursor(&t, &t, 8, 1, 0, 0);
}

void Screen::doneCursors()
{
    if (niceCursor)
        SDL_SetCursor(cursor);
    SDL_FreeCursor(emptyCursor);
}

SDL_Surface* Screen::createSubimage(int x, int y, int width, int height)
{
    SDL_Surface *s = SDL_CreateRGBSurface(
#if SDL_MAJOR_VERSION > 1
            0,
#else
            SDL_HWSURFACE | SDL_SRCCOLORKEY,
#endif
            width, height, screen->format->BitsPerPixel,
            screen->format->Rmask, screen->format->Gmask,
            screen->format->Bmask, screen->format->Amask);
    if (! s)
        throw Exception(L"Error creating buffer surface");
    SDL_Rect src = { x, y, width, height };
    SDL_Rect dst = { 0, 0, width, height };
    SDL_BlitSurface(screen, &src, s, &dst);
    return s;
}

#if SDL_MAJOR_VERSION > 1
void Screen::getMouse(int* x, int* y)
{
    SDL_GetMouseState(x, y);
    convertMouse(x, y);
}
void Screen::convertMouse(int* x, int* y)
{
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    (*x) = ((*x) * screen->w) / w;
    (*y) = ((*y) * screen->h) / h;
}
#endif
