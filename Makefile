DOCKER_IMAGE  = dockcross/linux-mipsel-lts
CC            = mipsel-unknown-linux-gnu-gcc
CFLAGS        = -nostdlib -static -Wl,-e,__start

SRC           = src/kpoke.S
BIN           = dist/kpoke

.PHONY: all clean docker local strip size help setup setup-local

all: docker

# Build inside dockcross container (default)
docker:
	@mkdir -p dist
	docker run --rm -v "$(CURDIR):/work" -w /work $(DOCKER_IMAGE) \
		$(CC) $(CFLAGS) -o $(BIN) $(SRC)
	@echo "Built: $(BIN) ($$(stat -c%s $(BIN)) bytes)"
	@file $(BIN)

# Build with local cross-compiler (if installed)
local:
	@mkdir -p dist
	mipsel-linux-gnu-gcc $(CFLAGS) -o $(BIN) $(SRC)
	@echo "Built: $(BIN) ($$(stat -c%s $(BIN)) bytes)"
	@file $(BIN)

# Strip to minimum size
strip: all
	docker run --rm -v "$(CURDIR):/work" -w /work $(DOCKER_IMAGE) \
		mipsel-unknown-linux-gnu-strip --strip-all $(BIN)
	@echo "Stripped: $(BIN) ($$(stat -c%s $(BIN)) bytes)"

# Pull dockcross image
setup:
	docker pull $(DOCKER_IMAGE)

# Install local cross-compiler (Debian/Ubuntu/Kali)
setup-local:
	sudo apt-get install -y gcc-mipsel-linux-gnu

size:
	@test -f $(BIN) && echo "$$(stat -c%s $(BIN)) bytes" || echo "Not built yet"

clean:
	rm -f $(BIN)

help:
	@echo "kpoke - kernel memory tool for embedded MIPS Linux"
	@echo ""
	@echo "Build targets:"
	@echo "  make           Build with dockcross (docker required)"
	@echo "  make local     Build with local mipsel-linux-gnu-gcc"
	@echo "  make strip     Build + strip symbols"
	@echo ""
	@echo "Setup:"
	@echo "  make setup       Pull dockcross docker image"
	@echo "  make setup-local Install local mipsel-linux-gnu-gcc (apt)"
	@echo ""
	@echo "Other:"
	@echo "  make size      Show binary size"
	@echo "  make clean     Remove build artifacts"
