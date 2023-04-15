#include <stdio.h>
#include <stdexcept>
#include <list>
#include <tuple>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

// See also:
// https://gamedev.stackexchange.com/a/135894


#define VIEW_WIDTH  1760
#define VIEW_HEIGHT 1320
// #define VIEW_WIDTH  640
// #define VIEW_HEIGHT 480
#define FBSIZE (VIEW_WIDTH*VIEW_HEIGHT*4)

#define S(fb,x,y,n) (fb[((x)+((y)*VIEW_WIDTH))*4+(n)])
#define T(fb,x,y) *(uint32_t*)(&S(fb,x,y,0))
#define R(fb,x,y) S(fb,x,y,2)
#define G(fb,x,y) S(fb,x,y,1)
#define B(fb,x,y) S(fb,x,y,0)
#define A(fb,x,y) S(fb,x,y,3)

#define MAP_FILE "assets/raybox-map.png"
#define MAP_WIDTH 64
#define MAP_HEIGHT 64
#define MAP_OVERLAY_SCALE int(1320/MAP_HEIGHT)

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
  num px, py;
  num dx, dy;
  num vx, vy;
  uint64_t m_prevTime;
  uint64_t m_thisTime;
  uint64_t m_frequency;
  traced_column_t m_traces[VIEW_WIDTH];

  RayboxSystem() {
    m_window = NULL;
    m_renderer = NULL;
    m_texture = NULL;
    m_fb = NULL;
    m_video_init = false;
    m_img_init = false;
    m_frame = 0;
    m_show_map_overlay = false;
    px = MAP_WIDTH>>1;
    py = MAP_HEIGHT>>1;
    dx = 0;
    dy = -1;
    vx = 0.66;
    vy = 0;
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
      px = player.x + 0.5;
      py = player.y + 0.5;
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
    num ppx = px*MAP_OVERLAY_SCALE;
    num ppy = py*MAP_OVERLAY_SCALE;
    T(m_fb, int(ppx), int(ppy)) = 0xff00ffff;
    // Render view vector:
    for (int n=0; n<MAP_OVERLAY_SCALE; ++n) {
      num nn = num(n)/num(MAP_OVERLAY_SCALE);
      num vvx = ppx+dx*n;
      num vvy = ppy+dy*n;
      T(m_fb, int(vvx), int(vvy)) = 0xff00ffff;
    }
    return true;
  }

  enum trace_modes {
    CRAWL_TRACE,
    LINE_TRACE,
    DDA_TRACE,
  };

  bool trace(int trace_mode = DDA_TRACE) {
    // Cast rays thru horizon:
    for (int x=0; x<VIEW_WIDTH; ++x) {

      // Current map cell:
      int mx = int(px); // Remember: casting to int() will round DOWN.
      int my = int(py);
      // (mx,my) is current map cell.

      // Calculate the direction of THIS ray:
      // First, camera x position along viewplane (-1 <= cx <= 1):
      num cx = 2*x / num(VIEW_WIDTH) - 1;
      num rx = dx + vx*cx;
      num ry = dy + vy*cx;
      // (rx,ry) is the ray vector.

      num dist = 0;
      num hx = px;
      num hy = py;
      int side = 0;
      uint32_t m = 0xffff00ff;

      switch (trace_mode) {
        case CRAWL_TRACE:
        {
          num e = 0.005;
          int mmx;
          int mmy;
          for (int x=0; x<10000; ++x) {
            dist += e;
            hx += e*rx;
            hy += e*ry;
            mmx = int(hx);
            mmy = int(hy);
            if (mmx>=0 && mmx<MAP_WIDTH && mmy>=0 && mmy<MAP_HEIGHT) {
              m = *(m_map.cell(mmx, mmy));
              if (m) {
                // We hit a wall.
                //SMELL: A hack to determine if we hit a NS or EW edge of the wall:
                side = abs(hx-floor(hx+0.5)) < abs(hy-floor(hy+0.5));
                break;
              }
            }
          }
          break;
        }
        case LINE_TRACE:
        {
          // Simple gridline intersection trace:
          // What we know:
          // - px,py is the precise player position.
          // - mx,my is the current map cell.
          // - rx,ry is the ray vector we need to trace. Its magnitude is important.
          dist = MAP_WIDTH*MAP_WIDTH + MAP_HEIGHT*MAP_HEIGHT; // Start with a distance that is way bigger than we'll ever trace.
          if (rx!=0) {
            // Loop thru all vertical-running grid lines, testing intersections with map cells:
            for (int x=0; x<MAP_WIDTH; ++x) {
              // Find where this ray intersects with this grid line:
              num xx = num(x);
              if ( (xx-px)*rx < 0 ) continue; // Skip lines that are in the wrong direction.
              num a = xx-px; // line segment run.
              num s = a/rx; // scale factor; ratio of run to vector x component.
              num b = s*ry; // line segment rise.
              num d = a*a + b*b;
              // Find exact floating point intersection of the ray with this vertical gridline:
              num wx = px+a;
              num wy = py+b;
              if (rx<0) wx--; // From this perspective, walls face the other way.
              if (int(wy)>=MAP_HEIGHT || int(wy)<0 || int(wx)>=MAP_WIDTH || int(wx)<0) continue; // out of map range.
              // Find out what's in the map at this point:
              uint32_t c = *m_map.cell(int(wx), int(wy));
              if (!c) continue; // nothing here.
              if (d < dist) {
                // This intersection is closer:
                dist = d;
                m = c;
                side = 0;
                hx = wx;
                hy = wy;
              }
            }
          }
          if (ry!=0) {
            // Loop thru all vertical-running grid lines, testing intersections with map cells:
            for (int y=0; y<MAP_HEIGHT; ++y) {
              // Find where this ray intersects with this grid line:
              num yy = num(y);
              if ( (yy-py)*ry < 0 ) continue; // Skip lines that are in the wrong direction.
              num a = yy-py; // line segment run.
              num s = a/ry; // scale factor; ratio of run to vector x component.
              num b = s*rx; // line segment rise.
              num d = a*a + b*b;
              // Find exact floating point intersection of the ray with this vertical gridline:
              num wx = px+b;
              num wy = py+a;
              if (ry<0) wy--; // From this perspective, walls face the other way.
              if (int(wy)>=MAP_HEIGHT || int(wy)<0 || int(wx)>=MAP_WIDTH || int(wx)<0) continue; // out of map range.
              // Find out what's in the map at this point:
              uint32_t c = *m_map.cell(int(wx), int(wy));
              if (!c) continue; // nothing here.
              if (d < dist) {
                // This intersection is closer:
                dist = d;
                m = c;
                side = 1;
                hx = wx;
                hy = wy;
              }
            }
          }
          dist = sqrt(dist)/sqrt(rx*rx+ry*ry);
          break;
        }
        case DDA_TRACE:
        {
          // Fast DDA trace...

          //NOTE: hx,hy are ray distances to H and V walls; 'h' for hypotenuse or hit distance.
          // Calculate the distance we have to advance the ray for each of an X step and Y step in map grid:
          num dx = (rx==0) ? 1e30 : abs(1/rx);
          num dy = (ry==0) ? 1e30 : abs(1/ry);
          // dx is the distance the ray must advance to move from one horizontal map cell to the next.
          // dy is the equivalent for vertical advances.

          // Calculate starting state:
          // Map cell step directions:
          int sx;
          int sy;
          if (rx < 0) { sx = -1; hx =     (px-mx) * dx; } // This ray steps W
          else        { sx = +1; hx = (1.0+mx-px) * dx; } // This ray steps E
          if (ry < 0) { sy = -1; hy =     (py-my) * dy; } // This ray steps N
          else        { sy = +1; hy = (1.0+my-py) * dy; } // This ray steps S

          uint32_t hit = 0;
          while (!hit) {
            if (hx < hy)  { hx += dx; mx += sx; side = 0; } // No hit yet, and hx distance hasn't yet caught up to hy distance.
            else          { hy += dy; my += sy; side = 1; }
            hit = *m_map.cell(mx, my);
            if (mx<0 || mx>=MAP_WIDTH || my<0 || my>=MAP_HEIGHT) {
              printf("\nRay escaped! %d,%d\n", mx, my);
              throw "ERROR";
            }
          }
          dist = (side==0) ? hx-dx : hy-dy;
          hx = px + dist*rx;
          hy = py + dist*ry;
          m = hit;
          break;
        }
      } // switch
      m_traces[x].dist  = dist;
      m_traces[x].hx    = hx;
      m_traces[x].hy    = hy;
      m_traces[x].side  = side;
      m_traces[x].color = m;
    } // for
    return true;
  } // trace()


  bool render_view() {
    // Render each column:
    for (int x=0; x<VIEW_WIDTH; ++x) {
      traced_column_t &col = m_traces[x];
      // .dist is the distance from the player to the wall hit.
      // .color is the wall color.
      // .hx,hy is the point of the hit, in map space.
      // .side is 0 (NS) or 1 (EW) depending on which side of a wall we hit.
      int h = VIEW_HEIGHT/2/col.dist;
      int y1 = (VIEW_HEIGHT>>1)-h;
      if (y1<0) y1 = 0;
      // int y2 = (VIEW_HEIGHT>>1)+h;
      // if (y2>VIEW_HEIGHT) y2=VIEW_HEIGHT;

      for (int t=-h; t<h; ++t) {
        int y = t+VIEW_HEIGHT/2;
        if (y<0 || y>=VIEW_HEIGHT) continue;
        num tt = num(t+h)/num(h*2);
        // Darken, depending on the side we hit:
        // T(m_fb, x, y) = col.color & (col.side ? 0xffffffff : 0xffc0c0c0);
        int tx = int(64.0*(col.side ? col.hx : col.hy));
        int ty = int(64.0*tt);

        // int r = (fx&1)        ? 0x0000ff : 0x000000;
        int g = (tx&4)^(ty&4) ? 0x00ff00 : 0x007000;
        // int b = (fy&1)        ? 0xff0000 : 0x000000;
        int r = 0;
        int b = 0;
        T(m_fb, x, y) = (r|g|b|0xff000000) & (col.side ? 0xffffffff : 0xffc0c0c0);
        // T(m_fb, x, y) =  ? 0xff888888 : 0xffcc00cc;
      }
    }
    return true;
  }

  void rotate(num a) {
    num nx, ny;
    num ca = cos(a);
    num sa = sin(a);
    // Rotate direction vector:
    nx =  dx*ca + dy*sa;
    ny = -dx*sa + dy*ca;
    dx = nx;
    dy = ny;
    // Rotate viewplane vector:
    nx =  vx*ca + vy*sa;
    ny = -vx*sa + vy*ca;
    vx = nx;
    vy = ny;
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
    if (keys[SDL_SCANCODE_W]) { px+=ts*dx; py+=ts*dy; }
    if (keys[SDL_SCANCODE_S]) { px-=ts*dx; py-=ts*dy; }
    if (keys[SDL_SCANCODE_A]) { px-=ts*vx; py-=ts*vy; }
    if (keys[SDL_SCANCODE_D]) { px+=ts*vx; py+=ts*vy; }
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
      if (!trace(DDA_TRACE)) break;
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
