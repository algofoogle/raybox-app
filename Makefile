#NOTE: In Makefiles, leading '@' on commands means don't echo them.

LDFLAGS := -lSDL2 -lSDL2_ttf -lSDL2_image
CC := g++

# Make the main executable, './raybox'
raybox: src/raybox.cpp
	$(CC) $^ -o $@ $(LDFLAGS)

# Run the main executable (if necessary, after making it):
run: raybox
	@echo "--- Running $^ ---"
	@./$^

clean:
	rm -rf raybox

# PHONY items are tasks, not artefacts that get created:
.PHONY: run clean
