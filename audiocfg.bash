# bash completion for audiocfg

_audiocfg()
{
	local cur prev words cword
	_init_completion || return

	if [[ $cur == -* ]]; then
		COMPREPLY=($(compgen -W '--list --device --list-profiles --profile --toggle --verbose --quiet --help --version' -- "$cur"))
		return
	fi

	if [[ ${prev} == -d || ${prev} == --device || ${prev} == -p || ${prev} == --profile || ${prev} == -t || ${prev} == --toggle ]]; then
		return
	fi
}
complete -F _audiocfg audiocfg
