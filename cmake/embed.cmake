# Embed a file as a C++ byte array (portable: MSVC caps string literals at 16 KB).
#   cmake -DIN=... -DOUT=... -DNAME=... -P embed.cmake
file(READ "${IN}" HEX HEX)
string(LENGTH "${HEX}" LEN)
math(EXPR SIZE "${LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX}")
file(WRITE "${OUT}" "// Generated from ${IN}; do not edit.\n#include <string_view>\nnamespace shaman::embedded {\nstatic const unsigned char ${NAME}_data[] = {${BYTES}0};\nextern const std::string_view ${NAME};\nconst std::string_view ${NAME}(reinterpret_cast<const char*>(${NAME}_data), ${SIZE});\n}\n")
