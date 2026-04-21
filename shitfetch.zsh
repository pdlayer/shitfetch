#compdef shitfetch

_shitfetch() {
	_arguments \
		'(-h --help)'{-h,--help}'[show help]' \
		'(-v --version)'{-v,--version}'[show version]' \
		'(-l --logo)'{-l,--logo}'[set logo]:logo:(auto none)'
}

_shitfetch "$@"
