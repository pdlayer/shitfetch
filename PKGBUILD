pkgname=shitfetch
pkgver=0.1.0
pkgrel=1
pkgdesc='minimal linux fetch'
arch=('x86_64')
url='https://github.com/pdlayer/shitfetch'
license=('BSD-3-Clause-Clear')
depends=('libdrm')
makedepends=('pkgconf')
source=()
sha256sums=()

build() {
	make -C "$startdir"
}

package() {
	make -C "$startdir" DESTDIR="$pkgdir" PREFIX=/usr install
	install -Dm644 "$startdir/shitfetch.bash" "$pkgdir/usr/share/bash-completion/completions/shitfetch"
	install -Dm644 "$startdir/shitfetch.fish" "$pkgdir/usr/share/fish/vendor_completions.d/shitfetch.fish"
	install -Dm644 "$startdir/shitfetch.zsh" "$pkgdir/usr/share/zsh/site-functions/_shitfetch"
	install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/shitfetch/LICENSE"
}
