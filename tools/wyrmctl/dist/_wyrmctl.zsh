#compdef wyrmctl


#
# _wyrmctl() {
#   local context state line
#

#     unpair)
#       _arguments \
#         '2:headset:_wyrm_headsets'
#       ;;
#
#     rename)
#       _arguments \
#         '2:headset:_wyrm_headsets' \
#         '3:new headset name:'
#       ;;
#
#     list-paired)
#       local args; args=(
#           "-k:Show public keys"
#           "--keys:Show public keys"
#         )
#       _describe -t args 'arg' args
#       ;;
#
#     tab)
#       _arguments \
#         '2:tab:_wyrm_tabs'
#       ;;
#

(( $+functions[_wyrmctl_pair] )) ||
_wyrmctl_pair(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '(-d --duration=)'{-d,--duration}'[Duration in minutes to allow new connections or \"unlimited\" (default: 2)]:duration:compadd -o nosort unlimited $(seq 0 10)'
}


(( $+functions[_wyrmctl_unpair] )) ||
_wyrmctl_unpair(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '*::headset:_wyrm_headsets'
}


(( $+functions[_wyrmctl_rename] )) ||
_wyrmctl_rename(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '1::headset:_wyrm_headsets' \
        '2::name'
}


(( $+functions[_wyrmctl_list-paired] )) ||
_wyrmctl_list-paired(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '(-)'{-k,--keys}'[Show public keys]'
}

(( $+functions[_wyrmctl_stop-server] )) ||
_wyrmctl_stop-server(){
    _arguments \
        '(-)'{-h,--help}'[Print help]'
}

(( $+functions[_wyrmctl_disconnect] )) ||
_wyrmctl_disconnect(){
    _arguments \
        '(-)'{-h,--help}'[Print help]'
}

(( $+functions[_wyrmctl_tab] )) ||
_wyrmctl_tab(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '*::tab:_wyrm_tabs'
}

(( $+functions[_wyrmctl_tabs] )) ||
_wyrm_tabs() {
  local -a tabs

  tabs=(
    "hidden"
    "overlay_only"
    "compact"
    "stats"
    "settings"
    "foveation_settings"
    "applications"
    "application_launcher"
  )
  _describe -t tabs 'tab' tabs
}

(( $+functions[_wyrmctl_headsets] )) ||
_wyrm_headsets() {
  local -a headsets

  headsets=("${(@f)$(
      wyrmctl list-paired 2>/dev/null | tail -n +2 | awk '{print $1 ":" substr($0, index($0,$2))}'
  )}")

  _describe -t headsets 'headset' headsets
}

(( $+functions[_wyrmctl_commands] )) ||
_wyrmctl_commands(){
    local -a _wyrmctl_cmds
    _wyrmctl_cmds=(
         'pair:Allow a new headset to connect'
         'unpair:Remove a headset'
         'rename:Rename a headset'
         'list-paired:List headsets allowed to connect'
         'stop-server:Stop wyrm-server process'
         'disconnect:Disconnect headset'
         'tab:Show or set current tab on headset'
    )

    if ((CURRENT == 1)); then
        _describe -t commands 'wyrmctl commands' _wyrmctl_cmds
    else
        local curcontext="$curcontext"
        cmd="${${_wyrmctl_cmds[(r)$words[1]:*]%%:*}}"
        if (($#cmd)); then
            if (( $+functions[_wyrmctl_$cmd] )); then
                _wyrmctl_$cmd
            else
                _message "no options for $cmd"
            fi
        else
            _message "no more options"
        fi
    fi
}

_arguments \
    '(- *)--help[Print help text]' \
    '*::wyrmctl commands:_wyrmctl_commands'
