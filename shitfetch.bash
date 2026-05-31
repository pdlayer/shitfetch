_shitfetch() {
	local cur prev
	local opts logos templates modules
	COMPREPLY=()
	cur=${COMP_WORDS[COMP_CWORD]}
	prev=${COMP_WORDS[COMP_CWORD-1]}
	opts='-h --help -v --version -l --logo --logo= -t --template --template= --modules --modules= -c --config --config= --no-config'
	logos='auto none'
	templates='default mini'
	modules='os kernel init uptime host shell wm dewm wm/de term terminal cpu gpu memory swap disk packages pkgs display locale lang local-ip local_ip ip'

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
	esac

	COMPREPLY=($(compgen -W "$opts" -- "$cur"))
}

complete -F _shitfetch shitfetch
