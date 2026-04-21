_shitfetch() {
	local cur prev
	COMPREPLY=()
	cur=${COMP_WORDS[COMP_CWORD]}
	prev=${COMP_WORDS[COMP_CWORD-1]}

	case $prev in
		-l|--logo)
			COMPREPLY=($(compgen -W 'auto none' -- "$cur"))
			return
			;;
	esac

	COMPREPLY=($(compgen -W '-h --help -v --version -l --logo' -- "$cur"))
}

complete -F _shitfetch shitfetch
