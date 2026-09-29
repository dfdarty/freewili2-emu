# fw2_emu_web.cmake — what the browser page knows about each app (apps.json).
#
# An app folder may hold fw2emu-web.json, read when the app is built for the
# browser:
#
#   {
#     "args":    "--radio @town",                   options when the URL has no ?args=
#     "source":  "https://github.com/you/your-app",  shown as a link next to the app
#     "license": "GPL-3.0",                          shown with the link
#     "guide": {                                     the "About this app" card
#       "what":  "What the app is.",
#       "see":   "What you see when it starts.",
#       "tryit": [ { "label": "Button text", "send": ["touch 240 160"], "gap_ms": 500,
#                    "text": "- what happens" },
#                  { "text": "A plain line." } ],
#       "check": "What to look for in the log.",
#       "panels": ["log", "sensors", "sound", "header", "controls"]
#     }
#   }
#
# "send" is a list of input-script commands (docs: scripting.md), sent
# gap_ms apart. The page shows every text as plain text, not HTML. Every
# member is optional.

# fw2_emu_app_web(<app dir> <app name> <out var>): the app's "web" object as
# JSON text, or "" if it has none. A malformed file is a warning, not an error.
function(fw2_emu_app_web dir name out)
    set(_web "")
    set(_file "${dir}/fw2emu-web.json")
    if(EXISTS "${_file}")
        file(READ "${_file}" _raw)
        string(STRIP "${_raw}" _raw)
        string(JSON _type ERROR_VARIABLE _err TYPE "${_raw}")
        if(_err OR NOT _type STREQUAL "OBJECT")
            message(WARNING "${_file}: not a JSON object (${_err}); the page ignores it")
        else()
            set(_ok TRUE)
            string(JSON _n LENGTH "${_raw}")
            if(_n GREATER 0)
                math(EXPR _last "${_n} - 1")
                foreach(_i RANGE ${_last})
                    string(JSON _key MEMBER "${_raw}" ${_i})
                    string(JSON _t TYPE "${_raw}" "${_key}")
                    if(_key MATCHES "^(args|source|license)$")
                        set(_want STRING)
                    elseif(_key STREQUAL "guide")
                        set(_want OBJECT)
                    else()
                        message(WARNING "${_file}: unknown member \"${_key}\" (args, source, license, guide); ignored")
                        continue()
                    endif()
                    if(NOT _t STREQUAL _want)
                        message(WARNING "${_file}: \"${_key}\" should be a JSON ${_want}, not ${_t}; the page ignores the file")
                        set(_ok FALSE)
                    endif()
                endforeach()
            endif()
            if(_ok)
                set(_web "${_raw}")
            endif()
        endif()
    elseif(name STREQUAL "dualcpu")
        # WiliBSP's example (its folder is WiliBSP's): its ESP32 half is the emulator's stand-in.
        set(_web "{\"args\": \"--peer esp32=dualcpu --radio @town\"}")
    endif()
    set(${out} "${_web}" PARENT_SCOPE)
endfunction()
