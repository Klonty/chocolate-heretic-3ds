// Hand-written config.h for the Nintendo 3DS build (normally generated
// by CMake/autotools from cmake/config.h.cin).

#define PACKAGE_NAME    "Chocolate Doom"
#define PACKAGE_TARNAME "chocolate-doom"
#define PACKAGE_VERSION "3.1.1"
#define PACKAGE_STRING  "Chocolate Heretic 3.1.1 (3DS)"
#define PROGRAM_PREFIX  "chocolate-"

#define HAVE_DIRENT_H
#define HAVE_DECL_STRCASECMP  1
#define HAVE_DECL_STRNCASECMP 1

// No SDL_mixer / SDL_net on this platform: audio goes through NDSP
// (port3ds/i_sound_3ds.c) and networking is stubbed out.
#define DISABLE_SDL2MIXER 1
#define DISABLE_SDL2NET   1
