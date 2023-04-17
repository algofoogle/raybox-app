#include <stdio.h>
#include <stdexcept>
#include <list>
#include <tuple>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

// See also:
// https://gamedev.stackexchange.com/a/135894


// #define VIEW_WIDTH  1680 // 7:5 gives us square walls with an FOV of 70deg.
// #define VIEW_HEIGHT 1200

// #define VIEW_WIDTH  1760
// #define VIEW_HEIGHT 1320

#define VIEW_WIDTH  640
#define VIEW_HEIGHT 480
#define FBSIZE (VIEW_WIDTH*VIEW_HEIGHT*4)

#define S(fb,x,y,n) (fb[((x)+((y)*VIEW_WIDTH))*4+(n)])
#define T(fb,x,y) *(uint32_t*)(&S(fb,x,y,0))
#define R(fb,x,y) S(fb,x,y,2)
#define G(fb,x,y) S(fb,x,y,1)
#define B(fb,x,y) S(fb,x,y,0)
#define A(fb,x,y) S(fb,x,y,3)

#define PI 3.141592653589793

#define MAP_FILE "assets/raybox-map.png"
#define MAP_WIDTH 64
#define MAP_HEIGHT 64
#define MAP_OVERLAY_SCALE int(VIEW_HEIGHT/MAP_HEIGHT)

//SMELL: Work out height correctly re view aspect ratio and FOV:
#define HEIGHT_FROM_DIST(d) (VIEW_HEIGHT/2/(d))

typedef float num;

void describe_pixel_format(Uint32 format) {
  printf("Pixel format: %d (%x)\n", format, format);
  printf(
    "SDL_PIXELTYPE             = %d\n"
    "SDL_PIXELORDER            = %d\n"
    "SDL_PIXELLAYOUT           = %d\n"
    "SDL_BITSPERPIXEL          = %d\n"
    "SDL_BYTESPERPIXEL         = %d\n"
    "SDL_ISPIXELFORMAT_INDEXED = %d\n"
    "SDL_ISPIXELFORMAT_ALPHA   = %d\n"
    "SDL_ISPIXELFORMAT_FOURCC  = %d\n",
    SDL_PIXELTYPE(format),
    SDL_PIXELORDER(format),
    SDL_PIXELLAYOUT(format),
    SDL_BITSPERPIXEL(format),
    SDL_BYTESPERPIXEL(format),
    SDL_ISPIXELFORMAT_INDEXED(format),
    SDL_ISPIXELFORMAT_ALPHA(format),
    SDL_ISPIXELFORMAT_FOURCC(format)
  );
}


// typedef std::tuple<int,int> PlayerStart;
typedef struct { int x, y; } PlayerStart;
typedef std::list<PlayerStart> PlayerStarts;


class RayboxMap {
public:
  uint32_t m_map[MAP_WIDTH*MAP_HEIGHT]; // Map is stored as BGRX. //SMELL: Use 2D array instead to simplify derefs?
  PlayerStarts m_player_starts;

  RayboxMap() {
    // Init a usable, but dummy map.
    // Fill map with red walls (BGRA order):
    for (int x=0; x<MAP_WIDTH; ++x) {
      for (int y=0; y<MAP_HEIGHT; ++y) {
        *cell(x,y) = 0x0000ffff;
      }
    }
    // Start with a player position in the middle of the map:
    PlayerStart start = { MAP_WIDTH>>1, MAP_HEIGHT>>1 };
    // Hollow out a part of the map around our player start position:
    for (int x=start.x-3; x<start.x+3; ++x) {
      for (int y=start.y-3; y<start.y+3; ++y) {
        *cell(x,y) = 0;
      }
    }
    m_player_starts.push_back(start);
  }

  uint32_t *cell(int x, int y) {
    return &(m_map[x+y*MAP_WIDTH]);
  }

