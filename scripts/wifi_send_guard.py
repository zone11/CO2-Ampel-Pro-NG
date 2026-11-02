# WiFiSocket::write() retries send() forever while the WiFi module's buffer is full. Points that one call (only in
# WiFiSocket.cpp) to guarded_send() in src/wifi_manager.cpp, which gives up after a deadline; the library stays unmodified
Import("env")


def guard(env, node):
    return env.Object(node, CPPDEFINES=list(env["CPPDEFINES"]) + [("send", "guarded_send")])


env.AddBuildMiddleware(guard, "*/utility/WiFiSocket.cpp")
