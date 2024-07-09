# Emscripten

## Build

```
autoreconf
emconfigure ./configure
emmake make
```

## Link

```
em++ -flto -O3 */*.o */*/*.o */*/*/*.o -o index.html -sUSE_SDL=2 -sUSE_BZIP2 -sUSE_LIBPNG -sUSE_FREETYPE=1 -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=mid -sASYNCIFY -sASYNCIFY_STACK_SIZE=81920 -sINITIAL_MEMORY=128mb --preload-file pak/ --preload-file config/ --preload-file text/ --preload-file font/ --preload-file themes/ --preload-file music/ --preload-file ai/ --preload-file script/ --preload-file dgguspat/ --preload-file timidity.cfg --closure 1 -sEXPORTED_RUNTIME_METHODS=['allocate'] -Wl,-u,htons -Wl,-u,ntohs -Wl,-u,htonl
```