  // This assumes s->format->format==SDL_PIXELFORMAT_RGB24:
  // See also: https://wiki.libsdl.org/SDL2/SDL_GetRGBA
  bool load_from_surface(SDL_Surface *s) {
    uint8_t r, g, b, a;
    a = 255;
    m_player_starts.clear();
    SDL_LockSurface(s);
    for (int y=0; y<MAP_HEIGHT; ++y) {
      for (int x=0; x<MAP_WIDTH; ++x) {
        r = ((uint8_t*)(s->pixels))[y*s->pitch + x*3 + 0];
        g = ((uint8_t*)(s->pixels))[y*s->pitch + x*3 + 1];
        b = ((uint8_t*)(s->pixels))[y*s->pitch + x*3 + 2];
        if (r==255 && b==255) {
          // Player position:
          *cell(x,y) = 0;
          PlayerStart player = {x,y};
          m_player_starts.push_back(player);
        }
        else {
          *cell(x,y) = (r+g+b==0) ? 0 : (a<<24)|(r<<16)|(g<<8)|b;
        }
      }
    }
    SDL_UnlockSurface(s);
    return true;
  }

  void debug_print_map() {
    int m;
    for (int y=0; y<MAP_HEIGHT; ++y) {
      for (int x=0; x<MAP_WIDTH; ++x) {
        m = 
          ((*cell(x,y)&0x000000ff) ? 1 : 0) | // blue.
          ((*cell(x,y)&0x0000ff00) ? 2 : 0) | // green.
          ((*cell(x,y)&0x00ff0000) ? 4 : 0);  // red.
        putchar(m ? m+'0' : ' ');
      }
      printf("\n");
    }
    int n = 0;
    for (PlayerStart p : m_player_starts) {
      ++n;
      printf("Player start %d: %d,%d\n", n, p.x, p.y);
    }
  }

};

typedef struct {
  num dist;
  num hx, hy;
  int side;
  uint32_t color;
} traced_column_t;

class RayboxSystem {
public:

  RayboxMap m_map;
  SDL_Window *m_window;
  SDL_Renderer *m_renderer;
  SDL_Texture *m_texture;
  uint8_t *m_fb;
  bool m_video_init;
  bool m_img_init;
  bool m_show_map_overlay;
  int m_frame;
  num playerX, playerY;
  num headingX, headingY;
  num viewX, viewY;
  num fov;
  num viewMag;
  uint64_t m_prevTime;
  uint64_t m_thisTime;
  uint64_t m_frequency;
  traced_column_t m_traces[VIEW_WIDTH];
  bool m_capture_traces;

  RayboxSystem() {
    m_window = NULL;
    m_renderer = NULL;
    m_texture = NULL;
    m_fb = NULL;
    m_video_init = false;
    m_img_init = false;
    m_frame = 0;
    m_show_map_overlay = false;
    m_capture_traces = false;
    playerX = MAP_WIDTH>>1;
    playerY = MAP_HEIGHT>>1;
    headingX = 0;
    headingY = -1;
    fov = 70.0 * PI / 180.0; // FOV in radians.
    viewMag = tan(fov/2.0); // Magnitude of the view plane vector (half of the total viewplane).
    viewX = viewMag;
    viewY = 0;
  }

  ~RayboxSystem() {
    printf("Shutting down RayboxSystem...\n");
    if (m_fb) delete m_fb;
    if (m_img_init) IMG_Quit();
    if (m_texture) SDL_DestroyTexture(m_texture); //SMELL: Are we meant to do this? Not sure.
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    if (m_video_init) SDL_Quit(); //SMELL: Match each SDL_InitSubSystem() instead?
    printf("Goodbye\n");
  }

