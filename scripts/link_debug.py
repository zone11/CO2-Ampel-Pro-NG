# Debug info for the LTO link: build_flags only reach the compiler, without -g at link time
# firmware.elf has no line numbers for addr2line (diag last_pc); the image is unchanged
Import("env")

env.Append(LINKFLAGS=["-g"])
