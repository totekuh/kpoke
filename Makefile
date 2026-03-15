DOCKER_IMAGE  = dockcross/linux-mipsel-lts
CC            = mipsel-unknown-linux-gnu-gcc
CFLAGS        = -nostdlib -static -Wl,-e,__start -Isrc

SRC           = src/kpoke.S
SRC_D         = src/kpoked.S
BIN           = dist/kpoke
BIN_D         = dist/kpoked

.PHONY: all clean docker local daemon strip size help setup setup-local

all: docker

# Build kpoke inside dockcross container (default)
docker:
	@mkdir -p dist
	docker run --rm -v "$(CURDIR):/work" -w /work $(DOCKER_IMAGE) \
		$(CC) $(CFLAGS) -o $(BIN) $(SRC)
	@echo "Built: $(BIN) ($$(stat -c%s $(BIN)) bytes)"
	@file $(BIN)

# Build kpoked (TCP daemon)
daemon:
	@mkdir -p dist
	docker run --rm -v "$(CURDIR):/work" -w /work $(DOCKER_IMAGE) \
		$(CC) $(CFLAGS) -o $(BIN_D) $(SRC_D)
	@echo "Built: $(BIN_D) ($$(stat -c%s $(BIN_D)) bytes)"
	@file $(BIN_D)

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
	@test -f $(BIN) && echo "kpoke:  $$(stat -c%s $(BIN)) bytes" || echo "kpoke:  not built"
	@test -f $(BIN_D) && echo "kpoked: $$(stat -c%s $(BIN_D)) bytes" || echo "kpoked: not built"

clean:
	rm -f $(BIN) $(BIN_D)

help:
	@echo "kpoke - kernel memory tool for embedded MIPS Linux"
	@echo ""
	@echo "Build targets:"
	@echo "  make           Build kpoke with dockcross (docker required)"
	@echo "  make daemon    Build kpoked (TCP daemon)"
	@echo "  make local     Build kpoke with local mipsel-linux-gnu-gcc"
	@echo "  make strip     Build + strip symbols"
	@echo ""
	@echo "Setup:"
	@echo "  make setup       Pull dockcross docker image"
	@echo "  make setup-local Install local mipsel-linux-gnu-gcc (apt)"
	@echo ""
	@echo "Other:"
	@echo "  make size      Show binary sizes"
	@echo "  make clean     Remove build artifacts"
