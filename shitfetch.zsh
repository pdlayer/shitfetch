#compdef shitfetch

_shitfetch() {
	_arguments \
		'(-h --help)'{-h,--help}'[show help]' \
		'(-v --version)'{-v,--version}'[show version]' \
		'(-l --logo)'{-l,--logo}'[set logo]:logo:(auto none)' \
		'(-t --template)'{-t,--template}'[set template]:template:(default mini)' \
		'--modules[set module order]:modules:(os kernel init uptime host shell wm dewm wm/de term terminal cpu gpu memory swap disk packages pkgs display locale lang local-ip local_ip ip)' \
		'(-c --config)'{-c,--config}'[load config file]:config file:_files' \
		'--no-config[skip automatic config loading]' \
		'--spin[animate the logo as spinning 2.5D relief]::axis:(x y xy)' \
		'--spin-speed[rotation radians per second]:speed:' \
		'--spin-size[projection scale]:size:' \
		'--spin-depth[relief thickness, 0 for auto]:depth:' \
		'--spin-height[relief rows, 0 for auto]:height:' \
		'--spin-frames[stop after N frames]:frames:' \
		'--spin-fps[target frame rate]:fps:' \
		'--spin-light[light direction]:light:(top-left top top-right left front right bottom-left bottom bottom-right)' \
		'--spin-shade[relief glyph alphabet]:shade:(auto ascii braille blocks)' \
		'--spin-chars[shading ramp, darkest first]:ramp:'
}

_shitfetch "$@"
