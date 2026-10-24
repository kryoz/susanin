#!/bin/sh
# docker-release.sh — релизные артефакты в Docker: бинарники, tar-архивы, .ipk и
# SHA256SUMS. Повторяет .github/workflows/release.yml на хосте без кросс-
# компиляторов (macOS, чистый Linux): тулчейны ставятся внутри контейнера.
#
# build/ не трогает: там лежат бинарники релиза (они в git), а сборочные выходы
# идут в dist/ — он в .gitignore.
#
# Usage:
#   sh tools/docker-release.sh                    # все архитектуры, как в CI
#   sh tools/docker-release.sh aarch64 mips       # только эти
#   IMAGE=ubuntu:24.04 DOCKER=podman sh tools/docker-release.sh
set -eu

SRC=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE="${IMAGE:-ubuntu:24.04}"
DOCKER="${DOCKER:-docker}"
VER=$(sed -n 's/^#define SUSANIN_VERSION "\(.*\)"/\1/p' "$SRC/src/version.h")
ARCHES="${*:-mipsel mips aarch64 armv7 x86_64}"

die() { echo "docker-release: $*" >&2; exit 1; }

command -v "$DOCKER" >/dev/null 2>&1 || die "нет '$DOCKER' (IMAGE/DOCKER — переопределяются)"
[ -n "$VER" ] || die "не прочитал версию из src/version.h"

JOB=$(mktemp "${TMPDIR:-/tmp}/susanin-release.XXXXXX") || die "mktemp не удался"
# shellcheck disable=SC2064
trap "rm -f '$JOB'" EXIT INT TERM

cat > "$JOB" <<'JOB_EOF'
set -eu

# arch -> "пакет gcc | пакет libc | триплет"
pick() {
	case "$1" in
	mipsel) echo "gcc-mipsel-linux-gnu libc6-dev-mipsel-cross mipsel-linux-gnu" ;;
	mips) echo "gcc-mips-linux-gnu libc6-dev-mips-cross mips-linux-gnu" ;;
	aarch64) echo "gcc-aarch64-linux-gnu libc6-dev-arm64-cross aarch64-linux-gnu" ;;
	armv7) echo "gcc-arm-linux-gnueabihf libc6-dev-armhf-cross arm-linux-gnueabihf" ;;
	x86_64) echo "gcc libc6-dev x86_64-linux-gnu" ;;
	*)
		echo "docker-release: неизвестная архитектура: $1" >&2
		return 1
		;;
	esac
}

sed -i 's/^Components: .*/Components: main restricted universe multiverse/' \
	/etc/apt/sources.list.d/ubuntu.sources
apt-get update -qq >/dev/null

# Нативные gcc+binutils обязательны: кросс-gcc в Ubuntu 24.04 резолвит ассемблер
# через них, иначе сборка падает как «fatal error: cannot execute 'as'».
PKGS="make file tar gzip gcc binutils"
for t in $SUSANIN_ARCHES; do
	pk=$(pick "$t")
	PKGS="$PKGS $(echo "$pk" | cut -d' ' -f1) $(echo "$pk" | cut -d' ' -f2)"
done
# shellcheck disable=SC2086
apt-get install -y -qq --no-install-recommends $PKGS >/tmp/apt.log 2>&1 || {
	echo "docker-release: apt-get install упал:" >&2
	tail -20 /tmp/apt.log >&2
	exit 1
}

sh tools/check-shipping.sh >/dev/null
mkdir -p dist

for t in $SUSANIN_ARCHES; do
	triplet=$(pick "$t" | cut -d' ' -f3)
	cc="$triplet-gcc"
	[ "$t" != "x86_64" ] || cc=gcc
	command -v "$cc" >/dev/null 2>&1 || {
		echo "docker-release: нет компилятора $cc" >&2
		exit 1
	}

	echo "== $t: сборка ($cc) =="
	make clean >/dev/null 2>&1 || true
	make CC="$cc" CFLAGS="-O2 -std=c11 -Wall -Wextra -Wpedantic -static" \
		LDFLAGS="-static" >/dev/null
	cp susanin-agent "dist/susanin-agent.$t"
	file "dist/susanin-agent.$t" | sed 's|^dist/||'

	echo "== $t: tar-архив и .ipk =="
	sh tools/stage-release.sh --out "pkg-$t" --bin "dist/susanin-agent.$t" >/dev/null
	tar -czf "dist/susanin-keenetic-deploy-$t.tar.gz" -C "pkg-$t" .
	rm -rf "pkg-$t"
	# .ipk — в отдельный каталог: по маске *mips* не отличить mips от mipsel
	sh tools/make-ipk.sh --one "$t" "dist/susanin-agent.$t" \
		--out "ipk-$t" --version "$SUSANIN_VER" >/dev/null
	ipk=$(ls "ipk-$t"/*.ipk | head -1)
	sh tools/check-shipping.sh --ipk "$ipk" >/dev/null
	mv -f "$ipk" dist/
	rm -rf "ipk-$t"
	echo "== $t: ok -> $(basename "$ipk")"
done

make clean >/dev/null 2>&1 || true
cd dist
rm -f SHA256SUMS
sha256sum susanin-keenetic-deploy-*.tar.gz susanin-keenetic_*.ipk >SHA256SUMS
echo "== dist/ =="
ls -l
cat SHA256SUMS
JOB_EOF

echo "docker-release: $ARCHES (v$VER) в $IMAGE"
"$DOCKER" run --rm \
	-e SUSANIN_ARCHES="$ARCHES" \
	-e SUSANIN_VER="$VER" \
	-v "$SRC":/src \
	-v "$JOB":/release-job.sh:ro \
	-w /src "$IMAGE" sh /release-job.sh