  bool load_map(const char *map_file) {
    SDL_Surface *s;
    s = IMG_Load(map_file);
    while (true) { //SMELL: Try throw instead of this old while/break style.
      if (!s) {
        printf("ERROR: Failed to load map image file: %s\n", map_file);
        break;
      }
      if (s->w != MAP_WIDTH || s->h != MAP_HEIGHT) {
        printf("ERROR: Map image file should be 64x64 pixels, but is: %dx%d\n", s->w, s->h);
        break;
      }
      Uint32 f = s->format->format;
      // Make sure the source image data is in a format we can process...
      if (f != SDL_PIXELFORMAT_RGB24) {
        printf("ERROR: Wrong pixel format. Was expecting %x but got %x\n", SDL_PIXELFORMAT_RGB24, f);
        break;
      }
      // OK, at this point we can assume the image format can be converted by us into a map array.
      m_map.load_from_surface(s);
      printf("Loaded map from %s\n", map_file);
      printf("%d player start(s)\n", (int)m_map.m_player_starts.size());
      PlayerStart player = m_map.m_player_starts.back();
      playerX = player.x + 0.5;
      playerY = player.y + 0.5;
      // m_map.debug_print_map();
      SDL_FreeSurface(s);
      s = NULL;
      return true;
    }
    // describe_pixel_format(s->format->format);
    if (s) SDL_FreeSurface(s);
    return false;
  }

