# LMIC declares a Class C function that is only defined with LMIC_ENABLE_class_c (off, Class A only):
# silence that one warning for lmic.c instead of building Class C (376 bytes) for nothing
Import("env")

def lmic_c(env, node):
    return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-Wno-unused-function"])

env.AddBuildMiddleware(lmic_c, "*/MCCI LoRaWAN LMIC library/src/lmic/lmic.c")
