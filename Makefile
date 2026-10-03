# LAST AISLE - build with `make`, run with `make run`.
CC      ?= clang
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
           $(SDL_CFLAGS) -Isrc -Isrc/gen
LDLIBS  += $(SDL_LIBS) -lm

ART     := $(wildcard art/*.art) art/manifest.txt art/palette.txt
SRC     := $(wildcard src/*.c) src/gen/atlas.c
OBJ     := $(patsubst src/%.c,build/obj/%.o,$(SRC))
BIN     := lastaisle

all: $(BIN)

assets/atlas.png src/gen/atlas.h src/gen/atlas.c: $(ART) tools/build_atlas.py
	python3 tools/build_atlas.py

build/obj/%.o: src/%.c src/gen/atlas.h $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDLIBS)

run: $(BIN)
	./$(BIN)

previews:
	python3 tools/build_atlas.py --previews --tiletest

# macOS application bundle with SDL3 bundled inside
APP := LastAisle.app
app: $(BIN)
	rm -rf $(APP) build/LastAisle.iconset
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources/assets $(APP)/Contents/Frameworks
	cp $(BIN) $(APP)/Contents/MacOS/lastaisle
	cp assets/atlas.png $(APP)/Contents/Resources/assets/
	python3 tools/make_icon.py build/LastAisle.iconset
	iconutil -c icns build/LastAisle.iconset -o $(APP)/Contents/Resources/LastAisle.icns
	cp tools/Info.plist $(APP)/Contents/Info.plist
	SDLLIB=$$(otool -L $(BIN) | awk '/libSDL3/ {print $$1}'); \
	  REAL=$$(python3 -c "import os,sys;print(os.path.realpath(sys.argv[1]))" $$(pkg-config --variable=libdir sdl3)/libSDL3.0.dylib); \
	  cp $$REAL $(APP)/Contents/Frameworks/libSDL3.0.dylib; \
	  chmod u+w $(APP)/Contents/Frameworks/libSDL3.0.dylib; \
	  install_name_tool -id @rpath/libSDL3.0.dylib $(APP)/Contents/Frameworks/libSDL3.0.dylib; \
	  install_name_tool -change $$SDLLIB @executable_path/../Frameworks/libSDL3.0.dylib $(APP)/Contents/MacOS/lastaisle
	codesign --force --deep -s - $(APP)
	@echo "built $(APP)"

clean:
	rm -rf build/obj $(BIN) $(APP)

.PHONY: all run clean previews app