  bool prep() {
    printf("Preparing RayboxSystem...\n");
    //SMELL: This needs proper error handling!
    printf("SDL_InitSubSystem(SDL_INIT_VIDEO): %d\n", SDL_InitSubSystem(SDL_INIT_VIDEO));
    m_video_init = true;
    //SMELL: Check each of the following for failure:
    // Do full SDL video prep and window init:
    m_window =
      SDL_CreateWindow(
        "Raybox",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        VIEW_WIDTH, VIEW_HEIGHT,
        0
      );
    m_renderer =
      SDL_CreateRenderer(
        m_window,
        -1,
        SDL_RENDERER_ACCELERATED
      );
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(m_renderer);
    m_texture =
      SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        VIEW_WIDTH, VIEW_HEIGHT
      );
    IMG_Init(IMG_INIT_PNG);
    m_img_init = true;
    // Create framebuffer:
    m_fb = new uint8_t[FBSIZE];
    printf("SDL window ready.\n");
    return true;
  }

  uint32_t sky_color()   { return 0xff666666; }
  uint32_t floor_color() { return 0xffcccccc; }


  bool render_backdrop() {
    for (int x=0; x<VIEW_WIDTH; ++x) {
      for (int y=0; y<VIEW_HEIGHT; ++y) {
        uint32_t c = (y < VIEW_HEIGHT>>1) ? sky_color() : floor_color();
        T(m_fb, x, y) = c;
      }
    }
    return true;
  }

  bool render_random(int l=0, int t=0, int r=0, int b=0) {
    for (int x=l; x<VIEW_WIDTH-r; ++x) {
      for (int y=t; y<VIEW_HEIGHT-b; ++y) {
        R(m_fb, x, y) = rand()%256;
        G(m_fb, x, y) = rand()%256;
        B(m_fb, x, y) = rand()%256;
        A(m_fb, x, y) = 255;
      }
    }
    return true;
  }

  bool render_map() {
    for (int x=0; x<VIEW_WIDTH; ++x) {
      for (int y=0; y<VIEW_HEIGHT; ++y) {
        if (x<=MAP_WIDTH*MAP_OVERLAY_SCALE && y<=MAP_HEIGHT*MAP_OVERLAY_SCALE) {
          if (x%MAP_OVERLAY_SCALE==0 || y%MAP_OVERLAY_SCALE==0) {
            // Black grid lines:
            T(m_fb, x, y) = 0xff000000;
          }
          else {
            uint32_t m = *(m_map.cell(x/MAP_OVERLAY_SCALE,y/MAP_OVERLAY_SCALE));
            if (m) {
              // Filled square:
              T(m_fb, x, y) = m;
            }
            else {
              T(m_fb, x, y) = ((x&y)&1) ? 0xff333333 : 0xff111111;
            }
          }
        }
      }
    }
    // Render player position:
    num ppx = playerX*MAP_OVERLAY_SCALE;
    num ppy = playerY*MAP_OVERLAY_SCALE;
    T(m_fb, int(ppx), int(ppy)) = 0xff00ffff;
    // Render view vector:
    for (int n=0; n<MAP_OVERLAY_SCALE; ++n) {
      num nn = num(n)/num(MAP_OVERLAY_SCALE);
      num vvx = ppx+headingX*n;
      num vvy = ppy+headingY*n;
      T(m_fb, int(vvx), int(vvy)) = 0xff00ffff;
    }
    return true;
  }

  bool trace() {
    // Trace a ray for each screen column:
    int screenWidth = VIEW_WIDTH;
    for (int screenX = 0; screenX < screenWidth; ++screenX) {
      // Get player's current map cell (but note that we'll modify these values in each iteration):
      int mapX = int(playerX);
      int mapY = int(playerY);
      // Convert screenX to cameraX (i.e. proportional position along the viewplane):
      num cameraX = 2*screenX / num(screenWidth) - 1.0; // cx = [-1,1)
      // Work out the base vector for the ray that goes from the player, through this slit of the viewplane:
      num rayDirX = headingX + viewX*cameraX;
      num rayDirY = headingY + viewY*cameraX;
      // Find out the distance our ray would normally travel to go from one full map grid line to the next,
      // for grid lines on each of the X and Y axes. Work this out for the "forward" direction of the ray:
      num stepX = (rayDirX>0) ? +1 : -1;
      num stepY = (rayDirY>0) ? +1 : -1;
      // What respective distances will we travel along the ray, with each step on X or Y gridlines?
      //NOTE: denominator could be 0!
      //NOTE: Because we end up treating these distances as though they are based on a normalised base ray,
      // we can just use 1/axis instead of ||rayDir||/axis.
      num stepXdist = abs(1.0/rayDirX);
      num stepYdist = abs(1.0/rayDirY);
      // Track separate distance counters for tracing through X gridlines and Y gridlines.
      // Start with the initial distances for each that reach the first gridlines;
      // these are scaled versions of stepXdist and stepYdist, based on on where the
      // camera origin (i.e. player) is within the current map cell.
      num trackXdist = ((rayDirX>0) ? mapX+1-playerX : playerX-mapX)*stepXdist;
      num trackYdist = ((rayDirY>0) ? mapY+1-playerY : playerY-mapY)*stepYdist;
      // Now perform DDA (Digital Differential Analysis), to find the first (nearest) edge we hit
      // that belongs to an occupied map cell:
      uint32_t wallHit = 0;
      int side = 0;
      while (!wallHit) {
        if (trackXdist < trackYdist) {
          // If the X-tracking distance is currently the nearest, then it means our ray is intersecting
          // now with an X gridline (vertical). In other words, it is entering the next X column
          // so we'll want to inspect that first:
          mapX += stepX;
          // Meanwhile, make sure the next time we check our X-tracking distance, it has been
          // advanced to match the start of the next X column, i.e. it is preemptively overshot:
          trackXdist += stepXdist;
          // We're inspecting an intersection at side 0 (X gridline, aka NS, aka vertical).
          side = 0;
        }
        else {
          mapY += stepY;
          trackYdist += stepYdist;
          side = 1;
        }
        // Is there a wall at our updated mapX,Y?
        wallHit = *m_map.cell(mapX, mapY);
      } // while
      //NOTE: We assume the map has no holes, and that our player is not inside a wall,
      // and hence we can assume we definitely have some wallHit value.
      // Now, since we know we have a hit, "side" tells us which side (and hence which distance tracker)
      // represents our hit. From this, we have to subtract one respective step distance
      // because the algorithm above overshoots by 1 extra step in each iteration (as it was
      // preparing for the next iteration):
      num visualWallDist = ((side==0) ? (trackXdist-stepXdist) : (trackYdist-stepYdist));
      //NOTE: visualWallDist is the actual distance, but based on normalising the base ray length
      // (i.e. inverse scaling such that the base ray length would be 1.0).
      m_traces[screenX].side  = side;
      m_traces[screenX].color = wallHit;
      m_traces[screenX].dist  = visualWallDist;
      m_traces[screenX].hx    = visualWallDist*rayDirX + playerX;
      m_traces[screenX].hy    = visualWallDist*rayDirY + playerY;
    } // for
    // Re hx,hy: Because visualWallDist is based on a normalised base ray...
    //...then it is a real distance which we can multiply by the ray's X and Y to get a map-level hit position.
    return true;
  } // trace()


  bool render_view() {
    // Render each column:
    for (int x=0; x<VIEW_WIDTH; ++x) {
      traced_column_t &col = m_traces[x];
      uint32_t color = col.color & (col.side ? 0xffffffff : 0xffc0c0c0);
      // .dist is the distance from the player to the wall hit.
      // .color is the wall color.
      // .hx,hy is the point of the hit, in map space.
      // .side is 0 (NS) or 1 (EW) depending on which side of a wall we hit.
      int h = HEIGHT_FROM_DIST(col.dist);
      int stop = VIEW_HEIGHT/2+h;
      if (stop > VIEW_HEIGHT) stop = VIEW_HEIGHT;
      uint32_t *pxup, *pxdn;
      int pitch = VIEW_WIDTH;
      pxup = (uint32_t*)(m_fb+(VIEW_HEIGHT/2-1)*(pitch*4)+(x*4));
      pxdn = pxup+pitch;
      for (int v=VIEW_HEIGHT/2; v<stop; ++v) {
        *pxup = color; pxup-=pitch;
        *pxdn = color; pxdn+=pitch;
      }
      // int y1 = (VIEW_HEIGHT>>1)-h;
      // if (y1<0) y1 = 0;
      // // int y2 = (VIEW_HEIGHT>>1)+h;
      // // if (y2>VIEW_HEIGHT) y2=VIEW_HEIGHT;

      // for (int t=-h; t<h; ++t) {
      //   int y = t+VIEW_HEIGHT/2;
      //   if (y<0 || y>=VIEW_HEIGHT) continue;
      //   num tt = num(t+h)/num(h*2);
      //   // Darken, depending on the side we hit:
      //   // T(m_fb, x, y) = col.color & (col.side ? 0xffffffff : 0xffc0c0c0);
      //   int tx = int(64.0*(col.side ? col.hx : col.hy));
      //   int ty = int(64.0*tt);

      //   // int r = (fx&1)        ? 0x0000ff : 0x000000;
      //   int g = (tx&4)^(ty&4) ? 0x00ff00 : 0x007000;
      //   // int b = (fy&1)        ? 0xff0000 : 0x000000;
      //   int r = 0;
      //   int b = 0;
      //   T(m_fb, x, y) = (r|g|b|0xff000000) & (col.side ? 0xffffffff : 0xffc0c0c0);
      //   // T(m_fb, x, y) =  ? 0xff888888 : 0xffcc00cc;
      // }
    }
    return true;
  }

  void rotate(num a) {
    num nx, ny;
    num ca = cos(a);
    num sa = sin(a);
    // Rotate direction vector:
    nx =  headingX*ca + headingY*sa;
    ny = -headingX*sa + headingY*ca;
    headingX = nx;
    headingY = ny;
    // Generate viewplane vector:
    viewX = -headingY * viewMag;
    viewY =  headingX * viewMag;
    // // Rotate viewplane vector:
    // nx =  viewX*ca + viewY*sa;
    // ny = -viewX*sa + viewY*ca;
    // viewX = nx;
    // viewY = ny;
  }

  bool render(bool real_render=true) {
    if (real_render) {
      if (!render_backdrop()) return false;
      // if (!render_random(50, 50, 50, 50)) return false;
      if (!render_view()) return false;
      if (m_show_map_overlay && !render_map()) return false;
      SDL_UpdateTexture(m_texture, NULL, m_fb, VIEW_WIDTH*4);
      SDL_RenderCopy(m_renderer, m_texture, NULL, NULL);
      SDL_RenderPresent(m_renderer);
    }
    ++m_frame;
    return true;
  }

  bool handle_input() {
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    m_show_map_overlay = keys[SDL_SCANCODE_TAB];
    num ts = time_step()*3;
    if (keys[SDL_SCANCODE_LEFT]) rotate(ts/3);
    if (keys[SDL_SCANCODE_RIGHT]) rotate(-ts/3);
    if (keys[SDL_SCANCODE_LSHIFT]) ts*=2;
    if (keys[SDL_SCANCODE_W]) { playerX+=ts*headingX; playerY+=ts*headingY; }
    if (keys[SDL_SCANCODE_S]) { playerX-=ts*headingX; playerY-=ts*headingY; }
    if (keys[SDL_SCANCODE_A]) { playerX-=ts*viewX; playerY-=ts*viewY; }
    if (keys[SDL_SCANCODE_D]) { playerX+=ts*viewX; playerY+=ts*viewY; }
    return true;
  }

  bool handle_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e) == 1) {
      if (SDL_QUIT == e.type) return false;
      if (SDL_KEYDOWN == e.type) {
        switch (e.key.keysym.sym) {
          case SDLK_ESCAPE:
            return false;
          case SDLK_c:
            m_capture_traces = true;
            break;
        }
      }
    }
    return true;
  }

  void debug_print() {
    m_map.debug_print_map();
  }

  num time_step() {
    num t = num(m_thisTime-m_prevTime)/num(m_frequency);
    return (t < 0.001) ? 0.001 : t;
  }

  void capture_traces() {
    FILE *fp = fopen("traces_capture.bin", "wb");
    for (int x=0; x<VIEW_WIDTH; ++x) {
      traced_column_t &col = m_traces[x];
      int h = HEIGHT_FROM_DIST(col.dist);
      uint8_t height = h<=240 ? h : 240;
      uint8_t side = col.side;
      fwrite(&height, sizeof(height), 1, fp);
      fwrite(&side, sizeof(side), 1, fp);
    }
    fclose(fp);
    printf("capture_traces(): Wrote traces_capture.bin\n");
  }

  void run() {
    static uint64_t initial_time = SDL_GetPerformanceCounter();
    bool quit = 0;
    m_prevTime = SDL_GetPerformanceCounter();
    m_frequency = SDL_GetPerformanceFrequency();
    int frame_count = 0;
    uint64_t fps_time = SDL_GetPerformanceCounter();
    uint64_t fps_1sec = m_frequency;

    while (!quit) {
      m_thisTime = SDL_GetPerformanceCounter();
      if (!handle_events()) break;
      if (!handle_input()) break;
      if (!trace()) break;
      if (m_capture_traces) {
        m_capture_traces = false;
        capture_traces();
      }
      if (!render(true)) break;
      ++frame_count;
      uint64_t now = SDL_GetPerformanceCounter();
      if (now-fps_time>=fps_1sec) {
        // At least 1 second has elapsed. How many frames have we done?
        uint64_t elapsed_time = now-initial_time;
        printf(
          "[%6.2f sec] Current FPS: %.2f - Overall FPS: %.2f\n",
          num(elapsed_time)/num(fps_1sec),
          num(frame_count)/(num(now-fps_time)/num(fps_1sec)),
          num(m_frame)/(num(elapsed_time)/num(fps_1sec))
        );
        fps_time = now;
        frame_count = 0;
      }
      
      m_prevTime = m_thisTime;
    }
  }

};


int main(int argc, char **argv) {

  RayboxSystem raybox;
  raybox.prep();
  raybox.load_map(MAP_FILE);
  raybox.debug_print();
  raybox.run();

  printf("Bye!\n");

  return EXIT_SUCCESS;
}
