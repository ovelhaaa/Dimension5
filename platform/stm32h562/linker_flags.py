import os
Import("env")

env.Append(LINKFLAGS=[
    "-mfloat-abi=hard",
    "-mfpu=fpv5-sp-d16",
    "--specs=nosys.specs",
    "-Tstm32h562.ld"
])

env.BuildSources(
    os.path.join("$BUILD_DIR", "src_dimension_dsp"),
    "../../src"
)
