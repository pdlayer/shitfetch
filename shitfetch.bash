_shitfetch() {
	local cur prev
	local opts logos templates modules axes lights shades
	COMPREPLY=()
	cur=${COMP_WORDS[COMP_CWORD]}
	prev=${COMP_WORDS[COMP_CWORD-1]}
	opts='-h --help -v --version -l --logo --logo= -t --template --template= --modules --modules= -c --config --config= --no-config --spin --spin= --spin-speed --spin-size --spin-depth --spin-height --spin-frames --spin-fps --spin-light --spin-shade --spin-chars'
	logos='auto none'
	templates='default mini'
	modules='os kernel init uptime host shell wm dewm wm/de term terminal cpu gpu memory swap disk packages pkgs display locale lang local-ip local_ip ip'
	axes='x y xy'
	lights='top-left top top-right left front right bottom-left bottom bottom-right'
	shades='auto ascii braille blocks'

	case $prev in
		-l|--logo)
			COMPREPLY=($(compgen -W "$logos" -- "$cur"))
			return
			;;
		-t|--template)
			COMPREPLY=($(compgen -W "$templates" -- "$cur"))
			return
			;;
		--modules)
			COMPREPLY=($(compgen -W "$modules" -- "$cur"))
			return
			;;
		-c|--config)
			COMPREPLY=($(compgen -f -- "$cur"))
			return
			;;
		--spin-light)
			COMPREPLY=($(compgen -W "$lights" -- "$cur"))
			return
			;;
		--spin-shade)
			COMPREPLY=($(compgen -W "$shades" -- "$cur"))
			return
			;;
		--spin-speed|--spin-size|--spin-depth|--spin-height|--spin-frames|--spin-fps|--spin-chars)
			return
			;;
	esac

	case $cur in
		--logo=*)
			COMPREPLY=($(compgen -P '--logo=' -W "$logos" -- "${cur#--logo=}"))
			return
			;;
		--template=*)
			COMPREPLY=($(compgen -P '--template=' -W "$templates" -- "${cur#--template=}"))
			return
			;;
		--modules=*)
			COMPREPLY=($(compgen -P '--modules=' -W "$modules" -- "${cur#--modules=}"))
			return
			;;
		--config=*)
			COMPREPLY=($(compgen -P '--config=' -f -- "${cur#--config=}"))
			return
			;;
		--spin=*)
			COMPREPLY=($(compgen -P '--spin=' -W "$axes" -- "${cur#--spin=}"))
			return
			;;
		--spin-light=*)
			COMPREPLY=($(compgen -P '--spin-light=' -W "$lights" -- "${cur#--spin-light=}"))
			return
			;;
		--spin-shade=*)
			COMPREPLY=($(compgen -P '--spin-shade=' -W "$shades" -- "${cur#--spin-shade=}"))
			return
			;;
	esac

	COMPREPLY=($(compgen -W "$opts" -- "$cur"))
}

complete -F _shitfetch shitfetch
